//
// SSBO Data Flow Example — Phase 4 Demonstration
//
// 保存之輪 — The Wheel of Preservation
//
// Demonstrates the full SSBO data flow cycle:
//   - GPU compute particle simulation
//   - Save particle state to disk (press S)
//   - Load particle state from disk (--from-file)
//   - Ring-buffered readback with stats logging
//

#pragma once

#include "App/ApplicationBase.h"

class SSBODataFlowApp : public ApplicationBase {
public:
    explicit SSBODataFlowApp(const ApplicationConfig& config, bool loadFromFile = false);

protected:
    void onInit() override;
    void onPostInit() override;
    void onUpdate(float dt) override;
    void onPreRender(float dt) override;
    void onKeyPressed(int keyCode) override;

private:
    // Mode flag: load particles from file instead of random init
    bool m_loadFromFile = false;

    // Particle system
    static constexpr uint32_t PARTICLE_COUNT = 150000;
    float m_particleTime = 0.0f;

    // FPS tracking
    uint32_t m_frameCount = 0;
    float m_fpsAccumulator = 0.0f;

    void updateParticleParams();
};
