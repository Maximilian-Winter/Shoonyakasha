//
// SkeletalAnimationSystem.h - ECS system for skeletal animation
//
// Runs each frame to:
//   1. Advance animation playback time
//   2. Evaluate keyframes via AnimationEvaluator
//   3. Compute final bone matrices
//   4. Upload bone matrices to per-entity SSBOs
//
// Priority: 35 (after physics at 30, before rendering)
//
// 動之系統 — The system of motion
//

#pragma once

#include "ECS/SkeletonComponents.h"
#include "Animation/AnimationEvaluator.h"
#include "GPU/GPUTypes.h"

#include <entt/entt.hpp>
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <cstdint>

// Forward declarations
namespace Shoonyakasha {
class VulkanDevice;
}

namespace Shoonyakasha {

class SkeletalAnimationSystem {
public:
    explicit SkeletalAnimationSystem(VulkanDevice& device);
    ~SkeletalAnimationSystem() = default;

    // ─── Per-Frame Update ───────────────────────────────────────

    /// Update all animated entities.
    /// Call once per frame before rendering.
    /// @param deltaTime Frame delta time in seconds
    /// @param registry ECS registry containing skinned entities
    void update(float deltaTime, entt::registry& registry);

    // ─── SSBO Management ────────────────────────────────────────

    /// Create the bone SSBO for an entity's skeleton component.
    /// Call once at entity creation time (e.g., after loading a skinned glTF).
    void createBoneSSBO(SkeletonComponent& skeleton);

    /// Upload bone matrices to the GPU SSBO.
    /// Called automatically by update() when dirty flag is set.
    void uploadBoneMatrices(SkeletonComponent& skeleton);

    /// Get the system priority (for ECS system ordering)
    int getPriority() const { return 35; }

private:
    VulkanDevice& m_device;

    /// Advance playback time and handle looping
    void updatePlayback(AnimationPlaybackComponent& playback, float deltaTime);

    /// Evaluate animation and compute bone matrices
    void evaluateAnimation(
        const AnimationPlaybackComponent& playback,
        SkeletonComponent& skeleton
    );
};

} // namespace Shoonyakasha
