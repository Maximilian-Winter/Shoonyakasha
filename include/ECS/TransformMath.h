//
// TransformMath.h - Converting matrices into TransformComponent's TRS convention
//
// रूपस्य विश्लेषणम् — the analysis of form.
//
// TransformComponent stores rotation as Euler angles applied Y (yaw) → X (pitch)
// → Z (roll), so R = Ry(y) * Rx(x) * Rz(z). Anything that has to turn a matrix
// back into that representation must use the matching extraction, which is what
// lives here rather than being written out again at each call site.
//

#pragma once

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>

namespace Shoonyakasha {

/// Euler angles (x = pitch, y = yaw, z = roll) for a pure rotation matrix,
/// in the Y→X→Z order TransformComponent::getLocalMatrix() applies them.
///
/// At pitch = ±90° the yaw and roll axes coincide (gimbal lock) and only their
/// sum is recoverable. Roll is pinned to zero there and the whole rotation is
/// folded into yaw, which reproduces the original matrix exactly.
///
/// The branch matters for matrices whose entries are exactly 0 and ±1 — which is
/// what hand-authored glTF node matrices contain. Without it both yaw and roll
/// read atan2(0, 0) and the rotation is lost. A matrix that reached ±90° through
/// floating-point trigonometry has a cosine near 4e-8 rather than 0, and the
/// naive form still recovers the angle from the ratio, which is why this went
/// unnoticed.
inline glm::vec3 eulerYXZFromRotation(const glm::mat3& r) {
    // glm is column-major: r[column][row].
    const float sinPitch = std::clamp(-r[2][1], -1.0f, 1.0f);
    const float pitch = std::asin(sinPitch);
    const float cosPitch = std::sqrt(std::max(0.0f, 1.0f - sinPitch * sinPitch));

    if (cosPitch < 1e-6f) {
        return glm::vec3(pitch, std::atan2(-r[0][2], r[0][0]), 0.0f);
    }

    return glm::vec3(pitch,
                     std::atan2(r[2][0], r[2][2]),
                     std::atan2(r[0][1], r[1][1]));
}

/// Split a transform matrix into the position / Euler rotation / scale that
/// TransformComponent holds. Feeding the results back through
/// TransformComponent::getLocalMatrix() reproduces `m`.
///
/// Exact for any matrix that is a product of translation, rotation and scale.
/// A sheared matrix has no such representation and will not round-trip — which
/// is why the glTF loader only ever passes node-local transforms here: the glTF
/// spec requires a node's matrix to be decomposable to TRS, forbidding shear.
inline void decomposeTRS(const glm::mat4& m,
                         glm::vec3& position,
                         glm::vec3& eulerRotation,
                         glm::vec3& scale) {
    position = glm::vec3(m[3]);

    scale = glm::vec3(glm::length(glm::vec3(m[0])),
                      glm::length(glm::vec3(m[1])),
                      glm::length(glm::vec3(m[2])));

    // Column lengths are always positive, so a mirrored transform would come back
    // un-mirrored and render inside out. Fold the flip into one axis.
    if (glm::determinant(glm::mat3(m)) < 0.0f) {
        scale.x = -scale.x;
    }

    glm::mat3 rotation(1.0f);
    if (scale.x != 0.0f) rotation[0] = glm::vec3(m[0]) / scale.x;
    if (scale.y != 0.0f) rotation[1] = glm::vec3(m[1]) / scale.y;
    if (scale.z != 0.0f) rotation[2] = glm::vec3(m[2]) / scale.z;

    eulerRotation = eulerYXZFromRotation(rotation);
}

} // namespace Shoonyakasha
