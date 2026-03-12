//
// CameraTest.cpp - Tests for CameraComponent projection matrices
//
// Tier 2: ECS integration — uses glm, no GPU
//

#include <gtest/gtest.h>
#include "ECS/Core.h"
#include "TestHelpers.h"

using namespace Shoonyakasha::ECS;

// ═══════════════════════════════════════════════════════════════
// Perspective Projection Tests
// ═══════════════════════════════════════════════════════════════

TEST(CameraComponent, DefaultType_IsPerspective) {
    CameraComponent cam;
    EXPECT_EQ(cam.type, CameraComponent::Perspective);
}

TEST(CameraComponent, Perspective_NonIdentity) {
    CameraComponent cam;
    auto proj = cam.getProjectionMatrix();

    // Projection matrix should not be identity
    glm::mat4 identity(1.0f);
    bool isIdentity = true;
    for (int c = 0; c < 4 && isIdentity; ++c)
        for (int r = 0; r < 4 && isIdentity; ++r)
            if (std::abs(proj[c][r] - identity[c][r]) > 1e-5f)
                isIdentity = false;
    EXPECT_FALSE(isIdentity);
}

TEST(CameraComponent, Perspective_FovAffectsProjection) {
    CameraComponent cam1;
    cam1.fov = 45.0f;
    auto proj1 = cam1.getProjectionMatrix();

    CameraComponent cam2;
    cam2.fov = 90.0f;
    auto proj2 = cam2.getProjectionMatrix();

    // Different FOVs should produce different projection matrices
    EXPECT_NE(proj1[0][0], proj2[0][0]);
}

TEST(CameraComponent, Perspective_AspectAffectsProjection) {
    CameraComponent cam1;
    cam1.aspectRatio = 16.0f / 9.0f;
    auto proj1 = cam1.getProjectionMatrix();

    CameraComponent cam2;
    cam2.aspectRatio = 4.0f / 3.0f;
    auto proj2 = cam2.getProjectionMatrix();

    EXPECT_NE(proj1[0][0], proj2[0][0]);
}

TEST(CameraComponent, Perspective_NearFarAffectsProjection) {
    CameraComponent cam1;
    cam1.nearPlane = 0.1f;
    cam1.farPlane = 100.0f;
    auto proj1 = cam1.getProjectionMatrix();

    CameraComponent cam2;
    cam2.nearPlane = 1.0f;
    cam2.farPlane = 1000.0f;
    auto proj2 = cam2.getProjectionMatrix();

    // The [3][2] element encodes -2*far*near/(far-near), which differs
    EXPECT_NE(proj1[3][2], proj2[3][2]);
}

// ═══════════════════════════════════════════════════════════════
// Orthographic Projection Tests
// ═══════════════════════════════════════════════════════════════

TEST(CameraComponent, Orthographic_NonIdentity) {
    CameraComponent cam;
    cam.type = CameraComponent::Orthographic;
    auto proj = cam.getProjectionMatrix();

    glm::mat4 identity(1.0f);
    bool isIdentity = true;
    for (int c = 0; c < 4 && isIdentity; ++c)
        for (int r = 0; r < 4 && isIdentity; ++r)
            if (std::abs(proj[c][r] - identity[c][r]) > 1e-5f)
                isIdentity = false;
    EXPECT_FALSE(isIdentity);
}

TEST(CameraComponent, Orthographic_OrthoSizeAffectsProjection) {
    CameraComponent cam1;
    cam1.type = CameraComponent::Orthographic;
    cam1.orthoSize = 10.0f;
    auto proj1 = cam1.getProjectionMatrix();

    CameraComponent cam2;
    cam2.type = CameraComponent::Orthographic;
    cam2.orthoSize = 20.0f;
    auto proj2 = cam2.getProjectionMatrix();

    // Larger ortho size = more world visible = smaller scale in matrix
    EXPECT_GT(std::abs(proj1[0][0]), std::abs(proj2[0][0]));
}

// ═══════════════════════════════════════════════════════════════
// Default Values Tests
// ═══════════════════════════════════════════════════════════════

TEST(CameraComponent, Default_IsNotMainCamera) {
    CameraComponent cam;
    EXPECT_FALSE(cam.isMainCamera);
}

TEST(CameraComponent, Default_ReasonableDefaults) {
    CameraComponent cam;
    EXPECT_FLOAT_EQ(cam.fov, 45.0f);
    EXPECT_FLOAT_EQ(cam.nearPlane, 0.1f);
    EXPECT_FLOAT_EQ(cam.farPlane, 1000.0f);
}
