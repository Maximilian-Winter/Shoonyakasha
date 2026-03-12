#include "ParticleFlowApp.h"

#include <GLFW/glfw3.h>
#include <cmath>
#include <chrono>
#include <iostream>

#include "ECS/RenderComponents.h"
#include "ECS/CameraController.h"
#include "Vulkan/FrameGraph/FrameGraph.h"

// ═══════════════════════════════════════════════════════════════
// Construction
// ═══════════════════════════════════════════════════════════════

ParticleFlowApp::ParticleFlowApp(const ApplicationConfig& config)
    : ApplicationBase(config)
{
}

// ═══════════════════════════════════════════════════════════════
// ApplicationBase Hooks
// ═══════════════════════════════════════════════════════════════

void ParticleFlowApp::onInit() {
    // Camera
    createCamera(glm::vec3(0.0f, 5.0f, 35.0f), 60.f, 8.f);

    // Load glTF scene
    GltfLoadOptions options;
    options.loadTextures = true;
    options.createEntities = true;
    options.namePrefix = "sponza";
    options.maxTextureSize = 4096;

    auto result = loadGltfScene("NewSponza_Main_glTF_003.gltf", options);
    if (!result.success) {
        throw std::runtime_error("Failed to load glTF: " + result.error);
    }

    // Directional sun only (point light commented out in original)
    createDirectionalLight(
        glm::vec3(-0.5f, -1.0f, -0.3f),
        glm::vec3(1.0f, 0.92f, 0.8f),
        3.0f);
}

void ParticleFlowApp::onPostInit() {
    // Nothing special needed after compile
}

void ParticleFlowApp::onUpdate(float dt) {
    // FPS tracking
    m_frameCount++;
    m_fpsAccumulator += dt;
    if (m_fpsAccumulator >= 1.0f) {
        float fps = static_cast<float>(m_frameCount) / m_fpsAccumulator;
        float frameMs = (m_fpsAccumulator / m_frameCount) * 1000.0f;
        getLogger().log(LogLevel::Info, "FPS: %.1f (%.2f ms/frame)", fps, frameMs);
        m_frameCount = 0;
        m_fpsAccumulator = 0.0f;
    }
}

void ParticleFlowApp::onPreRender(float dt) {
    updateParticleParams();
}

void ParticleFlowApp::onKeyPressed(int keyCode) {
    switch (keyCode) {
        case GLFW_KEY_SPACE: {
            auto* ctrl = getRegistry().try_get<ECS::CameraControllerComponent>(getCameraEntity());
            if (ctrl && ctrl->mode == ECS::CameraControllerComponent::Mode::Free) {
                ctrl->horizonLocked = !ctrl->horizonLocked;
                getLogger().log(LogLevel::Info, "Horizon Lock: %s", ctrl->horizonLocked ? "ON" : "OFF");
            }
            break;
        }

        // Screenshot: save all render targets to disk
        case GLFW_KEY_F12: {
            auto now = std::chrono::system_clock::now();
            auto time = std::chrono::system_clock::to_time_t(now);
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                now.time_since_epoch()) % 1000;
            std::tm tm{};
            localtime_s(&tm, &time);
            char stamp[64];
            std::snprintf(stamp, sizeof(stamp), "%04d%02d%02d_%02d%02d%02d_%03d",
                tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                tm.tm_hour, tm.tm_min, tm.tm_sec, static_cast<int>(ms.count()));

            std::string s(stamp);
            getRenderGraph().saveRenderTarget("swapchain",           "screenshots/final_" + s + ".png");
            getRenderGraph().saveRenderTarget("gPosition",           "screenshots/gPosition_" + s + ".png");
            getRenderGraph().saveRenderTarget("gNormal",             "screenshots/gNormal_" + s + ".png");
            getRenderGraph().saveRenderTarget("gAlbedo",             "screenshots/gAlbedo_" + s + ".png");
            getRenderGraph().saveRenderTarget("gMetallicRoughness",  "screenshots/gMetalRough_" + s + ".png");
            getRenderGraph().saveRenderTarget("litColorHDR",         "screenshots/litColorHDR_" + s + ".png");
            getRenderGraph().saveRenderTarget("bloomBright",         "screenshots/bloomBright_" + s + ".png");
            getRenderGraph().saveRenderTarget("bloomBlurred",        "screenshots/bloomBlurred_" + s + ".png");
            getLogger().log(LogLevel::Info, "Saved all render targets with stamp: %s", stamp);
            break;
        }
    }
}

// ═══════════════════════════════════════════════════════════════
// Particle Parameters — Dynamic visual showcase
// ═══════════════════════════════════════════════════════════════

void ParticleFlowApp::updateParticleParams() {
    m_particleTime += getDeltaTime();

    auto& ctx = getRenderGraph().getSceneContext();

    // Pulsing gravity — cycles between gentle and strong
    float gravPulse = 0.5f + 1.5f * (0.5f + 0.5f * sinf(m_particleTime * 0.3f));
    ctx.setCustom("particles.gravity", gravPulse);
    ctx.setCustom("particles.count", PARTICLE_COUNT);
    ctx.setCustom("particles.boundaryRadius", 20.0f);

    // Orbiting attractor — traces a lissajous figure-8
    float ax = sinf(m_particleTime * 0.4f) * 8.0f;
    float ay = 5.0f + cosf(m_particleTime * 0.25f) * 3.0f;
    float az = sinf(m_particleTime * 0.6f) * 5.0f;
    float attractStrength = 30.0f + 15.0f * sinf(m_particleTime * 0.5f);
    ctx.setCustom("particles.attractorPos", glm::vec4(ax, ay, az, attractStrength));

    // Stronger, more chaotic wind
    float windAngle = m_particleTime * 0.5f;
    ctx.setCustom("particles.wind", glm::vec4(
        sinf(windAngle) * 1.2f,
        0.3f + 0.5f * sinf(m_particleTime * 0.7f),
        cosf(windAngle * 1.3f) * 1.0f,
        0.8f   // high turbulence
    ));

    ctx.setCustom("particles.damping", 0.992f);  // less damping = wilder
    ctx.setCustom("particles.spawnHeight", 0.5f);
}
