#include "SSBODataFlowApp.h"

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

SSBODataFlowApp::SSBODataFlowApp(const ApplicationConfig& config, bool loadFromFile)
    : ApplicationBase(config)
    , m_loadFromFile(loadFromFile)
{
}

// ═══════════════════════════════════════════════════════════════
// ApplicationBase Hooks
// ═══════════════════════════════════════════════════════════════

void SSBODataFlowApp::onInit() {
    // Camera at origin-ish, looking forward
    createCamera(glm::vec3(0.0f, 0.0f, 15.0f), 60.f, 8.f);

    // Load glTF scene
    GltfLoadOptions options;
    options.loadTextures = true;
    options.createEntities = true;
    options.namePrefix = "sponza";
    options.maxTextureSize = 4096;

    auto result = loadGltfScene("Box.gltf", options);
    if (!result.success) {
        throw std::runtime_error("Failed to load glTF: " + result.error);
    }

    // Count opaque vs transparent
    auto& registry = getRegistry();
    size_t opaqueCount = 0, transparentCount = 0;
    auto view = registry.view<Shoonyakasha::MaterialComponentV5>();
    for (auto entity : view) {
        const auto& mat = view.get<Shoonyakasha::MaterialComponentV5>(entity);
        if (mat.isTransparent()) {
            transparentCount++;
        } else {
            opaqueCount++;
        }
    }
    getLogger().log(LogLevel::Info, "Entities: %zu opaque, %zu transparent", opaqueCount, transparentCount);

    // Lights
    createDirectionalLight(
        glm::vec3(-0.5f, -1.0f, -0.3f),
        glm::vec3(1.0f, 0.975f, 0.95f),
        2.0f);
    createPointLight(
        glm::vec3(0.0f, 3.0f, 0.0f),
        glm::vec3(1.0f, 0.8f, 0.6f),
        5.0f, 15.0f);
}

void SSBODataFlowApp::onPostInit() {
    // Register readback callback for particle SSBO stats
    getRenderGraph().registerReadbackCallback("particleSSBO",
        [this](const auto& result) {
            // Interpret raw data as particle structs for verification
            struct Particle { float px, py, pz, pw, vx, vy, vz, vw; };
            const auto* particles = static_cast<const Particle*>(result.data);
            uint32_t count = result.elementCount;

            if (count > 0) {
                getLogger().log(LogLevel::Info,
                    "Readback frame %llu: %u particles — first pos=(%.2f, %.2f, %.2f) vel=(%.2f, %.2f, %.2f)",
                    static_cast<unsigned long long>(result.frameNumber), count,
                    particles[0].px, particles[0].py, particles[0].pz,
                    particles[0].vx, particles[0].vy, particles[0].vz);
            }
        });
}

void SSBODataFlowApp::onUpdate(float dt) {
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

void SSBODataFlowApp::onPreRender(float dt) {
    updateParticleParams();
}

void SSBODataFlowApp::onKeyPressed(int keyCode) {
    switch (keyCode) {
        case GLFW_KEY_SPACE: {
            auto* ctrl = getRegistry().try_get<ECS::CameraControllerComponent>(getCameraEntity());
            if (ctrl && ctrl->mode == ECS::CameraControllerComponent::Mode::Free) {
                ctrl->horizonLocked = !ctrl->horizonLocked;
                getLogger().log(LogLevel::Info, "Horizon Lock: %s", ctrl->horizonLocked ? "ON" : "OFF");
            }
            break;
        }

        // Phase 4: Save particle state to disk
        case GLFW_KEY_S:
            if (!m_loadFromFile) {
                getRenderGraph().triggerSave("particleSSBO");
                getLogger().log(LogLevel::Info, "*** SAVE TRIGGERED — particle state will be saved to data/particles.bin ***");
            } else {
                getLogger().log(LogLevel::Info, "Save not available in LOAD mode");
            }
            break;

        // Screenshot: save render targets to disk
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

            getRenderGraph().saveRenderTarget("litColorHDR",
                std::string("screenshots/litColorHDR_") + stamp + ".hdr");
            getRenderGraph().saveRenderTarget("swapchain",
                std::string("screenshots/final_") + stamp + ".png");
            getLogger().log(LogLevel::Info, "*** SCREENSHOTS SAVED: %s ***", stamp);
            break;
        }
    }
}

// ═══════════════════════════════════════════════════════════════
// Particle Parameters
// ═══════════════════════════════════════════════════════════════

void SSBODataFlowApp::updateParticleParams() {
    m_particleTime += getDeltaTime();

    auto& ctx = getRenderGraph().getSceneContext();

    ctx.setCustom("particles.gravity", 1.5f);
    ctx.setCustom("particles.count", PARTICLE_COUNT);
    ctx.setCustom("particles.boundaryRadius", 15.0f);
    ctx.setCustom("particles.attractorPos", glm::vec4(0.0f, 5.0f, 0.0f, 25.0f));

    float windAngle = m_particleTime * 0.2f;
    ctx.setCustom("particles.wind", glm::vec4(
        sinf(windAngle) * 0.4f,
        0.1f,
        cosf(windAngle) * 0.4f,
        0.3f
    ));

    ctx.setCustom("particles.damping", 0.998f);
    ctx.setCustom("particles.spawnHeight", 0.5f);
}
