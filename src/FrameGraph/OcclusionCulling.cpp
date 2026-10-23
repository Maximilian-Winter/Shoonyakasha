//
// OcclusionCulling.cpp - depth pyramid and box occlusion test
//

#include "FrameGraph/OcclusionCulling.h"

#include <algorithm>
#include <cmath>

namespace Shoonyakasha {

void DepthPyramid::build(const float* depths, uint32_t width, uint32_t height, const glm::mat4& viewProj) {
    m_levels.clear();
    m_sizes.clear();
    m_viewProj = viewProj;
    if (!depths || width == 0 || height == 0) return;

    m_levels.emplace_back(depths, depths + static_cast<size_t>(width) * height);
    m_sizes.emplace_back(width, height);
    while (m_sizes.back().x > 1 || m_sizes.back().y > 1) {
        const glm::uvec2 below = m_sizes.back();
        const glm::uvec2 size((below.x + 1) / 2, (below.y + 1) / 2);
        const std::vector<float>& src = m_levels.back();
        std::vector<float> dst(static_cast<size_t>(size.x) * size.y);
        for (uint32_t y = 0; y < size.y; ++y) {
            const uint32_t y0 = 2 * y, y1 = std::min(2 * y + 1, below.y - 1);
            for (uint32_t x = 0; x < size.x; ++x) {
                const uint32_t x0 = 2 * x, x1 = std::min(2 * x + 1, below.x - 1);
                dst[static_cast<size_t>(y) * size.x + x] =
                    std::max(std::max(src[static_cast<size_t>(y0) * below.x + x0], src[static_cast<size_t>(y0) * below.x + x1]),
                             std::max(src[static_cast<size_t>(y1) * below.x + x0], src[static_cast<size_t>(y1) * below.x + x1]));
            }
        }
        m_levels.push_back(std::move(dst));
        m_sizes.push_back(size);
    }
}

bool DepthPyramid::occluded(const glm::vec3& worldMin, const glm::vec3& worldMax) const {
    if (m_levels.empty()) return false;

    // The box's screen rectangle and nearest depth.
    glm::vec2 lo(1e30f), hi(-1e30f);
    float nearest = 1e30f;
    for (int i = 0; i < 8; ++i) {
        const glm::vec3 corner((i & 1) ? worldMax.x : worldMin.x, (i & 2) ? worldMax.y : worldMin.y,
                               (i & 4) ? worldMax.z : worldMin.z);
        const glm::vec4 clip = m_viewProj * glm::vec4(corner, 1.0f);
        if (clip.w <= 1e-5f) return false;                 // behind the camera
        const glm::vec3 ndc = glm::vec3(clip) / clip.w;
        lo = glm::min(lo, glm::vec2(ndc));
        hi = glm::max(hi, glm::vec2(ndc));
        nearest = std::min(nearest, ndc.z);
    }
    if (nearest <= 0.0f) return false;                     // crosses the near plane
    if (hi.x < -1.0f || hi.y < -1.0f || lo.x > 1.0f || lo.y > 1.0f) return false;

    const glm::vec2 base = glm::vec2(m_sizes[0]);
    const glm::vec2 uvLo = glm::clamp(lo * 0.5f + 0.5f, 0.0f, 1.0f) * base;
    const glm::vec2 uvHi = glm::clamp(hi * 0.5f + 0.5f, 0.0f, 1.0f) * base;

    // The level at which the rectangle spans a few texels.
    const float extent = std::max(uvHi.x - uvLo.x, uvHi.y - uvLo.y);
    uint32_t level = extent > 2.0f ? static_cast<uint32_t>(std::ceil(std::log2(extent / 2.0f))) : 0u;
    level = std::min(level, levelCount() - 1);

    const glm::uvec2 size = m_sizes[level];
    const float scale = 1.0f / static_cast<float>(1u << level);
    const int x0 = std::max(static_cast<int>(std::floor(uvLo.x * scale)) - 1, 0);
    const int y0 = std::max(static_cast<int>(std::floor(uvLo.y * scale)) - 1, 0);
    const int x1 = std::min(static_cast<int>(std::floor(uvHi.x * scale)) + 1, static_cast<int>(size.x) - 1);
    const int y1 = std::min(static_cast<int>(std::floor(uvHi.y * scale)) + 1, static_cast<int>(size.y) - 1);

    float farthest = 0.0f;
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            farthest = std::max(farthest, at(level, static_cast<uint32_t>(x), static_cast<uint32_t>(y)));
        }
    }
    return nearest > farthest;
}

} // namespace Shoonyakasha
