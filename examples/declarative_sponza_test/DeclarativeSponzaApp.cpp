#include "DeclarativeSponzaApp.h"

#include <GLFW/glfw3.h>
#include <cmath>

#include "ECS/RenderComponents.h"
#include "ECS/CameraController.h"
#include "Vulkan/FrameGraph/FrameGraph.h"

// ═══════════════════════════════════════════════════════════════
// Construction
// ═══════════════════════════════════════════════════════════════

DeclarativeSponzaApp::DeclarativeSponzaApp(const ApplicationConfig& config)
    : ApplicationBase(config)
{
}

// ═══════════════════════════════════════════════════════════════
// ApplicationBase Hooks
// ═══════════════════════════════════════════════════════════════

void DeclarativeSponzaApp::onInit() {
    // Camera at (0, 0, 5) looking at origin
    createCamera(glm::vec3(0.0f, 0.0f, 5.0f), 60.f, 8.f);

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

    // Count opaque vs transparent for logging
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

void DeclarativeSponzaApp::onPostInit() {
    // Register readback callback for particle SSBO
    getRenderGraph().registerReadbackCallback("particleSSBO",
        [this](const auto& result) {
            getLogger().log(LogLevel::Info, "Particle readback: %u elements, frame %llu",
                            result.elementCount, static_cast<unsigned long long>(result.frameNumber));
        });
}

void DeclarativeSponzaApp::onPreRender(float dt) {
    updateParticleParams();
}

void DeclarativeSponzaApp::onKeyPressed(int keyCode) {
    if (keyCode == GLFW_KEY_SPACE) {
        auto* ctrl = getRegistry().try_get<ECS::CameraControllerComponent>(getCameraEntity());
        if (ctrl && ctrl->mode == ECS::CameraControllerComponent::Mode::Free) {
            ctrl->horizonLocked = !ctrl->horizonLocked;
            getLogger().log(LogLevel::Info, "Horizon Lock: %s", ctrl->horizonLocked ? "ON" : "OFF");
        }
    }
}

// ═══════════════════════════════════════════════════════════════
// Particle Parameters
// ═══════════════════════════════════════════════════════════════

void DeclarativeSponzaApp::updateParticleParams() {
    m_particleTime += getDeltaTime();

    auto& ctx = getRenderGraph().getSceneContext();

    ctx.setCustom("particles.gravity", 1.5f);
    ctx.setCustom("particles.count", PARTICLE_COUNT);
    ctx.setCustom("particles.boundaryRadius", 15.0f);

    // Attractor: warm point light in atrium center
    ctx.setCustom("particles.attractorPos", glm::vec4(0.0f, 5.0f, 0.0f, 25.0f));

    // Wind: gentle swirl with slow rotation
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
