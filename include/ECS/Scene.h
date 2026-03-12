//
// Created by maxim on 12.08.2025.
//

//
// ECS/Scene.h - The container of all existence in the game world
//

#pragma once

#include "Core.h"
#include "Systems.h"
#include "Resources/ResourceManager.h"
#include "Vulkan/VulkanDevice.h"
#include <nlohmann/json.hpp>
#include <chrono>
#include <fstream>
#include <iostream>

namespace Shoonyakasha {
namespace ECS {

// ═══════════════════════════════════════════════════════════════
// Scene - The complete game world container
// ═══════════════════════════════════════════════════════════════

class Scene {
public:
    Scene(const std::string& name, ResourceManager& resourceManager, VulkanDevice& device)
        : m_name(name), m_resourceManager(resourceManager), m_device(device) {

        // Initialize component registry
        registerAllComponents(m_componentRegistry);

        // Add default systems
        initializeDefaultSystems();

        m_lastUpdateTime = std::chrono::high_resolution_clock::now();
    }

    ~Scene() {
        m_systemManager.cleanup(m_registry);
    }

    // Entity creation and management - flowing like water
    EntityBuilder createEntity(const std::string& name = "") {
        auto builder = EntityBuilder(m_registry);
        if (!name.empty()) {
            builder.withName(name);
        }
        return builder;
    }

    entt::entity createEmptyEntity() {
        return m_registry.create();
    }

    void destroyEntity(entt::entity entity) {
        EntityHelper::destroyEntity(m_registry, entity);
    }

    bool isValid(entt::entity entity) const {
        return m_registry.valid(entity);
    }

    // Component management through string names (for Python binding)
    bool addComponent(entt::entity entity, const std::string& componentName) {
        return m_componentRegistry.createComponent(componentName, m_registry, entity);
    }

    bool removeComponent(entt::entity entity, const std::string& componentName) {
        return m_componentRegistry.removeComponent(componentName, m_registry, entity);
    }

    bool hasComponent(entt::entity entity, const std::string& componentName)  {
        return m_componentRegistry.hasComponent(componentName, m_registry, entity);
    }

    // Templated component access for C++
    template<typename T>
    T& addComponent(entt::entity entity) {
        return m_registry.emplace<T>(entity);
    }

    template<typename T, typename... Args>
    T& addComponent(entt::entity entity, Args&&... args) {
        return m_registry.emplace<T>(entity, std::forward<Args>(args)...);
    }

    template<typename T>
    T* getComponent(entt::entity entity) {
        return m_registry.try_get<T>(entity);
    }

    template<typename T>
    const T* getComponent(entt::entity entity) const {
        return m_registry.try_get<T>(entity);
    }

    template<typename T>
    void removeComponent(entt::entity entity) {
        m_registry.remove<T>(entity);
    }

    template<typename T>
    bool hasComponent(entt::entity entity) const {
        return m_registry.all_of<T>(entity);
    }

    // Entity queries and searching
    std::vector<entt::entity> findEntitiesWithTag(const std::string& tag) const {
        return EntityHelper::findEntitiesWithTag(m_registry, tag);
    }

    entt::entity findFirstWithTag(const std::string& tag) const {
        return EntityHelper::findFirstWithTag(m_registry, tag);
    }

    entt::entity findEntityByName(const std::string& name) const {
        auto view = m_registry.view<NameComponent>();
        for (auto entity : view) {
            const auto& nameComp = view.get<NameComponent>(entity);
            if (nameComp.name == name) {
                return entity;
            }
        }
        return entt::null;
    }

    template<typename... Components>
    auto getEntitiesWith() {
        return m_registry.view<Components...>();
    }

    // System management
    template<typename T, typename... Args>
    T* addSystem(Args&&... args) {
        return m_systemManager.addSystem<T>(std::forward<Args>(args)...);
    }

    template<typename T>
    T* getSystem() {
        return m_systemManager.getSystem<T>();
    }

    // Scene lifecycle
    void initialize() {
        m_systemManager.initialize(m_registry);
        m_initialized = true;
    }

    void update() {
        auto currentTime = std::chrono::high_resolution_clock::now();
        float deltaTime = std::chrono::duration<float>(currentTime - m_lastUpdateTime).count();
        m_lastUpdateTime = currentTime;

        // Clamp delta time to prevent large jumps
        deltaTime = std::min(deltaTime, 0.1f);

        m_systemManager.update(m_registry, deltaTime);
        m_frameCount++;
    }

    void fixedUpdate(float fixedDeltaTime) {
        // For physics and other fixed-timestep systems
        /*if (auto* physicsSystem = getSystem<PhysicsSystem>()) {
            physicsSystem->update(m_registry, fixedDeltaTime);
        }*/
    }

    entt::entity getMainCamera()  {
        if (auto* cameraSystem = getSystem<CameraSystem>()) {
            return cameraSystem->getMainCamera(m_registry);
        }
        return entt::null;
    }

    // Scene serialization - making the ephemeral permanent
    nlohmann::json serialize() const {
        nlohmann::json sceneJson;
        sceneJson["name"] = m_name;
        sceneJson["frameCount"] = m_frameCount;

        nlohmann::json entitiesJson = nlohmann::json::array();

        // Modern EnTT way to iterate over all entities
        // Option 1: Using view with no components (gets all entities)
        auto allEntities = m_registry.view<entt::entity>();
        for (auto entity : allEntities) {
            nlohmann::json entityJson;
            entityJson["id"] = static_cast<uint32_t>(entity);

            // Serialize components
            nlohmann::json componentsJson;

            // Transform component
            if (auto* transform = m_registry.try_get<TransformComponent>(entity)) {
                nlohmann::json transformJson;
                transformJson["position"] = {transform->position.x, transform->position.y, transform->position.z};
                transformJson["rotation"] = {transform->rotation.x, transform->rotation.y, transform->rotation.z};
                transformJson["scale"] = {transform->scale.x, transform->scale.y, transform->scale.z};
                componentsJson["Transform"] = transformJson;
            }

            // Name component
            if (auto* name = m_registry.try_get<NameComponent>(entity)) {
                componentsJson["Name"] = {{"name", name->name}};
            }

            // Tag component
            if (auto* tag = m_registry.try_get<TagComponent>(entity)) {
                componentsJson["Tag"] = {{"tag", tag->tag}};
            }

            // Camera component
            if (auto* camera = m_registry.try_get<CameraComponent>(entity)) {
                nlohmann::json cameraJson;
                cameraJson["type"] = (camera->type == CameraComponent::Perspective) ? "Perspective" : "Orthographic";
                cameraJson["fov"] = camera->fov;
                cameraJson["nearPlane"] = camera->nearPlane;
                cameraJson["farPlane"] = camera->farPlane;
                cameraJson["aspectRatio"] = camera->aspectRatio;
                cameraJson["orthoSize"] = camera->orthoSize;
                cameraJson["isMainCamera"] = camera->isMainCamera;
                componentsJson["Camera"] = cameraJson;
            }

            // Light component
            if (auto* light = m_registry.try_get<LightComponent>(entity)) {
                nlohmann::json lightJson;
                lightJson["type"] = (light->type == LightComponent::Directional) ? "Directional" :
                                  (light->type == LightComponent::Point) ? "Point" : "Spot";
                lightJson["color"] = {light->color.x, light->color.y, light->color.z};
                lightJson["intensity"] = light->intensity;
                lightJson["range"] = light->range;
                lightJson["innerCone"] = light->innerCone;
                lightJson["outerCone"] = light->outerCone;
                lightJson["castShadows"] = light->castShadows;
                componentsJson["Light"] = lightJson;
            }

            // Hierarchy component
            if (auto* hierarchy = m_registry.try_get<HierarchyComponent>(entity)) {
                nlohmann::json hierarchyJson;
                if (hierarchy->parent != entt::null) {
                    hierarchyJson["parent"] = static_cast<uint32_t>(hierarchy->parent);
                }

                nlohmann::json childrenJson = nlohmann::json::array();
                for (auto child : hierarchy->children) {
                    childrenJson.push_back(static_cast<uint32_t>(child));
                }
                hierarchyJson["children"] = childrenJson;

                componentsJson["Hierarchy"] = hierarchyJson;
            }

            entityJson["components"] = componentsJson;
            entitiesJson.push_back(entityJson);
        }

        sceneJson["entities"] = entitiesJson;
        return sceneJson;
    }

    void deserialize(const nlohmann::json& sceneJson) {
        // Clear current scene
        clear();

        if (sceneJson.contains("name")) {
            m_name = sceneJson["name"];
        }

        if (sceneJson.contains("entities")) {
            std::unordered_map<uint32_t, entt::entity> entityMap;

            // First pass: Create all entities
            for (const auto& entityJson : sceneJson["entities"]) {
                uint32_t oldId = entityJson["id"];
                entt::entity newEntity = m_registry.create();
                entityMap[oldId] = newEntity;
            }

            // Second pass: Restore components
            for (const auto& entityJson : sceneJson["entities"]) {
                uint32_t oldId = entityJson["id"];
                entt::entity entity = entityMap[oldId];

                if (entityJson.contains("components")) {
                    const auto& componentsJson = entityJson["components"];

                    // Transform component
                    if (componentsJson.contains("Transform")) {
                        const auto& transformJson = componentsJson["Transform"];
                        auto& transform = m_registry.emplace<TransformComponent>(entity);

                        if (transformJson.contains("position")) {
                            auto pos = transformJson["position"];
                            transform.position = glm::vec3(pos[0], pos[1], pos[2]);
                        }
                        if (transformJson.contains("rotation")) {
                            auto rot = transformJson["rotation"];
                            transform.rotation = glm::vec3(rot[0], rot[1], rot[2]);
                        }
                        if (transformJson.contains("scale")) {
                            auto scl = transformJson["scale"];
                            transform.scale = glm::vec3(scl[0], scl[1], scl[2]);
                        }
                        transform.isDirty = true;
                    }

                    // Name component
                    if (componentsJson.contains("Name")) {
                        const auto& nameJson = componentsJson["Name"];
                        auto& name = m_registry.emplace<NameComponent>(entity);
                        name.name = nameJson["name"];
                    }

                    // Tag component
                    if (componentsJson.contains("Tag")) {
                        const auto& tagJson = componentsJson["Tag"];
                        auto& tag = m_registry.emplace<TagComponent>(entity);
                        tag.tag = tagJson["tag"];
                    }

                    // Camera component
                    if (componentsJson.contains("Camera")) {
                        const auto& cameraJson = componentsJson["Camera"];
                        auto& camera = m_registry.emplace<CameraComponent>(entity);

                        if (cameraJson.contains("type")) {
                            std::string type = cameraJson["type"];
                            camera.type = (type == "Perspective") ? CameraComponent::Perspective : CameraComponent::Orthographic;
                        }
                        if (cameraJson.contains("fov")) {
                            camera.fov = cameraJson["fov"];
                        }
                        if (cameraJson.contains("nearPlane")) {
                            camera.nearPlane = cameraJson["nearPlane"];
                        }
                        if (cameraJson.contains("farPlane")) {
                            camera.farPlane = cameraJson["farPlane"];
                        }
                        if (cameraJson.contains("aspectRatio")) {
                            camera.aspectRatio = cameraJson["aspectRatio"];
                        }
                        if (cameraJson.contains("orthoSize")) {
                            camera.orthoSize = cameraJson["orthoSize"];
                        }
                        if (cameraJson.contains("isMainCamera")) {
                            camera.isMainCamera = cameraJson["isMainCamera"];
                        }
                    }

                    // Light component
                    if (componentsJson.contains("Light")) {
                        const auto& lightJson = componentsJson["Light"];
                        auto& light = m_registry.emplace<LightComponent>(entity);

                        if (lightJson.contains("type")) {
                            std::string type = lightJson["type"];
                            if (type == "Directional") light.type = LightComponent::Directional;
                            else if (type == "Point") light.type = LightComponent::Point;
                            else if (type == "Spot") light.type = LightComponent::Spot;
                        }
                        if (lightJson.contains("color")) {
                            auto color = lightJson["color"];
                            light.color = glm::vec3(color[0], color[1], color[2]);
                        }
                        if (lightJson.contains("intensity")) {
                            light.intensity = lightJson["intensity"];
                        }
                        if (lightJson.contains("range")) {
                            light.range = lightJson["range"];
                        }
                        if (lightJson.contains("innerCone")) {
                            light.innerCone = lightJson["innerCone"];
                        }
                        if (lightJson.contains("outerCone")) {
                            light.outerCone = lightJson["outerCone"];
                        }
                        if (lightJson.contains("castShadows")) {
                            light.castShadows = lightJson["castShadows"];
                        }
                    }

                }
            }

            // Third pass: Restore hierarchy relationships
            for (const auto& entityJson : sceneJson["entities"]) {
                uint32_t oldId = entityJson["id"];
                entt::entity entity = entityMap[oldId];

                if (entityJson.contains("components") && entityJson["components"].contains("Hierarchy")) {
                    const auto& hierarchyJson = entityJson["components"]["Hierarchy"];
                    auto& hierarchy = m_registry.emplace<HierarchyComponent>(entity);

                    if (hierarchyJson.contains("parent")) {
                        uint32_t oldParentId = hierarchyJson["parent"];
                        if (entityMap.find(oldParentId) != entityMap.end()) {
                            hierarchy.parent = entityMap[oldParentId];
                        }
                    }

                    if (hierarchyJson.contains("children")) {
                        for (const auto& oldChildId : hierarchyJson["children"]) {
                            uint32_t childId = oldChildId;
                            if (entityMap.find(childId) != entityMap.end()) {
                                hierarchy.children.push_back(entityMap[childId]);
                            }
                        }
                    }
                }
            }
        }

        // Reinitialize systems after loading
        if (m_initialized) {
            m_systemManager.initialize(m_registry);
        }
    }

    bool saveToFile(const std::string& filename) const {
        try {
            nlohmann::json sceneJson = serialize();
            std::ofstream file(filename);
            file << sceneJson.dump(4);
            return true;
        } catch (const std::exception& e) {
            return false;
        }
    }

    bool loadFromFile(const std::string& filename) {
        try {
            std::ifstream file(filename);
            nlohmann::json sceneJson;
            file >> sceneJson;
            deserialize(sceneJson);
            return true;
        } catch (const std::exception& e) {
            return false;
        }
    }

    // Scene management
    void clear() {
        m_registry.clear();
        m_frameCount = 0;
    }

    size_t getEntityCount() const {
        return m_registry.view<entt::entity>().size();
    }

    // Prefab system - templates for common entities
    struct Prefab {
        std::string name;
        nlohmann::json data;
    };

    void registerPrefab(const std::string& name, entt::entity templateEntity) {
        // Serialize a single entity as a prefab template
        Prefab prefab;
        prefab.name = name;

        nlohmann::json entityJson;
        entityJson["id"] = static_cast<uint32_t>(templateEntity);

        // Store component data (similar to serialize but for one entity)
        nlohmann::json componentsJson;

        if (auto* transform = m_registry.try_get<TransformComponent>(templateEntity)) {
            nlohmann::json transformJson;
            transformJson["position"] = {transform->position.x, transform->position.y, transform->position.z};
            transformJson["rotation"] = {transform->rotation.x, transform->rotation.y, transform->rotation.z};
            transformJson["scale"] = {transform->scale.x, transform->scale.y, transform->scale.z};
            componentsJson["Transform"] = transformJson;
        }

        // Add other components as needed...
        entityJson["components"] = componentsJson;
        prefab.data = entityJson;

        m_prefabs[name] = prefab;
    }

    entt::entity instantiatePrefab(const std::string& prefabName, const glm::vec3& position = {0,0,0}) {
        auto it = m_prefabs.find(prefabName);
        if (it == m_prefabs.end()) {
            return entt::null;
        }

        entt::entity newEntity = m_registry.create();

        // Apply prefab data to new entity
        const auto& prefabData = it->second.data;
        if (prefabData.contains("components")) {
            // Restore components from prefab (similar to deserialize)
            const auto& componentsJson = prefabData["components"];

            if (componentsJson.contains("Transform")) {
                const auto& transformJson = componentsJson["Transform"];
                auto& transform = m_registry.emplace<TransformComponent>(newEntity);

                if (transformJson.contains("position")) {
                    auto pos = transformJson["position"];
                    transform.position = glm::vec3(pos[0], pos[1], pos[2]) + position; // Apply position offset
                }
                if (transformJson.contains("rotation")) {
                    auto rot = transformJson["rotation"];
                    transform.rotation = glm::vec3(rot[0], rot[1], rot[2]);
                }
                if (transformJson.contains("scale")) {
                    auto scl = transformJson["scale"];
                    transform.scale = glm::vec3(scl[0], scl[1], scl[2]);
                }
                transform.isDirty = true;
            }

            // Add other components as needed...
        }

        return newEntity;
    }

    // Debug and introspection
    void printDebugInfo()  {
        std::cout << "Scene: " << m_name << std::endl;
        std::cout << "Entities: " << getEntityCount() << std::endl;
        std::cout << "Frame: " << m_frameCount << std::endl;

        // Count components
        std::unordered_map<std::string, int> componentCounts;

        // Modern EnTT way to iterate over all entities
        auto allEntities = m_registry.view<entt::entity>();
        for (auto entity : allEntities) {
            for (const auto& componentName : m_componentRegistry.getAllComponentNames()) {
                if (m_componentRegistry.hasComponent(componentName, m_registry, entity)) {
                    componentCounts[componentName]++;
                }
            }
        }

        std::cout << "Component counts:" << std::endl;
        for (const auto& [name, count] : componentCounts) {
            std::cout << "  " << name << ": " << count << std::endl;
        }
    }

    // Accessors
    const std::string& getName() const { return m_name; }
    void setName(const std::string& name) { m_name = name; }

    entt::registry& getRegistry() { return m_registry; }
    const entt::registry& getRegistry() const { return m_registry; }

    SystemManager& getSystemManager() { return m_systemManager; }
    const SystemManager& getSystemManager() const { return m_systemManager; }

    ComponentRegistry& getComponentRegistry() { return m_componentRegistry; }
    const ComponentRegistry& getComponentRegistry() const { return m_componentRegistry; }

private:
    std::string m_name;
    entt::registry m_registry;
    SystemManager m_systemManager;
    ComponentRegistry m_componentRegistry;
    ResourceManager& m_resourceManager;
    VulkanDevice& m_device;

    std::chrono::high_resolution_clock::time_point m_lastUpdateTime;
    uint64_t m_frameCount = 0;
    bool m_initialized = false;

    // Prefab system
    std::unordered_map<std::string, Prefab> m_prefabs;

    void initializeDefaultSystems() {
        // Add core systems in the right order
        addSystem<TransformSystem>()->priority = 0;
        addSystem<CameraSystem>()->priority = 10;
        addSystem<LifetimeSystem>()->priority = 100; // Run last to clean up expired entities
    }
};

// ═══════════════════════════════════════════════════════════════
// Scene Manager - Managing multiple scenes
// ═══════════════════════════════════════════════════════════════

class SceneManager {
public:
    SceneManager(ResourceManager& resourceManager, VulkanDevice& device)
        : m_resourceManager(resourceManager), m_device(device) {}

    std::shared_ptr<Scene> createScene(const std::string& name) {
        auto scene = std::make_shared<Scene>(name, m_resourceManager, m_device);
        m_scenes[name] = scene;

        if (!m_activeScene) {
            setActiveScene(name);
        }

        return scene;
    }

    std::shared_ptr<Scene> getScene(const std::string& name) {
        auto it = m_scenes.find(name);
        return (it != m_scenes.end()) ? it->second : nullptr;
    }

    bool setActiveScene(const std::string& name) {
        auto it = m_scenes.find(name);
        if (it != m_scenes.end()) {
            m_activeScene = it->second;
            m_activeSceneName = name;
            return true;
        }
        return false;
    }

    std::shared_ptr<Scene> getActiveScene() {
        return m_activeScene;
    }

    const std::string& getActiveSceneName() const {
        return m_activeSceneName;
    }

    bool loadScene(const std::string& name, const std::string& filename) {
        auto scene = createScene(name);
        if (scene->loadFromFile(filename)) {
            scene->initialize();
            return true;
        }

        // Remove failed scene
        m_scenes.erase(name);
        if (m_activeScene.get() == scene.get()) {
            m_activeScene.reset();
            m_activeSceneName.clear();
        }
        return false;
    }

    bool saveScene(const std::string& name, const std::string& filename) {
        auto scene = getScene(name);
        return scene ? scene->saveToFile(filename) : false;
    }

    void removeScene(const std::string& name) {
        auto it = m_scenes.find(name);
        if (it != m_scenes.end()) {
            if (m_activeScene == it->second) {
                m_activeScene.reset();
                m_activeSceneName.clear();
            }
            m_scenes.erase(it);
        }
    }

    void update() {
        if (m_activeScene) {
            m_activeScene->update();
        }
    }

    void fixedUpdate(float fixedDeltaTime) {
        if (m_activeScene) {
            m_activeScene->fixedUpdate(fixedDeltaTime);
        }
    }

    std::vector<std::string> getSceneNames() const {
        std::vector<std::string> names;
        for (const auto& [name, scene] : m_scenes) {
            names.push_back(name);
        }
        return names;
    }

private:
    ResourceManager& m_resourceManager;
    VulkanDevice& m_device;
    std::unordered_map<std::string, std::shared_ptr<Scene>> m_scenes;
    std::shared_ptr<Scene> m_activeScene;
    std::string m_activeSceneName;
};

} // namespace ECS
} // namespace Shoonyakasha

// Backward compatibility alias
namespace ECS = Shoonyakasha::ECS;