#include "Audio/SteamNodes.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Shoonyakasha::Audio::Steam {

namespace {

ma_uint32 g_mono[1] = {1};
ma_uint32 g_stereo[1] = {2};

void runBinaural(BinauralNode& n) {
    const float gain = n.gain.load(std::memory_order_relaxed);
    for (uint32_t k = 0; k < BLOCK; ++k) {
        n.input[k] *= n.lastGain + (gain - n.lastGain) * static_cast<float>(k + 1) / static_cast<float>(BLOCK);
    }
    n.lastGain = gain;
    IPLBinauralEffectParams params{};
    params.direction = IPLVector3{n.dx.load(std::memory_order_relaxed), n.dy.load(std::memory_order_relaxed),
                                  n.dz.load(std::memory_order_relaxed)};
    params.interpolation = IPL_HRTFINTERPOLATION_BILINEAR;
    params.spatialBlend = 1.0f;
    params.hrtf = n.hrtf;
    float* in[1] = {n.input};
    float* out[2] = {n.left, n.right};
    IPLAudioBuffer inBuffer{1, static_cast<IPLint32>(BLOCK), in};
    IPLAudioBuffer outBuffer{2, static_cast<IPLint32>(BLOCK), out};
    iplBinauralEffectApply(n.effect, &params, &inBuffer, &outBuffer);
}

void processBinaural(ma_node* pNode, const float** ppFramesIn, ma_uint32* pFrameCountIn,
                     float** ppFramesOut, ma_uint32* pFrameCountOut) {
    (void)pFrameCountIn;
    auto* n = reinterpret_cast<BinauralNode*>(pNode);
    const float* x = ppFramesIn[0];
    float* y = ppFramesOut[0];
    for (ma_uint32 i = 0; i < *pFrameCountOut; ++i) {
        y[2 * i] = n->left[n->pos];
        y[2 * i + 1] = n->right[n->pos];
        n->input[n->pos] = x[i];
        if (++n->pos == BLOCK) {
            runBinaural(*n);
            n->pos = 0;
        }
    }
}

ma_node_vtable g_binauralVtable = {processBinaural, nullptr, 1, 1, 0};

void runReverb(ReverbNode& n) {
    const float target = n.wet.load(std::memory_order_relaxed);
    const float ease = 1.0f - std::exp(-static_cast<float>(BLOCK) / (WET_EASE * static_cast<float>(n.sampleRate)));
    const float from = n.lastWet;
    const float to = from + (target - from) * ease;
    n.lastWet = to;
    if (from <= 0.0f && to <= 1e-5f) {
        std::fill(std::begin(n.outLeft), std::end(n.outLeft), 0.0f);
        std::fill(std::begin(n.outRight), std::end(n.outRight), 0.0f);
        n.lastWet = 0.0f;
        return;
    }
    IPLReflectionEffectParams params{};
    params.type = IPL_REFLECTIONEFFECTTYPE_PARAMETRIC;
    params.reverbTimes[0] = n.low.load(std::memory_order_relaxed);
    params.reverbTimes[1] = n.mid.load(std::memory_order_relaxed);
    params.reverbTimes[2] = n.high.load(std::memory_order_relaxed);
    params.numChannels = 1;
    params.irSize = n.irSize;
    float* ins[2] = {n.inLeft, n.inRight};
    float* outs[2] = {n.outLeft, n.outRight};
    for (int c = 0; c < 2; ++c) {
        float* in[1] = {ins[c]};
        float* out[1] = {outs[c]};
        IPLAudioBuffer inBuffer{1, static_cast<IPLint32>(BLOCK), in};
        IPLAudioBuffer outBuffer{1, static_cast<IPLint32>(BLOCK), out};
        iplReflectionEffectApply(n.effects[c], &params, &inBuffer, &outBuffer, nullptr);
    }
    for (uint32_t k = 0; k < BLOCK; ++k) {
        const float w = from + (to - from) * static_cast<float>(k + 1) / static_cast<float>(BLOCK);
        n.outLeft[k] *= w;
        n.outRight[k] *= w;
    }
}

void processReverb(ma_node* pNode, const float** ppFramesIn, ma_uint32* pFrameCountIn,
                   float** ppFramesOut, ma_uint32* pFrameCountOut) {
    (void)pFrameCountIn;
    auto* n = reinterpret_cast<ReverbNode*>(pNode);
    const float* x = ppFramesIn[0];
    float* y = ppFramesOut[0];
    for (ma_uint32 i = 0; i < *pFrameCountOut; ++i) {
        y[2 * i] = n->outLeft[n->pos];
        y[2 * i + 1] = n->outRight[n->pos];
        n->inLeft[n->pos] = x[2 * i];
        n->inRight[n->pos] = x[2 * i + 1];
        if (++n->pos == BLOCK) {
            runReverb(*n);
            n->pos = 0;
        }
    }
}

ma_node_vtable g_reverbVtable = {processReverb, nullptr, 1, 1, 0};

}  // namespace

bool init(Context& steam, uint32_t sampleRate) {
    IPLContextSettings contextSettings{};
    contextSettings.version = STEAMAUDIO_VERSION;
    contextSettings.simdLevel = IPL_SIMDLEVEL_AVX2;
    if (iplContextCreate(&contextSettings, &steam.context) != IPL_STATUS_SUCCESS) {
        steam.context = nullptr;
        return false;
    }
    steam.audio.samplingRate = static_cast<IPLint32>(sampleRate);
    steam.audio.frameSize = static_cast<IPLint32>(BLOCK);
    IPLHRTFSettings hrtfSettings{};
    hrtfSettings.type = IPL_HRTFTYPE_DEFAULT;
    hrtfSettings.volume = 1.0f;
    hrtfSettings.normType = IPL_HRTFNORMTYPE_NONE;
    if (iplHRTFCreate(steam.context, &steam.audio, &hrtfSettings, &steam.hrtf) != IPL_STATUS_SUCCESS) {
        steam.hrtf = nullptr;
        iplContextRelease(&steam.context);
        return false;
    }
    steam.ready = true;
    return true;
}

void release(Context& steam) {
    if (steam.hrtf != nullptr) iplHRTFRelease(&steam.hrtf);
    if (steam.context != nullptr) iplContextRelease(&steam.context);
    steam.ready = false;
}

bool initBinaural(BinauralNode& node, Context& steam, ma_node_graph* graph) {
    if (!steam.ready) return false;
    IPLBinauralEffectSettings settings{};
    settings.hrtf = steam.hrtf;
    if (iplBinauralEffectCreate(steam.context, &steam.audio, &settings, &node.effect) != IPL_STATUS_SUCCESS) {
        node.effect = nullptr;
        return false;
    }
    node.hrtf = steam.hrtf;
    ma_node_config config = ma_node_config_init();
    config.vtable = &g_binauralVtable;
    config.pInputChannels = g_mono;
    config.pOutputChannels = g_stereo;
    if (ma_node_init(graph, &config, nullptr, &node) != MA_SUCCESS) {
        iplBinauralEffectRelease(&node.effect);
        return false;
    }
    node.ready = true;
    return true;
}

void resetBinaural(BinauralNode& node) {
    iplBinauralEffectReset(node.effect);
    std::fill(std::begin(node.input), std::end(node.input), 0.0f);
    std::fill(std::begin(node.left), std::end(node.left), 0.0f);
    std::fill(std::begin(node.right), std::end(node.right), 0.0f);
    node.pos = 0;
    node.lastGain = 0.0f;
}

void setBinaural(BinauralNode& node, const float direction[3], float gain) {
    node.dx.store(direction[0], std::memory_order_relaxed);
    node.dy.store(direction[1], std::memory_order_relaxed);
    node.dz.store(direction[2], std::memory_order_relaxed);
    node.gain.store(gain, std::memory_order_relaxed);
}

void uninitBinaural(BinauralNode& node) {
    if (!node.ready) return;
    ma_node_uninit(&node, nullptr);
    iplBinauralEffectRelease(&node.effect);
    node.ready = false;
}

bool initReverb(ReverbNode& node, Context& steam, ma_node_graph* graph) {
    if (!steam.ready) return false;
    node.sampleRate = static_cast<uint32_t>(steam.audio.samplingRate);
    node.irSize = static_cast<int>(IR_SECONDS * static_cast<float>(node.sampleRate));
    IPLReflectionEffectSettings settings{};
    settings.type = IPL_REFLECTIONEFFECTTYPE_PARAMETRIC;
    settings.irSize = node.irSize;
    settings.numChannels = 1;
    for (int c = 0; c < 2; ++c) {
        if (iplReflectionEffectCreate(steam.context, &steam.audio, &settings, &node.effects[c]) != IPL_STATUS_SUCCESS) {
            node.effects[c] = nullptr;
            if (c == 1) iplReflectionEffectRelease(&node.effects[0]);
            return false;
        }
    }
    ma_node_config config = ma_node_config_init();
    config.vtable = &g_reverbVtable;
    config.pInputChannels = g_stereo;
    config.pOutputChannels = g_stereo;
    if (ma_node_init(graph, &config, nullptr, &node) != MA_SUCCESS) {
        iplReflectionEffectRelease(&node.effects[0]);
        iplReflectionEffectRelease(&node.effects[1]);
        return false;
    }
    node.ready = true;
    return true;
}

void uninitReverb(ReverbNode& node) {
    if (!node.ready) return;
    ma_node_uninit(&node, nullptr);
    iplReflectionEffectRelease(&node.effects[0]);
    iplReflectionEffectRelease(&node.effects[1]);
    node.ready = false;
}

}  // namespace Shoonyakasha::Audio::Steam
