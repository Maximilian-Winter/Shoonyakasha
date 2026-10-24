#include "Resources/TextureResidency.h"

#include <algorithm>
#include <cmath>
#include <queue>

namespace Shoonyakasha::TextureResidency {

float uvPerUnit(const std::vector<glm::vec3>& positions, const std::vector<glm::vec2>& uvs,
                const std::vector<uint32_t>& indices) {
    if (uvs.size() != positions.size() || positions.empty()) return 0.0f;
    const size_t corners = indices.empty() ? positions.size() : indices.size();
    double area = 0.0, uvArea = 0.0;
    for (size_t i = 0; i + 2 < corners; i += 3) {
        const uint32_t a = indices.empty() ? static_cast<uint32_t>(i) : indices[i];
        const uint32_t b = indices.empty() ? static_cast<uint32_t>(i + 1) : indices[i + 1];
        const uint32_t c = indices.empty() ? static_cast<uint32_t>(i + 2) : indices[i + 2];
        if (a >= positions.size() || b >= positions.size() || c >= positions.size()) continue;
        area += 0.5 * glm::length(glm::cross(positions[b] - positions[a], positions[c] - positions[a]));
        const glm::vec2 e1 = uvs[b] - uvs[a], e2 = uvs[c] - uvs[a];
        uvArea += 0.5 * std::abs(static_cast<double>(e1.x) * e2.y - static_cast<double>(e1.y) * e2.x);
    }
    if (area <= 0.0 || uvArea <= 0.0) return 0.0f;
    return static_cast<float>(std::sqrt(uvArea / area));
}

uint32_t mipCount(uint32_t width, uint32_t height) {
    uint32_t count = 1;
    for (uint32_t size = std::max(width, height); size > 1; size >>= 1) ++count;
    return count;
}

uint32_t neededMip(uint32_t width, uint32_t height, float pixelsPerUv) {
    const uint32_t last = mipCount(width, height) - 1;
    if (!(pixelsPerUv > 0.0f)) return last;
    const float texelsPerPixel = static_cast<float>(std::max(width, height)) / pixelsPerUv;
    if (texelsPerPixel <= 1.0f) return 0;
    return std::min(static_cast<uint32_t>(std::floor(std::log2(texelsPerPixel))), last);
}

uint64_t chainBytes(VkFormat format, uint32_t width, uint32_t height, uint32_t firstMip) {
    uint64_t blockBytes = 0;   // 0: 4 bytes a texel, no blocks
    switch (format) {
        case VK_FORMAT_BC1_RGB_UNORM_BLOCK: case VK_FORMAT_BC1_RGB_SRGB_BLOCK:
        case VK_FORMAT_BC1_RGBA_UNORM_BLOCK: case VK_FORMAT_BC1_RGBA_SRGB_BLOCK:
        case VK_FORMAT_BC4_UNORM_BLOCK: case VK_FORMAT_BC4_SNORM_BLOCK:
            blockBytes = 8;
            break;
        case VK_FORMAT_BC2_UNORM_BLOCK: case VK_FORMAT_BC2_SRGB_BLOCK:
        case VK_FORMAT_BC3_UNORM_BLOCK: case VK_FORMAT_BC3_SRGB_BLOCK:
        case VK_FORMAT_BC5_UNORM_BLOCK: case VK_FORMAT_BC5_SNORM_BLOCK:
        case VK_FORMAT_BC6H_UFLOAT_BLOCK: case VK_FORMAT_BC6H_SFLOAT_BLOCK:
        case VK_FORMAT_BC7_UNORM_BLOCK: case VK_FORMAT_BC7_SRGB_BLOCK:
            blockBytes = 16;
            break;
        default:
            break;
    }
    uint64_t total = 0;
    const uint32_t count = mipCount(width, height);
    for (uint32_t mip = firstMip; mip < count; ++mip) {
        const uint64_t w = std::max(width >> mip, 1u), h = std::max(height >> mip, 1u);
        total += blockBytes ? ((w + 3) / 4) * ((h + 3) / 4) * blockBytes : w * h * 4;
    }
    return total;
}

std::vector<uint32_t> fitBudget(const std::vector<Candidate>& candidates, uint64_t budget) {
    std::vector<uint32_t> target(candidates.size());
    uint64_t total = 0;
    for (size_t i = 0; i < candidates.size(); ++i) {
        const Candidate& c = candidates[i];
        target[i] = std::min(c.needed, c.coarsest);
        total += chainBytes(c.format, c.width, c.height, target[i]);
    }
    if (budget == 0 || total <= budget) return target;

    // The next to give up a mip: the least given up already, then the
    // largest chain.
    struct Entry {
        uint32_t givenUp;
        uint64_t bytes;
        size_t index;
        bool operator<(const Entry& o) const {
            return givenUp != o.givenUp ? givenUp > o.givenUp : bytes < o.bytes;
        }
    };
    std::priority_queue<Entry> queue;
    auto push = [&](size_t i) {
        const Candidate& c = candidates[i];
        if (target[i] < c.coarsest) {
            queue.push({target[i] - std::min(c.needed, target[i]), chainBytes(c.format, c.width, c.height, target[i]),
                        i});
        }
    };
    for (size_t i = 0; i < candidates.size(); ++i) push(i);
    while (total > budget && !queue.empty()) {
        const Entry e = queue.top();
        queue.pop();
        const Candidate& c = candidates[e.index];
        const uint64_t before = chainBytes(c.format, c.width, c.height, target[e.index]);
        ++target[e.index];
        total -= before - chainBytes(c.format, c.width, c.height, target[e.index]);
        push(e.index);
    }
    return target;
}

} // namespace Shoonyakasha::TextureResidency
