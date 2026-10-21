//
// SceneTextureTable.h
//
// The scene's textures by slot, for pipelines' arrays of them (a
// combined_image_sampler binding with "source": "sceneTextures"). Built every
// frame from the materials of the meshes that render: each texture that
// exists gets a slot, one per distinct image view, in order of first use, so
// slots stay put while the scene does. Ray-instance rows and the
// entity.material.textures.<name>.slot dot-path both read their slots here.
//

#pragma once

#include <entt/entt.hpp>
#include <vulkan/vulkan.h>

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace Shoonyakasha {

struct GPUTexture;

class SceneTextureTable {
public:
    static constexpr uint32_t kNoTexture = 0xFFFFFFFFu;

    /// A texture at the slot that is its index in slots().
    struct Slot {
        VkImageView view = VK_NULL_HANDLE;
        VkSampler sampler = VK_NULL_HANDLE;
        bool operator==(const Slot& o) const { return view == o.view && sampler == o.sampler; }
    };

    /// Give slots to the textures of every entity with a valid mesh, a
    /// material and a renderable tag that renders, at most `capacity` of them.
    void build(entt::registry& registry, uint32_t capacity);

    /// The slot of `texture`; kNoTexture for none, one that does not exist,
    /// or one that got no slot.
    uint32_t slotOf(const GPUTexture* texture) const;

    const std::vector<Slot>& slots() const { return m_slots; }

private:
    void add(const GPUTexture& texture, uint32_t capacity);

    std::vector<Slot> m_slots;
    std::unordered_map<VkImageView, uint32_t> m_slotOfView;
};

} // namespace Shoonyakasha
