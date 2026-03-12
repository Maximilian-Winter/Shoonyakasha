//
// Declarative Sponza Test Application
//
// 虚空之道 — The Way of Emptiness
//
// This example demonstrates the declarative frame graph:
// - Dot-path UBOs (camera, lights) auto-filled from ECS via JSON sources
// - MaterialComponentV5 + MeshComponent for materials
// - Dot-path resolution for push constants and uniform buffers
// - Dynamic lights from LightComponent entities
// - Minimal C++ code - just load and render
//

#pragma once

#include "App/ApplicationBase.h"

class DeclarativeSponzaApp : public ApplicationBase {
public:
    explicit DeclarativeSponzaApp(const ApplicationConfig& config);

protected:
    void onInit() override;
    void onPostInit() override;
    void onPreRender(float dt) override;
    void onKeyPressed(int keyCode) override;

private:
    // Particle system
    static constexpr uint32_t PARTICLE_COUNT = 50000;
    float m_particleTime = 0.0f;

    void updateParticleParams();
};
