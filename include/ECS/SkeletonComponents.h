//
// SkeletonComponents.h - ECS Components for Skeletal Animation
//
// Defines:
//   - SkeletonComponent: Holds skeleton data + per-entity bone matrices + GPU SSBO
//   - AnimationPlaybackComponent: Controls animation playback state
//
// 骨之器 — The vessel of bones
//

#pragma once

#include "Resources/AnimationData.h"
#include "GPU/GPUTypes.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>
#include <memory>
#include <cstdint>

namespace Shoonyakasha {

// ============================================================================
// SkeletonComponent - Attached to every skinned mesh entity
// ============================================================================
//
// Multiple entities can share the same Skeleton (shared_ptr), but each gets
// its own boneMatrices and boneSSBO because they may be at different animation
// frames or playing different clips.
//

struct SkeletonComponent {
    // Shared skeleton definition (joint hierarchy + inverse bind matrices)
    std::shared_ptr<Skeleton> skeleton;

    // Final bone matrices, updated each frame by SkeletalAnimationSystem
    // boneMatrices[i] = globalTransform[i] * inverseBindMatrix[i]
    std::vector<glm::mat4> boneMatrices;

    // GPU storage buffer for uploading bone matrices to the vertex shader
    GPUBuffer boneSSBO;

    // Whether boneMatrices have been updated and need re-upload to GPU
    bool dirty = true;

    // Pre-allocate matrices for the skeleton
    void allocate() {
        if (skeleton) {
            boneMatrices.resize(skeleton->jointCount(), glm::mat4(1.0f));
        }
    }

    // Get the number of joints
    uint32_t jointCount() const {
        return skeleton ? skeleton->jointCount() : 0;
    }

    // SSBO size in bytes
    uint32_t ssboSize() const {
        return jointCount() * static_cast<uint32_t>(sizeof(glm::mat4));
    }
};

// ============================================================================
// AnimationPlaybackComponent - Controls animation playback
// ============================================================================
//
// Attached to entities that play skeletal animations.
// The SkeletalAnimationSystem reads this component, advances time,
// evaluates keyframes, and writes results into SkeletonComponent::boneMatrices.
//

struct AnimationPlaybackComponent {
    // Available animation clips (can be shared across instances)
    std::vector<std::shared_ptr<AnimationClip>> clips;

    // Playback state
    int currentClipIndex = -1;
    float currentTime = 0.0f;
    float speed = 1.0f;
    bool playing = false;
    bool loop = true;

    // Intermediate per-joint local transforms for the current frame
    // Sized to skeleton->jointCount(), populated by AnimationEvaluator
    std::vector<glm::vec3> jointTranslations;
    std::vector<glm::quat> jointRotations;
    std::vector<glm::vec3> jointScales;

    // Pre-allocate intermediate arrays
    void allocate(uint32_t jointCount) {
        jointTranslations.resize(jointCount, glm::vec3(0.0f));
        jointRotations.resize(jointCount, glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
        jointScales.resize(jointCount, glm::vec3(1.0f));
    }

    // Start playing a clip
    void play(int clipIndex) {
        if (clipIndex >= 0 && clipIndex < static_cast<int>(clips.size())) {
            currentClipIndex = clipIndex;
            currentTime = 0.0f;
            playing = true;
        }
    }

    // Stop playback
    void stop() {
        playing = false;
        currentTime = 0.0f;
    }

    // Get current clip (nullptr if not playing or invalid index)
    const AnimationClip* getCurrentClip() const {
        if (currentClipIndex >= 0 && currentClipIndex < static_cast<int>(clips.size())) {
            return clips[currentClipIndex].get();
        }
        return nullptr;
    }
};

} // namespace Shoonyakasha
