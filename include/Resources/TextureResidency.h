//
// TextureResidency.h
//
// The arithmetic of texture residency for streamed glTF textures: how much
// uv a mesh spreads over its surface, which mip a texture needs at a given
// on-screen size, what a mip chain costs in memory, and which mips fit a
// memory budget.
//
#pragma once

#include <glm/glm.hpp>
#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>

namespace Shoonyakasha::TextureResidency {

/// Uv units per unit of surface: the square root of the triangles' total
/// uv area over their total area (`indices` empty: every three vertices are
/// a triangle). 0 without uvs or without area.
float uvPerUnit(const std::vector<glm::vec3>& positions, const std::vector<glm::vec2>& uvs,
                const std::vector<uint32_t>& indices);

/// Mip levels of a full chain for `width` x `height`, down to 1 x 1.
uint32_t mipCount(uint32_t width, uint32_t height);

/// The coarsest mip of a `width` x `height` texture that still has at least
/// one texel per pixel where it covers `pixelsPerUv` pixels per uv unit,
/// along its larger side; the last mip when `pixelsPerUv` is 0 or less.
uint32_t neededMip(uint32_t width, uint32_t height, float pixelsPerUv);

/// Bytes of the mips from `firstMip` to the last of a `width` x `height`
/// texture of `format`: RGBA8, or BC1 to BC7 by 4 x 4 blocks.
uint64_t chainBytes(VkFormat format, uint32_t width, uint32_t height, uint32_t firstMip);

/// A texture for fitBudget.
struct Candidate {
    uint32_t width = 0;
    uint32_t height = 0;
    VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
    uint32_t needed = 0;      // the mip it needs
    uint32_t coarsest = 0;    // the coarsest mip it may be held at
};

/// The first mip each candidate is held at so that their chains together
/// take at most `budget` bytes, or as close as the coarsest mips allow:
/// each starts at the mip it needs (at most its coarsest), and while over
/// the budget the one that has given up the fewest mips beyond its need
/// gives up one more (the larger chain first between equals). A budget of 0
/// is no budget.
std::vector<uint32_t> fitBudget(const std::vector<Candidate>& candidates, uint64_t budget);

} // namespace Shoonyakasha::TextureResidency
