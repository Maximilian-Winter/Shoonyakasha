//
// VideoRecorder.h - Encode a stream of frames by piping them to ffmpeg
//
// चलच्चित्रम् — the moving picture.
//
// No new dependency: raw RGBA goes down a pipe to an ffmpeg process, which does
// the encoding and the container. Linking libavcodec would mean a vcpkg
// dependency an order of magnitude larger than the engine's own, for a feature
// most builds never use; ffmpeg is a single executable that is either present or
// not, and this reports which.
//
// Capture is synchronous — see RenderTargetSaver::readbackRGBA8 — so recording
// costs frame rate. That is the right trade for recording a clip of a demo and
// the wrong one for anything shipping.
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

        /// x264's constant rate factor: 0 is lossless, 18 is visually lossless,
        /// 23 is the ffmpeg default, 51 is the worst. Ignored by codecs that do
        /// not understand it.
        int quality = 18;

        /// Anything ffmpeg can encode. libx264 in a .mkv plays everywhere;
        /// "libx264rgb" avoids the RGB->YUV colour shift at the cost of
        /// compatibility.
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

    /// Hand one frame to the encoder. `rgba` must be width * height * 4 bytes.
    /// Returns false once the pipe has broken — which is how a caller finds out
    /// ffmpeg died, since it is a separate process.
    bool writeFrame(const uint8_t* rgba, size_t byteCount);

    /// Close the pipe and wait for ffmpeg to finalise the container. A recording
    /// that is not stopped produces a file that may not be playable.
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

    /// Is video recording available at all on this machine?
    static bool available() { return !findFfmpeg().empty(); }

private:
    std::FILE*  m_pipe = nullptr;
    std::string m_path;
    std::string m_command;      // kept so a failure can say what was run
    std::string m_ffmpegPath;
    std::string m_lastError;
    uint64_t    m_frameCount = 0;
    size_t      m_expectedFrameBytes = 0;
};

} // namespace Shoonyakasha
