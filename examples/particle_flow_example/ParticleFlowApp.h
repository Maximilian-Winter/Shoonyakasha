//
// Particle Flow — Visual Showcase
//
// GPU compute particle simulation with dynamic attractors,
// pulsing gravity, and rich visual effects.
//

#pragma once

#include "App/ApplicationBase.h"

#include <chrono>

class ParticleFlowApp : public ApplicationBase {
public:
    explicit ParticleFlowApp(const ApplicationConfig& config);

protected:
    void onInit() override;
    void onPostInit() override;
    void onUpdate(float dt) override;
    void onPreRender(float dt) override;
    void onKeyPressed(int keyCode) override;

private:
    // Particle system
    static constexpr uint32_t PARTICLE_COUNT = 100000;
    float m_particleTime = 0.0f;

    // FPS tracking
    uint32_t m_frameCount = 0;
    float m_fpsAccumulator = 0.0f;

    void updateParticleParams();
};
