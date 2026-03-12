//
// DotPathResolver.cpp
//

#include "FrameGraph/DotPathResolver.h"
#include "ECS/SkeletonComponents.h"
#include <sstream>
#include <algorithm>
#include <charconv>
#include <cmath>
#include <iostream>

namespace Shoonyakasha {

// ============================================================================
// SceneContext - Update from ECS registry
// ============================================================================

void SceneContext::updateFromRegistry(entt::registry& registry) {
    // Find active camera

    // Compute aspect ratio from current screen dimensions
    this->cameraAspect = (screenHeight > 0.0f) ? (screenWidth / screenHeight) : 1.77f;

    auto cameraEntities = registry.view<ECS::CameraComponent, ECS::TransformComponent>();
    bool foundCamera = false;

    for (auto entity : cameraEntities) {
        const auto& cam = cameraEntities.get<ECS::CameraComponent>(entity);
        if (cam.isMainCamera) {
            const auto& transform = cameraEntities.get<ECS::TransformComponent>(entity);

            this->cameraView = cam.viewMatrix;
            this->cameraProjection = cam.projectionMatrix;
            this->cameraProjection[1][1] *= -1;  // Vulkan Y-flip (NDC Y is inverted)
            this->cameraViewProjection = cameraProjection * cameraView;
            this->cameraInvView = glm::inverse(cameraView);
            this->cameraInvProj = glm::inverse(cameraProjection);
            this->cameraPosition = transform.position;
            this->cameraFov = cam.fov;
            this->cameraNearPlane = cam.nearPlane;
            this->cameraFarPlane = cam.farPlane;
            foundCamera = true;

            break;
        }
    }

    if (!foundCamera) {
        // Only warn once to avoid spam
        static bool warnedNoCamera = false;
        if (!warnedNoCamera) {
            std::cerr << "[WARNING] No main camera found in scene!" << std::endl;
            warnedNoCamera = true;
        }
    }

    // ─── Collect lights ─────────────────────────────────────────
    lightCount = 0;
    auto lightEntities = registry.view<ECS::LightComponent, ECS::TransformComponent>();
    for (auto entity : lightEntities) {
        if (lightCount >= MAX_SCENE_LIGHTS) break;

        const auto& light = lightEntities.get<ECS::LightComponent>(entity);
        const auto& transform = lightEntities.get<ECS::TransformComponent>(entity);

        auto& packed = lights[lightCount];

        // Position + type (0=Directional, 1=Point, 2=Spot)
        packed.positionType = glm::vec4(
            transform.position,
            static_cast<float>(light.type)
        );

        // Color + intensity
        packed.colorIntensity = glm::vec4(light.color, light.intensity);

        // Direction (forward vector from transform) + range
        glm::vec3 direction = transform.getForward();
        packed.directionRange = glm::vec4(direction, light.range);

        // Attenuation params + cos(outerCone)
        packed.attenuation = glm::vec4(
            light.constant,
            light.linear,
            light.quadratic,
            glm::cos(glm::radians(light.outerCone))
        );

        lightCount++;
    }

    // Zero out unused light slots
    for (uint32_t i = lightCount; i < MAX_SCENE_LIGHTS; i++) {
        lights[i] = PackedLight{};
    }
}

// ============================================================================
// DotPathResolver - Path Analysis
// ============================================================================

DotPathResolver::PathRoot DotPathResolver::getPathRoot(const std::string& path) {
    if (path.empty()) return PathRoot::Invalid;

    if (path.starts_with("scene.")) return PathRoot::Scene;
    if (path.starts_with("entity.")) return PathRoot::Entity;
    if (path.starts_with("const.")) return PathRoot::Const;

    // No prefix = resource reference (e.g., "gPosition", "litColorHDR")
    // Check it's a valid identifier
    if (std::isalpha(path[0]) || path[0] == '_') {
        return PathRoot::Resource;
    }

    return PathRoot::Invalid;
}

std::vector<std::string_view> DotPathResolver::splitPath(std::string_view path) {
    std::vector<std::string_view> parts;
    size_t start = 0;
    size_t end = 0;

    while ((end = path.find('.', start)) != std::string_view::npos) {
        parts.push_back(path.substr(start, end - start));
        start = end + 1;
    }
    if (start < path.size()) {
        parts.push_back(path.substr(start));
    }

    return parts;
}

std::string_view DotPathResolver::stripPrefix(std::string_view path, std::string_view prefix) {
    if (path.starts_with(prefix)) {
        return path.substr(prefix.size());
    }
    return path;
}

// ============================================================================
// DotPathResolver - Resolution Methods
// ============================================================================

ResolvedValue DotPathResolver::resolve(const std::string& path,
                                        const SceneContext& scene,
                                        entt::entity entity,
                                        entt::registry& registry) const {
    switch (getPathRoot(path)) {
        case PathRoot::Scene:
            return resolveScenePath(path, scene);
        case PathRoot::Entity:
            return resolveEntityPath(path, entity, registry);
        case PathRoot::Const:
            return resolveConstPath(path);
        case PathRoot::Resource:
            // Resource paths are handled by the frame graph, not the resolver
            return ResolvedValue();
        default:
            return ResolvedValue();
    }
}

ResolvedValue DotPathResolver::resolveScene(const std::string& path, const SceneContext& scene) const {
    if (getPathRoot(path) == PathRoot::Scene) {
        return resolveScenePath(path, scene);
    }
    if (getPathRoot(path) == PathRoot::Const) {
        return resolveConstPath(path);
    }
    return ResolvedValue();
}

ResolvedValue DotPathResolver::resolveEntity(const std::string& path,
                                              entt::entity entity,
                                              entt::registry& registry) const {
    if (getPathRoot(path) == PathRoot::Entity) {
        return resolveEntityPath(path, entity, registry);
    }
    return ResolvedValue();
}

// ============================================================================
// Scene Path Resolution
// ============================================================================

ResolvedValue DotPathResolver::resolveScenePath(std::string_view path, const SceneContext& scene) const {
    // Strip "scene." prefix
    auto subpath = stripPrefix(path, "scene.");
    auto parts = splitPath(subpath);

    if (parts.empty()) return ResolvedValue();

    // ─── Camera paths ───────────────────────────────────────────
    if (parts[0] == "camera") {
        if (parts.size() < 2) return ResolvedValue();

        if (parts[1] == "view") return ResolvedValue(scene.cameraView);
        if (parts[1] == "projection") return ResolvedValue(scene.cameraProjection);
        if (parts[1] == "viewProjection") return ResolvedValue(scene.cameraViewProjection);
        if (parts[1] == "invView") return ResolvedValue(scene.cameraInvView);
        if (parts[1] == "invProj") return ResolvedValue(scene.cameraInvProj);
        if (parts[1] == "position") return ResolvedValue(scene.cameraPosition);
        if (parts[1] == "fov") return ResolvedValue(scene.cameraFov);
        if (parts[1] == "nearPlane") return ResolvedValue(scene.cameraNearPlane);
        if (parts[1] == "farPlane") return ResolvedValue(scene.cameraFarPlane);
        if (parts[1] == "aspect") return ResolvedValue(scene.cameraAspect);

        // Composite paths for shader-compatible packed formats
        if (parts[1] == "positionVec4")
            return ResolvedValue(glm::vec4(scene.cameraPosition, 1.0f));
        if (parts[1] == "nearFarFovAspect")
            return ResolvedValue(glm::vec4(scene.cameraNearPlane, scene.cameraFarPlane,
                                           scene.cameraFov, scene.cameraAspect));
    }

    // ─── Environment paths ──────────────────────────────────────
    if (parts[0] == "environment" && scene.environment) {
        if (parts.size() < 2) return ResolvedValue();

        if (parts[1] == "irradianceMap") return ResolvedValue(scene.environment->irradianceMap);
        if (parts[1] == "prefilterMap") return ResolvedValue(scene.environment->prefilterMap);
        if (parts[1] == "brdfLUT") return ResolvedValue(scene.environment->brdfLUT);
        if (parts[1] == "environmentMap") return ResolvedValue(scene.environment->environmentMap);
    }

    // ─── Time paths ─────────────────────────────────────────────
    if (parts[0] == "time") {
        if (parts.size() < 2) return ResolvedValue();

        if (parts[1] == "elapsed") return ResolvedValue(scene.timeElapsed);
        if (parts[1] == "delta") return ResolvedValue(scene.timeDelta);
        if (parts[1] == "frame") return ResolvedValue(scene.timeFrame);
    }

    // ─── Screen paths ───────────────────────────────────────────
    if (parts[0] == "screen") {
        if (parts.size() < 2) return ResolvedValue();

        if (parts[1] == "width") return ResolvedValue(scene.screenWidth);
        if (parts[1] == "height") return ResolvedValue(scene.screenHeight);
        if (parts[1] == "resolution") return ResolvedValue(glm::vec2(scene.screenWidth, scene.screenHeight));
    }

    // ─── Light paths ────────────────────────────────────────────
    // scene.lights.count → uint32_t
    // scene.lights[N].positionType → vec4, etc.
    if (parts[0] == "lights" && parts.size() >= 2) {
        // scene.lights.count
        if (parts[1] == "count") return ResolvedValue(scene.lightCount);
    }

    // scene.lights[N].field — splitPath splits "lights[0].positionType" into ["lights[0]", "positionType"]
    {
        std::string_view firstPart = parts[0];
        auto bracket = firstPart.find('[');
        if (bracket != std::string_view::npos && firstPart.substr(0, bracket) == "lights") {
            auto closeBracket = firstPart.find(']', bracket);
            if (closeBracket != std::string_view::npos && parts.size() >= 2) {
                auto numStr = firstPart.substr(bracket + 1, closeBracket - bracket - 1);
                uint32_t index = 0;
                auto [ptr, ec] = std::from_chars(numStr.data(), numStr.data() + numStr.size(), index);

                if (ec == std::errc()) {
                    // Out-of-bounds returns zero vec4 (safe fallback for unused slots)
                    if (index >= SceneContext::MAX_SCENE_LIGHTS) {
                        return ResolvedValue(glm::vec4(0.f));
                    }
                    const auto& packed = scene.lights[index];
                    if (parts[1] == "positionType")    return ResolvedValue(packed.positionType);
                    if (parts[1] == "colorIntensity")  return ResolvedValue(packed.colorIntensity);
                    if (parts[1] == "directionRange")  return ResolvedValue(packed.directionRange);
                    if (parts[1] == "attenuation")     return ResolvedValue(packed.attenuation);
                }
            }
        }
    }

    // ─── Custom application values ─────────────────────────────
    // scene.custom.<key> — key can contain dots for namespacing
    if (parts[0] == "custom" && parts.size() >= 2) {
        // Reconstruct the key from remaining parts (e.g., "particles.gravity")
        std::string key;
        for (size_t i = 1; i < parts.size(); i++) {
            if (i > 1) key += '.';
            key += std::string(parts[i]);
        }
        auto it = scene.customValues.find(key);
        if (it != scene.customValues.end()) {
            return it->second;
        }
        return ResolvedValue();
    }

    return ResolvedValue();
}

// ============================================================================
// Entity Path Resolution
// ============================================================================

ResolvedValue DotPathResolver::resolveEntityPath(std::string_view path,
                                                   entt::entity entity,
                                                   entt::registry& registry) const {
    if (entity == entt::null || !registry.valid(entity)) {
        return ResolvedValue();
    }

    // Strip "entity." prefix
    auto subpath = stripPrefix(path, "entity.");
    auto parts = splitPath(subpath);

    if (parts.empty()) return ResolvedValue();

    // ─── Transform paths ────────────────────────────────────────
    if (parts[0] == "transform") {
        auto* transform = registry.try_get<ECS::TransformComponent>(entity);
        if (!transform || parts.size() < 2) return ResolvedValue();

        if (parts[1] == "worldMatrix") return ResolvedValue(transform->worldMatrix);
        if (parts[1] == "localMatrix") return ResolvedValue(transform->localMatrix);
        if (parts[1] == "position") return ResolvedValue(transform->position);
        if (parts[1] == "rotation") return ResolvedValue(glm::vec4(transform->rotation.x,
                                                                    transform->rotation.y,
                                                                    transform->rotation.z, 0.0f));
        if (parts[1] == "scale") return ResolvedValue(transform->scale);
    }

    // ─── Material paths ─────────────────────────────────────────
    if (parts[0] == "material") {
        auto* material = registry.try_get<MaterialComponentV5>(entity);
        if (!material || parts.size() < 2) return ResolvedValue();

        // material.params.<name>
        if (parts[1] == "params" && parts.size() >= 3) {
            std::string paramName(parts[2]);
            auto it = material->params.find(paramName);
            if (it != material->params.end()) {
                const auto& param = it->second;
                switch (param.type) {
                    case MaterialParam::Type::Float: return ResolvedValue(param.as<float>());
                    case MaterialParam::Type::Vec2:  return ResolvedValue(param.as<glm::vec2>());
                    case MaterialParam::Type::Vec3:  return ResolvedValue(param.as<glm::vec3>());
                    case MaterialParam::Type::Vec4:  return ResolvedValue(param.as<glm::vec4>());
                    case MaterialParam::Type::Mat3:  return ResolvedValue(param.as<glm::mat3>());
                    case MaterialParam::Type::Mat4:  return ResolvedValue(param.as<glm::mat4>());
                    case MaterialParam::Type::Int:   return ResolvedValue(param.as<int32_t>());
                    case MaterialParam::Type::UInt:  return ResolvedValue(param.as<uint32_t>());
                }
            }
            return ResolvedValue();
        }

        // material.textures.<name>
        if (parts[1] == "textures" && parts.size() >= 3) {
            std::string textureName(parts[2]);

            // Check for .exists suffix
            if (parts.size() >= 4 && parts[3] == "exists") {
                auto it = material->textures.find(textureName);
                bool exists = (it != material->textures.end() && it->second.exists);
                return ResolvedValue(exists ? 1.0f : 0.0f);
            }

            // Return the texture itself
            auto it = material->textures.find(textureName);
            if (it != material->textures.end()) {
                return ResolvedValue(it->second);
            }
            return ResolvedValue();
        }

        // material.alphaCutoff
        if (parts[1] == "alphaCutoff") return ResolvedValue(material->alphaCutoff);

        // material.alphaMode
        if (parts[1] == "alphaMode") return ResolvedValue(static_cast<uint32_t>(material->alphaMode));

        // material.doubleSided
        if (parts[1] == "doubleSided") return ResolvedValue(material->doubleSided ? 1.0f : 0.0f);
    }

    // ─── Mesh paths ─────────────────────────────────────────────
    if (parts[0] == "mesh") {
        auto* mesh = registry.try_get<MeshComponent>(entity);
        if (!mesh || parts.size() < 2) return ResolvedValue();

        if (parts[1] == "vertexCount") return ResolvedValue(mesh->vertexCount);
        if (parts[1] == "indexCount") return ResolvedValue(mesh->indexCount);
    }

    // ─── Skeleton paths ─────────────────────────────────────────
    // entity.skeleton.hasSkeleton   → float (1.0 or 0.0)
    // entity.skeleton.jointCount    → uint32_t
    if (parts[0] == "skeleton") {
        auto* skelComp = registry.try_get<SkeletonComponent>(entity);

        if (parts.size() >= 2 && parts[1] == "hasSkeleton") {
            return ResolvedValue(skelComp ? 1.0f : 0.0f);
        }

        if (!skelComp || parts.size() < 2) return ResolvedValue();

        if (parts[1] == "jointCount") return ResolvedValue(skelComp->jointCount());
    }

    return ResolvedValue();
}

// ============================================================================
// Constant Path Resolution
// ============================================================================

ResolvedValue DotPathResolver::resolveConstPath(std::string_view path) const {
    // Strip "const." prefix
    auto valueStr = stripPrefix(path, "const.");

    // Count dots to determine if it's a vector constant
    size_t dotCount = std::count(valueStr.begin(), valueStr.end(), '.');

    if (dotCount == 0) {
        // Single value - parse as float
        float value = 0.0f;
        auto result = std::from_chars(valueStr.data(), valueStr.data() + valueStr.size(), value);
        if (result.ec == std::errc()) {
            return ResolvedValue(value);
        }
        return ResolvedValue();
    }

    // Multiple components - parse as vector
    auto parts = splitPath(valueStr);
    std::vector<float> components;
    components.reserve(parts.size());

    for (const auto& part : parts) {
        float value = 0.0f;
        auto result = std::from_chars(part.data(), part.data() + part.size(), value);
        if (result.ec != std::errc()) {
            return ResolvedValue();
        }
        components.push_back(value);
    }

    switch (components.size()) {
        case 2: return ResolvedValue(glm::vec2(components[0], components[1]));
        case 3: return ResolvedValue(glm::vec3(components[0], components[1], components[2]));
        case 4: return ResolvedValue(glm::vec4(components[0], components[1], components[2], components[3]));
        default: return ResolvedValue(components[0]);  // Single component
    }
}

// ============================================================================
// Path Validation
// ============================================================================

std::string DotPathResolver::validatePath(const std::string& path) const {
    auto root = getPathRoot(path);

    if (root == PathRoot::Invalid) {
        return "Invalid path: '" + path + "' - must start with scene., entity., const., or be a valid identifier";
    }

    auto parts = splitPath(path);

    if (root == PathRoot::Scene) {
        if (parts.size() < 2) {
            return "Invalid scene path: '" + path + "' - must specify property (e.g., scene.camera.view)";
        }
        // Valid scene categories
        static const std::vector<std::string> validCategories = {"camera", "environment", "time", "screen"};
        std::string category(parts[1]);
        if (std::find(validCategories.begin(), validCategories.end(), category) == validCategories.end()) {
            return "Invalid scene category: '" + category + "' in path '" + path + "'";
        }
    }

    if (root == PathRoot::Entity) {
        if (parts.size() < 2) {
            return "Invalid entity path: '" + path + "' - must specify component (e.g., entity.transform.worldMatrix)";
        }
        // Valid entity components
        static const std::vector<std::string> validComponents = {"transform", "material", "mesh", "skeleton"};
        std::string component(parts[1]);
        if (std::find(validComponents.begin(), validComponents.end(), component) == validComponents.end()) {
            return "Invalid entity component: '" + component + "' in path '" + path + "'";
        }
    }

    return "";  // Valid
}

MaterialParam::Type DotPathResolver::getExpectedType(const std::string& path) const {
    // This is a simplified type inference - the full implementation would
    // have a complete mapping of paths to types

    if (path.find("Matrix") != std::string::npos || path.find("matrix") != std::string::npos) {
        return MaterialParam::Type::Mat4;
    }
    if (path.find("position") != std::string::npos || path.find("scale") != std::string::npos) {
        return MaterialParam::Type::Vec3;
    }
    if (path.find("Color") != std::string::npos || path.find("Factor") != std::string::npos) {
        return MaterialParam::Type::Vec4;
    }
    if (path.find("resolution") != std::string::npos) {
        return MaterialParam::Type::Vec2;
    }
    if (path.find("Count") != std::string::npos || path.find("frame") != std::string::npos) {
        return MaterialParam::Type::UInt;
    }

    return MaterialParam::Type::Float;  // Default
}

// ============================================================================
// BufferLayoutResolver
// ============================================================================

void BufferLayoutResolver::fillSceneBuffer(void* buffer,
                                           const CompiledBufferLayout& layout,
                                           const SceneContext& scene) const {
    for (const auto& field : layout.fields) {
        // Array expansion: when arrayCount > 1 and source contains [i],
        // expand to [0], [1], ..., [arrayCount-1] and write each element
        if (field.arrayCount > 1 && field.source.find("[i]") != std::string::npos) {
            uint32_t stride = (field.arrayStride > 0) ? field.arrayStride : field.size;
            for (uint32_t i = 0; i < field.arrayCount; i++) {
                std::string expanded = field.source;
                auto pos = expanded.find("[i]");
                if (pos != std::string::npos) {
                    expanded.replace(pos, 3, "[" + std::to_string(i) + "]");
                }
                auto value = m_pathResolver.resolveScene(expanded, scene);
                // Write at element's offset within the array
                BufferField elemField = field;
                elemField.offset = field.offset + i * stride;
                elemField.arrayCount = 1;
                writeField(buffer, elemField, value);
            }
        } else {
            // Single value (or array without [i] — broadcast same value)
            auto value = m_pathResolver.resolveScene(field.source, scene);
            writeField(buffer, field, value);
        }
    }
}

void BufferLayoutResolver::fillEntityBuffer(void* buffer,
                                            const CompiledBufferLayout& layout,
                                            const SceneContext& scene,
                                            entt::entity entity,
                                            entt::registry& registry) const {
    for (const auto& field : layout.fields) {
        auto value = m_pathResolver.resolve(field.source, scene, entity, registry);
        writeField(buffer, field, value);
    }
}

void BufferLayoutResolver::fillBuffer(void* buffer,
                                      const CompiledBufferLayout& layout,
                                      const SceneContext& scene,
                                      entt::entity entity,
                                      entt::registry& registry) const {
    // Same as fillEntityBuffer - handles all source types
    fillEntityBuffer(buffer, layout, scene, entity, registry);
}

void BufferLayoutResolver::writeField(void* buffer, const BufferField& field, const ResolvedValue& value) const {
    if (!value.isValid()) {
        // Write zeros for invalid values
        std::memset(static_cast<uint8_t*>(buffer) + field.offset, 0, field.size);
        return;
    }

    uint8_t* dest = static_cast<uint8_t*>(buffer) + field.offset;
    value.copyTo(dest);
}

} // namespace Shoonyakasha
