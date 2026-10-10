//
// CameraRay.h - Rays from a camera through window pixels, and hits on quads
//
// The projection is CameraComponent::projectionMatrix: GL-style, with clip
// depth -1..1 and no Vulkan Y-flip (the flip is applied only when SceneContext
// is filled). Window pixels have their origin at the top-left with y down, so
// pixel row 0 maps to NDC y = +1.
//

#pragma once

#include <glm/glm.hpp>

#include <cmath>
#include <optional>

namespace Shoonyakasha {
namespace ECS {

struct Ray {
    glm::vec3 origin{0.0f};
    glm::vec3 direction{0.0f, 0.0f, -1.0f};  // unit length
};

/// Ray through the window pixel `pixel` for a camera with the given view and
/// projection. The origin lies on the near plane, so the same code serves
/// perspective and orthographic cameras.
inline Ray screenPointToRay(const glm::vec2& pixel, const glm::vec2& screenSize,
                            const glm::mat4& view, const glm::mat4& projection) {
    const glm::vec2 ndc(2.0f * pixel.x / screenSize.x - 1.0f,
                        1.0f - 2.0f * pixel.y / screenSize.y);
    const glm::mat4 inverseViewProjection = glm::inverse(projection * view);

    glm::vec4 nearPoint = inverseViewProjection * glm::vec4(ndc, -1.0f, 1.0f);
    glm::vec4 farPoint  = inverseViewProjection * glm::vec4(ndc,  1.0f, 1.0f);
    nearPoint /= nearPoint.w;
    farPoint /= farPoint.w;

    Ray ray;
    ray.origin = glm::vec3(nearPoint);
    ray.direction = glm::normalize(glm::vec3(farPoint) - glm::vec3(nearPoint));
    return ray;
}

struct QuadHit {
    float distance = 0.0f;   // along the ray, in world units
    glm::vec2 uv{0.0f};      // u from local x = -0.5; v from local y = +0.5 (top)
    bool frontFacing = true; // the ray arrives from the quad's local +Z side
};

/// Hit of `ray` on the unit quad (local x and y in [-0.5, 0.5], z = 0) placed
/// by `worldMatrix`. Both sides are hit. Returns nothing when the ray is
/// parallel to the quad, the quad is behind the origin, or the hit falls
/// outside it.
inline std::optional<QuadHit> intersectUnitQuad(const Ray& ray, const glm::mat4& worldMatrix) {
    const glm::mat4 toLocal = glm::inverse(worldMatrix);
    const glm::vec3 origin = glm::vec3(toLocal * glm::vec4(ray.origin, 1.0f));
    const glm::vec3 direction = glm::vec3(toLocal * glm::vec4(ray.direction, 0.0f));

    if (std::abs(direction.z) < 1e-8f) return std::nullopt;

    // An affine map keeps the ray parameter, so t is also the world distance
    // along the unit-length world direction.
    const float t = -origin.z / direction.z;
    if (t < 0.0f) return std::nullopt;

    const glm::vec3 local = origin + t * direction;
    if (local.x < -0.5f || local.x > 0.5f || local.y < -0.5f || local.y > 0.5f) {
        return std::nullopt;
    }

    QuadHit hit;
    hit.distance = t;
    hit.uv = glm::vec2(local.x + 0.5f, 0.5f - local.y);
    hit.frontFacing = direction.z < 0.0f;
    return hit;
}

} // namespace ECS
} // namespace Shoonyakasha
