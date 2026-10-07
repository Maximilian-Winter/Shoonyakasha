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
#include <fcntl.h>
#include <io.h>
#else
#include <unistd.h>
#endif

namespace Shoonyakasha {

namespace {

bool isExecutable(const std::filesystem::path& candidate) {
    std::error_code ec;
    return !candidate.empty() && std::filesystem::is_regular_file(candidate, ec);
}

/// Quote a path on the command line: for the shell popen() starts, or for
/// CreateProcess on Windows.
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
#pragma warning(disable : 4996)  // getenv: the value is copied before any
                                 // further environment access
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

    // Common install roots, for an ffmpeg that is not on PATH.
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

std::string VideoRecorder::buildArguments(const std::string& path,
                                          uint32_t width, uint32_t height,
                                          const Options& options) {
    std::ostringstream args;
    args << "-hide_banner -loglevel error -y"
         << " -f rawvideo -pixel_format rgba"
         << " -video_size " << width << "x" << height
         << " -framerate " << options.fps
         << " -i -"                       // frames arrive on stdin
         << " -an"                        // no audio stream
         << " -c:v " << options.codec
         << " -crf " << options.quality
         << " -pix_fmt yuv420p";          // widely supported chroma format

    // No flip filter. Vulkan's framebuffer origin is top-left and ffmpeg reads
    // rawvideo top row first, so readback rows are already in the order ffmpeg
    // expects. Adding "-vf vflip" here inverts every frame; VideoRecorderTest's
    // DoesNotFlipTheImage guards against it.

    if (!options.extraArgs.empty()) {
        args << " " << options.extraArgs;
    }
    args << " " << quote(path);
    return args.str();
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
    // 4:2:0 chroma needs even dimensions. A window whose client area is odd
    // (a 1080-high window shrunk to fit the screen, say) is encoded without
    // its last column or row rather than refused.
    const uint32_t encodedWidth = evenSize(width);
    const uint32_t encodedHeight = evenSize(height);
    if (encodedWidth == 0 || encodedHeight == 0) {
        m_lastError = "frame size " + std::to_string(width) + "x" + std::to_string(height)
                    + " is too small to encode";
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

    const std::string commandLine =
        quote(ffmpeg) + " " + buildArguments(path, encodedWidth, encodedHeight, options);
    if (!openPipe(commandLine)) {
        m_lastError = "could not start ffmpeg (" + ffmpeg + ")";
        return false;
    }

    // The command is kept for the error text of writeFrame(), which is where
    // an ffmpeg that started but then exited (an unknown codec, an unwritable
    // path) shows.
    m_command = commandLine;
    m_ffmpegPath = ffmpeg;

    m_path = path;
    m_frameCount = 0;
    m_width = width;
    m_encodedWidth = encodedWidth;
    m_encodedHeight = encodedHeight;
    m_expectedFrameBytes = static_cast<size_t>(width) * height * 4;
    return true;
}

bool VideoRecorder::openPipe(const std::string& commandLine) {
#ifdef _WIN32
    // ffmpeg is started directly rather than through _popen's shell, in a
    // process group of its own. Ctrl+C in the console then reaches only the
    // application, which can stop the recording and finish the file, and not
    // ffmpeg too, which would quit with frames still to come.
    SECURITY_ATTRIBUTES inheritable{};
    inheritable.nLength = sizeof(inheritable);
    inheritable.bInheritHandle = TRUE;
    HANDLE readEnd = nullptr;
    HANDLE writeEnd = nullptr;
    if (!CreatePipe(&readEnd, &writeEnd, &inheritable, 1u << 20)) {
        return false;
    }
    SetHandleInformation(writeEnd, HANDLE_FLAG_INHERIT, 0);   // ffmpeg inherits only its end

    STARTUPINFOA startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = readEnd;
    startup.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);   // ffmpeg's errors still show

    std::string mutableCommand = commandLine;              // CreateProcessA may write to it
    PROCESS_INFORMATION process{};
    const BOOL started = CreateProcessA(nullptr, mutableCommand.data(), nullptr, nullptr, TRUE,
                                        CREATE_NEW_PROCESS_GROUP, nullptr, nullptr,
                                        &startup, &process);
    CloseHandle(readEnd);
    if (!started) {
        CloseHandle(writeEnd);
        return false;
    }
    CloseHandle(process.hThread);

    // Binary mode: text mode would turn every 0x0A byte of the pixels into CRLF.
    const int fd = _open_osfhandle(reinterpret_cast<intptr_t>(writeEnd), _O_BINARY);
    m_pipe = fd >= 0 ? _fdopen(fd, "wb") : nullptr;
    if (!m_pipe) {
        if (fd >= 0) _close(fd);
        else CloseHandle(writeEnd);
        TerminateProcess(process.hProcess, 1);
        CloseHandle(process.hProcess);
        return false;
    }
    m_process = process.hProcess;
    return true;
#else
    // POSIX popen() takes only "r" or "w" (glibc refuses "wb"), and has no
    // text mode to avoid.
    m_pipe = popen(commandLine.c_str(), "w");
    return m_pipe != nullptr;
#endif
}

int VideoRecorder::closePipe() {
    int status = 0;
#ifdef _WIN32
    std::fclose(m_pipe);   // the end of input: ffmpeg finishes the file and exits
    if (m_process) {
        WaitForSingleObject(m_process, INFINITE);
        DWORD code = 1;
        GetExitCodeProcess(m_process, &code);
        CloseHandle(m_process);
        m_process = nullptr;
        status = static_cast<int>(code);
    }
#else
    status = pclose(m_pipe);
#endif
    m_pipe = nullptr;
    return status;
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

    bool complete;
    if (m_encodedWidth == m_width) {
        // Whole rows: the frame, less any last row, in one write
        const size_t bytes = static_cast<size_t>(m_width) * m_encodedHeight * 4;
        complete = std::fwrite(rgba, 1, bytes, m_pipe) == bytes;
    } else {
        // Each row without its last pixel
        const size_t rowBytes = static_cast<size_t>(m_encodedWidth) * 4;
        const size_t stride = static_cast<size_t>(m_width) * 4;
        complete = true;
        for (uint32_t y = 0; y < m_encodedHeight && complete; ++y) {
            complete = std::fwrite(rgba + y * stride, 1, rowBytes, m_pipe) == rowBytes;
        }
    }
    if (!complete) {
        // ffmpeg has exited: an unknown codec, an unwritable path, or a full
        // disk. Close the pipe and report the command that was run.
        m_lastError = "ffmpeg stopped accepting frames after "
                    + std::to_string(m_frameCount) + " frames. Command was: "
                    + m_command;
        closePipe();
        return false;
    }

    ++m_frameCount;
    return true;
}

bool VideoRecorder::stop() {
    if (!m_pipe) {
        return true;
    }

    const int status = closePipe();

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
