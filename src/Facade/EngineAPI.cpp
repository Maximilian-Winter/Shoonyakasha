//
// Facade/EngineAPI.cpp - PIMPL implementation with CallbackApp : ApplicationBase
//
// All Vulkan, EnTT, and engine internals are confined to this translation unit.
//

#include <entt/entt.hpp>
#include "ECS/Core.h"
#include "ECS/Sprite2DComponents.h"

#include "Facade/EngineAPI.h"
#include "Facade/SceneAPI.h"
#include "InputAPIImpl.h"
#include "PhysicsAPIImpl.h"
#include "FacadeInternal.h"

#include "App/ApplicationBase.h"
#include "ECS/Scene.h"
#include "ECS/PhysicsSystem.h"
#include "ECS/SkeletalAnimationSystem.h"
#include "ECS/SkeletonComponents.h"
#include "FrameGraph/DotPathResolver.h"
#include "Vulkan/FrameGraph/FrameGraph.h"

using namespace Shoonyakasha::Facade::Internal;

namespace Shoonyakasha {
namespace Facade {

// ═══════════════════════════════════════════════════════════════
// CallbackApp — routes ApplicationBase hooks to std::function
// ═══════════════════════════════════════════════════════════════

struct EngineAPI::Impl {
    // Stored callbacks
    VoidCallback   onInitCb;
    VoidCallback   onPostInitCb;
    UpdateCallback onUpdateCb;
    UpdateCallback onPreRenderCb;
    VoidCallback   onPostRenderCb;
    KeyCallback    onKeyPressedCb;
    ResizeCallback onResizeCb;
    VoidCallback   onCleanupCb;

    // Sub-APIs (created in CallbackApp::onInit)
    std::unique_ptr<SceneAPI>   sceneAPI;
    std::unique_ptr<InputAPI>   inputAPI;
    std::unique_ptr<PhysicsAPI> physicsAPI;

    // The actual application (defined below, after Impl)
    class CallbackApp;
    std::unique_ptr<CallbackApp> app;
};

// ─── CallbackApp Definition ──────────────────────────────────

class EngineAPI::Impl::CallbackApp : public ApplicationBase {
public:
    CallbackApp(const ApplicationConfig& config, Impl* owner)
        : ApplicationBase(config)
        , m_owner(owner)
    {}

protected:
    void registerSystems() override {
        ApplicationBase::registerSystems();
        // Always register PhysicsSystem (disabled by default).
        // User activates via PhysicsAPI.setEnabled(true).
        m_physicsSystem = getScene().addSystem<ECS::PhysicsSystem>();
        m_physicsSystem->enabled = false;
    }

    void onInit() override {
        // Create skeletal animation system (needs VulkanDevice — available after Vulkan init)
        m_animationSystem = std::make_unique<SkeletalAnimationSystem>(getDevice());

        // Create sub-APIs now that engine internals are ready
        m_owner->sceneAPI = std::make_unique<SceneAPI>(getScene());
        m_owner->sceneAPI->wireSprite2DManager(&getSprite2DManager());

        // Wire InputAPI via Impl::wire (nested class can access private m_impl)
        m_owner->inputAPI = std::make_unique<InputAPI>();
        InputAPI::Impl::wire(*m_owner->inputAPI,
                             &getInputHandler(),
                             &getEventDispatcher());

        // Wire PhysicsAPI via Impl::wire
        m_owner->physicsAPI = std::make_unique<PhysicsAPI>();
        PhysicsAPI::Impl::wire(*m_owner->physicsAPI,
                               m_physicsSystem,
                               &getRegistry());

        if (m_owner->onInitCb) m_owner->onInitCb();
    }

    void onPostInit() override {
        if (m_owner->onPostInitCb) m_owner->onPostInitCb();
    }

    void onUpdate(float dt) override {
        if (m_owner->onUpdateCb) m_owner->onUpdateCb(dt);
    }

    void onPreRender(float dt) override {
        // Update skeletal animations (evaluate keyframes, upload bone SSBOs)
        if (m_animationSystem) {
            m_animationSystem->update(getDeltaTime(), getRegistry());
        }
        if (m_owner->onPreRenderCb) m_owner->onPreRenderCb(dt);
    }

    void onPostRender() override {
        if (m_owner->onPostRenderCb) m_owner->onPostRenderCb();
    }

    void onKeyPressed(int keyCode) override {
        if (m_owner->onKeyPressedCb) m_owner->onKeyPressedCb(keyCode);
    }

    void onResize(uint32_t width, uint32_t height) override {
        if (m_owner->onResizeCb) m_owner->onResizeCb(width, height);
    }

    void onCleanup() override {
        if (m_owner->onCleanupCb) m_owner->onCleanupCb();
    }

private:
    Impl* m_owner;

public:
    // Public forwarders — expose protected ApplicationBase methods for EngineAPI
    using ApplicationBase::createCamera;
    using ApplicationBase::loadGltfScene;
    using ApplicationBase::createDirectionalLight;
    using ApplicationBase::createPointLight;
    using ApplicationBase::createSprite;
    using ApplicationBase::createUIPanel;
    using ApplicationBase::getSprite2DManager;
    using ApplicationBase::getCameraEntity;
    using ApplicationBase::getDeltaTime;
    using ApplicationBase::getRenderGraph;
    using ApplicationBase::getScene;
    using ApplicationBase::getEventDispatcher;
    using ApplicationBase::getInputHandler;
    using ApplicationBase::getRegistry;

    ECS::PhysicsSystem* m_physicsSystem = nullptr;
    std::unique_ptr<SkeletalAnimationSystem> m_animationSystem;

    /// Create bone SSBOs for all entities with SkeletonComponent that lack one.
    void createBoneSSBOs() {
        if (!m_animationSystem) return;
        auto view = getRegistry().view<SkeletonComponent>();
        for (auto entity : view) {
            auto& skel = view.get<SkeletonComponent>(entity);
            // Only create if SSBO hasn't been allocated yet
            if (skel.boneSSBO.buffer == VK_NULL_HANDLE) {
                m_animationSystem->createBoneSSBO(skel);
            }
        }
    }
};

// ═══════════════════════════════════════════════════════════════
// Config Conversion: EngineConfig → ApplicationConfig
// ═══════════════════════════════════════════════════════════════

static ApplicationConfig toAppConfig(const EngineConfig& fc) {
    ApplicationConfig ac;
    ac.width  = fc.width;
    ac.height = fc.height;
    ac.title  = fc.title;
    ac.logFile = fc.logFile;

    // Map integer logLevel to enum
    switch (fc.logLevel) {
        case 0:  ac.logLevel = LogLevel::Debug;   break;
        case 1:  ac.logLevel = LogLevel::Info;    break;
        case 2:  ac.logLevel = LogLevel::Warning; break;
        case 3:  ac.logLevel = LogLevel::Error;   break;
        default: ac.logLevel = LogLevel::Info;    break;
    }

    ac.hdrEnvironmentPath = fc.hdrEnvironmentPath;
    ac.pipelineJsonPath   = fc.pipelineJsonPath;
    ac.maxFramesInFlight  = fc.maxFramesInFlight;

    // Convert vector<pair> → unordered_map
    for (const auto& [key, val] : fc.renderGraphParameters) {
        ac.renderGraphParameters[key] = val;
    }

    return ac;
}

// ═══════════════════════════════════════════════════════════════
// Options Conversion: GltfOptions → GltfLoadOptions
// ═══════════════════════════════════════════════════════════════

static GltfLoadOptions toLoadOptions(const GltfOptions& fo) {
    GltfLoadOptions lo;
    lo.loadTextures     = fo.loadTextures;
    lo.loadMaterials    = fo.loadMaterials;
    lo.createEntities   = fo.createEntities;
    lo.loadSkins        = fo.loadSkins;
    lo.loadAnimations   = fo.loadAnimations;
    lo.flattenHierarchy = fo.flattenHierarchy;
    lo.maxTextureSize   = fo.maxTextureSize;
    lo.generateMipmaps  = fo.generateMipmaps;
    lo.srgbAlbedo       = fo.srgbAlbedo;
    lo.namePrefix       = fo.namePrefix;
    return lo;
}

// ═══════════════════════════════════════════════════════════════
// Result Conversion: GltfLoadResult → GltfResult
// ═══════════════════════════════════════════════════════════════

static GltfResult toGltfResult(const GltfLoadResult& lr) {
    GltfResult fr;
    fr.success        = lr.success;
    fr.error          = lr.error;
    fr.totalVertices  = lr.totalVertices;
    fr.totalIndices   = lr.totalIndices;
    fr.totalTextures  = lr.totalTextures;
    fr.totalMaterials = lr.totalMaterials;

    fr.entities.reserve(lr.entities.size());
    for (auto e : lr.entities) {
        fr.entities.push_back(toHandle(e));
    }

    // Animation metadata
    fr.skeletonCount = lr.skeletons.size();
    fr.animationClips.reserve(lr.animationClips.size());
    for (const auto& clip : lr.animationClips) {
        fr.animationClips.push_back({clip->name, clip->duration});
    }

    return fr;
}

// ═══════════════════════════════════════════════════════════════
// Construction / Destruction
// ═══════════════════════════════════════════════════════════════

EngineAPI::EngineAPI(const EngineConfig& config)
    : m_impl(std::make_unique<Impl>())
{
    auto appConfig = toAppConfig(config);
    m_impl->app = std::make_unique<Impl::CallbackApp>(appConfig, m_impl.get());
}

EngineAPI::~EngineAPI() = default;

// ═══════════════════════════════════════════════════════════════
// Lifecycle
// ═══════════════════════════════════════════════════════════════

void EngineAPI::run() {
    m_impl->app->run();
}

// ═══════════════════════════════════════════════════════════════
// Callback Registration
// ═══════════════════════════════════════════════════════════════

void EngineAPI::setOnInit(VoidCallback cb)         { m_impl->onInitCb       = std::move(cb); }
void EngineAPI::setOnPostInit(VoidCallback cb)     { m_impl->onPostInitCb   = std::move(cb); }
void EngineAPI::setOnUpdate(UpdateCallback cb)     { m_impl->onUpdateCb     = std::move(cb); }
void EngineAPI::setOnPreRender(UpdateCallback cb)  { m_impl->onPreRenderCb  = std::move(cb); }
void EngineAPI::setOnPostRender(VoidCallback cb)   { m_impl->onPostRenderCb = std::move(cb); }
void EngineAPI::setOnKeyPressed(KeyCallback cb)    { m_impl->onKeyPressedCb = std::move(cb); }
void EngineAPI::setOnResize(ResizeCallback cb)     { m_impl->onResizeCb     = std::move(cb); }
void EngineAPI::setOnCleanup(VoidCallback cb)      { m_impl->onCleanupCb    = std::move(cb); }

// ═══════════════════════════════════════════════════════════════
// Sub-API Access
// ═══════════════════════════════════════════════════════════════

SceneAPI& EngineAPI::getScene() {
    return *m_impl->sceneAPI;
}

InputAPI& EngineAPI::getInput() {
    return *m_impl->inputAPI;
}

PhysicsAPI& EngineAPI::getPhysics() {
    return *m_impl->physicsAPI;
}

// ═══════════════════════════════════════════════════════════════
// Convenience Helpers
// ═══════════════════════════════════════════════════════════════

EntityHandle EngineAPI::createCamera(const glm::vec3& pos, float fov,
                                      float speed, float nearPlane, float farPlane) {
    auto e = m_impl->app->createCamera(pos, fov, speed, nearPlane, farPlane);
    return toHandle(e);
}

GltfResult EngineAPI::loadGltfScene(const std::string& path, const GltfOptions& opts) {
    auto lr = m_impl->app->loadGltfScene(path, toLoadOptions(opts));
    // Auto-create bone SSBOs for any newly loaded skinned entities
    m_impl->app->createBoneSSBOs();
    return toGltfResult(lr);
}

EntityHandle EngineAPI::createDirectionalLight(const glm::vec3& direction,
                                                const glm::vec3& color,
                                                float intensity) {
    auto e = m_impl->app->createDirectionalLight(direction, color, intensity);
    return toHandle(e);
}

EntityHandle EngineAPI::createPointLight(const glm::vec3& position,
                                          const glm::vec3& color,
                                          float intensity, float range) {
    auto e = m_impl->app->createPointLight(position, color, intensity, range);
    return toHandle(e);
}

EntityHandle EngineAPI::createSprite(const glm::vec3& worldPos,
                                      const std::string& texturePath,
                                      const glm::vec2& size,
                                      const glm::vec4& tint) {
    auto e = m_impl->app->createSprite(worldPos, texturePath, size, tint);
    return toHandle(e);
}

EntityHandle EngineAPI::createUIPanel(UIAnchor anchor,
                                       const glm::vec2& offsetPixels,
                                       const glm::vec2& sizePixels,
                                       const std::string& texturePath,
                                       const glm::vec4& color) {
    auto engineAnchor = static_cast<UIAnchorComponent::Anchor>(static_cast<uint8_t>(anchor));
    auto e = m_impl->app->createUIPanel(engineAnchor, offsetPixels, sizePixels, texturePath, color);
    return toHandle(e);
}

EntityHandle EngineAPI::getCameraEntity() const {
    return toHandle(m_impl->app->getCameraEntity());
}

float EngineAPI::getDeltaTime() const {
    return m_impl->app->getDeltaTime();
}

// ═══════════════════════════════════════════════════════════════
// Scene Context Custom Values
// ═══════════════════════════════════════════════════════════════

void EngineAPI::setCustomFloat(const std::string& key, float value) {
    m_impl->app->getRenderGraph().getSceneContext().setCustom(key, value);
}

void EngineAPI::setCustomVec2(const std::string& key, const glm::vec2& value) {
    m_impl->app->getRenderGraph().getSceneContext().setCustom(key, value);
}

void EngineAPI::setCustomVec3(const std::string& key, const glm::vec3& value) {
    m_impl->app->getRenderGraph().getSceneContext().setCustom(key, value);
}

void EngineAPI::setCustomVec4(const std::string& key, const glm::vec4& value) {
    m_impl->app->getRenderGraph().getSceneContext().setCustom(key, value);
}

void EngineAPI::setCustomUint(const std::string& key, uint32_t value) {
    m_impl->app->getRenderGraph().getSceneContext().setCustom(key, value);
}

} // namespace Facade
} // namespace Shoonyakasha
