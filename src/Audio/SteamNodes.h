#pragma once

// Steam Audio inside miniaudio's node graph (Audio/AudioEngine.cpp): a
// binaural node for a voice, and a parametric reverb node for the World bus.
// Each node runs its Steam Audio effect on blocks of BLOCK frames, so its
// output is BLOCK frames late.

#include <miniaudio.h>
#include <phonon.h>

#include <atomic>
#include <cstdint>

namespace Shoonyakasha::Audio::Steam {

constexpr uint32_t BLOCK = 256;          // frames an effect is run on at once

/// Steam Audio's context and its default HRTF, at one sample rate.
struct Context {
    IPLContext context = nullptr;
    IPLHRTF hrtf = nullptr;
    IPLAudioSettings audio{};
    bool ready = false;
};

/// False when Steam Audio cannot start.
bool init(Context& steam, uint32_t sampleRate);
void release(Context& steam);

/// Mono in, stereo out: the HRTF for a direction in the listener's frame
/// (+x right, +y up, -z ahead), times a gain ramped over each block.
struct BinauralNode {
    ma_node_base base;
    IPLBinauralEffect effect = nullptr;
    IPLHRTF hrtf = nullptr;
    std::atomic<float> dx{0.0f}, dy{0.0f}, dz{-1.0f};
    std::atomic<float> gain{1.0f};
    float lastGain = 0.0f;       // audio thread only
    uint32_t pos = 0;
    float input[BLOCK] = {};
    float left[BLOCK] = {};
    float right[BLOCK] = {};
    bool ready = false;
};

/// The node made in `graph`, not attached; false when it could not be.
bool initBinaural(BinauralNode& node, Context& steam, ma_node_graph* graph);
/// Its effect's and its blocks' past cleared, the gain to ramp up from 0;
/// only while it is not attached.
void resetBinaural(BinauralNode& node);
/// `direction` a unit vector in the listener's frame.
void setBinaural(BinauralNode& node, const float direction[3], float gain);
void uninitBinaural(BinauralNode& node);

/// Stereo in and out: each channel through its own parametric reverb (three
/// bands' RT60 in seconds), the wet signal eased to `wet` over about
/// WET_EASE seconds.
constexpr float WET_EASE = 0.3f;
constexpr float IR_SECONDS = 2.0f;

struct ReverbNode {
    ma_node_base base;
    IPLReflectionEffect effects[2] = {nullptr, nullptr};
    std::atomic<float> low{0.0f}, mid{0.0f}, high{0.0f};
    std::atomic<float> wet{0.0f};
    float lastWet = 0.0f;        // audio thread only
    uint32_t pos = 0;
    uint32_t sampleRate = 0;
    int irSize = 0;
    float inLeft[BLOCK] = {};
    float inRight[BLOCK] = {};
    float outLeft[BLOCK] = {};
    float outRight[BLOCK] = {};
    bool ready = false;
};

bool initReverb(ReverbNode& node, Context& steam, ma_node_graph* graph);
void uninitReverb(ReverbNode& node);

}  // namespace Shoonyakasha::Audio::Steam
