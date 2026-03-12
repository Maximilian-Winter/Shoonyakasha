//
// _glm_bridge.h — GLM ↔ Python tuple conversion helpers
//
// Header-only C++ bridge for Cython. Provides plain-function access to GLM
// types, since Cython cannot access GLM's union/swizzle members directly.
//
// 碼道之橋 — The bridge of the Way of Code
//

#pragma once

#include <glm/glm.hpp>

namespace ShoonyakashaBridge {

// ═══════════════════════════════════════════════════════════════
// Construction — Python tuple components → GLM types
// ═══════════════════════════════════════════════════════════════

inline glm::vec2 make_vec2(float x, float y) {
    return glm::vec2(x, y);
}

inline glm::vec3 make_vec3(float x, float y, float z) {
    return glm::vec3(x, y, z);
}

inline glm::vec4 make_vec4(float x, float y, float z, float w) {
    return glm::vec4(x, y, z, w);
}

// GLM mat4 is column-major. Parameters are given column-by-column:
//   col0 = (m00, m01, m02, m03), col1 = (m10, m11, m12, m13), ...
inline glm::mat4 make_mat4(
    float m00, float m01, float m02, float m03,
    float m10, float m11, float m12, float m13,
    float m20, float m21, float m22, float m23,
    float m30, float m31, float m32, float m33)
{
    return glm::mat4(
        m00, m01, m02, m03,
        m10, m11, m12, m13,
        m20, m21, m22, m23,
        m30, m31, m32, m33
    );
}

// ═══════════════════════════════════════════════════════════════
// Extraction — GLM types → individual float components
// ═══════════════════════════════════════════════════════════════

inline float vec2_x(const glm::vec2& v) { return v.x; }
inline float vec2_y(const glm::vec2& v) { return v.y; }

inline float vec3_x(const glm::vec3& v) { return v.x; }
inline float vec3_y(const glm::vec3& v) { return v.y; }
inline float vec3_z(const glm::vec3& v) { return v.z; }

inline float vec4_x(const glm::vec4& v) { return v.x; }
inline float vec4_y(const glm::vec4& v) { return v.y; }
inline float vec4_z(const glm::vec4& v) { return v.z; }
inline float vec4_w(const glm::vec4& v) { return v.w; }

// Access mat4 element by [col][row] (GLM's native column-major order)
inline float mat4_get(const glm::mat4& m, int col, int row) {
    return m[col][row];
}

} // namespace ShoonyakashaBridge
