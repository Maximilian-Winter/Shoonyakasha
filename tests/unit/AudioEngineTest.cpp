//
// AudioEngineTest.cpp - the audio engine on miniaudio, without a device
//
// Every engine here is made with AudioConfig::device = false, so the mix is
// pulled with read() and the tests hear exactly what a device would.
//

#include <gtest/gtest.h>

#include "Audio/AudioEngine.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>

using namespace Shoonyakasha::Audio;

namespace {

constexpr uint32_t RATE = 48000;

AudioConfig headless(uint32_t voices = 64) {
    AudioConfig config;
    config.device = false;
    config.sampleRate = RATE;
    config.channels = 2;
    config.voices = voices;
    return config;
}

/// One channel of `seconds` of a cosine at `hz`, amplitude 0.5.
std::vector<float> tone(float hz, float seconds) {
    const auto frames = static_cast<size_t>(seconds * RATE);
    std::vector<float> samples(frames);
    for (size_t i = 0; i < frames; ++i) {
        samples[i] = 0.5f * std::cos(2.0f * 3.14159265f * hz * static_cast<float>(i) / RATE);
    }
    return samples;
}

ClipId loadTone(AudioEngine& audio, float hz, float seconds = 1.0f) {
    const std::vector<float> samples = tone(hz, seconds);
    return audio.loadSamples(samples.data(), samples.size(), 1, RATE);
}

std::vector<float> pull(AudioEngine& audio, uint64_t frames) {
    std::vector<float> out(frames * 2, 0.0f);
    EXPECT_EQ(audio.read(out.data(), frames), frames);
    return out;
}

/// The root mean square of one channel (0 left, 1 right, -1 both).
double rms(const std::vector<float>& stereo, int channel = -1) {
    double sum = 0.0;
    size_t count = 0;
    for (size_t i = 0; i < stereo.size(); ++i) {
        if (channel >= 0 && static_cast<int>(i % 2) != channel) continue;
        sum += static_cast<double>(stereo[i]) * stereo[i];
        ++count;
    }
    return count == 0 ? 0.0 : std::sqrt(sum / static_cast<double>(count));
}

/// A mono 16-bit WAV file of the samples at RATE.
void writeWav(const std::filesystem::path& path, const std::vector<float>& samples) {
    std::ofstream file(path, std::ios::binary);
    auto u32 = [&](uint32_t v) { file.write(reinterpret_cast<const char*>(&v), 4); };
    auto u16 = [&](uint16_t v) { file.write(reinterpret_cast<const char*>(&v), 2); };
    const auto bytes = static_cast<uint32_t>(samples.size() * 2);
    file.write("RIFF", 4);
    u32(36 + bytes);
    file.write("WAVEfmt ", 8);
    u32(16);
    u16(1);                 // PCM
    u16(1);                 // mono
    u32(RATE);
    u32(RATE * 2);          // bytes a second
    u16(2);                 // bytes a frame
    u16(16);                // bits a sample
    file.write("data", 4);
    u32(bytes);
    for (float s : samples) u16(static_cast<uint16_t>(static_cast<int16_t>(std::lround(s * 32767.0f))));
}

PlayParams plain(Bus bus = Bus::UI) {
    PlayParams params;
    params.bus = bus;
    params.spatial = false;
    params.loop = true;
    return params;
}

/// The level of a looped 1 kHz tone straight ahead at `distance` metres.
double levelAt(float distance) {
    AudioEngine audio(headless());
    PlayParams params;
    params.loop = true;
    params.position = {0.0f, 0.0f, -distance};
    params.minDistance = 1.0f;
    params.maxDistance = 1000.0f;
    audio.play(loadTone(audio, 1000.0f), params);
    pull(audio, 4800);                 // past the spatializer's gain smoothing
    return rms(pull(audio, 9600));
}

}  // namespace

TEST(AudioEngineTest, StartsWithoutADevice) {
    AudioEngine audio(headless());
    EXPECT_FALSE(audio.hasDevice());
    EXPECT_EQ(audio.sampleRate(), RATE);
    EXPECT_EQ(audio.channels(), 2u);
    EXPECT_EQ(audio.timeFrames(), 0u);
    pull(audio, 480);
    EXPECT_EQ(audio.timeFrames(), 480u);
}

TEST(AudioEngineTest, LevelFallsOffInverselyWithDistance) {
    const double near = levelAt(10.0f);
    const double far = levelAt(100.0f);
    ASSERT_GT(far, 0.0);
    EXPECT_NEAR(20.0 * std::log10(near / far), 20.0, 1.0);
}

TEST(AudioEngineTest, DelayedVoiceStartsOnTime) {
    AudioEngine audio(headless());
    PlayParams params = plain();
    params.delay = 0.5f;
    const VoiceId voice = audio.play(loadTone(audio, 1000.0f), params);
    EXPECT_TRUE(audio.active(voice));
    const std::vector<float> out = pull(audio, RATE);
    size_t first = out.size();
    for (size_t i = 0; i < out.size(); ++i) {
        if (std::fabs(out[i]) > 1e-6f) {
            first = i / 2;
            break;
        }
    }
    EXPECT_NEAR(static_cast<double>(first), RATE * 0.5, RATE * 0.001);
}

TEST(AudioEngineTest, PausedBusIsSilentWhileAnotherPlays) {
    AudioEngine audio(headless());
    const ClipId clip = loadTone(audio, 1000.0f);
    const VoiceId world = audio.play(clip, plain(Bus::World));
    const VoiceId ui = audio.play(clip, plain(Bus::UI));
    audio.pauseBus(Bus::World);
    EXPECT_TRUE(audio.busPaused(Bus::World));
    pull(audio, 480);
    EXPECT_GT(rms(pull(audio, 4800)), 0.1);

    audio.stop(ui);
    pull(audio, 480);
    EXPECT_EQ(rms(pull(audio, 4800)), 0.0);
    EXPECT_TRUE(audio.active(world));

    audio.resumeBus(Bus::World);
    EXPECT_FALSE(audio.busPaused(Bus::World));
    pull(audio, 480);
    EXPECT_GT(rms(pull(audio, 4800)), 0.1);
}

TEST(AudioEngineTest, StaleHandleNamesNothing) {
    AudioEngine audio(headless(1));
    PlayParams params = plain();
    params.loop = false;
    const ClipId clip = loadTone(audio, 1000.0f, 0.01f);
    const VoiceId first = audio.play(clip, params);
    ASSERT_NE(first, 0u);
    pull(audio, 4800);
    EXPECT_FALSE(audio.active(first));

    params.loop = true;
    const VoiceId second = audio.play(clip, params);
    ASSERT_NE(second, 0u);
    EXPECT_NE(first, second);
    audio.stop(first);
    EXPECT_TRUE(audio.active(second));
    EXPECT_EQ(audio.activeVoices(), 1);
}

TEST(AudioEngineTest, PlayReturnsNoVoiceWhenEverySlotIsTaken) {
    AudioEngine audio(headless(2));
    const ClipId clip = loadTone(audio, 1000.0f);
    EXPECT_NE(audio.play(clip, plain()), 0u);
    EXPECT_NE(audio.play(clip, plain()), 0u);
    EXPECT_EQ(audio.play(clip, plain()), 0u);
    EXPECT_EQ(audio.play(0, plain()), 0u);
}

TEST(AudioEngineTest, StoppedVoiceGivesItsSlotBackAfterItsFade) {
    AudioEngine audio(headless(1));
    const ClipId clip = loadTone(audio, 1000.0f);
    const VoiceId voice = audio.play(clip, plain());
    audio.stop(voice, 0.05f);
    EXPECT_TRUE(audio.active(voice));
    pull(audio, RATE / 10);
    EXPECT_FALSE(audio.active(voice));
    EXPECT_NE(audio.play(clip, plain()), 0u);
}

TEST(AudioEngineTest, BusVolumeScalesTheLevel) {
    AudioEngine audio(headless());
    audio.play(loadTone(audio, 1000.0f), plain(Bus::UI));
    pull(audio, 480);
    const double full = rms(pull(audio, 4800));
    audio.setBusVolume(Bus::UI, 0.5f);
    EXPECT_FLOAT_EQ(audio.busVolume(Bus::UI), 0.5f);
    pull(audio, 960);
    EXPECT_NEAR(rms(pull(audio, 4800)) / full, 0.5, 0.02);
}

TEST(AudioEngineTest, WorldLowpassCutsHighsOnTheWorldBusOnly) {
    AudioEngine audio(headless());
    const ClipId high = loadTone(audio, 8000.0f);
    const VoiceId world = audio.play(high, plain(Bus::World));
    pull(audio, 480);
    const double open = rms(pull(audio, 4800));
    audio.setWorldLowpass(500.0f);
    EXPECT_FLOAT_EQ(audio.worldLowpass(), 500.0f);
    pull(audio, 960);
    EXPECT_LT(rms(pull(audio, 4800)), 0.05 * open);

    audio.stop(world);
    audio.play(high, plain(Bus::Suit));
    pull(audio, 960);
    EXPECT_NEAR(rms(pull(audio, 4800)) / open, 1.0, 0.05);
}

TEST(AudioEngineTest, WorldLowpassMovesOverItsFade) {
    AudioEngine audio(headless());
    audio.play(loadTone(audio, 8000.0f), plain(Bus::World));
    pull(audio, 480);
    const double open = rms(pull(audio, 4800));
    audio.setWorldLowpass(500.0f, 0.5f);
    pull(audio, RATE / 10);
    const double early = rms(pull(audio, 2400));
    pull(audio, RATE / 2);
    const double late = rms(pull(audio, 2400));
    EXPECT_GT(early, 0.2 * open);
    EXPECT_LT(late, 0.05 * open);
}

TEST(AudioEngineTest, FilteredVoiceLowpassAndHighpass) {
    AudioEngine audio(headless());
    const ClipId high = loadTone(audio, 8000.0f);
    const ClipId low = loadTone(audio, 200.0f);
    VoiceId voice = audio.play(high, plain());
    pull(audio, 480);
    const double open = rms(pull(audio, 4800));
    audio.stop(voice);

    PlayParams params = plain();
    params.filtered = true;
    params.lowpass = 500.0f;
    voice = audio.play(high, params);
    pull(audio, 480);
    EXPECT_LT(rms(pull(audio, 4800)), 0.05 * open);
    audio.stop(voice);

    params.lowpass = 0.0f;
    params.highpass = 3000.0f;
    voice = audio.play(low, params);
    pull(audio, 480);
    EXPECT_LT(rms(pull(audio, 4800)), 0.05 * open);
    audio.stop(voice);

    params.highpass = 0.0f;
    voice = audio.play(high, params);
    pull(audio, 480);
    EXPECT_NEAR(rms(pull(audio, 4800)) / open, 1.0, 0.05);
}

TEST(AudioEngineTest, VoiceFilterMovesOverItsFade) {
    AudioEngine audio(headless());
    PlayParams params = plain();
    params.filtered = true;
    const VoiceId voice = audio.play(loadTone(audio, 8000.0f), params);
    pull(audio, 480);
    const double open = rms(pull(audio, 4800));
    audio.setVoiceFilter(voice, 500.0f, 0.0f, 0.5f);
    pull(audio, RATE / 10);
    const double early = rms(pull(audio, 2400));
    pull(audio, RATE / 2);
    const double late = rms(pull(audio, 2400));
    EXPECT_GT(early, 0.2 * open);
    EXPECT_LT(late, 0.05 * open);
    audio.setVoiceFilter(voice, 0.0f, 0.0f);
    pull(audio, 960);
    EXPECT_NEAR(rms(pull(audio, 4800)) / open, 1.0, 0.05);
}

TEST(AudioEngineTest, UnfilteredVoiceIgnoresAFilter) {
    AudioEngine audio(headless());
    const VoiceId voice = audio.play(loadTone(audio, 8000.0f), plain());
    pull(audio, 480);
    const double open = rms(pull(audio, 4800));
    audio.setVoiceFilter(voice, 500.0f, 0.0f);
    pull(audio, 960);
    EXPECT_NEAR(rms(pull(audio, 4800)) / open, 1.0, 0.02);
}

TEST(AudioEngineTest, SlotOfAFilteredVoiceServesAnUnfilteredOne) {
    AudioEngine audio(headless(1));
    const ClipId high = loadTone(audio, 8000.0f);
    PlayParams params = plain();
    params.filtered = true;
    params.lowpass = 500.0f;
    VoiceId voice = audio.play(high, params);
    pull(audio, 480);
    const double filtered = rms(pull(audio, 4800));
    audio.stop(voice);
    voice = audio.play(high, plain());
    ASSERT_NE(voice, 0u);
    pull(audio, 480);
    EXPECT_GT(rms(pull(audio, 4800)), 10.0 * filtered);
}

TEST(AudioEngineTest, CategoryLimitDropsAQuieterVoice) {
    AudioEngine audio(headless());
    audio.setCategoryLimit(1, 2);
    EXPECT_EQ(audio.categoryLimit(1), 2);
    const ClipId clip = loadTone(audio, 1000.0f);
    PlayParams params = plain();
    params.category = 1;
    EXPECT_NE(audio.play(clip, params), 0u);
    EXPECT_NE(audio.play(clip, params), 0u);
    params.volume = 0.5f;
    EXPECT_EQ(audio.play(clip, params), 0u);
    EXPECT_EQ(audio.activeVoices(1), 2);
    params.category = 0;
    EXPECT_NE(audio.play(clip, params), 0u);
    EXPECT_EQ(audio.activeVoices(), 3);
}

TEST(AudioEngineTest, CategoryLimitTakesOverTheQuietest) {
    AudioEngine audio(headless());
    audio.setCategoryLimit(2, 2);
    const ClipId clip = loadTone(audio, 1000.0f);
    PlayParams params = plain();
    params.category = 2;
    params.volume = 0.2f;
    const VoiceId quiet = audio.play(clip, params);
    params.volume = 0.5f;
    const VoiceId middle = audio.play(clip, params);
    params.volume = 1.0f;
    const VoiceId loud = audio.play(clip, params);
    ASSERT_NE(loud, 0u);
    pull(audio, 480);
    EXPECT_FALSE(audio.active(quiet));
    EXPECT_TRUE(audio.active(middle));
    EXPECT_TRUE(audio.active(loud));
    EXPECT_EQ(audio.activeVoices(2), 2);
}

TEST(AudioEngineTest, LoudnessCountsTheDistance) {
    AudioEngine audio(headless());
    audio.setCategoryLimit(3, 1);
    const ClipId clip = loadTone(audio, 1000.0f);
    PlayParams params;
    params.loop = true;
    params.category = 3;
    params.position = {0.0f, 0.0f, -100.0f};
    const VoiceId far = audio.play(clip, params);
    params.volume = 0.5f;
    params.position = {0.0f, 0.0f, -2.0f};
    const VoiceId near = audio.play(clip, params);
    ASSERT_NE(near, 0u);
    pull(audio, 480);
    EXPECT_FALSE(audio.active(far));
    audio.setVoicePosition(near, {0.0f, 0.0f, -400.0f});
    params.volume = 1.0f;
    params.position = {0.0f, 0.0f, -50.0f};
    EXPECT_NE(audio.play(clip, params), 0u);
}

TEST(AudioEngineTest, FullPoolGivesUpALowerPriority) {
    AudioEngine audio(headless(2));
    const ClipId clip = loadTone(audio, 1000.0f);
    PlayParams params = plain();
    params.priority = 100;
    const VoiceId a = audio.play(clip, params);
    params.volume = 0.5f;
    const VoiceId b = audio.play(clip, params);
    params.priority = 50;
    params.volume = 1.0f;
    EXPECT_EQ(audio.play(clip, params), 0u);
    params.priority = 200;
    const VoiceId c = audio.play(clip, params);
    ASSERT_NE(c, 0u);
    EXPECT_TRUE(audio.active(a));
    EXPECT_FALSE(audio.active(b));
    params.priority = 100;
    params.volume = 0.2f;
    EXPECT_EQ(audio.play(clip, params), 0u);
    audio.stop(a, 0.05f);
    const VoiceId d = audio.play(clip, params);
    EXPECT_NE(d, 0u);
    EXPECT_FALSE(audio.active(a));
}

TEST(AudioEngineTest, ListenerRightIsForwardCrossUp) {
    AudioEngine audio(headless());
    PlayParams params;
    params.loop = true;
    params.position = {5.0f, 0.0f, 0.0f};
    audio.play(loadTone(audio, 1000.0f), params);

    audio.setListener({0.0f, 0.0f, -1.0f}, {0.0f, 1.0f, 0.0f});
    pull(audio, 4800);
    std::vector<float> out = pull(audio, 4800);
    EXPECT_GT(rms(out, 1), 2.0 * rms(out, 0));

    audio.setListener({0.0f, 0.0f, 1.0f}, {0.0f, 1.0f, 0.0f});
    pull(audio, 4800);
    out = pull(audio, 4800);
    EXPECT_GT(rms(out, 0), 2.0 * rms(out, 1));
}

TEST(AudioEngineTest, UnloadStopsTheClipsVoices) {
    AudioEngine audio(headless());
    const ClipId clip = loadTone(audio, 1000.0f, 0.25f);
    EXPECT_NEAR(audio.clipLength(clip), 0.25, 1e-6);
    const VoiceId voice = audio.play(clip, plain());
    audio.unload(clip);
    EXPECT_FALSE(audio.active(voice));
    EXPECT_EQ(audio.activeVoices(), 0);
    EXPECT_EQ(audio.clipLength(clip), 0.0);
    EXPECT_EQ(audio.play(clip, plain()), 0u);
}

TEST(AudioEngineTest, LoadsAWavFileDecodedOrStreamed) {
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "shoonyakasha_audio_test.wav";
    writeWav(path, tone(1000.0f, 0.5f));
    AudioEngine audio(headless());
    for (bool stream : {false, true}) {
        const ClipId clip = audio.load(path.string(), stream);
        EXPECT_NEAR(audio.clipLength(clip), 0.5, 1e-3);
        const VoiceId voice = audio.play(clip, plain());
        pull(audio, 480);
        EXPECT_GT(rms(pull(audio, 4800)), 0.1);
        audio.stop(voice);
        audio.unload(clip);
    }
    std::filesystem::remove(path);
}

TEST(AudioEngineTest, MissingFileThrows) {
    AudioEngine audio(headless());
    EXPECT_THROW(audio.load("no/such/sound.wav"), std::runtime_error);
    EXPECT_THROW(audio.load("no/such/sound.wav", true), std::runtime_error);
}

TEST(AudioEngineTest, BusesByName) {
    EXPECT_EQ(busFromName("master"), Bus::Master);
    EXPECT_EQ(busFromName("world"), Bus::World);
    EXPECT_EQ(busFromName("suit"), Bus::Suit);
    EXPECT_EQ(busFromName("ui"), Bus::UI);
    EXPECT_EQ(busFromName("music"), Bus::Music);
    EXPECT_THROW(busFromName("drums"), std::invalid_argument);
}
