//
// TestHelpers.h - Shared test utilities
//

#pragma once

#include <gtest/gtest.h>
#include <glm/glm.hpp>
#include <vulkan/vulkan.h>
#include <cmath>

namespace TestHelpers {

// ── Floating-point comparison helpers ───────────────────────

inline void ExpectVec2Near(const glm::vec2& a, const glm::vec2& b, float epsilon = 1e-5f) {
    EXPECT_NEAR(a.x, b.x, epsilon) << "vec2.x mismatch";
    EXPECT_NEAR(a.y, b.y, epsilon) << "vec2.y mismatch";
}

inline void ExpectVec3Near(const glm::vec3& a, const glm::vec3& b, float epsilon = 1e-5f) {
    EXPECT_NEAR(a.x, b.x, epsilon) << "vec3.x mismatch";
    EXPECT_NEAR(a.y, b.y, epsilon) << "vec3.y mismatch";
    EXPECT_NEAR(a.z, b.z, epsilon) << "vec3.z mismatch";
}

inline void ExpectVec4Near(const glm::vec4& a, const glm::vec4& b, float epsilon = 1e-5f) {
    EXPECT_NEAR(a.x, b.x, epsilon) << "vec4.x mismatch";
    EXPECT_NEAR(a.y, b.y, epsilon) << "vec4.y mismatch";
    EXPECT_NEAR(a.z, b.z, epsilon) << "vec4.z mismatch";
    EXPECT_NEAR(a.w, b.w, epsilon) << "vec4.w mismatch";
}

inline void ExpectMat4Near(const glm::mat4& a, const glm::mat4& b, float epsilon = 1e-5f) {
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r)
            EXPECT_NEAR(a[c][r], b[c][r], epsilon) << "mat4[" << c << "][" << r << "] mismatch";
}

inline void ExpectMat3Near(const glm::mat3& a, const glm::mat3& b, float epsilon = 1e-5f) {
    for (int c = 0; c < 3; ++c)
        for (int r = 0; r < 3; ++r)
            EXPECT_NEAR(a[c][r], b[c][r], epsilon) << "mat3[" << c << "][" << r << "] mismatch";
}

// ── Dummy Vulkan handles for validity testing ───────────────
// These are never dereferenced — only used for isValid() checks.

inline VkBuffer dummyVkBuffer() {
    return reinterpret_cast<VkBuffer>(uintptr_t(1));
}

inline VkImage dummyVkImage() {
    return reinterpret_cast<VkImage>(uintptr_t(1));
}

inline VkImageView dummyVkImageView() {
    return reinterpret_cast<VkImageView>(uintptr_t(1));
}

inline VkSampler dummyVkSampler() {
    return reinterpret_cast<VkSampler>(uintptr_t(1));
}

} // namespace TestHelpers
