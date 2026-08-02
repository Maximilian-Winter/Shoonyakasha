//
// Combined PBR + Physics + Particles + Bloom Example
//
// 萬法歸一 — All phenomena return to One
//
// Demonstrates all major engine features together:
// - Procedural PBR meshes (boxes, spheres, ground plane)
// - Bullet3 physics simulation
// - GPU compute particle system (75K particles)
// - Deferred rendering with IBL
// - Bloom post-processing
// - Impact-driven particle attractors
//

#pragma once

#include "App/ApplicationBase.h"
#include "ECS/PhysicsSystem.h"
#include "GPU/GPUResourceFactory.h"
#include "ECS/RenderComponents.h"

#include <random>
#include <vector>
#include <deque>
#include <unordered_map>

class CombinedExampleApp : public ApplicationBase {
public:
    explicit CombinedExampleApp(const ApplicationConfig& config);

protected:
    // ─── ApplicationBase Hooks ───────────────────────────────────
    void registerSystems() override;
    void onInit() override;
    void onPostInit() override;
    void onUpdate(float dt) override;
    void onPreRender(float dt) override;
    void onKeyPressed(int keyCode) override;

private:
    // ─── Physics State ───────────────────────────────────────────
    ECS::PhysicsSystem* m_physicsSystem = nullptr;  // Non-owning
    int m_spawnCount = 0;
    std::mt19937 m_rng{42};
    std::vector<entt::entity> m_physicsEntities;    // Tracked for reset

    // ─── Particle System ─────────────────────────────────────────
    static constexpr uint32_t PARTICLE_COUNT = 75000;
    float m_particleTime = 0.0f;
    bool m_particlesEnabled = true;

    // ─── Impact Tracking (physics -> particle attractors) ────────
    static constexpr int MAX_ATTRACTORS = 4;

    struct ImpactAttractor {
        glm::vec3 position{0.0f};
        float strength = 0.0f;
        float age = 0.0f;
        static constexpr float LIFETIME = 3.0f;
    };

    std::deque<ImpactAttractor> m_impactAttractors;
    std::unordered_map<entt::entity, glm::vec3> m_prevVelocities;

    // ─── FPS Tracking ────────────────────────────────────────────
    uint32_t m_frameCount = 0;
    float m_fpsAccumulator = 0.0f;

    // ─── Procedural Mesh ─────────────────────────────────────────
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

    // ─── Particle Updates ────────────────────────────────────────
    void updateParticleParams();
    void detectImpacts();
    void updateAttractors();
};
