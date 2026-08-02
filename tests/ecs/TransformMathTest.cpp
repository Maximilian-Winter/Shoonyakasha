//
// TransformMathTest.cpp - decomposeTRS must invert TransformComponent::getLocalMatrix
//
// The glTF loader stores node transforms on TransformComponent rather than
// baking them into vertex data, so a matrix passed through decomposeTRS() and
// back through getLocalMatrix() must reproduce the original. A sign error or a
// mismatched Euler order places geometry incorrectly with no error reported.
//

#include <gtest/gtest.h>

#include "ECS/TransformMath.h"
#include "ECS/Core.h"

#include <glm/gtc/matrix_transform.hpp>

using namespace Shoonyakasha;

namespace {

constexpr float kEps = 1e-4f;

void expectMatrixNear(const glm::mat4& a, const glm::mat4& b, const char* what) {
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) {
            EXPECT_NEAR(a[c][r], b[c][r], kEps)
                << what << " differs at column " << c << " row " << r;
        }
    }
}

/// Decompose, rebuild through TransformComponent, and compare the matrices.
/// Matrices are compared instead of TRS triples because more than one triple is
/// correct at gimbal lock.
void expectRoundTrip(const glm::mat4& m, const char* what) {
    glm::vec3 position, rotation, scale;
    decomposeTRS(m, position, rotation, scale);

    ECS::TransformComponent transform;
    transform.position = position;
    transform.rotation = rotation;
    transform.scale = scale;

    expectMatrixNear(transform.getLocalMatrix(), m, what);
}

glm::mat4 trs(const glm::vec3& t, const glm::vec3& eulerYXZ, const glm::vec3& s) {
    ECS::TransformComponent transform;
    transform.position = t;
    transform.rotation = eulerYXZ;
    transform.scale = s;
    return transform.getLocalMatrix();
}

} // namespace

TEST(TransformMath, Identity) {
    expectRoundTrip(glm::mat4(1.0f), "identity");
}

TEST(TransformMath, TranslationOnly) {
    expectRoundTrip(trs({3.0f, -4.0f, 5.5f}, {}, {1, 1, 1}), "translation");
}

TEST(TransformMath, NonUniformScale) {
    expectRoundTrip(trs({}, {}, {2.0f, 0.5f, 3.0f}), "scale");
}

TEST(TransformMath, YawPitchRollTogether) {
    expectRoundTrip(trs({1, 2, 3}, {0.3f, -1.1f, 0.7f}, {1.5f, 1.5f, 1.5f}),
                    "combined rotation");
}

TEST(TransformMath, EveryOctantOfRotation) {
    const float angles[] = {-2.5f, -0.9f, -0.2f, 0.0f, 0.2f, 0.9f, 2.5f};
    for (float y : angles) {
        for (float x : angles) {
            for (float z : angles) {
                // Pitch is extracted with asin and spans [-pi/2, pi/2]; values
                // outside that range map to a different equivalent triple.
                if (std::abs(x) > 1.5f) continue;
                expectRoundTrip(trs({0.5f, 0.0f, -0.5f}, {x, y, z}, {1, 1, 1}),
                                "rotation sweep");
            }
        }
    }
}

// ═══════════════════════════════════════════════════════════════
// Gimbal lock
// ═══════════════════════════════════════════════════════════════

TEST(TransformMath, GimbalLockPitchDownKeepsTheYaw) {
    // At pitch = -90 the yaw and roll axes coincide, and an extraction without
    // the gimbal branch evaluates atan2(0, 0) for both.
    expectRoundTrip(trs({}, {-glm::half_pi<float>(), 1.2f, 0.0f}, {1, 1, 1}),
                    "gimbal lock, pitch -90");
}

TEST(TransformMath, GimbalLockPitchUpKeepsTheYaw) {
    expectRoundTrip(trs({}, {glm::half_pi<float>(), -0.8f, 0.0f}, {1, 1, 1}),
                    "gimbal lock, pitch +90");
}

TEST(TransformMath, ExactGimbalLockWithYaw) {
    // A matrix whose entries are exactly 0 and ±1, as hand-authored glTF node
    // matrices are. The two tests above do not exercise the gimbal branch: built
    // through getLocalMatrix(), the cosine of ±90° is 4e-8 rather than 0 and the
    // general extraction still recovers the angle. With exact zeros it evaluates
    // atan2(0, 0) for both yaw and roll.
    //
    // Yaw +90 composed with pitch -90, column-major.
    const glm::mat4 m( 0, 0, -1, 0,
                      -1, 0,  0, 0,
                       0, 1,  0, 0,
                       0, 0,  0, 1);
    expectRoundTrip(m, "exact gimbal lock with yaw");
}

TEST(TransformMath, BoxGltfRootNode) {
    // The actual matrix on Box.gltf's root node: -90 degrees about X, which is the
    // Y-up to Z-up convention flip and lands exactly on gimbal lock. Every example
    // that loads it depends on this one decomposing correctly.
    const glm::mat4 m(1, 0,  0, 0,
                      0, 0, -1, 0,
                      0, 1,  0, 0,
                      0, 0,  0, 1);
    expectRoundTrip(m, "Box.gltf root");
}

TEST(TransformMath, MirroredTransformKeepsItsHandedness) {
    // Scale read from column lengths is always positive, so without the
    // determinant check a mirrored node comes back un-mirrored and renders
    // inside out.
    const glm::mat4 m = glm::scale(glm::mat4(1.0f), glm::vec3(-1.0f, 1.0f, 1.0f));

    glm::vec3 position, rotation, scale;
    decomposeTRS(m, position, rotation, scale);

    ECS::TransformComponent transform;
    transform.position = position;
    transform.rotation = rotation;
    transform.scale = scale;

    EXPECT_LT(glm::determinant(glm::mat3(transform.getLocalMatrix())), 0.0f)
        << "the mirror was lost";
    expectMatrixNear(transform.getLocalMatrix(), m, "mirrored");
}

TEST(TransformMath, MirroredAndRotated) {
    const glm::mat4 m = trs({1, 2, 3}, {0.4f, 0.9f, -0.3f}, {2.0f, -1.5f, 1.0f});
    expectRoundTrip(m, "mirrored and rotated");
}
