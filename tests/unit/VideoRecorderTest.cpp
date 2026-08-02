//
// VideoRecorderTest.cpp - the ffmpeg command a recording is built from
//
// Tier 1: no GPU, no ffmpeg. These assert the command string, because that is
// where the one bug this feature has had actually lived.
//
// A recording came out upside down for its whole first life, and the checks in
// place at the time -- codec name, frame size, frame rate, frame count, "does
// ffprobe read it back" -- were all green, because every one of them is blind
// to orientation. The frames were fine; a "-vf vflip" in the command was not.
// So: pin what goes into the command, not just what comes out of the file.
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
    // Vulkan's origin is top-left and ffmpeg reads rawvideo top row first, so
    // the two already agree. Any flip or transpose here inverts every frame.
    const std::string args = argumentsFor();

    EXPECT_FALSE(contains(args, "vflip")) << args;
    EXPECT_FALSE(contains(args, "hflip")) << args;
    EXPECT_FALSE(contains(args, "transpose")) << args;
    EXPECT_FALSE(contains(args, "-vf")) << args;
}

TEST(VideoRecorderCommand, DescribesTheRawFramesItWillBeFed) {
    // These four have to match what writeFrame() actually sends, byte for
    // byte: RGBA, that size, from stdin. A mismatch is not a decode error --
    // ffmpeg reinterprets the bytes and produces a plausible, wrong picture.
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
    EXPECT_TRUE(contains(argumentsFor(), "-an"));  // no audio stream to wait on
}

TEST(VideoRecorderCommand, QuotesTheOutputPathAndPutsItLast) {
    // Paths with spaces are the common case on Windows, and the path has to be
    // the final argument or ffmpeg reads it as an option's value.
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
    // 4:2:0 chroma needs even dimensions. Saying so up front beats letting
    // ffmpeg die after the first frame is already down the pipe.
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
    EXPECT_TRUE(recorder.stop());  // stopping when idle is not an error
}
