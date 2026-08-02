//
// VideoRecorder.cpp
//

#include "Capture/VideoRecorder.h"

#include <cstdlib>
#include <filesystem>
#include <sstream>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#define SHOONYAKASHA_POPEN  _popen
#define SHOONYAKASHA_PCLOSE _pclose
#else
#include <unistd.h>
#define SHOONYAKASHA_POPEN  popen
#define SHOONYAKASHA_PCLOSE pclose
#endif

namespace Shoonyakasha {

namespace {

bool isExecutable(const std::filesystem::path& candidate) {
    std::error_code ec;
    return !candidate.empty() && std::filesystem::is_regular_file(candidate, ec);
}

/// Quote a path for the shell popen() hands the command to.
std::string quote(const std::string& value) {
    return "\"" + value + "\"";
}

} // namespace

std::string VideoRecorder::findFfmpeg(const std::string& hint) {
#ifdef _WIN32
    const std::string name = "ffmpeg.exe";
#else
    const std::string name = "ffmpeg";
#endif

    if (!hint.empty() && isExecutable(hint)) {
        return hint;
    }

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4996)  // getenv; the value is used immediately
#endif
    if (const char* fromEnv = std::getenv("FFMPEG")) {
        if (isExecutable(fromEnv)) {
            return fromEnv;
        }
    }

    const char* pathEnv = std::getenv("PATH");
#ifdef _MSC_VER
#pragma warning(pop)
#endif

    if (pathEnv) {
#ifdef _WIN32
        const char separator = ';';
#else
        const char separator = ':';
#endif
        std::stringstream stream(pathEnv);
        std::string entry;
        while (std::getline(stream, entry, separator)) {
            if (entry.empty()) continue;
            for (const std::string& fileName : {name, std::string("ffmpeg")}) {
                auto candidate = std::filesystem::path(entry) / fileName;
                if (isExecutable(candidate)) {
                    return candidate.string();
                }
            }
        }
    }

    // Common install roots, for someone who installed ffmpeg without adding it
    // to PATH — which on Windows is most people.
    const char* roots[] = {
#ifdef _WIN32
        "C:/ffmpeg/bin", "C:/tools/ffmpeg/bin", "C:/Program Files/ffmpeg/bin",
#else
        "/usr/bin", "/usr/local/bin", "/opt/homebrew/bin", "/snap/bin",
#endif
    };
    for (const char* root : roots) {
        auto candidate = std::filesystem::path(root) / name;
        if (isExecutable(candidate)) {
            return candidate.string();
        }
    }

    return {};
}

VideoRecorder::~VideoRecorder() {
    if (m_pipe) {
        stop();
    }
}

bool VideoRecorder::start(const std::string& path, uint32_t width, uint32_t height,
                          const Options& options) {
    m_lastError.clear();

    if (m_pipe) {
        m_lastError = "already recording to '" + m_path + "'";
        return false;
    }
    if (width == 0 || height == 0) {
        m_lastError = "zero frame size";
        return false;
    }
    // x264 needs even dimensions for the default 4:2:0 chroma subsampling. Saying
    // so beats letting ffmpeg fail after the first frame is already written.
    if ((width % 2) != 0 || (height % 2) != 0) {
        m_lastError = "frame size " + std::to_string(width) + "x" + std::to_string(height)
                    + " is not even, which most codecs require";
        return false;
    }

    const std::string ffmpeg = findFfmpeg(options.ffmpegPath);
    if (ffmpeg.empty()) {
        m_lastError = "ffmpeg not found. Install it, set $FFMPEG, or put it on PATH.";
        return false;
    }

    std::error_code ec;
    auto parent = std::filesystem::path(path).parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, ec);
    }

    std::ostringstream command;
    command << quote(ffmpeg)
            << " -hide_banner -loglevel error -y"
            << " -f rawvideo -pixel_format rgba"
            << " -video_size " << width << "x" << height
            << " -framerate " << options.fps
            << " -i -"                       // frames arrive on stdin
            << " -an"                        // no audio track to wait for
            << " -c:v " << options.codec
            << " -crf " << options.quality
            << " -pix_fmt yuv420p"           // what players actually accept
            << " -vf vflip";                 // Vulkan's origin is top-left

    if (!options.extraArgs.empty()) {
        command << " " << options.extraArgs;
    }
    command << " " << quote(path);

    std::string commandLine = command.str();

#ifdef _WIN32
    // cmd.exe strips the outermost pair of quotes from the command it is given,
    // so a line that both starts and ends with a quoted token -- which this one
    // does, the ffmpeg path and the output path -- gets mangled. Wrapping the
    // whole thing in one more pair is the documented workaround.
    commandLine = "\"" + commandLine + "\"";
#endif

    // "wb": in text mode Windows would translate 0x0A bytes in the pixel data
    // into CRLF and corrupt every frame.
    m_pipe = SHOONYAKASHA_POPEN(commandLine.c_str(), "wb");
    if (!m_pipe) {
        m_lastError = "could not start ffmpeg (" + ffmpeg + ")";
        return false;
    }

    // popen on Windows starts a shell, which succeeds whether or not the program
    // exists -- so a valid handle proves nothing. The first writeFrame reports
    // the real outcome; keep the command so that error can name it.
    m_command = commandLine;
    m_ffmpegPath = ffmpeg;

    m_path = path;
    m_frameCount = 0;
    m_expectedFrameBytes = static_cast<size_t>(width) * height * 4;
    return true;
}

bool VideoRecorder::writeFrame(const uint8_t* rgba, size_t byteCount) {
    if (!m_pipe) {
        m_lastError = "not recording";
        return false;
    }
    if (!rgba || byteCount != m_expectedFrameBytes) {
        m_lastError = "frame is " + std::to_string(byteCount) + " bytes, expected "
                    + std::to_string(m_expectedFrameBytes);
        return false;
    }

    const size_t written = std::fwrite(rgba, 1, byteCount, m_pipe);
    if (written != byteCount) {
        // ffmpeg exited — a bad codec name, an unwritable path, a full disk. The
        // pipe is the only channel we have, so close it and report.
        m_lastError = "ffmpeg stopped accepting frames after "
                    + std::to_string(m_frameCount) + " frames. Command was: "
                    + m_command;
        SHOONYAKASHA_PCLOSE(m_pipe);
        m_pipe = nullptr;
        return false;
    }

    ++m_frameCount;
    return true;
}

bool VideoRecorder::stop() {
    if (!m_pipe) {
        return true;
    }

    const int status = SHOONYAKASHA_PCLOSE(m_pipe);
    m_pipe = nullptr;

    if (status != 0) {
        m_lastError = "ffmpeg exited with status " + std::to_string(status);
        return false;
    }
    if (m_frameCount == 0) {
        m_lastError = "no frames were written";
        return false;
    }
    return true;
}

} // namespace Shoonyakasha
