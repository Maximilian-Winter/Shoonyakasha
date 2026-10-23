//
// OcclusionCulling.h - testing bounds against a depth pyramid on the CPU
//
// A DepthPyramid holds a depth image (each texel the farthest depth of the
// screen block it covers, depth 0 near and 1 far) and the view-projection it
// was rendered with, and a chain of coarser levels, each texel the farthest
// of the texels below it. occluded() says whether a world-space box lies
// entirely behind that depth.
//

#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace Shoonyakasha {

class DepthPyramid {
public:
    /// Take `width` x `height` depths, rows top to bottom, rendered with
    /// `viewProj` (clip space x and y in [-1, 1] with y down the image, z in
    /// [0, 1]), and build the coarser levels down to 1 x 1.
    void build(const float* depths, uint32_t width, uint32_t height, const glm::mat4& viewProj);

    bool empty() const { return m_levels.empty(); }
    const glm::mat4& viewProj() const { return m_viewProj; }
    uint32_t levelCount() const { return static_cast<uint32_t>(m_levels.size()); }
    glm::uvec2 levelSize(uint32_t level) const { return m_sizes[level]; }
    float at(uint32_t level, uint32_t x, uint32_t y) const {
        return m_levels[level][static_cast<size_t>(y) * m_sizes[level].x + x];
    }

    /// Whether the box from `worldMin` to `worldMax` is hidden: its nearest
    /// depth is behind the farthest depth over the screen rectangle it covers,
    /// widened by a texel of the level it is tested at. A box crossing the
    /// near plane, or one wholly outside the image, is not hidden.
    bool occluded(const glm::vec3& worldMin, const glm::vec3& worldMax) const;

private:
    std::vector<std::vector<float>> m_levels;
    std::vector<glm::uvec2> m_sizes;
    glm::mat4 m_viewProj{1.0f};
};

} // namespace Shoonyakasha
