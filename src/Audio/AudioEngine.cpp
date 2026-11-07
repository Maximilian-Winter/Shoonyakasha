#include "Audio/AudioEngine.h"

#include <miniaudio.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace Shoonyakasha::Audio {

namespace {

constexpr float LOWPASS_Q = 0.70710678f;       // Butterworth
constexpr float LOWPASS_MIN_HZ = 10.0f;
constexpr float NYQUIST_SHARE = 0.45f;         // the highest cutoff, as a share of the sample rate
constexpr float BUS_MIN_FADE = 0.005f;         // s

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

/// A two-pole low-pass whose cutoff moves to its target evenly in octaves
/// over the fade it was given, the coefficients set once per block. The
/// target and the fade arrive together in one atomic; a command equal to
/// the last one changes nothing.
struct LowpassNode {
    ma_node_base base;
    ma_lpf2 lpf;
    std::atomic<uint64_t> command{0};
    uint64_t lastCommand = 0;
    float octaves = 0.0f;         // log2 of the cutoff, audio thread only
    float targetOctaves = 0.0f;
    float step = 0.0f;            // octaves per frame
    uint32_t sampleRate = 0;
    uint32_t channels = 0;
    float maxHz = 0.0f;
};

void setLowpassCutoff(LowpassNode& node, float hz) {
    ma_lpf2_config config = ma_lpf2_config_init(ma_format_f32, node.channels, node.sampleRate, hz, LOWPASS_Q);
    ma_lpf2_reinit(&config, &node.lpf);
}

void processLowpass(ma_node* pNode, const float** ppFramesIn, ma_uint32* pFrameCountIn,
                    float** ppFramesOut, ma_uint32* pFrameCountOut) {
    (void)pFrameCountIn;
    auto* node = reinterpret_cast<LowpassNode*>(pNode);
    const ma_uint32 frames = *pFrameCountOut;

    const uint64_t command = node->command.load(std::memory_order_acquire);
    if (command != node->lastCommand) {
        node->lastCommand = command;
        const float hz = bitsFloat(static_cast<uint32_t>(command));
        const float fade = bitsFloat(static_cast<uint32_t>(command >> 32));
        node->targetOctaves = std::log2(std::clamp(hz, LOWPASS_MIN_HZ, node->maxHz));
        const float fadeFrames = fade * static_cast<float>(node->sampleRate);
        node->step = fadeFrames >= 1.0f ? std::fabs(node->targetOctaves - node->octaves) / fadeFrames : 0.0f;
    }
    if (node->octaves != node->targetOctaves) {
        const float move = node->step * static_cast<float>(frames);
        if (move <= 0.0f || std::fabs(node->targetOctaves - node->octaves) <= move) {
            node->octaves = node->targetOctaves;
        } else {
            node->octaves += node->targetOctaves > node->octaves ? move : -move;
        }
        setLowpassCutoff(*node, std::exp2(node->octaves));
    }
    ma_lpf2_process_pcm_frames(&node->lpf, ppFramesOut[0], ppFramesIn[0], frames);
}

ma_node_vtable g_lowpassVtable = {
    processLowpass,
    nullptr,    // onGetRequiredInputFrameCount
    1,          // input buses
    1,          // output buses
    0           // flags
};

struct Clip {
    std::string name;             // the file's path, or the samples' registered name
    bool stream = false;
    bool used = false;
    std::vector<float> samples;   // kept alive while registered
    double length = 0.0;          // s
};

struct Voice {
    ma_sound sound;
    ClipId clip = 0;
    uint32_t generation = 0;
    bool used = false;
    bool stopping = false;
};

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
    LowpassNode lowpass;
    bool lowpassReady = false;
    float lowpassHz = 0.0f;
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
        if (lowpassReady) {
            ma_node_uninit(&lowpass, nullptr);
            ma_lpf2_uninit(&lowpass.lpf, nullptr);
        }
        ma_engine_uninit(&engine);
        engineReady = false;
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

        LowpassNode& lp = s.lowpass;
        lp.sampleRate = ma_engine_get_sample_rate(&s.engine);
        lp.channels = s.channels;
        lp.maxHz = NYQUIST_SHARE * static_cast<float>(lp.sampleRate);
        lp.octaves = lp.targetOctaves = std::log2(lp.maxHz);
        s.lowpassHz = lp.maxHz;
        ma_lpf2_config lpfConfig = ma_lpf2_config_init(ma_format_f32, lp.channels, lp.sampleRate, lp.maxHz, LOWPASS_Q);
        if (ma_lpf2_init(&lpfConfig, nullptr, &lp.lpf) != MA_SUCCESS) {
            throw std::runtime_error("miniaudio could not make the low-pass filter");
        }
        ma_node_config nodeConfig = ma_node_config_init();
        nodeConfig.vtable = &g_lowpassVtable;
        nodeConfig.pInputChannels = &lp.channels;
        nodeConfig.pOutputChannels = &lp.channels;
        if (ma_node_init(ma_engine_get_node_graph(&s.engine), &nodeConfig, nullptr, &lp) != MA_SUCCESS) {
            ma_lpf2_uninit(&lp.lpf, nullptr);
            throw std::runtime_error("miniaudio could not make the low-pass node");
        }
        s.lowpassReady = true;
        ma_node_attach_output_bus(&s.lowpass, 0, &s.buses[0], 0);
        ma_node_attach_output_bus(&s.buses[static_cast<int>(Bus::World)], 0, &s.lowpass, 0);

        s.voiceCount = std::max<uint32_t>(config.voices, 1);
        s.voices = std::make_unique<Voice[]>(s.voiceCount);
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
    uint32_t slot = s.voiceCount;
    for (uint32_t i = 0; i < s.voiceCount; ++i) {
        if (!s.voices[i].used) {
            slot = i;
            break;
        }
    }
    if (slot == s.voiceCount) return 0;

    Voice& v = s.voices[slot];
    ma_uint32 flags = clip->stream ? MA_SOUND_FLAG_STREAM : MA_SOUND_FLAG_DECODE;
    if (!params.spatial) flags |= MA_SOUND_FLAG_NO_SPATIALIZATION;
    ma_sound_group* group = &s.buses[static_cast<int>(params.bus)];
    if (ma_sound_init_from_file(&s.engine, clip->name.c_str(), flags, group, nullptr, &v.sound) != MA_SUCCESS) {
        return 0;
    }
    v.used = true;
    v.stopping = false;
    v.clip = id;
    ++v.generation;
    if (v.generation == 0) v.generation = 1;

    ma_sound_set_volume(&v.sound, params.volume);
    ma_sound_set_pitch(&v.sound, params.pitch);
    ma_sound_set_looping(&v.sound, params.loop ? MA_TRUE : MA_FALSE);
    if (params.spatial) {
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
    ma_sound_set_position(&v->sound, position[0], position[1], position[2]);
    ma_sound_set_velocity(&v->sound, velocity[0], velocity[1], velocity[2]);
}

void AudioEngine::setVoiceVolume(VoiceId id, float volume) {
    if (Voice* v = m_impl->voice(id)) ma_sound_set_volume(&v->sound, volume);
}

void AudioEngine::setVoicePitch(VoiceId id, float pitch) {
    if (Voice* v = m_impl->voice(id)) ma_sound_set_pitch(&v->sound, pitch);
}

int AudioEngine::activeVoices() const {
    int count = 0;
    for (uint32_t i = 0; i < m_impl->voiceCount; ++i) {
        Voice& v = m_impl->voices[i];
        if (v.used && !Impl::finished(v)) ++count;
    }
    return count;
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
    LowpassNode& lp = m_impl->lowpass;
    m_impl->lowpassHz = std::clamp(cutoffHz, LOWPASS_MIN_HZ, lp.maxHz);
    const uint64_t command = (static_cast<uint64_t>(floatBits(std::max(fade, 0.0f))) << 32) | floatBits(cutoffHz);
    lp.command.store(command, std::memory_order_release);
}

float AudioEngine::worldLowpass() const { return m_impl->lowpassHz; }

void AudioEngine::setListener(const AudioVec3& forward, const AudioVec3& up, const AudioVec3& velocity) {
    ma_engine& e = m_impl->engine;
    ma_engine_listener_set_direction(&e, 0, forward[0], forward[1], forward[2]);
    ma_engine_listener_set_world_up(&e, 0, up[0], up[1], up[2]);
    ma_engine_listener_set_velocity(&e, 0, velocity[0], velocity[1], velocity[2]);
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
