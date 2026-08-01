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
    ~SkeletalAnimationSystem();

    /// Free bone SSBOs automatically when a SkeletonComponent is destroyed.
    ///
    /// GPUBuffer is a plain handle with no destructor — its own comment says
    /// reset() "does NOT free GPU memory" — so every destroyed skinned entity
    /// leaked one VMA allocation for the lifetime of the allocator. Must be
    /// called once with the registry the skinned entities live in; the
    /// connection is released in the destructor.
    void attachTo(entt::registry& registry);

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
    entt::connection m_onSkeletonDestroy;   // released in ~SkeletalAnimationSystem

    /// on_destroy<SkeletonComponent> handler — frees the bone SSBO.
    void onSkeletonDestroyed(entt::registry& registry, entt::entity entity);

    /// Advance playback time and handle looping
    void updatePlayback(AnimationPlaybackComponent& playback, float deltaTime);

    /// Evaluate animation and compute bone matrices
    void evaluateAnimation(
        const AnimationPlaybackComponent& playback,
        SkeletonComponent& skeleton
    );
};

} // namespace Shoonyakasha
