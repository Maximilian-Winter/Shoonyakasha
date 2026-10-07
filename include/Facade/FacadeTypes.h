//
// Facade/FacadeTypes.h - Shared types for the Python-facing facade layer
//
// No Vulkan, no EnTT, no internal engine includes.
// Only GLM + standard library.
//

#pragma once

#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <functional>
#include <cstdint>
#include <utility>

namespace Shoonyakasha {
namespace Facade {

// ═══════════════════════════════════════════════════════════════
// Entity Handle — uint32_t matching entt::entity's underlying type
// ═══════════════════════════════════════════════════════════════

using EntityHandle = uint32_t;
static constexpr EntityHandle NullEntity = UINT32_MAX;

// ═══════════════════════════════════════════════════════════════
// Facade Enums — mirror internal enums with stable numeric values
// ═══════════════════════════════════════════════════════════════

enum class CameraType : uint8_t {
    Perspective  = 0,
    Orthographic = 1
};

enum class LightType : uint8_t {
    Directional = 0,
    Point       = 1,
    Spot        = 2
};

enum class RigidBodyType : uint8_t {
    Static    = 0,
    Kinematic = 1,
    Dynamic   = 2
};

enum class ColliderShape : uint8_t {
    Box     = 0,
    Sphere  = 1,
    Capsule = 2,
    Mesh    = 3,
    Plane   = 4
};

// Screen-space anchor for UI panels and text labels.
enum class UIAnchor : uint8_t {
    TopLeft      = 0,
    TopCenter    = 1,
    TopRight     = 2,
    MiddleLeft   = 3,
    MiddleCenter = 4,
    MiddleRight  = 5,
    BottomLeft   = 6,
    BottomCenter = 7,
    BottomRight  = 8
};

enum class TextHAlign : uint8_t {
    Left   = 0,
    Center = 1,
    Right  = 2
};

// ═══════════════════════════════════════════════════════════════
// Callback Aliases
// ═══════════════════════════════════════════════════════════════

using VoidCallback   = std::function<void()>;
using UpdateCallback = std::function<void(float dt)>;
using KeyCallback    = std::function<void(int keyCode)>;
using ResizeCallback = std::function<void(uint32_t width, uint32_t height)>;

// ═══════════════════════════════════════════════════════════════
// Engine Configuration — clean mirror of ApplicationConfig
// ═══════════════════════════════════════════════════════════════

struct EngineConfig {
    int width  = 1600;
    int height = 900;
    std::string title   = "Shoonyakasha Application";
    std::string logFile = "application.log";
    int logLevel = 1;   // 0=Debug, 1=Info, 2=Warning, 3=Error

    std::string hdrEnvironmentPath;   // empty = uniform IBL, for pipelines that sample it
    float uniformEnvironmentColor[3] = {0.25f, 0.28f, 0.33f};
    std::string pipelineJsonPath;     // empty = the default pipeline

    uint32_t maxFramesInFlight = 2;

    // Vulkan validation layers. On by default; falls back to off with a warning if the
    // Khronos layer is not installed. Turn off for release builds or profiling runs.
    bool enableValidation = true;

    // Render graph parameters (SSBO sizing, dispatch counts, etc.)
    std::vector<std::pair<std::string, uint32_t>> renderGraphParameters;
};

// ═══════════════════════════════════════════════════════════════
// GLTF Loading — clean mirrors of GltfLoadOptions / GltfLoadResult
// ═══════════════════════════════════════════════════════════════

/// How to encode a recording. Mirrors VideoRecorder::Options so Facade headers
/// stay self-contained, the way GltfOptions mirrors GltfLoadOptions.
struct RecordingOptions {
    int fps = 30;
    /// x264 constant rate factor: 0 lossless, 18 visually lossless, 51 worst.
    int quality = 18;
    /// Any encoder ffmpeg has. libx264 in a .mkv plays everywhere.
    std::string codec = "libx264";
    /// Empty searches $FFMPEG, then PATH, then the usual install locations.
    std::string ffmpegPath;
};

// ═══════════════════════════════════════════════════════════════
// Render statistics — mirrors FrameGraph::RenderStats without Vulkan types
// ═══════════════════════════════════════════════════════════════

/// One pipeline pass, averaged over the last second.
struct RenderPassStats {
    std::string name;
    double   cpuMs = 0.0;        ///< recording it on the CPU
    double   gpuMs = 0.0;        ///< GPU time it added after the previous pass
    bool     gpuValid = false;   ///< false without GPU timing
    uint32_t drawCalls = 0;
    uint32_t dispatches = 0;
    uint64_t vertices = 0;       ///< vertex/index counts times instances
};

/// Frame rate and costs over the last whole second. GPU figures describe
/// frames that finished one or two frames ago.
struct RenderStatsSnapshot {
    bool     enabled = false;    ///< false: stats are off, everything else zero
    double   fps = 0.0;
    double   frameTimeMs = 0.0;      ///< mean time between frames
    double   frameTimeMaxMs = 0.0;   ///< longest time between frames
    double   cpuRecordMs = 0.0;      ///< recording the pipeline's passes
    double   gpuMs = 0.0;            ///< the pipeline's GPU work
    bool     gpuValid = false;
    uint32_t drawCalls = 0;
    uint32_t dispatches = 0;
    uint64_t vertices = 0;
    std::vector<RenderPassStats> passes;   ///< in execution order
    std::string summary;                   ///< the same, as readable text
};

struct GltfOptions {
    bool loadTextures     = true;
    bool loadMaterials    = true;
    bool createEntities   = true;
    bool loadSkins        = true;
    bool loadAnimations   = true;
    bool flattenHierarchy = false;  // see GltfLoadOptions::flattenHierarchy
    int  maxTextureSize   = 0;
    bool generateMipmaps  = true;
    bool srgbAlbedo       = true;
    std::string namePrefix;
};

struct GltfResult {
    bool success = false;
    std::string error;
    std::vector<EntityHandle> entities;
    size_t totalVertices  = 0;
    size_t totalIndices   = 0;
    size_t totalTextures  = 0;
    size_t totalMaterials = 0;

    // Animation clip metadata (populated when loadAnimations = true)
    struct ClipInfo {
        std::string name;
        float duration = 0.0f;
    };
    std::vector<ClipInfo> animationClips;
    size_t skeletonCount = 0;
};

} // namespace Facade
} // namespace Shoonyakasha
