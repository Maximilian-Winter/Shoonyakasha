//
// DotPathResolver.h
//
// Resolves dot-path expressions from JSON to actual ECS data at runtime.
//
// Examples:
//   "scene.camera.view"                    → mat4 from CameraComponent
//   "entity.material.params.baseColorFactor" → vec4 from MaterialComponentV5
//   "entity.transform.worldMatrix"         → mat4 from TransformComponent
//   "const.0"                              → float 0.0
//
// This is the bridge between declarative JSON and imperative C++.
// Without this, dot-paths are just strings. With this, they become data.
//

#pragma once

#include "GPU/GPUTypes.h"
#include "ECS/RenderComponents.h"
#include "ECS/Core.h"
#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <array>
#include <string>
#include <string_view>
#include <variant>
#include <optional>
#include <vector>
#include <unordered_map>
#include <functional>
#include <cstring>

namespace Shoonyakasha {

// ============================================================================
// ResolvedValue - A type-safe container for resolved path values
// ============================================================================

struct ResolvedValue {
    using ValueType = std::variant<
        std::monostate,     // Invalid/not found
        float,
        glm::vec2,
        glm::vec3,
        glm::vec4,
        glm::mat3,
        glm::mat4,
        int32_t,
        uint32_t,
        GPUTexture          // For texture bindings
    >;

    ValueType value;

    // ─── Constructors ───────────────────────────────────────────

    ResolvedValue() : value(std::monostate{}) {}
    ResolvedValue(float v) : value(v) {}
    ResolvedValue(const glm::vec2& v) : value(v) {}
    ResolvedValue(const glm::vec3& v) : value(v) {}
    ResolvedValue(const glm::vec4& v) : value(v) {}
    ResolvedValue(const glm::mat3& v) : value(v) {}
    ResolvedValue(const glm::mat4& v) : value(v) {}
    ResolvedValue(int32_t v) : value(v) {}
    ResolvedValue(uint32_t v) : value(v) {}
    ResolvedValue(const GPUTexture& v) : value(v) {}

    // ─── Type checking ──────────────────────────────────────────

    bool isValid() const { return !std::holds_alternative<std::monostate>(value); }
    bool isFloat() const { return std::holds_alternative<float>(value); }
    bool isVec2() const { return std::holds_alternative<glm::vec2>(value); }
    bool isVec3() const { return std::holds_alternative<glm::vec3>(value); }
    bool isVec4() const { return std::holds_alternative<glm::vec4>(value); }
    bool isMat3() const { return std::holds_alternative<glm::mat3>(value); }
    bool isMat4() const { return std::holds_alternative<glm::mat4>(value); }
    bool isInt() const { return std::holds_alternative<int32_t>(value); }
    bool isUInt() const { return std::holds_alternative<uint32_t>(value); }
    bool isTexture() const { return std::holds_alternative<GPUTexture>(value); }

    // ─── Type-safe getters ──────────────────────────────────────

    template<typename T>
    T as() const {
        if (auto* ptr = std::get_if<T>(&value)) {
            return *ptr;
        }
        return T{};
    }

    template<typename T>
    std::optional<T> tryAs() const {
        if (auto* ptr = std::get_if<T>(&value)) {
            return *ptr;
        }
        return std::nullopt;
    }

    // ─── Size in bytes (for buffer layout) ──────────────────────

    size_t byteSize() const {
        return std::visit([](auto&& v) -> size_t {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, std::monostate>) return 0;
            else if constexpr (std::is_same_v<T, GPUTexture>) return 0; // Textures don't go in buffers
            else return sizeof(T);
        }, value);
    }

    // ─── Copy raw bytes to buffer ───────────────────────────────

    void copyTo(void* dest) const {
        std::visit([dest](auto&& v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (!std::is_same_v<T, std::monostate> && !std::is_same_v<T, GPUTexture>) {
                std::memcpy(dest, &v, sizeof(T));
            }
        }, value);
    }
};

// ============================================================================
// SceneContext - Global scene data accessible via "scene.*" paths
// ============================================================================

struct SceneContext {
    // Camera (from active camera entity)
    glm::mat4 cameraView = glm::mat4(1.0f);
    glm::mat4 cameraProjection = glm::mat4(1.0f);       // Vulkan Y-flipped
    glm::mat4 cameraViewProjection = glm::mat4(1.0f);
    glm::mat4 cameraInvView = glm::mat4(1.0f);          // Inverse of view matrix
    glm::mat4 cameraInvProj = glm::mat4(1.0f);          // Inverse of (Y-flipped) projection
    glm::vec3 cameraPosition = glm::vec3(0.0f);
    float cameraFov = 60.0f;
    float cameraNearPlane = 0.1f;
    float cameraFarPlane = 1000.0f;
    float cameraAspect = 1.77f;                          // screenWidth / screenHeight

    // Environment (IBL textures)
    const SceneEnvironment* environment = nullptr;

    // Time
    float timeElapsed = 0.0f;    // Seconds since start
    float timeDelta = 0.0f;      // Frame delta time
    uint32_t timeFrame = 0;      // Frame counter

    // Screen/viewport
    float screenWidth = 1920.0f;
    float screenHeight = 1080.0f;

    // ─── Lights ─────────────────────────────────────────────────
    // Collected each frame from entities with LightComponent + TransformComponent
    static constexpr uint32_t MAX_SCENE_LIGHTS = 16;

    struct PackedLight {
        glm::vec4 positionType{0.f};      // xyz=world position, w=type (0=dir,1=point,2=spot)
        glm::vec4 colorIntensity{0.f};    // xyz=color, w=intensity
        glm::vec4 directionRange{0.f};    // xyz=direction (forward), w=range
        glm::vec4 attenuation{0.f};       // x=constant, y=linear, z=quadratic, w=cos(outerCone)
    };

    std::array<PackedLight, MAX_SCENE_LIGHTS> lights{};
    uint32_t lightCount = 0;

    // ─── Custom Application Values ─────────────────────────────
    // Generic key→value storage for application-specific data.
    // Accessed via "scene.custom.<key>" dot-paths.
    // Keys use dots for namespacing: "particles.gravity", "weather.windSpeed", etc.
    std::unordered_map<std::string, ResolvedValue> customValues;

    // Convenience setters
    void setCustom(const std::string& key, float value) { customValues[key] = ResolvedValue(value); }
    void setCustom(const std::string& key, uint32_t value) { customValues[key] = ResolvedValue(value); }
    void setCustom(const std::string& key, int32_t value) { customValues[key] = ResolvedValue(value); }
    void setCustom(const std::string& key, const glm::vec2& value) { customValues[key] = ResolvedValue(value); }
    void setCustom(const std::string& key, const glm::vec3& value) { customValues[key] = ResolvedValue(value); }
    void setCustom(const std::string& key, const glm::vec4& value) { customValues[key] = ResolvedValue(value); }
    void setCustom(const std::string& key, const glm::mat4& value) { customValues[key] = ResolvedValue(value); }

    // ─── Update from ECS ────────────────────────────────────────

    void updateFromRegistry(entt::registry& registry);
};

// ============================================================================
// DotPathResolver - The core resolver
// ============================================================================

class DotPathResolver {
public:
    // ─── Resolution Methods ─────────────────────────────────────

    // Resolve a path against scene context only (for "scene.*" and "const.*")
    ResolvedValue resolveScene(const std::string& path, const SceneContext& scene) const;

    // Resolve a path against an entity (for "entity.*" paths)
    ResolvedValue resolveEntity(const std::string& path,
                                 entt::entity entity,
                                 entt::registry& registry) const;

    // Resolve any path (dispatches based on prefix)
    ResolvedValue resolve(const std::string& path,
                          const SceneContext& scene,
                          entt::entity entity,
                          entt::registry& registry) const;

    // ─── Path Analysis ──────────────────────────────────────────

    enum class PathRoot {
        Scene,      // "scene.*"
        Entity,     // "entity.*"
        Const,      // "const.*"
        Resource,   // Plain name (graph resource)
        Invalid
    };

    static PathRoot getPathRoot(const std::string& path);
    static bool isScenePath(const std::string& path) { return getPathRoot(path) == PathRoot::Scene; }
    static bool isEntityPath(const std::string& path) { return getPathRoot(path) == PathRoot::Entity; }
    static bool isConstPath(const std::string& path) { return getPathRoot(path) == PathRoot::Const; }
    static bool isResourcePath(const std::string& path) { return getPathRoot(path) == PathRoot::Resource; }

    // ─── Validation ─────────────────────────────────────────────

    // Validate a path at compile time (returns error message or empty string)
    std::string validatePath(const std::string& path) const;

    // Get the expected type for a path (for validation)
    MaterialParam::Type getExpectedType(const std::string& path) const;

private:
    // ─── Internal Resolution ────────────────────────────────────

    ResolvedValue resolveScenePath(std::string_view path, const SceneContext& scene) const;
    ResolvedValue resolveEntityPath(std::string_view path, entt::entity entity, entt::registry& registry) const;
    ResolvedValue resolveConstPath(std::string_view path) const;

    // ─── Path Parsing ───────────────────────────────────────────

    static std::vector<std::string_view> splitPath(std::string_view path);
    static std::string_view stripPrefix(std::string_view path, std::string_view prefix);
};

// ============================================================================
// BufferLayoutResolver - Fills buffers from resolved paths
// ============================================================================
//
// Given a compiled buffer layout (from JSON) and scene/entity context,
// fills a byte buffer with resolved values at the correct offsets.
//

struct BufferField {
    std::string name;
    std::string source;         // Dot-path source (may contain [i] for arrays)
    MaterialParam::Type type;
    uint32_t offset;            // Byte offset in buffer
    uint32_t size;              // Size in bytes (single element)
    uint32_t arrayCount = 1;    // Number of array elements (1 = not an array)
    uint32_t arrayStride = 0;   // Stride between array elements (0 = use size)
};

struct CompiledBufferLayout {
    std::string name;
    std::vector<BufferField> fields;
    uint32_t totalSize = 0;

    // Classification
    bool hasSceneSources = false;   // Contains scene.* paths
    bool hasEntitySources = false;  // Contains entity.* paths
    bool hasConstSources = false;   // Contains const.* paths
};

class BufferLayoutResolver {
public:
    explicit BufferLayoutResolver(const DotPathResolver& pathResolver)
        : m_pathResolver(pathResolver) {}

    // Fill a buffer with scene-only data (for UBOs updated once per frame)
    void fillSceneBuffer(void* buffer,
                         const CompiledBufferLayout& layout,
                         const SceneContext& scene) const;

    // Fill a buffer with entity data (for push constants per draw call)
    void fillEntityBuffer(void* buffer,
                          const CompiledBufferLayout& layout,
                          const SceneContext& scene,
                          entt::entity entity,
                          entt::registry& registry) const;

    // Fill a buffer with mixed sources
    void fillBuffer(void* buffer,
                    const CompiledBufferLayout& layout,
                    const SceneContext& scene,
                    entt::entity entity,
                    entt::registry& registry) const;

private:
    const DotPathResolver& m_pathResolver;

    void writeField(void* buffer, const BufferField& field, const ResolvedValue& value) const;
};

} // namespace Shoonyakasha
