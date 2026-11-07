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

/// Voices are of a category, 1 to CategoryCount - 1, each with a limit of
/// voices (setCategoryLimit), or of none (0), which has no limit.
constexpr int CategoryCount = 16;

struct AudioConfig {
    uint32_t sampleRate = 48000;
    uint32_t channels = 2;
    uint32_t voices = 64;       ///< voices that can play at once
    /// When false, no device is opened and the mix is pulled by read();
    /// when true and no device can be opened, the engine runs as if false
    /// (hasDevice is then false).
    bool device = true;
    /// With `device`: a device on miniaudio's null backend, which takes the
    /// mix in real time and plays it nowhere.
    bool nullDevice = false;
};

/// How a voice starts. A spatial voice's position and velocity are relative
/// to the listener, which is at the origin; its gain falls off as
/// minDistance / (minDistance + rolloff * (d - minDistance)) beyond
/// minDistance, with d held at maxDistance beyond it; one farther than 1.1
/// times maxDistance is not played (play returns 0). A voice with a delay
/// starts that many seconds of the mix's time after play. A filtered voice
/// goes through its own low-pass then high-pass, at `lowpass` and
/// `highpass` Hz (0: open), which setVoiceFilter moves.
///
/// A voice's loudness is its volume times its falloff where it is (1 when
/// not spatial). With its category's limit reached, a new voice takes over
/// the quietest of the category's (which fades out over 5 ms) when it is
/// louder, else it is not played. With every slot taken, a voice fading
/// out gives its slot up first; else the voice of the lowest priority, the
/// quietest of those, gives it up to a new voice of higher priority, or of
/// the same and louder; else the new voice is not played.
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
    uint8_t category = 0;                    ///< 0: none
    uint8_t priority = 128;
};

/// Sound on miniaudio: clips, a pool of voices, the buses and one listener.
/// Its methods are called from one thread; miniaudio mixes on its own.
///
/// Clips are files (WAV, FLAC or MP3), decoded at load and shared between
/// their voices, or streamed from disk by each voice, or samples given in
/// memory (loadSamples). A voice that ends, or is stopped and has faded out,
/// gives its slot back; play returns 0 when no slot can be had (PlayParams).
class AudioEngine {
public:
    /// Throws std::runtime_error when miniaudio cannot start.
    explicit AudioEngine(const AudioConfig& config = {});
    ~AudioEngine();
    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;

    bool hasDevice() const;
    /// With a device: when it has stopped on its own (unplugged, say), the
    /// default device opened in its place, at most once a second, the mix
    /// going on where it was. Returns whether the device plays. Call it
    /// once a frame.
    bool keepDevice();
    /// Whether the device stopped on its own and has not been opened again.
    bool deviceLost() const;
#ifdef SHOONYAKASHA_TESTING
    /// The device stopped as if it had stopped on its own.
    void loseDeviceForTesting();
#endif
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

    /// Returns 0 for no clip, or when its category's limit or every slot
    /// keeps it from playing (PlayParams).
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
    /// Voices holding a slot, of `category` (-1: all).
    int activeVoices(int category = -1) const;
    /// At most `limit` voices of `category` (1 to CategoryCount - 1) play
    /// at once; 0 is no limit.
    void setCategoryLimit(uint8_t category, int limit);
    int categoryLimit(uint8_t category) const;

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

    /// Binaural sound on Steam Audio (its default HRTF), with two channels:
    /// while on, a spatial voice played goes mono through the HRTF for its
    /// direction from the listener in place of miniaudio's panning, with the
    /// same falloff and Doppler shift, and through its own filter (as a
    /// filtered voice); it is 256 frames late. A voice keeps how it was
    /// played. binaural() is whether it is on and available.
    bool binauralAvailable() const;
    void setBinaural(bool on);
    bool binaural() const;

    /// The World bus's reverb on Steam Audio: each channel through a
    /// parametric reverb of RT60 `low`, `mid` and `high` seconds (three
    /// bands), its wet signal at `wet` (0 to 1, eased over about 0.3 s)
    /// beside the dry, 256 frames late. Off (wet 0) at first.
    bool reverbAvailable() const;
    void setReverb(float low, float mid, float high, float wet);
    std::array<float, 3> reverbTimes() const;
    float reverbWet() const;

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
