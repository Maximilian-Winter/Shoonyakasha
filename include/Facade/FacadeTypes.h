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

    std::string hdrEnvironmentPath;   // empty = no IBL
    std::string pipelineJsonPath;     // Required — JSON render graph

    uint32_t maxFramesInFlight = 2;

    // Render graph parameters (SSBO sizing, dispatch counts, etc.)
    std::vector<std::pair<std::string, uint32_t>> renderGraphParameters;
};

// ═══════════════════════════════════════════════════════════════
// GLTF Loading — clean mirrors of GltfLoadOptions / GltfLoadResult
// ═══════════════════════════════════════════════════════════════

struct GltfOptions {
    bool loadTextures     = true;
    bool loadMaterials    = true;
    bool createEntities   = true;
    bool loadSkins        = true;
    bool loadAnimations   = true;
    bool flattenHierarchy = true;
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
