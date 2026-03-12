//
// Created by maxim on 12.08.2025.
//

//
// ECS/Core.h - The beating heart of the entity system
//

#pragma once

#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <functional>
#include <typeindex>

// ═══════════════════════════════════════════════════════════════
// Component Types - The elemental nature of entities
// ═══════════════════════════════════════════════════════════════

namespace Shoonyakasha {
namespace ECS {

// Identity and Hierarchy Components
struct TagComponent {
    std::string tag = "Entity";

    TagComponent() = default;
    TagComponent(const std::string& t) : tag(t) {}
};

struct NameComponent {
    std::string name = "Unnamed";

    NameComponent() = default;
    NameComponent(const std::string& n) : name(n) {}
};

struct HierarchyComponent {
    entt::entity parent = entt::null;
    std::vector<entt::entity> children;

    void addChild(entt::entity child) {
        if (std::find(children.begin(), children.end(), child) == children.end()) {
            children.push_back(child);
        }
    }

    void removeChild(entt::entity child) {
        children.erase(std::remove(children.begin(), children.end(), child), children.end());
    }
};

// Transform Components - The spatial dance of existence
struct TransformComponent {
    glm::vec3 position{0.0f};
    glm::vec3 rotation{0.0f}; // Euler angles in radians
    glm::vec3 scale{1.0f};

    // Computed matrices (updated by transform system)
    glm::mat4 localMatrix{1.0f};
    glm::mat4 worldMatrix{1.0f};
    bool isDirty = true;

    TransformComponent() = default;
    TransformComponent(const glm::vec3& pos) : position(pos) {}
    TransformComponent(const glm::vec3& pos, const glm::vec3& rot, const glm::vec3& scl)
        : position(pos), rotation(rot), scale(scl) {}

    glm::mat4 getLocalMatrix() const {
        glm::mat4 t = glm::translate(glm::mat4(1.0f), position);
        // Rotation order: Y (yaw) → X (pitch) → Z (roll)
        // This is the standard FPS camera order - yaw first, then pitch
        // Prevents the "tilted horizon" problem when looking up/down
        glm::mat4 r = glm::rotate(glm::mat4(1.0f), rotation.y, glm::vec3(0, 1, 0));  // Yaw first
        r = glm::rotate(r, rotation.x, glm::vec3(1, 0, 0));  // Then pitch
        r = glm::rotate(r, rotation.z, glm::vec3(0, 0, 1));  // Then roll (usually 0)
        glm::mat4 s = glm::scale(glm::mat4(1.0f), scale);
        return t * r * s;
    }

    glm::vec3 getForward() const {
        // Extract forward (-Z) from the rotation matrix
        // Uses same Y→X→Z rotation order as getLocalMatrix()
        float cy = cos(rotation.y), sy = sin(rotation.y);
        float cx = cos(rotation.x), sx = sin(rotation.x);
        // Forward is -Z column of rotation matrix (negated)
        return glm::vec3(-sy * cx, sx, -cy * cx);
    }

    glm::vec3 getRight() const {
        // Extract right (+X) from the rotation matrix
        float cy = cos(rotation.y), sy = sin(rotation.y);
        return glm::vec3(cy, 0.0f, -sy);
    }

    glm::vec3 getUp() const {
        // Extract up (+Y) from the rotation matrix
        float cy = cos(rotation.y), sy = sin(rotation.y);
        float cx = cos(rotation.x), sx = sin(rotation.x);
        return glm::vec3(sy * sx, cx, cy * sx);
    }
};

// Camera Components - The eye that perceives the world
struct CameraComponent {
    enum Type { Perspective, Orthographic };

    Type type = Perspective;
    float fov = 45.0f; // Field of view in degrees
    float nearPlane = 0.1f;
    float farPlane = 1000.0f;
    float aspectRatio = 16.0f / 9.0f;

    // Orthographic specific
    float orthoSize = 10.0f;

    // Computed matrices
    glm::mat4 viewMatrix{1.0f};
    glm::mat4 projectionMatrix{1.0f};
    bool isMainCamera = false;

    glm::mat4 getProjectionMatrix() const {
        if (type == Perspective) {
            return glm::perspective(glm::radians(fov), aspectRatio, nearPlane, farPlane);
        } else {
            float halfSize = orthoSize * 0.5f;
            return glm::ortho(-halfSize * aspectRatio, halfSize * aspectRatio,
                            -halfSize, halfSize, nearPlane, farPlane);
        }
    }
};

// Lighting Components - Illumination of the virtual realm
struct LightComponent {
    enum Type { Directional, Point, Spot };

    Type type = Point;
    glm::vec3 color{1.0f};
    float intensity = 1.0f;

    // Point/Spot light specific
    float range = 10.0f;
    float constant = 1.0f;
    float linear = 0.09f;
    float quadratic = 0.032f;

    // Spot light specific
    float innerCone = 30.0f; // degrees
    float outerCone = 45.0f; // degrees

    // Shadow casting
    bool castShadows = false;
    uint32_t shadowMapSize = 1024;
};

// Physics Components - The laws of motion and interaction
struct RigidBodyComponent {
    enum Type { Static, Kinematic, Dynamic };

    Type type = Dynamic;
    float mass = 1.0f;
    glm::vec3 velocity{0.0f};
    glm::vec3 angularVelocity{0.0f};
    float drag = 0.1f;
    float angularDrag = 0.1f;

    bool useGravity = true;
    bool isKinematic = false;
    bool freezeRotation = false;

    // Bullet3 specific (opaque pointer)
    void* bulletRigidBody = nullptr;
};

struct ColliderComponent {
    enum Shape { Box, Sphere, Capsule, Mesh, Plane };

    Shape shape = Box;
    glm::vec3 size{1.0f}; // Box: extents, Sphere: radius in x, Capsule: radius in x, height in y
    glm::vec3 center{0.0f}; // Local offset

    bool isTrigger = false;
    float friction = 0.5f;
    float restitution = 0.0f;

    // Bullet3 specific
    void* bulletShape = nullptr;
};

// Utility Components - Supporting the greater whole
struct LifetimeComponent {
    float timeToLive;
    bool destroyOnExpire = true;

    LifetimeComponent(float ttl) : timeToLive(ttl) {}
};

struct ActiveComponent {
    bool active = true;
};

// ═══════════════════════════════════════════════════════════════
// Component Registration - Automatic Python binding helpers
// ═══════════════════════════════════════════════════════════════

class ComponentRegistry {
public:
    using ComponentFactory = std::function<void(entt::registry&, entt::entity)>;
    using ComponentRemover = std::function<void(entt::registry&, entt::entity)>;
    using ComponentChecker = std::function<bool(const entt::registry&, entt::entity)>;

    template<typename T>
    void registerComponent(const std::string& name) {
        m_factories[name] = [](entt::registry& reg, entt::entity entity) {
            reg.emplace<T>(entity);
        };

        m_removers[name] = [](entt::registry& reg, entt::entity entity) {
            reg.remove<T>(entity);
        };

        m_checkers[name] = [](const entt::registry& reg, entt::entity entity) {
            return reg.all_of<T>(entity);
        };

        std::type_index typeIdx(typeid(T));
        m_typeToName[typeIdx] = name;
        m_nameToType.emplace(name, typeIdx);  // Use emplace instead of operator[]
    }

    bool createComponent(const std::string& name, entt::registry& registry, entt::entity entity) {
        auto it = m_factories.find(name);
        if (it != m_factories.end()) {
            it->second(registry, entity);
            return true;
        }
        return false;
    }

    bool removeComponent(const std::string& name, entt::registry& registry, entt::entity entity) {
        auto it = m_removers.find(name);
        if (it != m_removers.end()) {
            it->second(registry, entity);
            return true;
        }
        return false;
    }

    bool hasComponent(const std::string& name, const entt::registry& registry, entt::entity entity) const {
        auto it = m_checkers.find(name);
        if (it != m_checkers.end()) {
            return it->second(registry, entity);
        }
        return false;
    }

    const std::string& getComponentName(std::type_index type) const {
        static std::string empty;
        auto it = m_typeToName.find(type);
        return it != m_typeToName.end() ? it->second : empty;
    }

    std::vector<std::string> getAllComponentNames() const {
        std::vector<std::string> names;
        for (const auto& pair : m_factories) {
            names.push_back(pair.first);
        }
        return names;
    }

private:
    std::unordered_map<std::string, ComponentFactory> m_factories;
    std::unordered_map<std::string, ComponentRemover> m_removers;
    std::unordered_map<std::string, ComponentChecker> m_checkers;
    std::unordered_map<std::type_index, std::string> m_typeToName;
    std::unordered_map<std::string, std::type_index> m_nameToType;
};

// ═══════════════════════════════════════════════════════════════
// Entity Factory - Creating entities with fluent interface
// ═══════════════════════════════════════════════════════════════

class EntityBuilder {
public:
    EntityBuilder(entt::registry& registry) : m_registry(registry), m_entity(registry.create()) {}

    template<typename T, typename... Args>
    EntityBuilder& with(Args&&... args) {
        m_registry.emplace<T>(m_entity, std::forward<Args>(args)...);
        return *this;
    }

    EntityBuilder& withName(const std::string& name) {
        return with<NameComponent>(name);
    }

    EntityBuilder& withTag(const std::string& tag) {
        return with<TagComponent>(tag);
    }

    EntityBuilder& withTransform(const glm::vec3& pos = {0,0,0},
                                const glm::vec3& rot = {0,0,0},
                                const glm::vec3& scale = {1,1,1}) {
        return with<TransformComponent>(pos, rot, scale);
    }

    EntityBuilder& withParent(entt::entity parent) {
        auto& hierarchy = m_registry.emplace<HierarchyComponent>(m_entity);
        hierarchy.parent = parent;

        if (m_registry.valid(parent)) {
            auto& parentHierarchy = m_registry.get_or_emplace<HierarchyComponent>(parent);
            parentHierarchy.addChild(m_entity);
        }
        return *this;
    }

    EntityBuilder& withCamera(bool isMain = false) {
        auto& camera = with<CameraComponent>().m_registry.get<CameraComponent>(m_entity);
        camera.isMainCamera = isMain;
        return *this;
    }

    // Camera Controller builders (forward declarations - implemented after CameraController.h is included)
    EntityBuilder& withFreeCameraController(float moveSpeed = 5.0f);
    EntityBuilder& withOrbitCameraController(const glm::vec3& target = glm::vec3(0.0f),
                                              float distance = 10.0f,
                                              bool autoRotate = false);
    EntityBuilder& withFirstPersonController(float eyeHeight = 1.7f);
    EntityBuilder& withThirdPersonController(entt::entity target,
                                              const glm::vec3& offset = glm::vec3(0.0f, 2.0f, 5.0f));

    EntityBuilder& withLight(LightComponent::Type type, const glm::vec3& color = {1,1,1}, float intensity = 1.0f) {
        auto& light = with<LightComponent>().m_registry.get<LightComponent>(m_entity);
        light.type = type;
        light.color = color;
        light.intensity = intensity;
        return *this;
    }

    EntityBuilder& withRigidBody(RigidBodyComponent::Type type = RigidBodyComponent::Dynamic, float mass = 1.0f) {
        auto& rb = with<RigidBodyComponent>().m_registry.get<RigidBodyComponent>(m_entity);
        rb.type = type;
        rb.mass = mass;
        return *this;
    }

    EntityBuilder& withCollider(ColliderComponent::Shape shape, const glm::vec3& size = {1,1,1}) {
        auto& collider = with<ColliderComponent>().m_registry.get<ColliderComponent>(m_entity);
        collider.shape = shape;
        collider.size = size;
        return *this;
    }

    EntityBuilder& withLifetime(float seconds) {
        return with<LifetimeComponent>(seconds);
    }

    entt::entity build() {
        // Ensure all entities have at least a transform and active component
        if (!m_registry.all_of<TransformComponent>(m_entity)) {
            with<TransformComponent>();
        }
        if (!m_registry.all_of<ActiveComponent>(m_entity)) {
            with<ActiveComponent>();
        }
        return m_entity;
    }

private:
    entt::registry& m_registry;
    entt::entity m_entity;
};

// ═══════════════════════════════════════════════════════════════
// Component Access Helpers - Simplifying entity manipulation
// ═══════════════════════════════════════════════════════════════

class EntityHelper {
public:
    static std::string getName(const entt::registry& registry, entt::entity entity) {
        if (auto* name = registry.try_get<NameComponent>(entity)) {
            return name->name;
        }
        return "Entity_" + std::to_string(static_cast<uint32_t>(entity));
    }

    static void setName(entt::registry& registry, entt::entity entity, const std::string& name) {
        auto& nameComp = registry.get_or_emplace<NameComponent>(entity);
        nameComp.name = name;
    }

    static glm::vec3 getWorldPosition(const entt::registry& registry, entt::entity entity) {
        if (auto* transform = registry.try_get<TransformComponent>(entity)) {
            return glm::vec3(transform->worldMatrix[3]);
        }
        return glm::vec3(0);
    }

    static void setWorldPosition(entt::registry& registry, entt::entity entity, const glm::vec3& position) {
        if (auto* transform = registry.try_get<TransformComponent>(entity)) {
            if (auto* hierarchy = registry.try_get<HierarchyComponent>(entity)) {
                if (hierarchy->parent != entt::null) {
                    // Convert world position to local position
                    if (auto* parentTransform = registry.try_get<TransformComponent>(hierarchy->parent)) {
                        glm::mat4 parentWorldInverse = glm::inverse(parentTransform->worldMatrix);
                        glm::vec4 localPos = parentWorldInverse * glm::vec4(position, 1.0f);
                        transform->position = glm::vec3(localPos);
                    } else {
                        transform->position = position;
                    }
                } else {
                    transform->position = position;
                }
            } else {
                transform->position = position;
            }
            transform->isDirty = true;
        }
    }

    static void destroyEntity(entt::registry& registry, entt::entity entity) {
        // Remove from parent's children list
        if (auto* hierarchy = registry.try_get<HierarchyComponent>(entity)) {
            if (hierarchy->parent != entt::null && registry.valid(hierarchy->parent)) {
                if (auto* parentHierarchy = registry.try_get<HierarchyComponent>(hierarchy->parent)) {
                    parentHierarchy->removeChild(entity);
                }
            }

            // Recursively destroy children or reparent them
            for (auto child : hierarchy->children) {
                if (registry.valid(child)) {
                    destroyEntity(registry, child);
                }
            }
        }

        registry.destroy(entity);
    }

    static std::vector<entt::entity> findEntitiesWithTag(const entt::registry& registry, const std::string& tag) {
        std::vector<entt::entity> entities;
        auto view = registry.view<TagComponent>();
        for (auto entity : view) {
            const auto& tagComp = view.get<TagComponent>(entity);
            if (tagComp.tag == tag) {
                entities.push_back(entity);
            }
        }
        return entities;
    }

    static entt::entity findFirstWithTag(const entt::registry& registry, const std::string& tag) {
        auto view = registry.view<TagComponent>();
        for (auto entity : view) {
            const auto& tagComp = view.get<TagComponent>(entity);
            if (tagComp.tag == tag) {
                return entity;
            }
        }
        return entt::null;
    }

    static bool isActive(const entt::registry& registry, entt::entity entity) {
        if (auto* active = registry.try_get<ActiveComponent>(entity)) {
            return active->active;
        }
        return true; // Default to active if no component
    }

    static void setActive(entt::registry& registry, entt::entity entity, bool active) {
        auto& activeComp = registry.get_or_emplace<ActiveComponent>(entity);
        activeComp.active = active;
    }
};

// ═══════════════════════════════════════════════════════════════
// Component Registration - Setting up the system
// ═══════════════════════════════════════════════════════════════

inline void registerAllComponents(ComponentRegistry& registry) {
    registry.registerComponent<TagComponent>("Tag");
    registry.registerComponent<NameComponent>("Name");
    registry.registerComponent<HierarchyComponent>("Hierarchy");
    registry.registerComponent<TransformComponent>("Transform");
    registry.registerComponent<CameraComponent>("Camera");
    registry.registerComponent<LightComponent>("Light");
    registry.registerComponent<RigidBodyComponent>("RigidBody");
    registry.registerComponent<ColliderComponent>("Collider");
    registry.registerComponent<ActiveComponent>("Active");
}

} // namespace ECS
} // namespace Shoonyakasha

// Backward compatibility alias
namespace ECS = Shoonyakasha::ECS;