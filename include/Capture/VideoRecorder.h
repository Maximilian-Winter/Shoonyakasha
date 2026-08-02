//
// VideoRecorder.h - Encode a stream of frames by piping them to ffmpeg
//
// Raw RGBA frames are written to the stdin of an ffmpeg process, which performs
// the encoding and writes the container. ffmpeg is located at runtime; see
// findFfmpeg(). No encoding library is linked into the engine.
//
// Frames come from RenderTargetSaver::readbackRGBA8, which is synchronous, so
// recording lowers the frame rate.
//

#pragma once

#include <cstdint>
#include <cstdio>
#include <string>

namespace Shoonyakasha {

class VideoRecorder {
public:
    struct Options {
        int fps = 30;

        /// x264 constant rate factor: 0 is lossless, 18 is visually lossless,
        /// 23 is the ffmpeg default, 51 is the lowest quality. Ignored by codecs
        /// that do not accept -crf.
        int quality = 18;

        /// Any encoder ffmpeg accepts for -c:v. "libx264rgb" encodes without
        /// the RGB to YUV conversion, at the cost of player support.
        std::string codec = "libx264";

        /// Empty means search PATH and the usual install locations.
        std::string ffmpegPath;

        /// Extra arguments inserted before the output path.
        std::string extraArgs;
    };

    VideoRecorder() = default;
    ~VideoRecorder();

    VideoRecorder(const VideoRecorder&) = delete;
    VideoRecorder& operator=(const VideoRecorder&) = delete;

    /// Start encoding to `path`. The container follows the file extension, so
    /// .mkv, .mp4 and .webm all work. Fails if ffmpeg cannot be found or the
    /// process cannot be started; check lastError().
    bool start(const std::string& path, uint32_t width, uint32_t height,
               const Options& options = {});

    /// Write one frame to the encoder. `rgba` must be width * height * 4 bytes.
    /// Returns false once the pipe has broken, which is how the exit of the
    /// ffmpeg process is reported.
    bool writeFrame(const uint8_t* rgba, size_t byteCount);

    /// Close the pipe and wait for ffmpeg to finalise the container. A recording
    /// that is not stopped may produce a file that will not play.
    bool stop();

    bool isRecording() const { return m_pipe != nullptr; }
    uint64_t frameCount() const { return m_frameCount; }
    const std::string& outputPath() const { return m_path; }

    /// Why the last start()/writeFrame()/stop() failed. Empty on success.
    const std::string& lastError() const { return m_lastError; }

    /// The ffmpeg binary in use, once start() has resolved one.
    const std::string& ffmpegPath() const { return m_ffmpegPath; }

    /// Absolute path to a usable ffmpeg, or empty. Searched once per call:
    /// $FFMPEG, then PATH, then the usual install locations.
    static std::string findFfmpeg(const std::string& hint = {});

    /// The ffmpeg arguments a recording would use, without the executable path
    /// or the shell quoting. Exposed for tests; see VideoRecorderTest.cpp.
    static std::string buildArguments(const std::string& path,
                                      uint32_t width, uint32_t height,
                                      const Options& options);

    /// Whether an ffmpeg executable could be found.
    static bool available() { return !findFfmpeg().empty(); }

private:
    std::FILE*  m_pipe = nullptr;
    std::string m_path;
    std::string m_command;      // reported in the error from writeFrame()
    std::string m_ffmpegPath;
    std::string m_lastError;
    uint64_t    m_frameCount = 0;
    size_t      m_expectedFrameBytes = 0;
};

} // namespace Shoonyakasha
