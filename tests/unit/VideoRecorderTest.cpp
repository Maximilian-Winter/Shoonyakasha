//
// VideoRecorderTest.cpp - the ffmpeg command a recording is built from
//
// Tier 1: no GPU, no ffmpeg process. These assert the contents of the command
// string that VideoRecorder::buildArguments produces.
//
// Container-level checks (codec, frame size, frame rate, frame count) do not
// detect a filter that transforms the image, so the arguments are asserted
// directly. See DoesNotFlipTheImage.
//

#include <gtest/gtest.h>

#include "Capture/VideoRecorder.h"

using namespace Shoonyakasha;

namespace {

std::string argumentsFor(uint32_t width = 1920, uint32_t height = 1080,
                         VideoRecorder::Options options = {}) {
    return VideoRecorder::buildArguments("out.mkv", width, height, options);
}

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

} // namespace

TEST(VideoRecorderCommand, DoesNotFlipTheImage) {
    // Vulkan's framebuffer origin is top-left and ffmpeg reads rawvideo top row
    // first, so readback rows are already in the expected order. A flip or
    // transpose filter here inverts every frame.
    const std::string args = argumentsFor();

    EXPECT_FALSE(contains(args, "vflip")) << args;
    EXPECT_FALSE(contains(args, "hflip")) << args;
    EXPECT_FALSE(contains(args, "transpose")) << args;
    EXPECT_FALSE(contains(args, "-vf")) << args;
}

TEST(VideoRecorderCommand, DescribesTheRawFramesItWillBeFed) {
    // These must match what writeFrame() sends: RGBA, that frame size, on
    // stdin. A mismatch is not reported as an error; ffmpeg reinterprets the
    // bytes and produces a valid file with wrong contents.
    const std::string args = argumentsFor(1280, 720);

    EXPECT_TRUE(contains(args, "-f rawvideo")) << args;
    EXPECT_TRUE(contains(args, "-pixel_format rgba")) << args;
    EXPECT_TRUE(contains(args, "-video_size 1280x720")) << args;
    EXPECT_TRUE(contains(args, "-i -")) << args;
}

TEST(VideoRecorderCommand, CarriesTheRequestedEncodingOptions) {
    VideoRecorder::Options options;
    options.fps = 60;
    options.quality = 23;
    options.codec = "libx265";

    const std::string args = argumentsFor(640, 480, options);

    EXPECT_TRUE(contains(args, "-framerate 60")) << args;
    EXPECT_TRUE(contains(args, "-crf 23")) << args;
    EXPECT_TRUE(contains(args, "-c:v libx265")) << args;
}

TEST(VideoRecorderCommand, EncodesToSomethingPlayersAccept) {
    EXPECT_TRUE(contains(argumentsFor(), "-pix_fmt yuv420p"));
    EXPECT_TRUE(contains(argumentsFor(), "-an"));  // no audio stream
}

TEST(VideoRecorderCommand, QuotesTheOutputPathAndPutsItLast) {
    // The output path must be quoted and must be the final argument; otherwise
    // ffmpeg reads it as the value of the preceding option.
    VideoRecorder::Options options;
    const std::string args =
        VideoRecorder::buildArguments("C:/some folder/clip.mkv", 640, 480, options);

    EXPECT_TRUE(contains(args, "\"C:/some folder/clip.mkv\"")) << args;
    EXPECT_EQ(args.size() - std::string("\"C:/some folder/clip.mkv\"").size(),
              args.rfind("\"C:/some folder/clip.mkv\"")) << args;
}

TEST(VideoRecorderCommand, ExtraArgsLandBeforeTheOutputPath) {
    VideoRecorder::Options options;
    options.extraArgs = "-preset ultrafast";

    const std::string args = argumentsFor(640, 480, options);

    ASSERT_TRUE(contains(args, "-preset ultrafast")) << args;
    EXPECT_LT(args.find("-preset ultrafast"), args.find("\"out.mkv\"")) << args;
}

TEST(VideoRecorderCommand, RejectsOddFrameSizesBeforeStarting) {
    // 4:2:0 chroma requires even dimensions. start() reports this before
    // opening the pipe.
    VideoRecorder recorder;

    EXPECT_FALSE(recorder.start("out.mkv", 801, 500));
    EXPECT_NE(std::string::npos, recorder.lastError().find("not even"))
        << recorder.lastError();

    EXPECT_FALSE(recorder.start("out.mkv", 0, 0));
    EXPECT_FALSE(recorder.isRecording());
}

TEST(VideoRecorderCommand, WriteFrameWithoutStartingFails) {
    VideoRecorder recorder;
    const uint8_t pixel[4] = {0, 0, 0, 0};

    EXPECT_FALSE(recorder.writeFrame(pixel, sizeof(pixel)));
    EXPECT_EQ(0u, recorder.frameCount());
    EXPECT_TRUE(recorder.stop());  // stopping when not recording succeeds
}
