//
// TransformMath.h - Converting matrices into TransformComponent's TRS convention
//
// TransformComponent stores rotation as Euler angles applied Y (yaw), then
// X (pitch), then Z (roll), so R = Ry(y) * Rx(x) * Rz(z). The extraction here
// matches that order.
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
/// sum is recoverable. Roll is set to zero there and the rotation is folded into
/// yaw, which reproduces the original matrix.
///
/// That branch is required for matrices whose entries are exactly 0 and ±1, as
/// hand-authored glTF node matrices are: without it both yaw and roll evaluate
/// atan2(0, 0) and the rotation is lost. A matrix that reaches ±90° through
/// floating-point trigonometry has a cosine near 4e-8, for which the general
/// form still recovers the angle.
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
/// A sheared matrix has no such representation and will not round-trip. The
/// glTF loader passes only node-local transforms, which the glTF specification
/// requires to be decomposable to TRS.
inline void decomposeTRS(const glm::mat4& m,
                         glm::vec3& position,
                         glm::vec3& eulerRotation,
                         glm::vec3& scale) {
    position = glm::vec3(m[3]);

    scale = glm::vec3(glm::length(glm::vec3(m[0])),
                      glm::length(glm::vec3(m[1])),
                      glm::length(glm::vec3(m[2])));

    // Column lengths are always positive, so a mirrored transform would be
    // returned un-mirrored. Fold the reflection into one axis.
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
