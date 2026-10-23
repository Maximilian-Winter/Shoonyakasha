//
// SkeletalAnimationSystem.cpp - Skeletal animation ECS system
//

#include "ECS/SkeletalAnimationSystem.h"
#include "ECS/MorphComponents.h"
#include "Vulkan/VulkanDevice.h"
#include "GPU/GPUResourceFactory.h"

#include <entt/entt.hpp>
#include <cstring>
#include <cmath>

namespace Shoonyakasha {

// ============================================================================
// Constructor
// ============================================================================

SkeletalAnimationSystem::SkeletalAnimationSystem(VulkanDevice& device)
    : m_device(device)
{
}

// ============================================================================
// Per-Frame Update
// ============================================================================

void SkeletalAnimationSystem::update(float deltaTime, entt::registry& registry) {
    auto view = registry.view<AnimationPlaybackComponent, SkeletonComponent>();

    for (auto entity : view) {
        auto& playback = view.get<AnimationPlaybackComponent>(entity);
        auto& skeleton = view.get<SkeletonComponent>(entity);

        if (!skeleton.skeleton) continue;

        // Advance playback
        if (playback.playing) {
            updatePlayback(playback, deltaTime);
        }

        // Evaluate animation and compute bone matrices
        evaluateAnimation(playback, skeleton);

        // Upload to GPU if dirty
        if (skeleton.dirty && skeleton.boneSSBO && skeleton.boneSSBO->isValid()) {
            uploadBoneMatrices(skeleton);
        }
    }

    // Skeletons without clips hold their default pose, computed and uploaded
    // once.
    auto stills = registry.view<SkeletonComponent>(entt::exclude<AnimationPlaybackComponent>);
    std::vector<glm::vec3> translations, scales;
    std::vector<glm::quat> rotations;
    for (auto entity : stills) {
        auto& skeleton = stills.get<SkeletonComponent>(entity);
        if (!skeleton.skeleton || !skeleton.dirty) continue;
        const uint32_t joints = skeleton.skeleton->jointCount();
        translations.assign(joints, glm::vec3(0.0f));
        rotations.assign(joints, glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
        scales.assign(joints, glm::vec3(1.0f));
        AnimationEvaluator::setDefaultPose(*skeleton.skeleton, translations, rotations, scales);
        AnimationEvaluator::computeBoneMatrices(*skeleton.skeleton, translations, rotations, scales,
                                                skeleton.boneMatrices);
        if (skeleton.boneSSBO && skeleton.boneSSBO->isValid()) uploadBoneMatrices(skeleton);
    }

    // Morph target weights from the current clip's channels for the node
    // each entity was made from. Playback advances here for entities
    // without a skeleton.
    auto morphs = registry.view<AnimationPlaybackComponent, MorphComponent>();
    std::vector<float> weights;
    for (auto entity : morphs) {
        auto& playback = morphs.get<AnimationPlaybackComponent>(entity);
        auto& morph = morphs.get<MorphComponent>(entity);
        if (playback.playing && !registry.all_of<SkeletonComponent>(entity)) {
            updatePlayback(playback, deltaTime);
        }
        const AnimationClip* clip = playback.getCurrentClip();
        if (!clip) continue;
        for (const MorphWeightChannel& channel : clip->weightChannels) {
            if (channel.targetNode != morph.sourceNode) continue;
            AnimationEvaluator::sampleWeights(channel, playback.currentTime, weights);
            morph.setWeights(weights);
        }
    }
}

// ============================================================================
// Playback Control
// ============================================================================

void SkeletalAnimationSystem::updatePlayback(
    AnimationPlaybackComponent& playback,
    float deltaTime)
{
    const AnimationClip* clip = playback.getCurrentClip();
    if (!clip || clip->duration <= 0.0f) return;

    playback.currentTime += deltaTime * playback.speed;

    if (playback.loop) {
        playback.currentTime = std::fmod(playback.currentTime, clip->duration);
        if (playback.currentTime < 0.0f) {
            playback.currentTime += clip->duration;
        }
    } else {
        if (playback.currentTime >= clip->duration) {
            playback.currentTime = clip->duration;
            playback.playing = false;
        }
        if (playback.currentTime < 0.0f) {
            playback.currentTime = 0.0f;
            playback.playing = false;
        }
    }
}

// ============================================================================
// Animation Evaluation
// ============================================================================

void SkeletalAnimationSystem::evaluateAnimation(
    const AnimationPlaybackComponent& playback,
    SkeletonComponent& skeleton)
{
    if (!skeleton.skeleton) return;

    const Skeleton& skel = *skeleton.skeleton;
    uint32_t jointCount = skel.jointCount();

    // Ensure intermediate arrays are sized correctly
    // (const_cast is safe here because we're writing to mutable intermediate data)
    auto& mutablePlayback = const_cast<AnimationPlaybackComponent&>(playback);
    if (mutablePlayback.jointTranslations.size() != jointCount) {
        mutablePlayback.allocate(jointCount);
        // Initialize with default pose
        AnimationEvaluator::setDefaultPose(skel,
            mutablePlayback.jointTranslations,
            mutablePlayback.jointRotations,
            mutablePlayback.jointScales);
    }

    // Reset to default pose before sampling (handles joints without channels)
    AnimationEvaluator::setDefaultPose(skel,
        mutablePlayback.jointTranslations,
        mutablePlayback.jointRotations,
        mutablePlayback.jointScales);

    // Sample animation clip if playing
    const AnimationClip* clip = playback.getCurrentClip();
    if (clip) {
        AnimationEvaluator::sampleClip(
            *clip,
            playback.currentTime,
            skel,
            mutablePlayback.jointTranslations,
            mutablePlayback.jointRotations,
            mutablePlayback.jointScales
        );
    }

    // Compute final bone matrices
    AnimationEvaluator::computeBoneMatrices(
        skel,
        mutablePlayback.jointTranslations,
        mutablePlayback.jointRotations,
        mutablePlayback.jointScales,
        skeleton.boneMatrices
    );

    skeleton.dirty = true;
}

// ============================================================================
// SSBO Management
// ============================================================================

SkeletalAnimationSystem::~SkeletalAnimationSystem() = default;

void SkeletalAnimationSystem::attachTo(entt::registry&) {
    // No-op. See the header: boneSSBO owns itself now.
}

void SkeletalAnimationSystem::createBoneSSBO(SkeletonComponent& skeleton) {
    if (!skeleton.skeleton) return;

    uint32_t size = skeleton.ssboSize();
    if (size == 0) return;

    // Create a host-coherent storage buffer (maps directly to CPU memory)
    // Bone data changes every frame, so host-coherent is appropriate
    skeleton.boneSSBO = m_device.getDeleteQueue().adopt(
        GPUResourceFactory::createBuffer(
            m_device.getAllocator().getHandle(),
            size,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VMA_MEMORY_USAGE_CPU_TO_GPU  // Host-visible, device-accessible
        ));
}

void SkeletalAnimationSystem::uploadBoneMatrices(SkeletonComponent& skeleton) {
    if (!skeleton.boneSSBO || !skeleton.boneSSBO->isValid() || skeleton.boneMatrices.empty()) return;

    uint32_t size = skeleton.ssboSize();

    // Map the buffer and write bone matrices directly
    void* mapped = GPUResourceFactory::mapBuffer(
        m_device.getAllocator().getHandle(),
        *skeleton.boneSSBO
    );

    if (mapped) {
        // The previous upload's matrices follow this frame's; on the first
        // upload they are the same.
        if (skeleton.previousBoneMatrices.size() != skeleton.boneMatrices.size()) {
            skeleton.previousBoneMatrices = skeleton.boneMatrices;
        }
        const size_t half = size / 2;
        std::memcpy(mapped, skeleton.boneMatrices.data(), half);
        std::memcpy(static_cast<char*>(mapped) + half, skeleton.previousBoneMatrices.data(), half);
        skeleton.previousBoneMatrices = skeleton.boneMatrices;
        GPUResourceFactory::unmapBuffer(
            m_device.getAllocator().getHandle(),
            *skeleton.boneSSBO
        );
    }

    skeleton.dirty = false;
}

} // namespace Shoonyakasha
