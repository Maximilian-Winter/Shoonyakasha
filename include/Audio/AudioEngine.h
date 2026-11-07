#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Shoonyakasha::Audio {

using AudioVec3 = std::array<float, 3>;

/// A clip: decoded sound data, or a file streamed by each voice. 0 is no clip.
using ClipId = uint32_t;

/// A voice: one playing sound, named by its slot and the slot's generation,
/// so a handle kept after its voice ended names nothing. 0 is no voice.
using VoiceId = uint64_t;

/// The mix: every voice plays into one of World, Suit, UI or Music, and those
/// four into Master. World goes through a low-pass filter on its way
/// (setWorldLowpass).
enum class Bus : uint8_t { Master = 0, World = 1, Suit = 2, UI = 3, Music = 4 };
constexpr int BusCount = 5;

struct AudioConfig {
    uint32_t sampleRate = 48000;
    uint32_t channels = 2;
    uint32_t voices = 64;       ///< voices that can play at once
    /// When false, no device is opened and the mix is pulled by read();
    /// when true and no device can be opened, the engine runs as if false
    /// (hasDevice is then false).
    bool device = true;
};

/// How a voice starts. A spatial voice's position and velocity are relative
/// to the listener, which is at the origin; its gain falls off as
/// minDistance / (minDistance + rolloff * (d - minDistance)) beyond
/// minDistance, with d held at maxDistance beyond it. A voice with a delay
/// starts that many seconds of the mix's time after play. A filtered voice
/// goes through its own low-pass then high-pass, at `lowpass` and
/// `highpass` Hz (0: open), which setVoiceFilter moves.
struct PlayParams {
    Bus bus = Bus::World;
    float volume = 1.0f;
    float pitch = 1.0f;
    bool loop = false;
    bool spatial = true;
    AudioVec3 position{0.0f, 0.0f, 0.0f};    // m
    AudioVec3 velocity{0.0f, 0.0f, 0.0f};    // m/s
    float minDistance = 1.0f;                // m
    float maxDistance = 500.0f;              // m
    float rolloff = 1.0f;
    float doppler = 1.0f;                    ///< 0 turns the Doppler shift off
    float delay = 0.0f;                      // s
    float fadeIn = 0.0f;                     // s
    bool filtered = false;
    float lowpass = 0.0f;                    // Hz; 0: open
    float highpass = 0.0f;                   // Hz; 0: open
};

/// Sound on miniaudio: clips, a pool of voices, the buses and one listener.
/// Its methods are called from one thread; miniaudio mixes on its own.
///
/// Clips are files (WAV, FLAC or MP3), decoded at load and shared between
/// their voices, or streamed from disk by each voice, or samples given in
/// memory (loadSamples). A voice that ends, or is stopped and has faded out,
/// gives its slot back; play returns 0 when every slot is taken.
class AudioEngine {
public:
    /// Throws std::runtime_error when miniaudio cannot start.
    explicit AudioEngine(const AudioConfig& config = {});
    ~AudioEngine();
    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;

    bool hasDevice() const;
    uint32_t sampleRate() const;
    uint32_t channels() const;
    /// Frames the mix has produced since the engine started.
    uint64_t timeFrames() const;

    /// A file's clip; throws std::runtime_error when it cannot be read.
    ClipId load(const std::string& path, bool stream = false);
    /// A clip of interleaved float samples, copied.
    ClipId loadSamples(const float* samples, uint64_t frames, uint32_t channels, uint32_t sampleRate);
    /// The clip removed, its voices stopped at once.
    void unload(ClipId clip);
    /// The clip's length in seconds, 0 for no clip.
    double clipLength(ClipId clip) const;

    /// Returns 0 for no clip or no free voice.
    VoiceId play(ClipId clip, const PlayParams& params = {});
    /// The voice stopped, fading out over `fade` seconds.
    void stop(VoiceId voice, float fade = 0.0f);
    /// Whether the voice still holds its slot: waiting for its delay,
    /// playing, or fading out.
    bool active(VoiceId voice) const;
    void setVoicePosition(VoiceId voice, const AudioVec3& position, const AudioVec3& velocity = {0.0f, 0.0f, 0.0f});
    /// A filtered voice's cutoffs (Hz; 0: open), reached over `fade`
    /// seconds, moving evenly in octaves, held between 10 Hz and 0.45 of
    /// the sample rate; nothing for a voice played unfiltered.
    void setVoiceFilter(VoiceId voice, float lowpass, float highpass, float fade = 0.0f);
    void setVoiceVolume(VoiceId voice, float volume);
    void setVoicePitch(VoiceId voice, float pitch);
    int activeVoices() const;

    /// The bus's volume, reached linearly over `fade` seconds (at least 5 ms).
    void setBusVolume(Bus bus, float volume, float fade = 0.0f);
    float busVolume(Bus bus) const;
    /// A paused bus is silent and its voices hold their place.
    void pauseBus(Bus bus);
    void resumeBus(Bus bus);
    bool busPaused(Bus bus) const;

    /// The World bus's low-pass cutoff, reached over `fade` seconds,
    /// moving evenly in octaves.
    void setWorldLowpass(float cutoffHz, float fade = 0.0f);
    float worldLowpass() const;

    /// The listener's facing, up and velocity; it is at the origin. Its
    /// right is forward x up.
    void setListener(const AudioVec3& forward, const AudioVec3& up,
                     const AudioVec3& velocity = {0.0f, 0.0f, 0.0f});

    /// Without a device: the next `frames` frames of the mix, interleaved,
    /// into `out` (frames * channels floats). Returns the frames written.
    uint64_t read(float* out, uint64_t frames);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

/// A bus by name: "master", "world", "suit", "ui" or "music". Throws
/// std::invalid_argument for any other.
Bus busFromName(const std::string& name);

}  // namespace Shoonyakasha::Audio
