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

/// Opaque facade entity identifier matching the native registry's entity value.
using EntityHandle = uint32_t;
/// Sentinel representing no entity. A non-null handle can still be stale.
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

// Start and End follow a canvas UI paragraph's direction: left and right for
// left-to-right text, right and left for right-to-left text. 2D text labels,
// which have no direction, take them as Left and Right.
enum class TextHAlign : uint8_t {
    Left   = 0,
    Center = 1,
    Right  = 2,
    Start  = 3,
    End    = 4
};

// Vertical text alignment in a canvas UI element.
enum class TextVAlign : uint8_t {
    Top    = 0,
    Middle = 1,
    Bottom = 2
};

// The direction of each paragraph of canvas UI text. Auto takes it from the
// paragraph's first letter with a direction, or from the element's layout
// direction when it has none; FromLayout always from the layout direction.
enum class TextDirection : uint8_t {
    Auto        = 0,
    LeftToRight = 1,
    RightToLeft = 2,
    FromLayout  = 3
};

// How canvas UI text runs: in horizontal lines, or in vertical columns
// stacked from the right (Chinese, Japanese) or from the left (Mongolian).
enum class TextOrientation : uint8_t {
    Horizontal          = 0,
    VerticalRightToLeft = 1,
    VerticalLeftToRight = 2
};

// What a canvas UI element is to screen readers. Default keeps the role its
// components give it: a button, a toggle (CheckBox), a slider, a text field
// (Edit), a scroll view (Pane) or text.
enum class AccessibleRole : uint8_t {
    Default  = 0,
    Group    = 1,
    Button   = 2,
    CheckBox = 3,
    Slider   = 4,
    Edit     = 5,
    Pane     = 6,
    Text     = 7
};

// Which way a canvas UI element, or a canvas, lays out its children. Inherit
// takes its parent's; a canvas left as Inherit lays out left to right.
enum class LayoutDirection : uint8_t {
    Inherit     = 0,
    LeftToRight = 1,
    RightToLeft = 2
};

// How a size fitter sizes an element along one axis.
enum class SizeFit : uint8_t {
    Unconstrained = 0,   // its own size, untouched
    Min           = 1,   // its min layout size
    Preferred     = 2    // its preferred layout size
};

// How a canvas maps its units to pixels.
enum class CanvasScaleMode : uint8_t {
    ConstantPixel   = 0,   // a fixed number of pixels per canvas unit
    ScaleWithScreen = 1    // scaled so the reference size fits the target
};

// ═══════════════════════════════════════════════════════════════
// Callback Aliases
// ═══════════════════════════════════════════════════════════════

/// Lifecycle callback with no arguments.
using VoidCallback   = std::function<void()>;
/// Per-frame callback receiving elapsed time in seconds.
using UpdateCallback = std::function<void(float dt)>;
/// Keyboard callback receiving an integer key code.
using KeyCallback    = std::function<void(int keyCode)>;
/// Window resize callback receiving width and height.
using ResizeCallback = std::function<void(uint32_t width, uint32_t height)>;

// ═══════════════════════════════════════════════════════════════
// Engine Configuration — clean mirror of ApplicationConfig
// ═══════════════════════════════════════════════════════════════

/// Application settings copied when EngineAPI is constructed.
/// Leave pipelineJsonPath empty to select the built-in render pipeline.
/// Validation defaults depend on NDEBUG and may be overridden at runtime by
/// the SHOONYAKASHA_VALIDATION environment variable.
struct EngineConfig {
    int width  = 1600;
    int height = 900;
    std::string title   = "Shoonyakasha Application";
    std::string logFile = "application.log";
    int logLevel = 1;   // 0=Debug, 1=Info, 2=Warning, 3=Error

    std::string hdrEnvironmentPath;   // HDR map or baked .ibl; empty = uniform IBL, for pipelines that sample it
    float uniformEnvironmentColor[3] = {0.25f, 0.28f, 0.33f};
    bool iblCache = true;             // save an HDR map's IBL beside it (sky.hdr.ibl) and reuse it
    std::string pipelineJsonPath;     // empty = the default pipeline

    uint32_t maxFramesInFlight = 2;

    // Vulkan validation layers: on in debug builds, off in release builds,
    // where they would add microseconds to every command recorded.
    // SHOONYAKASHA_VALIDATION=1 or 0 overrides this either way. Falls back to
    // off with a warning if the Khronos layer is not installed.
#ifdef NDEBUG
    bool enableValidation = false;
#else
    bool enableValidation = true;
#endif

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
    /// While recording, every frame advances the clock by exactly 1/fps, so
    /// the video plays motion at its own speed however fast frames render.
    bool fixedStep = false;
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
    bool     validationLayers = false;  ///< on: CPU times include the layers' checks
    std::vector<RenderPassStats> passes;   ///< in execution order
    std::string summary;                   ///< the same, as readable text
};

/// Facade glTF import flags, translated to native GltfLoadOptions.
/// Some options have implementation limits; consult the scene-loading guide.
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
    bool compressTextures = false;  // see GltfLoadOptions::compressTextures
    std::string namePrefix;
};

/// Result of EngineAPI::loadGltfScene(). Check success before using entities.
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
