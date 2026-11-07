#include "Audio/AudioEngine.h"
#include "Audio/SteamNodes.h"

#include <miniaudio.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace Shoonyakasha::Audio {

namespace {

constexpr float FILTER_Q = 0.70710678f;         // Butterworth
constexpr float FILTER_MIN_HZ = 10.0f;
constexpr float NYQUIST_SHARE = 0.45f;         // the highest cutoff, as a share of the sample rate
constexpr float BUS_MIN_FADE = 0.005f;         // s
constexpr float TAKEN_FADE = 0.005f;           // s a voice taken over by a louder one fades out over
constexpr float SPEED_OF_SOUND = 343.3f;       // m/s, as miniaudio's Doppler shift has it

ma_uint64 milliseconds(float seconds) {
    return static_cast<ma_uint64>(std::lround(std::max(seconds, 0.0f) * 1000.0f));
}

uint32_t floatBits(float value) {
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof bits);
    return bits;
}

float bitsFloat(uint32_t bits) {
    float value;
    std::memcpy(&value, &bits, sizeof value);
    return value;
}

/// One cutoff of a FilterNode. Its target and fade arrive together in one
/// atomic, and a command equal to the last one changes nothing; the cutoff
/// moves to its target evenly in octaves over the fade.
struct Cutoff {
    std::atomic<uint64_t> command{0};
    uint64_t lastCommand = 0;
    float octaves = 0.0f;         // log2 of the cutoff, audio thread only
    float targetOctaves = 0.0f;
    float step = 0.0f;            // octaves per frame

    void send(float hz, float fade) {
        command.store((static_cast<uint64_t>(floatBits(std::max(fade, 0.0f))) << 32) | floatBits(hz),
                      std::memory_order_release);
    }

    /// At `hz` at once; only while its node is not attached.
    void reset(float hz) {
        send(hz, 0.0f);
        lastCommand = command.load(std::memory_order_relaxed);
        octaves = targetOctaves = std::log2(hz);
        step = 0.0f;
    }

    /// Moved on by `frames`, the target held within [minHz, maxHz]; whether it moved.
    bool advance(ma_uint32 frames, uint32_t sampleRate, float minHz, float maxHz) {
        const uint64_t now = command.load(std::memory_order_acquire);
        if (now != lastCommand) {
            lastCommand = now;
            const float hz = bitsFloat(static_cast<uint32_t>(now));
            const float fade = bitsFloat(static_cast<uint32_t>(now >> 32));
            targetOctaves = std::log2(std::clamp(hz, minHz, maxHz));
            const float fadeFrames = fade * static_cast<float>(sampleRate);
            step = fadeFrames >= 1.0f ? std::fabs(targetOctaves - octaves) / fadeFrames : 0.0f;
        }
        if (octaves == targetOctaves) return false;
        const float move = step * static_cast<float>(frames);
        if (move <= 0.0f || std::fabs(targetOctaves - octaves) <= move) {
            octaves = targetOctaves;
        } else {
            octaves += targetOctaves > octaves ? move : -move;
        }
        return true;
    }
};

/// A two-pole low-pass then a two-pole high-pass, each cutoff a Cutoff,
/// the coefficients set once per block. Open, the low-pass is at maxHz and
/// the high-pass at minHz.
struct FilterNode {
    ma_node_base base;
    ma_lpf2 lpf;
    ma_hpf2 hpf;
    Cutoff low;
    Cutoff high;
    uint32_t sampleRate = 0;
    uint32_t channels = 0;
    float minHz = FILTER_MIN_HZ;
    float maxHz = 0.0f;
    bool ready = false;
};

void processFilter(ma_node* pNode, const float** ppFramesIn, ma_uint32* pFrameCountIn,
                   float** ppFramesOut, ma_uint32* pFrameCountOut) {
    (void)pFrameCountIn;
    auto* node = reinterpret_cast<FilterNode*>(pNode);
    const ma_uint32 frames = *pFrameCountOut;
    if (node->low.advance(frames, node->sampleRate, node->minHz, node->maxHz)) {
        ma_lpf2_config config = ma_lpf2_config_init(ma_format_f32, node->channels, node->sampleRate,
                                                    std::exp2(node->low.octaves), FILTER_Q);
        ma_lpf2_reinit(&config, &node->lpf);
    }
    if (node->high.advance(frames, node->sampleRate, node->minHz, node->maxHz)) {
        ma_hpf2_config config = ma_hpf2_config_init(ma_format_f32, node->channels, node->sampleRate,
                                                    std::exp2(node->high.octaves), FILTER_Q);
        ma_hpf2_reinit(&config, &node->hpf);
    }
    ma_lpf2_process_pcm_frames(&node->lpf, ppFramesOut[0], ppFramesIn[0], frames);
    ma_hpf2_process_pcm_frames(&node->hpf, ppFramesOut[0], ppFramesOut[0], frames);
}

ma_node_vtable g_filterVtable = {
    processFilter,
    nullptr,    // onGetRequiredInputFrameCount
    1,          // input buses
    1,          // output buses
    0           // flags
};

/// The node made in `graph`, open, not attached; false when miniaudio could not.
bool initFilter(FilterNode& node, ma_node_graph* graph, uint32_t sampleRate, uint32_t channels) {
    node.sampleRate = sampleRate;
    node.channels = channels;
    node.maxHz = NYQUIST_SHARE * static_cast<float>(sampleRate);
    node.low.reset(node.maxHz);
    node.high.reset(node.minHz);
    ma_lpf2_config lpfConfig = ma_lpf2_config_init(ma_format_f32, channels, sampleRate, node.maxHz, FILTER_Q);
    if (ma_lpf2_init(&lpfConfig, nullptr, &node.lpf) != MA_SUCCESS) return false;
    ma_hpf2_config hpfConfig = ma_hpf2_config_init(ma_format_f32, channels, sampleRate, node.minHz, FILTER_Q);
    if (ma_hpf2_init(&hpfConfig, nullptr, &node.hpf) != MA_SUCCESS) {
        ma_lpf2_uninit(&node.lpf, nullptr);
        return false;
    }
    ma_node_config nodeConfig = ma_node_config_init();
    nodeConfig.vtable = &g_filterVtable;
    nodeConfig.pInputChannels = &node.channels;
    nodeConfig.pOutputChannels = &node.channels;
    if (ma_node_init(graph, &nodeConfig, nullptr, &node) != MA_SUCCESS) {
        ma_hpf2_uninit(&node.hpf, nullptr);
        ma_lpf2_uninit(&node.lpf, nullptr);
        return false;
    }
    node.ready = true;
    return true;
}

void uninitFilter(FilterNode& node) {
    if (!node.ready) return;
    ma_node_uninit(&node, nullptr);
    ma_hpf2_uninit(&node.hpf, nullptr);
    ma_lpf2_uninit(&node.lpf, nullptr);
    node.ready = false;
}

/// The biquad's past samples zeroed on every channel (ma_biquad_clear_cache
/// zeroes the first channel's only).
void clearBiquad(ma_biquad& bq) {
    for (ma_uint32 c = 0; c < bq.channels; ++c) {
        bq.pR1[c].f32 = 0.0f;
        bq.pR2[c].f32 = 0.0f;
    }
}

/// A filter, not attached, set to `lowpass` and `highpass` at once (0: open)
/// with its filters' past samples cleared.
void resetFilter(FilterNode& node, float lowpass, float highpass) {
    const float low = lowpass > 0.0f ? std::clamp(lowpass, node.minHz, node.maxHz) : node.maxHz;
    const float high = highpass > 0.0f ? std::clamp(highpass, node.minHz, node.maxHz) : node.minHz;
    node.low.reset(low);
    node.high.reset(high);
    ma_lpf2_config lpfConfig = ma_lpf2_config_init(ma_format_f32, node.channels, node.sampleRate, low, FILTER_Q);
    ma_lpf2_reinit(&lpfConfig, &node.lpf);
    ma_hpf2_config hpfConfig = ma_hpf2_config_init(ma_format_f32, node.channels, node.sampleRate, high, FILTER_Q);
    ma_hpf2_reinit(&hpfConfig, &node.hpf);
    clearBiquad(node.lpf.bq);
    clearBiquad(node.hpf.bq);
}

struct Clip {
    std::string name;             // the file's path, or the samples' registered name
    bool stream = false;
    bool used = false;
    std::vector<float> samples;   // kept alive while registered
    double length = 0.0;          // s
};

struct Voice {
    ma_sound sound;
    FilterNode filter;            // between the sound and its bus while `filtered`
    ClipId clip = 0;
    uint32_t generation = 0;
    bool used = false;
    bool stopping = false;
    bool filtered = false;
    // What its loudness is reckoned from (`loudness`).
    uint8_t category = 0;
    uint8_t priority = 128;
    bool spatial = false;
    float volume = 1.0f;
    AudioVec3 position{0.0f, 0.0f, 0.0f};
    float minDistance = 1.0f;
    float maxDistance = 500.0f;
    float rolloff = 1.0f;
    // A binaural voice: its sound mono through `binaural`, then its filter.
    Steam::BinauralNode binaural;
    bool binauralOn = false;
    AudioVec3 velocity{0.0f, 0.0f, 0.0f};
    float basePitch = 1.0f;
    float doppler = 1.0f;
};

float dot3(const AudioVec3& a, const AudioVec3& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

AudioVec3 cross3(const AudioVec3& a, const AudioVec3& b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}

AudioVec3 unit3(const AudioVec3& a, const AudioVec3& otherwise) {
    const float d = std::sqrt(dot3(a, a));
    return d > 1e-6f ? AudioVec3{a[0] / d, a[1] / d, a[2] / d} : otherwise;
}

/// The gain of the inverse falloff at `position` from the listener.
float falloff(const AudioVec3& position, float minDistance, float maxDistance, float rolloff) {
    if (minDistance >= maxDistance) return 1.0f;
    const float d = std::sqrt(position[0] * position[0] + position[1] * position[1] + position[2] * position[2]);
    const float held = std::clamp(d, minDistance, maxDistance);
    return minDistance / (minDistance + rolloff * (held - minDistance));
}

/// How loud a voice is heard: its volume times its falloff.
float loudness(const Voice& v) {
    return v.volume * (v.spatial ? falloff(v.position, v.minDistance, v.maxDistance, v.rolloff) : 1.0f);
}

float loudness(const PlayParams& p) {
    return p.volume * (p.spatial ? falloff(p.position, p.minDistance, p.maxDistance, p.rolloff) : 1.0f);
}

}  // namespace

struct AudioEngine::Impl {
    ma_engine engine;
    bool engineReady = false;
    bool device = false;
    uint32_t channels = 0;
    ma_sound_group buses[BusCount];
    bool busReady[BusCount] = {};
    float busVolumes[BusCount] = {1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
    bool busPausedFlags[BusCount] = {};
    FilterNode world;             // the World bus's, its low-pass only
    float lowpassHz = 0.0f;
    ma_node_graph* graph = nullptr;
    Steam::Context steam;
    bool binauralWanted = false;
    Steam::ReverbNode reverb;     // fed from the World bus by `splitter`
    ma_splitter_node splitter;
    bool splitterReady = false;
    float reverbTimes[3] = {0.0f, 0.0f, 0.0f};
    float reverbWet = 0.0f;
    // The listener's frame: right, up and forward, and its velocity.
    AudioVec3 right{1.0f, 0.0f, 0.0f};
    AudioVec3 up{0.0f, 1.0f, 0.0f};
    AudioVec3 forward{0.0f, 0.0f, -1.0f};
    AudioVec3 listenerVelocity{0.0f, 0.0f, 0.0f};
    int categoryLimits[CategoryCount] = {};   // 0: no limit
    std::vector<Clip> clips;      // ClipId is the index + 1
    std::unique_ptr<Voice[]> voices;
    uint32_t voiceCount = 0;
    uint32_t samplesCounter = 0;

    ma_resource_manager* resources() { return ma_engine_get_resource_manager(&engine); }

    Clip* clip(ClipId id) {
        if (id == 0 || id > clips.size() || !clips[id - 1].used) return nullptr;
        return &clips[id - 1];
    }

    Voice* voice(VoiceId id) const {
        const auto slot = static_cast<uint32_t>(id & 0xffffffffu);
        const auto generation = static_cast<uint32_t>(id >> 32);
        if (id == 0 || slot >= voiceCount) return nullptr;
        Voice& v = voices[slot];
        if (!v.used || v.generation != generation) return nullptr;
        return &v;
    }

    static bool finished(Voice& v) {
        return ma_sound_at_end(&v.sound) || (v.stopping && !ma_sound_is_playing(&v.sound));
    }

    void release(Voice& v) {
        ma_sound_uninit(&v.sound);
        if (v.binauralOn) ma_node_detach_output_bus(&v.binaural, 0);
        if (v.filtered) ma_node_detach_output_bus(&v.filter, 0);
        v.binauralOn = false;
        v.filtered = false;
        v.used = false;
        v.stopping = false;
        v.clip = 0;
    }

    void reclaim() {
        for (uint32_t i = 0; i < voiceCount; ++i) {
            if (voices[i].used && finished(voices[i])) release(voices[i]);
        }
    }

    void shutdown() {
        if (!engineReady) return;
        if (device) ma_engine_stop(&engine);
        for (uint32_t i = 0; i < voiceCount; ++i) {
            if (voices[i].used) release(voices[i]);
        }
        for (Clip& c : clips) {
            if (!c.used) continue;
            if (c.stream) continue;
            if (c.samples.empty()) {
                ma_resource_manager_unregister_file(resources(), c.name.c_str());
            } else {
                ma_resource_manager_unregister_data(resources(), c.name.c_str());
            }
        }
        clips.clear();
        for (int b = BusCount - 1; b >= 0; --b) {
            if (busReady[b]) ma_sound_group_uninit(&buses[b]);
        }
        for (uint32_t i = 0; i < voiceCount; ++i) {
            uninitFilter(voices[i].filter);
            Steam::uninitBinaural(voices[i].binaural);
        }
        if (splitterReady) ma_splitter_node_uninit(&splitter, nullptr);
        splitterReady = false;
        Steam::uninitReverb(reverb);
        uninitFilter(world);
        ma_engine_uninit(&engine);
        Steam::release(steam);
        engineReady = false;
    }

    /// A binaural voice's direction in the listener's frame, its falloff
    /// and its Doppler shift (as miniaudio's: c - doppler times each
    /// velocity along the line from the source to the listener) set from
    /// its position and velocity.
    void updateBinaural(Voice& v) {
        const AudioVec3& p = v.position;
        const float d = std::sqrt(dot3(p, p));
        float direction[3] = {0.0f, 0.0f, -1.0f};
        float shift = 1.0f;
        if (d > 1e-4f) {
            const AudioVec3 local = unit3({dot3(p, right), dot3(p, up), -dot3(p, forward)}, {0.0f, 0.0f, -1.0f});
            direction[0] = local[0];
            direction[1] = local[1];
            direction[2] = local[2];
            if (v.doppler > 0.0f) {
                const AudioVec3 toListener{-p[0] / d, -p[1] / d, -p[2] / d};
                const float most = SPEED_OF_SOUND / v.doppler;
                const float vl = std::min(dot3(toListener, listenerVelocity), most);
                const float vs = std::min(dot3(toListener, v.velocity), most * 0.99f);
                shift = (SPEED_OF_SOUND - v.doppler * vl) / (SPEED_OF_SOUND - v.doppler * vs);
            }
        }
        Steam::setBinaural(v.binaural, direction, falloff(p, v.minDistance, v.maxDistance, v.rolloff));
        ma_sound_set_pitch(&v.sound, v.basePitch * shift);
    }

    /// The slot's binaural node made on first use; false when it cannot be.
    bool ensureBinaural(Voice& v) {
        return v.binaural.ready || Steam::initBinaural(v.binaural, steam, graph);
    }
};

AudioEngine::AudioEngine(const AudioConfig& config) : m_impl(std::make_unique<Impl>()) {
    Impl& s = *m_impl;
    ma_engine_config engineConfig = ma_engine_config_init();
    engineConfig.sampleRate = config.sampleRate;
    engineConfig.channels = config.channels;
    engineConfig.listenerCount = 1;
    engineConfig.defaultVolumeSmoothTimeInPCMFrames = config.sampleRate / 200;    // 5 ms
    engineConfig.noDevice = config.device ? MA_FALSE : MA_TRUE;
    ma_result result = ma_engine_init(&engineConfig, &s.engine);
    if (result != MA_SUCCESS && config.device) {
        engineConfig.noDevice = MA_TRUE;
        result = ma_engine_init(&engineConfig, &s.engine);
    }
    if (result != MA_SUCCESS) {
        throw std::runtime_error(std::string("miniaudio could not start: ") + ma_result_description(result));
    }
    s.engineReady = true;
    s.device = engineConfig.noDevice == MA_FALSE;
    s.channels = ma_engine_get_channels(&s.engine);

    try {
        if (ma_sound_group_init(&s.engine, 0, nullptr, &s.buses[0]) != MA_SUCCESS) {
            throw std::runtime_error("miniaudio could not make the master bus");
        }
        s.busReady[0] = true;
        for (int b = 1; b < BusCount; ++b) {
            if (ma_sound_group_init(&s.engine, 0, &s.buses[0], &s.buses[b]) != MA_SUCCESS) {
                throw std::runtime_error("miniaudio could not make a bus");
            }
            s.busReady[b] = true;
        }

        ma_node_graph* graph = ma_engine_get_node_graph(&s.engine);
        const uint32_t rate = ma_engine_get_sample_rate(&s.engine);
        if (!initFilter(s.world, graph, rate, s.channels)) {
            throw std::runtime_error("miniaudio could not make the World bus's low-pass");
        }
        s.lowpassHz = s.world.maxHz;
        s.graph = graph;
        if (s.channels == 2) Steam::init(s.steam, rate);
        if (s.steam.ready && Steam::initReverb(s.reverb, s.steam, graph)) {
            ma_splitter_node_config splitterConfig = ma_splitter_node_config_init(s.channels);
            if (ma_splitter_node_init(graph, &splitterConfig, nullptr, &s.splitter) == MA_SUCCESS) {
                s.splitterReady = true;
                ma_node_attach_output_bus(&s.splitter, 0, &s.buses[0], 0);
                ma_node_attach_output_bus(&s.reverb, 0, &s.buses[0], 0);
                ma_node_attach_output_bus(&s.splitter, 1, &s.reverb, 0);
                ma_node_attach_output_bus(&s.world, 0, &s.splitter, 0);
            } else {
                Steam::uninitReverb(s.reverb);
            }
        }
        if (!s.splitterReady) ma_node_attach_output_bus(&s.world, 0, &s.buses[0], 0);
        ma_node_attach_output_bus(&s.buses[static_cast<int>(Bus::World)], 0, &s.world, 0);

        s.voiceCount = std::max<uint32_t>(config.voices, 1);
        s.voices = std::make_unique<Voice[]>(s.voiceCount);
        for (uint32_t i = 0; i < s.voiceCount; ++i) {
            if (!initFilter(s.voices[i].filter, graph, rate, s.channels)) {
                throw std::runtime_error("miniaudio could not make a voice's filter");
            }
        }
    } catch (...) {
        s.shutdown();
        throw;
    }
}

AudioEngine::~AudioEngine() {
    m_impl->shutdown();
}

bool AudioEngine::hasDevice() const { return m_impl->device; }

uint32_t AudioEngine::sampleRate() const { return ma_engine_get_sample_rate(&m_impl->engine); }

uint32_t AudioEngine::channels() const { return m_impl->channels; }

uint64_t AudioEngine::timeFrames() const { return ma_engine_get_time_in_pcm_frames(&m_impl->engine); }

ClipId AudioEngine::load(const std::string& path, bool stream) {
    Impl& s = *m_impl;
    if (!stream) {
        ma_result result = ma_resource_manager_register_file(s.resources(), path.c_str(),
                                                            MA_RESOURCE_MANAGER_DATA_SOURCE_FLAG_DECODE);
        if (result != MA_SUCCESS) {
            throw std::runtime_error("could not load sound '" + path + "': " + ma_result_description(result));
        }
    }
    ma_sound probe;
    const ma_uint32 flags = (stream ? MA_SOUND_FLAG_STREAM : MA_SOUND_FLAG_DECODE) | MA_SOUND_FLAG_NO_DEFAULT_ATTACHMENT;
    ma_result result = ma_sound_init_from_file(&s.engine, path.c_str(), flags, nullptr, nullptr, &probe);
    if (result != MA_SUCCESS) {
        if (!stream) ma_resource_manager_unregister_file(s.resources(), path.c_str());
        throw std::runtime_error("could not load sound '" + path + "': " + ma_result_description(result));
    }
    float length = 0.0f;
    ma_sound_get_length_in_seconds(&probe, &length);
    ma_sound_uninit(&probe);

    Clip clip;
    clip.name = path;
    clip.stream = stream;
    clip.used = true;
    clip.length = length;
    s.clips.push_back(std::move(clip));
    return static_cast<ClipId>(s.clips.size());
}

ClipId AudioEngine::loadSamples(const float* samples, uint64_t frames, uint32_t channels, uint32_t sampleRate) {
    Impl& s = *m_impl;
    if (samples == nullptr || frames == 0 || channels == 0 || sampleRate == 0) {
        throw std::invalid_argument("loadSamples needs samples, a channel count and a sample rate");
    }
    Clip clip;
    clip.name = "shoonyakasha-samples:" + std::to_string(++s.samplesCounter);
    clip.samples.assign(samples, samples + frames * channels);
    clip.used = true;
    clip.length = static_cast<double>(frames) / sampleRate;
    ma_result result = ma_resource_manager_register_decoded_data(s.resources(), clip.name.c_str(), clip.samples.data(),
                                                                 frames, ma_format_f32, channels, sampleRate);
    if (result != MA_SUCCESS) {
        throw std::runtime_error(std::string("could not register samples: ") + ma_result_description(result));
    }
    s.clips.push_back(std::move(clip));
    return static_cast<ClipId>(s.clips.size());
}

void AudioEngine::unload(ClipId id) {
    Impl& s = *m_impl;
    Clip* clip = s.clip(id);
    if (clip == nullptr) return;
    for (uint32_t i = 0; i < s.voiceCount; ++i) {
        if (s.voices[i].used && s.voices[i].clip == id) s.release(s.voices[i]);
    }
    if (!clip->stream) {
        if (clip->samples.empty()) {
            ma_resource_manager_unregister_file(s.resources(), clip->name.c_str());
        } else {
            ma_resource_manager_unregister_data(s.resources(), clip->name.c_str());
        }
    }
    clip->used = false;
    clip->samples = {};
}

double AudioEngine::clipLength(ClipId id) const {
    const Clip* clip = m_impl->clip(id);
    return clip == nullptr ? 0.0 : clip->length;
}

VoiceId AudioEngine::play(ClipId id, const PlayParams& params) {
    Impl& s = *m_impl;
    Clip* clip = s.clip(id);
    if (clip == nullptr) return 0;
    s.reclaim();
    const float loud = loudness(params);
    const int limit = params.category < CategoryCount ? s.categoryLimits[params.category] : 0;
    if (params.category > 0 && limit > 0) {
        int count = 0;
        Voice* quietest = nullptr;
        for (uint32_t i = 0; i < s.voiceCount; ++i) {
            Voice& v = s.voices[i];
            if (!v.used || v.stopping || v.category != params.category || Impl::finished(v)) continue;
            ++count;
            if (quietest == nullptr || loudness(v) < loudness(*quietest)) quietest = &v;
        }
        if (count >= limit) {
            if (quietest == nullptr || loudness(*quietest) >= loud) return 0;
            ma_sound_stop_with_fade_in_milliseconds(&quietest->sound, milliseconds(TAKEN_FADE));
            quietest->stopping = true;
        }
    }
    uint32_t slot = s.voiceCount;
    for (uint32_t i = 0; i < s.voiceCount; ++i) {
        if (!s.voices[i].used) {
            slot = i;
            break;
        }
    }
    if (slot == s.voiceCount) {
        // Every slot taken: a voice fading out gives its slot up first; else
        // the voice of the lowest priority, the quietest of those, if it is
        // below the new one's priority, or of it and quieter.
        auto rank = [](const Voice& v) { return v.stopping ? -1 : static_cast<int>(v.priority); };
        uint32_t victim = s.voiceCount;
        for (uint32_t i = 0; i < s.voiceCount; ++i) {
            const Voice& v = s.voices[i];
            if (victim == s.voiceCount || rank(v) < rank(s.voices[victim])
                || (rank(v) == rank(s.voices[victim]) && loudness(v) < loudness(s.voices[victim]))) {
                victim = i;
            }
        }
        const Voice& v = s.voices[victim];
        if (!v.stopping && (v.priority > params.priority || (v.priority == params.priority && loudness(v) >= loud))) {
            return 0;
        }
        s.release(s.voices[victim]);
        slot = victim;
    }

    Voice& v = s.voices[slot];
    const bool binaural = params.spatial && binauralAvailable() && s.binauralWanted && s.ensureBinaural(v);
    const bool filtered = params.filtered || binaural;
    ma_uint32 flags = clip->stream ? MA_SOUND_FLAG_STREAM : MA_SOUND_FLAG_DECODE;
    if (!params.spatial || binaural) flags |= MA_SOUND_FLAG_NO_SPATIALIZATION;
    if (filtered) flags |= MA_SOUND_FLAG_NO_DEFAULT_ATTACHMENT;
    ma_sound_group* group = &s.buses[static_cast<int>(params.bus)];
    ma_sound_config soundConfig = ma_sound_config_init_2(&s.engine);
    soundConfig.pFilePath = clip->name.c_str();
    soundConfig.flags = flags;
    soundConfig.pInitialAttachment = filtered ? nullptr : group;
    if (binaural) soundConfig.channelsOut = 1;
    if (ma_sound_init_ex(&s.engine, &soundConfig, &v.sound) != MA_SUCCESS) {
        return 0;
    }
    if (filtered) {
        resetFilter(v.filter, params.lowpass, params.highpass);
        ma_node_attach_output_bus(&v.filter, 0, group, 0);
        if (binaural) {
            Steam::resetBinaural(v.binaural);
            ma_node_attach_output_bus(&v.binaural, 0, &v.filter, 0);
            ma_node_attach_output_bus(&v.sound, 0, &v.binaural, 0);
        } else {
            ma_node_attach_output_bus(&v.sound, 0, &v.filter, 0);
        }
    }
    v.filtered = filtered;
    v.binauralOn = binaural;
    v.velocity = params.velocity;
    v.basePitch = params.pitch;
    v.doppler = params.doppler;
    v.category = params.category;
    v.priority = params.priority;
    v.spatial = params.spatial;
    v.volume = params.volume;
    v.position = params.position;
    v.minDistance = params.minDistance;
    v.maxDistance = params.maxDistance;
    v.rolloff = params.rolloff;
    v.used = true;
    v.stopping = false;
    v.clip = id;
    ++v.generation;
    if (v.generation == 0) v.generation = 1;

    ma_sound_set_volume(&v.sound, params.volume);
    ma_sound_set_pitch(&v.sound, params.pitch);
    ma_sound_set_looping(&v.sound, params.loop ? MA_TRUE : MA_FALSE);
    if (binaural) {
        s.updateBinaural(v);
    } else if (params.spatial) {
        ma_sound_set_positioning(&v.sound, ma_positioning_absolute);
        ma_sound_set_attenuation_model(&v.sound, ma_attenuation_model_inverse);
        ma_sound_set_position(&v.sound, params.position[0], params.position[1], params.position[2]);
        ma_sound_set_velocity(&v.sound, params.velocity[0], params.velocity[1], params.velocity[2]);
        ma_sound_set_min_distance(&v.sound, params.minDistance);
        ma_sound_set_max_distance(&v.sound, params.maxDistance);
        ma_sound_set_rolloff(&v.sound, params.rolloff);
        ma_sound_set_doppler_factor(&v.sound, params.doppler);
    }
    if (params.fadeIn > 0.0f) {
        ma_sound_set_fade_in_milliseconds(&v.sound, 0.0f, 1.0f, milliseconds(params.fadeIn));
    }
    if (params.delay > 0.0f) {
        const auto frames = static_cast<ma_uint64>(std::llround(params.delay * static_cast<double>(sampleRate())));
        ma_sound_set_start_time_in_pcm_frames(&v.sound, timeFrames() + frames);
    }
    ma_sound_start(&v.sound);
    return (static_cast<VoiceId>(v.generation) << 32) | slot;
}

void AudioEngine::stop(VoiceId id, float fade) {
    Voice* v = m_impl->voice(id);
    if (v == nullptr) return;
    if (fade > 0.0f) {
        ma_sound_stop_with_fade_in_milliseconds(&v->sound, milliseconds(fade));
    } else {
        ma_sound_stop(&v->sound);
    }
    v->stopping = true;
}

bool AudioEngine::active(VoiceId id) const {
    Voice* v = m_impl->voice(id);
    return v != nullptr && !Impl::finished(*v);
}

void AudioEngine::setVoicePosition(VoiceId id, const AudioVec3& position, const AudioVec3& velocity) {
    Voice* v = m_impl->voice(id);
    if (v == nullptr) return;
    v->position = position;
    v->velocity = velocity;
    if (v->binauralOn) {
        m_impl->updateBinaural(*v);
        return;
    }
    ma_sound_set_position(&v->sound, position[0], position[1], position[2]);
    ma_sound_set_velocity(&v->sound, velocity[0], velocity[1], velocity[2]);
}

void AudioEngine::setVoiceFilter(VoiceId id, float lowpass, float highpass, float fade) {
    Voice* v = m_impl->voice(id);
    if (v == nullptr || !v->filtered) return;
    v->filter.low.send(lowpass > 0.0f ? lowpass : v->filter.maxHz, fade);
    v->filter.high.send(highpass > 0.0f ? highpass : v->filter.minHz, fade);
}

void AudioEngine::setVoiceVolume(VoiceId id, float volume) {
    if (Voice* v = m_impl->voice(id)) {
        v->volume = volume;
        ma_sound_set_volume(&v->sound, volume);
    }
}

void AudioEngine::setVoicePitch(VoiceId id, float pitch) {
    Voice* v = m_impl->voice(id);
    if (v == nullptr) return;
    v->basePitch = pitch;
    if (v->binauralOn) {
        m_impl->updateBinaural(*v);
    } else {
        ma_sound_set_pitch(&v->sound, pitch);
    }
}

bool AudioEngine::binauralAvailable() const { return m_impl->steam.ready && m_impl->channels == 2; }

void AudioEngine::setBinaural(bool on) { m_impl->binauralWanted = on; }

bool AudioEngine::binaural() const { return m_impl->binauralWanted && binauralAvailable(); }

bool AudioEngine::reverbAvailable() const { return m_impl->splitterReady; }

void AudioEngine::setReverb(float low, float mid, float high, float wet) {
    Impl& s = *m_impl;
    s.reverbTimes[0] = std::max(low, 0.0f);
    s.reverbTimes[1] = std::max(mid, 0.0f);
    s.reverbTimes[2] = std::max(high, 0.0f);
    s.reverbWet = std::clamp(wet, 0.0f, 1.0f);
    s.reverb.low.store(s.reverbTimes[0], std::memory_order_relaxed);
    s.reverb.mid.store(s.reverbTimes[1], std::memory_order_relaxed);
    s.reverb.high.store(s.reverbTimes[2], std::memory_order_relaxed);
    s.reverb.wet.store(s.reverbWet, std::memory_order_relaxed);
}

std::array<float, 3> AudioEngine::reverbTimes() const {
    return {m_impl->reverbTimes[0], m_impl->reverbTimes[1], m_impl->reverbTimes[2]};
}

float AudioEngine::reverbWet() const { return m_impl->reverbWet; }

int AudioEngine::activeVoices(int category) const {
    int count = 0;
    for (uint32_t i = 0; i < m_impl->voiceCount; ++i) {
        Voice& v = m_impl->voices[i];
        if (v.used && !Impl::finished(v) && (category < 0 || v.category == category)) ++count;
    }
    return count;
}

void AudioEngine::setCategoryLimit(uint8_t category, int limit) {
    if (category > 0 && category < CategoryCount) m_impl->categoryLimits[category] = std::max(limit, 0);
}

int AudioEngine::categoryLimit(uint8_t category) const {
    return category < CategoryCount ? m_impl->categoryLimits[category] : 0;
}

void AudioEngine::setBusVolume(Bus bus, float volume, float fade) {
    const int b = static_cast<int>(bus);
    m_impl->busVolumes[b] = volume;
    ma_sound_group_set_fade_in_milliseconds(&m_impl->buses[b], -1.0f, volume,
                                            milliseconds(std::max(fade, BUS_MIN_FADE)));
}

float AudioEngine::busVolume(Bus bus) const { return m_impl->busVolumes[static_cast<int>(bus)]; }

void AudioEngine::pauseBus(Bus bus) {
    const int b = static_cast<int>(bus);
    ma_sound_group_stop(&m_impl->buses[b]);
    m_impl->busPausedFlags[b] = true;
}

void AudioEngine::resumeBus(Bus bus) {
    const int b = static_cast<int>(bus);
    ma_sound_group_start(&m_impl->buses[b]);
    m_impl->busPausedFlags[b] = false;
}

bool AudioEngine::busPaused(Bus bus) const { return m_impl->busPausedFlags[static_cast<int>(bus)]; }

void AudioEngine::setWorldLowpass(float cutoffHz, float fade) {
    FilterNode& world = m_impl->world;
    m_impl->lowpassHz = std::clamp(cutoffHz, world.minHz, world.maxHz);
    world.low.send(cutoffHz, fade);
}

float AudioEngine::worldLowpass() const { return m_impl->lowpassHz; }

void AudioEngine::setListener(const AudioVec3& forward, const AudioVec3& up, const AudioVec3& velocity) {
    ma_engine& e = m_impl->engine;
    ma_engine_listener_set_direction(&e, 0, forward[0], forward[1], forward[2]);
    ma_engine_listener_set_world_up(&e, 0, up[0], up[1], up[2]);
    ma_engine_listener_set_velocity(&e, 0, velocity[0], velocity[1], velocity[2]);
    Impl& s = *m_impl;
    s.forward = unit3(forward, {0.0f, 0.0f, -1.0f});
    s.right = unit3(cross3(s.forward, up), {1.0f, 0.0f, 0.0f});
    s.up = cross3(s.right, s.forward);
    s.listenerVelocity = velocity;
    for (uint32_t i = 0; i < s.voiceCount; ++i) {
        Voice& v = s.voices[i];
        if (v.used && v.binauralOn) s.updateBinaural(v);
    }
}

uint64_t AudioEngine::read(float* out, uint64_t frames) {
    if (m_impl->device || out == nullptr) return 0;
    ma_uint64 framesRead = 0;
    ma_engine_read_pcm_frames(&m_impl->engine, out, frames, &framesRead);
    return framesRead;
}

Bus busFromName(const std::string& name) {
    if (name == "master") return Bus::Master;
    if (name == "world") return Bus::World;
    if (name == "suit") return Bus::Suit;
    if (name == "ui") return Bus::UI;
    if (name == "music") return Bus::Music;
    throw std::invalid_argument("no audio bus named '" + name + "'");
}

}  // namespace Shoonyakasha::Audio
