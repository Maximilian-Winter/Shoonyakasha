//
// Facade/EngineAPI.cpp - PIMPL implementation with CallbackApp : ApplicationBase
//
// All Vulkan, EnTT, and engine internals are confined to this translation unit.
//

#include <algorithm>
#include <optional>
#include <tuple>
#include <utility>
#include <stdexcept>
#include <entt/entt.hpp>
#include "ECS/Core.h"
#include "ECS/Sprite2DComponents.h"

#include "Facade/EngineAPI.h"
#include "Core/AssetPaths.h"
#include "Resources/Sprite2DManager.h"
#include "Capture/VideoRecorder.h"
#include <filesystem>
#include "Facade/SceneAPI.h"
#include "Facade/EcsAPI.h"
#include "Facade/UIAPI.h"
#include "InputAPIImpl.h"
#include "PhysicsAPIImpl.h"
#include "FacadeInternal.h"

#include "App/ApplicationBase.h"
#include "Vulkan/VulkanSwapChain.h"
#include "ECS/Scene.h"
#include "ECS/PhysicsSystem.h"
#include "ECS/SkeletalAnimationSystem.h"
#include "ECS/SkeletonComponents.h"
#include "FrameGraph/DotPathResolver.h"
#include "Vulkan/FrameGraph/FrameGraphJson.h"
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
    std::unique_ptr<EcsAPI>     ecsAPI;
    std::unique_ptr<UIAPI>      uiAPI;

    // setRenderStatsEnabled before run(): (enabled, gpuTiming), applied in onInit
    std::optional<std::pair<bool, bool>> pendingStats;

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
        // Free bone SSBOs when skinned entities are destroyed; without this each
        // one leaks a VMA allocation for the lifetime of the allocator.
        m_animationSystem->attachTo(getRegistry());

        // Create sub-APIs now that engine internals are ready
        m_owner->sceneAPI = std::make_unique<SceneAPI>(getScene());
        m_owner->sceneAPI->wireSprite2DManager(&getSprite2DManager());
        m_owner->ecsAPI = std::make_unique<EcsAPI>(getScene());
        m_owner->uiAPI = std::make_unique<UIAPI>(getScene(), getUIContext(), &getSprite2DManager());

        // InputAPI and PhysicsAPI already exist — they are default-constructible
        // and were built in the EngineAPI constructor so callers can hold them
        // before run(). Only the wiring needs the engine to be up.
        InputAPI::Impl::wire(*m_owner->inputAPI,
                             &getInputHandler(),
                             &getEventDispatcher());

        PhysicsAPI::Impl::wire(*m_owner->physicsAPI,
                               m_physicsSystem,
                               &getRegistry());

        if (m_owner->pendingStats) {
            const auto [enabled, gpuTiming] = *std::exchange(m_owner->pendingStats, std::nullopt);
            getRenderGraph().setStatsEnabled(enabled, gpuTiming);
        }

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
    using ApplicationBase::getSwapChain;
    using ApplicationBase::captureScreenshot;
    using ApplicationBase::setEnvironment;
    using ApplicationBase::setEnvironmentColor;
    using ApplicationBase::bakeEnvironment;
    using ApplicationBase::startRecording;
    using ApplicationBase::stopRecording;
    using ApplicationBase::isRecording;
    using ApplicationBase::getRecordedFrameCount;
    using ApplicationBase::setRecordingPaused;
    using ApplicationBase::isRecordingPaused;
    using ApplicationBase::setRenderScale;
    using ApplicationBase::getRenderScale;
    using ApplicationBase::reloadPipeline;
    using ApplicationBase::loadGltfScene;
    using ApplicationBase::createMesh;
    using ApplicationBase::getGltfLoader;
    using ApplicationBase::setTextureUploadBudget;
    using ApplicationBase::createDirectionalLight;
    using ApplicationBase::createPointLight;
    using ApplicationBase::createSprite;
    using ApplicationBase::createUIPanel;
    using ApplicationBase::createText;
    using ApplicationBase::getSprite2DManager;
    using ApplicationBase::getFontLoader;
    using ApplicationBase::getCameraEntity;
    using ApplicationBase::getDeltaTime;
    using ApplicationBase::getRenderGraph;
    using ApplicationBase::getDevice;
    using ApplicationBase::hasDevice;
    using ApplicationBase::hasRenderGraph;
    using ApplicationBase::getScene;
    using ApplicationBase::getEventDispatcher;
    using ApplicationBase::getInputHandler;
    using ApplicationBase::getRegistry;
    using ApplicationBase::getUIContext;

    ECS::PhysicsSystem* m_physicsSystem = nullptr;
    std::unique_ptr<SkeletalAnimationSystem> m_animationSystem;

    /// Create bone SSBOs for all entities with SkeletonComponent that lack one.
    void createBoneSSBOs() {
        if (!m_animationSystem) return;
        auto view = getRegistry().view<SkeletonComponent>();
        for (auto entity : view) {
            auto& skel = view.get<SkeletonComponent>(entity);
            // Only create if SSBO hasn't been allocated yet
            if (!skel.boneSSBO) {
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
    ac.uniformEnvironmentColor = glm::vec3(fc.uniformEnvironmentColor[0], fc.uniformEnvironmentColor[1],
                                           fc.uniformEnvironmentColor[2]);
    ac.iblCache = fc.iblCache;
    ac.pipelineJsonPath   = fc.pipelineJsonPath;
    ac.maxFramesInFlight  = fc.maxFramesInFlight;
    ac.enableValidation   = fc.enableValidation;

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
    lo.loadLights       = fo.loadLights;
    lo.streamTextures   = fo.streamTextures;
    lo.flattenHierarchy = fo.flattenHierarchy;
    lo.maxTextureSize   = fo.maxTextureSize;
    lo.generateMipmaps  = fo.generateMipmaps;
    lo.srgbAlbedo       = fo.srgbAlbedo;
    lo.compressTextures = fo.compressTextures;
    lo.generateLods     = fo.generateLods;
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
    for (auto e : lr.lights) {
        fr.lights.push_back(toHandle(e));
    }
    fr.variants = lr.variants;

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

    // Built here rather than in onInit: both are default-constructible and only
    // their wiring needs a live engine, so getInput() and getPhysics() are usable
    // from the moment the EngineAPI exists. Registering input callbacks before
    // run() is the natural thing to do, and it used to dereference null.
    m_impl->inputAPI   = std::make_unique<InputAPI>();
    m_impl->physicsAPI = std::make_unique<PhysicsAPI>();
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
void EngineAPI::requestQuit() { if (m_impl->app) m_impl->app->requestQuit(); }
glm::vec2 EngineAPI::getFramebufferSize() const {
    if (!m_impl->sceneAPI) return glm::vec2(0);
    const auto extent = m_impl->app->getSwapChain().getSwapChainExtent();
    return {static_cast<float>(extent.width), static_cast<float>(extent.height)};
}
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
    // Needs a live ECS::Scene, which only exists once run() has started. This
    // used to dereference a null unique_ptr, and the C++ API docs told readers
    // it was valid immediately after construction.
    if (!m_impl->sceneAPI) {
        throw std::logic_error(
            "EngineAPI::getScene() is only available after run() has started. "
            "Use setOnInit() to run scene setup at the right moment.");
    }
    return *m_impl->sceneAPI;
}

InputAPI& EngineAPI::getInput() {
    return *m_impl->inputAPI;
}

PhysicsAPI& EngineAPI::getPhysics() {
    return *m_impl->physicsAPI;
}

EcsAPI& EngineAPI::getEcs() {
    if (!m_impl->ecsAPI) {
        throw std::logic_error(
            "EngineAPI::getEcs() is only available after run() has started. "
            "Use setOnInit() to run ECS setup at the right moment.");
    }
    return *m_impl->ecsAPI;
}

UIAPI& EngineAPI::getUI() {
    if (!m_impl->uiAPI) {
        throw std::logic_error(
            "EngineAPI::getUI() is only available after run() has started. "
            "Use setOnInit() to build the UI at the right moment.");
    }
    return *m_impl->uiAPI;
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

std::pair<size_t, size_t> EngineAPI::getTextureStreamingProgress() const {
    return m_impl->app->getGltfLoader().textureStreamingProgress();
}

void EngineAPI::setTextureUploadBudget(size_t bytes) {
    m_impl->app->setTextureUploadBudget(bytes);
}

void EngineAPI::setTextureMemoryBudget(uint64_t bytes) {
    m_impl->app->getGltfLoader().setTextureMemoryBudget(bytes);
}

TextureResidencyStats EngineAPI::getTextureResidency() const {
    const auto report = m_impl->app->getGltfLoader().textureResidency();
    TextureResidencyStats stats;
    stats.textures = report.textures;
    stats.residentBytes = report.residentBytes;
    stats.neededBytes = report.neededBytes;
    stats.budget = report.budget;
    stats.waiting = report.waiting;
    stats.heldBack = report.heldBack;
    return stats;
}

namespace {

/// Flat float lists as MeshArrays; an error when one is not whole vectors.
std::string meshArrays(const std::vector<float>& positions, const std::vector<uint32_t>& indices,
                       const std::vector<float>& normals, const std::vector<float>& uvs,
                       const std::vector<float>& colors, const std::vector<float>& tangents, MeshArrays& out) {
    for (const auto& [values, width, what] : {std::tuple{&positions, 3u, "positions"}, {&normals, 3u, "normals"},
                                              {&uvs, 2u, "uvs"}, {&colors, 3u, "colors"},
                                              {&tangents, 4u, "tangents"}}) {
        if (values->size() % width != 0) {
            return std::string(what) + " must hold " + std::to_string(width) + " numbers for each vertex";
        }
    }
    auto vec3s = [](const std::vector<float>& v) {
        std::vector<glm::vec3> out(v.size() / 3);
        for (size_t i = 0; i < out.size(); ++i) out[i] = {v[3 * i], v[3 * i + 1], v[3 * i + 2]};
        return out;
    };
    out.positions = vec3s(positions);
    out.normals = vec3s(normals);
    out.colors = vec3s(colors);
    out.uvs.resize(uvs.size() / 2);
    for (size_t i = 0; i < out.uvs.size(); ++i) out.uvs[i] = {uvs[2 * i], uvs[2 * i + 1]};
    out.tangents.resize(tangents.size() / 4);
    for (size_t i = 0; i < out.tangents.size(); ++i) {
        out.tangents[i] = {tangents[4 * i], tangents[4 * i + 1], tangents[4 * i + 2], tangents[4 * i + 3]};
    }
    out.indices = indices;
    return {};
}

} // namespace

EntityHandle EngineAPI::createMesh(const std::vector<float>& positions, const std::vector<uint32_t>& indices,
                                   const std::vector<float>& normals, const std::vector<float>& uvs,
                                   const std::vector<float>& colors, const std::vector<float>& tangents,
                                   const std::string& name, std::string& error) {
    MeshArrays arrays;
    error = meshArrays(positions, indices, normals, uvs, colors, tangents, arrays);
    if (!error.empty()) return NullEntity;
    const entt::entity e = m_impl->app->createMesh(name, arrays, error);
    return e == entt::null ? NullEntity : toHandle(e);
}

std::string EngineAPI::setMesh(EntityHandle entity, const std::vector<float>& positions,
                               const std::vector<uint32_t>& indices, const std::vector<float>& normals,
                               const std::vector<float>& uvs, const std::vector<float>& colors,
                               const std::vector<float>& tangents) {
    MeshArrays arrays;
    const std::string error = meshArrays(positions, indices, normals, uvs, colors, tangents, arrays);
    if (!error.empty()) return error;
    return m_impl->app->getGltfLoader().setMeshGeometry(m_impl->app->getRegistry(), toEntt(entity), arrays);
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

std::vector<EntityHandle> EngineAPI::createPointLights(const std::vector<float>& positions,
                                                      const std::vector<float>& colors,
                                                      const std::vector<float>& intensities,
                                                      const std::vector<float>& ranges) {
    if (positions.size() % 3 != 0) {
        throw std::invalid_argument("createPointLights: positions must hold x, y, z for each light");
    }
    const size_t count = positions.size() / 3;
    auto perLight = [count](const std::vector<float>& values, size_t width, const char* what) {
        if (values.size() != width && values.size() != width * count) {
            throw std::invalid_argument(std::string("createPointLights: ") + what + " must hold " +
                                        std::to_string(width) + " values for all lights or " +
                                        std::to_string(width) + " for each of " + std::to_string(count));
        }
        return values.size() != width;
    };
    const bool eachColor = perLight(colors, 3, "colors");
    const bool eachIntensity = perLight(intensities, 1, "intensities");
    const bool eachRange = perLight(ranges, 1, "ranges");

    std::vector<EntityHandle> out;
    out.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        const size_t c = eachColor ? 3 * i : 0;
        auto e = m_impl->app->createPointLight(
            glm::vec3(positions[3 * i], positions[3 * i + 1], positions[3 * i + 2]),
            glm::vec3(colors[c], colors[c + 1], colors[c + 2]),
            intensities[eachIntensity ? i : 0], ranges[eachRange ? i : 0]);
        out.push_back(toHandle(e));
    }
    return out;
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

EntityHandle EngineAPI::createText(const std::string& text,
                                    UIAnchor anchor,
                                    const glm::vec2& offsetPixels,
                                    const std::string& fontPath,
                                    float fontSize,
                                    const glm::vec4& color) {
    auto engineAnchor = static_cast<UIAnchorComponent::Anchor>(static_cast<uint8_t>(anchor));
    auto e = m_impl->app->createText(text, engineAnchor, offsetPixels, fontPath, fontSize, color);
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

void EngineAPI::setCustomMat4(const std::string& key, const glm::mat4& value) {
    m_impl->app->getRenderGraph().getSceneContext().setCustom(key, value);
}

void EngineAPI::setCustomUint(const std::string& key, uint32_t value) {
    m_impl->app->getRenderGraph().getSceneContext().setCustom(key, value);
}

void EngineAPI::setCustomFloatArray(const std::string& key, const std::vector<float>& values) {
    m_impl->app->getRenderGraph().getSceneContext().setCustomArray(key, values);
}

void EngineAPI::setCustomVec2Array(const std::string& key, const std::vector<glm::vec2>& values) {
    m_impl->app->getRenderGraph().getSceneContext().setCustomArray(key, values);
}

void EngineAPI::setCustomVec3Array(const std::string& key, const std::vector<glm::vec3>& values) {
    m_impl->app->getRenderGraph().getSceneContext().setCustomArray(key, values);
}

void EngineAPI::setCustomVec4Array(const std::string& key, const std::vector<glm::vec4>& values) {
    m_impl->app->getRenderGraph().getSceneContext().setCustomArray(key, values);
}

void EngineAPI::setCustomMat4Array(const std::string& key, const std::vector<glm::mat4>& values) {
    m_impl->app->getRenderGraph().getSceneContext().setCustomArray(key, values);
}

void EngineAPI::setCustomUintArray(const std::string& key, const std::vector<uint32_t>& values) {
    m_impl->app->getRenderGraph().getSceneContext().setCustomArray(key, values);
}

std::string EngineAPI::setBuffer(const std::string& name, const void* data, size_t size, size_t offset) {
    if (!m_impl->app) return "the engine is not running";
    return m_impl->app->getRenderGraph().setBufferData(name, data, size, offset);
}

bool EngineAPI::requestReadback(const std::string& name) {
    return m_impl->app && m_impl->app->getRenderGraph().requestReadback(name);
}

bool EngineAPI::getReadback(const std::string& name, Readback& out) const {
    if (!m_impl->app) return false;
    const auto* result = m_impl->app->getRenderGraph().latestReadback(name);
    if (!result || !result->data) return false;
    out = Readback{};
    out.data = result->data;
    out.size = static_cast<size_t>(result->size);
    out.frame = result->frameNumber;
    out.image = result->isImage;
    if (result->isImage) {
        out.width = result->imageWidth;
        out.height = result->imageHeight;
        out.format = FrameGraph::JsonUtils::formatToString(result->imageFormat);
    } else {
        out.elementCount = result->elementCount;
        out.elementStride = result->elementStride;
    }
    return true;
}

void EngineAPI::setLocalShadowSettings(uint32_t spotCount, uint32_t pointCount,
                                       uint32_t spotResolution, uint32_t pointResolution,
                                       uint32_t atlasResolution) {
    auto& settings = m_impl->app->getRenderGraph().getSceneContext().localShadow.settings;
    settings.spotCount = std::min(spotCount, MAX_SPOT_SHADOWS);
    settings.pointCount = std::min(pointCount, MAX_POINT_SHADOWS);
    settings.spotResolution = spotResolution;
    settings.pointResolution = pointResolution;
    settings.atlasResolution = atlasResolution;
}

void EngineAPI::setSunShadowSettings(uint32_t cascadeCount, float maxDistance,
                                     float splitLambda, uint32_t resolution,
                                     float casterExtension, uint32_t sun) {
    auto& suns = m_impl->app->getRenderGraph().getSceneContext().suns;
    if (sun >= suns.size()) return;
    auto& settings = suns[sun].settings;
    settings.cascadeCount = cascadeCount;
    settings.maxDistance = maxDistance;
    settings.splitLambda = splitLambda;
    settings.resolution = resolution;
    settings.casterExtension = casterExtension;
}

glm::mat4 EngineAPI::getSunShadowCascade(uint32_t index, uint32_t sun) const {
    const auto& suns = m_impl->app->getRenderGraph().getSceneContext().suns;
    if (sun >= suns.size()) return glm::mat4(1.0f);
    const auto& cascades = suns[sun].cascades;
    if (!cascades.valid || index >= cascades.count) return glm::mat4(1.0f);
    return cascades.viewProj[index];
}

bool EngineAPI::setPassEnabled(const std::string& passName, bool enabled) {
    return m_impl->app->getRenderGraph().setPassEnabled(passName, enabled);
}

bool EngineAPI::isPassEnabled(const std::string& passName) const {
    return m_impl->app->getRenderGraph().isPassEnabled(passName);
}

bool EngineAPI::applyPipelinePreset(const std::string& name) {
    return m_impl->app->getRenderGraph().applyPreset(name);
}

bool EngineAPI::setPipelineImage(const std::string& name, const std::string& path) {
    if (!m_impl->app) return false;
    // The sprite manager's loader keeps the texture, cached by path, for as
    // long as the engine runs, so the graph can sample it however often it
    // is swapped.
    const GPUTexture texture = m_impl->app->getSprite2DManager().loadTexture(AssetPaths::locate(path).string());
    if (texture.view == VK_NULL_HANDLE) return false;
    m_impl->app->getRenderGraph().setExternalImage(name, texture.view, texture.sampler);
    return true;
}

std::string EngineAPI::setPipelineImageData(const std::string& name, const void* data, size_t size,
                                            uint32_t width, uint32_t height, const std::string& format) {
    if (!m_impl->app || !m_impl->app->hasDevice()) return "the engine is not running";
    VkFormat vkFormat = VK_FORMAT_UNDEFINED;
    try {
        vkFormat = FrameGraph::JsonUtils::stringToFormat(format);
    } catch (const std::exception&) {
        return "unknown format '" + format + "'";
    }
    return m_impl->app->getRenderGraph().setExternalImageData(name, data, size, width, height, vkFormat);
}

bool EngineAPI::rayQuerySupported() const {
    return m_impl->app && m_impl->app->hasDevice() && m_impl->app->getDevice().hasRayQuery();
}

std::vector<std::string> EngineAPI::getPipelinePresets() const {
    return m_impl->app->getRenderGraph().getPresetNames();
}

uint32_t EngineAPI::getPassDrawnCount(const std::string& passName) const {
    uint32_t drawn = 0, culled = 0;
    m_impl->app->getRenderGraph().getPassDrawStats(passName, drawn, culled);
    return drawn;
}

uint32_t EngineAPI::getPassCulledCount(const std::string& passName) const {
    uint32_t drawn = 0, culled = 0;
    m_impl->app->getRenderGraph().getPassDrawStats(passName, drawn, culled);
    return culled;
}

uint32_t EngineAPI::getPassOccludedCount(const std::string& passName) const {
    uint32_t drawn = 0, culled = 0, occluded = 0;
    m_impl->app->getRenderGraph().getPassDrawStats(passName, drawn, culled, &occluded);
    return occluded;
}

void EngineAPI::setOcclusionCulling(bool enabled) {
    m_impl->app->getRenderGraph().setOcclusionCullingEnabled(enabled);
}

bool EngineAPI::isOcclusionCullingEnabled() const {
    return m_impl->app->getRenderGraph().isOcclusionCullingEnabled();
}

void EngineAPI::setLodThreshold(float pixels) {
    m_impl->app->getRenderGraph().setLodThreshold(pixels);
}

float EngineAPI::getLodThreshold() const {
    return m_impl->app->getRenderGraph().getLodThreshold();
}

std::vector<uint32_t> EngineAPI::getLodCounts() const {
    return m_impl->app->getRenderGraph().getLodCounts();
}

void EngineAPI::setRenderStatsEnabled(bool enabled, bool gpuTiming) {
    if (m_impl->app && m_impl->app->hasRenderGraph()) {
        m_impl->app->getRenderGraph().setStatsEnabled(enabled, gpuTiming);
    } else {
        m_impl->pendingStats = std::make_pair(enabled, gpuTiming);
    }
}

bool EngineAPI::isRenderStatsEnabled() const {
    if (m_impl->app && m_impl->app->hasRenderGraph()) {
        return m_impl->app->getRenderGraph().isStatsEnabled();
    }
    return m_impl->pendingStats && m_impl->pendingStats->first;
}

RenderStatsSnapshot EngineAPI::getRenderStats() const {
    RenderStatsSnapshot out;
    if (!m_impl->app || !m_impl->app->hasRenderGraph()) return out;
    const FrameGraph::RenderStats* stats = m_impl->app->getRenderGraph().getStats();
    if (!stats) return out;

    const auto& avg = stats->average;
    out.enabled = true;
    out.fps = stats->fps;
    out.frameTimeMs = stats->frameTimeMs;
    out.frameTimeMaxMs = stats->frameTimeMaxMs;
    out.cpuRecordMs = avg.cpuRecordMs;
    out.gpuMs = avg.gpuMs;
    out.gpuValid = avg.gpuValid;
    out.drawCalls = avg.draws.drawCalls;
    out.dispatches = avg.draws.dispatches;
    out.vertices = avg.draws.vertices;
    out.validationLayers = stats->validationLayers;
    out.passes.reserve(avg.passes.size());
    for (const auto& pass : avg.passes) {
        RenderPassStats p;
        p.name = pass.name;
        p.cpuMs = pass.cpuMs;
        p.gpuMs = pass.gpuMs;
        p.gpuValid = pass.gpuValid;
        p.drawCalls = pass.draws.drawCalls;
        p.dispatches = pass.draws.dispatches;
        p.vertices = pass.draws.vertices;
        out.passes.push_back(std::move(p));
    }
    out.summary = FrameGraph::formatRenderStats(*stats);
    return out;
}


// ═══════════════════════════════════════════════════════════════
// Assets
// ═══════════════════════════════════════════════════════════════

bool videoRecordingAvailable() {
    return VideoRecorder::available();
}

std::string findFfmpeg() {
    return VideoRecorder::findFfmpeg();
}

std::string checkPipelineJson(const std::string& text) {
    try {
        FrameGraph::FrameGraphBuilder builder;
        FrameGraph::loadGraphFromJson(builder, nlohmann::json::parse(text));
        return {};
    } catch (const std::exception& e) {
        return e.what();
    }
}

bool EngineAPI::captureScreenshot(const std::string& path) {
    return m_impl->app->captureScreenshot(path);
}

bool EngineAPI::setEnvironment(const std::string& path) {
    return m_impl->app->setEnvironment(path);
}

bool EngineAPI::setEnvironmentColor(const glm::vec3& color) {
    return m_impl->app->setEnvironmentColor(color);
}

bool EngineAPI::bakeEnvironment(const std::string& hdrPath, const std::string& outputPath) {
    return m_impl->app->bakeEnvironment(hdrPath, outputPath);
}

bool EngineAPI::startRecording(const std::string& path, const RecordingOptions& options) {
    VideoRecorder::Options engineOptions;
    engineOptions.fps = options.fps;
    engineOptions.quality = options.quality;
    engineOptions.codec = options.codec;
    engineOptions.ffmpegPath = options.ffmpegPath;
    engineOptions.fixedStep = options.fixedStep;
    return m_impl->app->startRecording(path, engineOptions);
}

bool EngineAPI::stopRecording() {
    return m_impl->app->stopRecording();
}

bool EngineAPI::isRecording() const {
    return m_impl->app->isRecording();
}

uint64_t EngineAPI::getRecordedFrameCount() const {
    return m_impl->app->getRecordedFrameCount();
}

void EngineAPI::setRecordingPaused(bool paused) {
    m_impl->app->setRecordingPaused(paused);
}

bool EngineAPI::isRecordingPaused() const {
    return m_impl->app->isRecordingPaused();
}

void EngineAPI::setRenderScale(float scale) {
    m_impl->app->setRenderScale(scale);
}

float EngineAPI::getRenderScale() const {
    return m_impl->app->getRenderScale();
}

std::string EngineAPI::reloadPipeline() {
    return m_impl->app->reloadPipeline();
}

bool assetExists(const std::string& relativePath) {
    return AssetPaths::exists(relativePath);
}

std::string resolveAsset(const std::string& relativePath) {
    return AssetPaths::locate(relativePath).string();
}

std::string describeAssetRoot() {
    return AssetPaths::describe();
}

} // namespace Facade
} // namespace Shoonyakasha
