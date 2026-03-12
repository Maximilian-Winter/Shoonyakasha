//
// Physics Test Application
//
// 重力之道 — The Way of Gravity
//
// Demonstrates Bullet3 physics integration:
// - Ground plane with static body
// - Falling boxes and spheres with dynamic bodies
// - Spawn objects with Space key
// - PBR rendered via declarative frame graph
//

#pragma once

#include "App/ApplicationBase.h"
#include "ECS/PhysicsSystem.h"
#include "GPU/GPUResourceFactory.h"
#include "ECS/RenderComponents.h"

#include <random>
#include <vector>

class PhysicsTestApp : public ApplicationBase {
public:
    explicit PhysicsTestApp(const ApplicationConfig& config);

protected:
    // ─── ApplicationBase Hooks ───────────────────────────────────
    void registerSystems() override;
    void onInit() override;
    void onPostInit() override;
    void onKeyPressed(int keyCode) override;

private:
    // ─── Physics State ───────────────────────────────────────────
    ECS::PhysicsSystem* m_physicsSystem = nullptr;  // Non-owning
    int m_spawnCount = 0;
    std::mt19937 m_rng{42};
    std::vector<entt::entity> m_physicsEntities;    // Tracked for reset

    // ─── Procedural Mesh ─────────────────────────────────────────

    /// Standard vertex matching the "standard" vertex format in the pipeline JSON.
    struct StandardVertex {
        glm::vec3 position;   // location 0
        glm::vec3 color;      // location 1
        glm::vec2 texCoord;   // location 2
        glm::vec3 normal;     // location 3
    };

    entt::entity createBox(const glm::vec3& pos, const glm::vec3& size,
                           const glm::vec3& color, float mass = 1.0f);
    entt::entity createSphere(const glm::vec3& pos, float radius,
                              const glm::vec3& color, float mass = 1.0f);
    entt::entity createGroundPlane(float y = 0.0f, float size = 50.0f);
    Shoonyakasha::MeshComponent uploadMesh(
        const std::vector<StandardVertex>& vertices,
        const std::vector<uint32_t>& indices);

    // ─── Scene Management ────────────────────────────────────────
    void createPhysicsScene();
    void spawnRandomObject();
    void resetScene();
    glm::vec3 randomColor();
};
