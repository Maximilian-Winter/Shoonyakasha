#include "SkinnedMeshApp.h"

#include <GLFW/glfw3.h>
#include <iostream>
#include <algorithm>

#include "ECS/RenderComponents.h"
#include "ECS/SkeletonComponents.h"

// ═══════════════════════════════════════════════════════════════
// Construction
// ═══════════════════════════════════════════════════════════════

SkinnedMeshApp::SkinnedMeshApp(const ApplicationConfig& config)
    : ApplicationBase(config)
{
}

// ═══════════════════════════════════════════════════════════════
// ApplicationBase Hooks
// ═══════════════════════════════════════════════════════════════

void SkinnedMeshApp::onInit() {
    // Create animation system (needs device — available after Vulkan init)
    m_animationSystem = std::make_unique<Shoonyakasha::SkeletalAnimationSystem>(getDevice());

    // Camera — Fox is ~80 units tall, center at ~Y=40, on +Z axis
    createCamera(glm::vec3(0.0f, 40.0f, 200.0f), 60.f, 50.f, 1.0f, 2000.f);

    // Load the skinned model
    loadSkinnedModel();

    // Lights
    createDirectionalLight(
        glm::vec3(-0.5f, -1.0f, -0.3f),
        glm::vec3(1.0f, 0.975f, 0.95f),
        3.0f);
    createPointLight(
        glm::vec3(3.0f, 1.0f, 2.0f),
        glm::vec3(1.0f, 0.85f, 0.7f),
        5.0f, 20.0f);
}

void SkinnedMeshApp::onPreRender(float dt) {
    // Evaluate keyframes, compute bone matrices, upload SSBOs
    m_animationSystem->update(getDeltaTime(), getRegistry());
}

void SkinnedMeshApp::onKeyPressed(int keyCode) {
    auto& registry = getRegistry();

    switch (keyCode) {
        // Animation clip switching: 4, 5, 6
        // (Keys 1/2/3 are used by base class for camera mode switching)
        case GLFW_KEY_4:
            switchAnimation(0);
            break;
        case GLFW_KEY_5:
            switchAnimation(1);
            break;
        case GLFW_KEY_6:
            switchAnimation(2);
            break;

        // Pause/resume animation
        case GLFW_KEY_SPACE: {
            auto view = registry.view<Shoonyakasha::AnimationPlaybackComponent>();
            for (auto entity : view) {
                auto& playback = view.get<Shoonyakasha::AnimationPlaybackComponent>(entity);
                if (playback.playing) {
                    playback.playing = false;
                    std::cout << "  Animation PAUSED" << std::endl;
                } else {
                    playback.playing = true;
                    std::cout << "  Animation PLAYING" << std::endl;
                }
            }
            break;
        }

        // Speed control
        case GLFW_KEY_EQUAL:  // + key
        case GLFW_KEY_KP_ADD: {
            auto view = registry.view<Shoonyakasha::AnimationPlaybackComponent>();
            for (auto entity : view) {
                auto& playback = view.get<Shoonyakasha::AnimationPlaybackComponent>(entity);
                playback.speed = std::min(playback.speed * 1.5f, 10.0f);
                std::cout << "  Animation speed: " << playback.speed << "x" << std::endl;
            }
            break;
        }
        case GLFW_KEY_MINUS:
        case GLFW_KEY_KP_SUBTRACT: {
            auto view = registry.view<Shoonyakasha::AnimationPlaybackComponent>();
            for (auto entity : view) {
                auto& playback = view.get<Shoonyakasha::AnimationPlaybackComponent>(entity);
                playback.speed = std::max(playback.speed / 1.5f, 0.1f);
                std::cout << "  Animation speed: " << playback.speed << "x" << std::endl;
            }
            break;
        }
    }
}

// ═══════════════════════════════════════════════════════════════
// Load the Skinned Model
// 骨之器 — The vessel of bones
// ═══════════════════════════════════════════════════════════════

void SkinnedMeshApp::loadSkinnedModel() {
    GltfLoadOptions options;
    options.loadTextures = true;
    options.loadMaterials = true;
    options.createEntities = true;
    options.loadSkins = true;
    options.loadAnimations = true;
    options.namePrefix = "fox";

    auto result = loadGltfScene("models/Fox.glb", options);

    if (!result.success) {
        throw std::runtime_error("Failed to load skinned glTF: " + result.error);
    }

    getLogger().log(LogLevel::Info, "Loaded: %zu primitives, %zu vertices, %zu skeletons, %zu clips",
                    result.primitives.size(), result.totalVertices,
                    result.skeletons.size(), result.animationClips.size());

    // Report animation clips
    m_totalClips = static_cast<int>(result.animationClips.size());
    for (size_t i = 0; i < result.animationClips.size(); ++i) {
        const auto& clip = result.animationClips[i];
        getLogger().log(LogLevel::Info, "  Clip %zu: '%s' (%.2fs, %zu channels)",
                        i, clip->name.c_str(), clip->duration, clip->channels.size());
        std::cout << "  Animation " << i << ": " << clip->name
                  << " (" << clip->duration << "s)" << std::endl;
    }

    // Create bone SSBOs for all skinned entities
    auto& registry = getRegistry();
    auto view = registry.view<Shoonyakasha::SkeletonComponent>();
    for (auto entity : view) {
        auto& skelComp = view.get<Shoonyakasha::SkeletonComponent>(entity);
        m_animationSystem->createBoneSSBO(skelComp);
        getLogger().log(LogLevel::Info, "Created bone SSBO: %u joints, %u bytes",
                        skelComp.jointCount(), skelComp.ssboSize());
    }

    // Count entities by type
    size_t staticCount = 0, skinnedCount = 0;
    auto meshView = registry.view<Shoonyakasha::MeshComponent>();
    for (auto entity : meshView) {
        if (registry.all_of<Shoonyakasha::SkeletonComponent>(entity)) {
            skinnedCount++;
        } else {
            staticCount++;
        }
    }

    getLogger().log(LogLevel::Info, "Entities: %zu static, %zu skinned", staticCount, skinnedCount);
    std::cout << "  Entities: " << staticCount << " static, " << skinnedCount << " skinned" << std::endl;
}

void SkinnedMeshApp::switchAnimation(int clipIndex) {
    if (clipIndex >= m_totalClips) {
        std::cout << "  No clip at index " << clipIndex << " (total: " << m_totalClips << ")" << std::endl;
        return;
    }

    auto& registry = getRegistry();
    auto view = registry.view<Shoonyakasha::AnimationPlaybackComponent>();
    for (auto entity : view) {
        auto& playback = view.get<Shoonyakasha::AnimationPlaybackComponent>(entity);
        playback.play(clipIndex);
        m_currentClipIndex = clipIndex;

        const auto* clip = playback.getCurrentClip();
        if (clip) {
            std::cout << "  Playing: " << clip->name << " (" << clip->duration << "s)" << std::endl;
        }
    }
}
