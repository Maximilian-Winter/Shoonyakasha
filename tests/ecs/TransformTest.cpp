//
// TransformTest.cpp - Tests for TransformComponent
//
// Tier 2: ECS integration — uses EnTT, no GPU
//

#include <gtest/gtest.h>
#include "ECS/Core.h"
#include "TestHelpers.h"
#include <glm/gtc/constants.hpp>

using namespace Shoonyakasha::ECS;

// ═══════════════════════════════════════════════════════════════
// Local Matrix Tests
// ═══════════════════════════════════════════════════════════════

TEST(TransformComponent, Default_IdentityMatrix) {
    TransformComponent t;
    auto m = t.getLocalMatrix();
    TestHelpers::ExpectMat4Near(m, glm::mat4(1.0f));
}

TEST(TransformComponent, TranslationOnly) {
    TransformComponent t;
    t.position = glm::vec3(1.0f, 2.0f, 3.0f);
    auto m = t.getLocalMatrix();

    // Translation should be in the 4th column
    EXPECT_FLOAT_EQ(m[3][0], 1.0f);
    EXPECT_FLOAT_EQ(m[3][1], 2.0f);
    EXPECT_FLOAT_EQ(m[3][2], 3.0f);
}

TEST(TransformComponent, ScaleOnly) {
    TransformComponent t;
    t.scale = glm::vec3(2.0f, 3.0f, 4.0f);
    auto m = t.getLocalMatrix();

    // Diagonal elements should be the scale
    EXPECT_FLOAT_EQ(m[0][0], 2.0f);
    EXPECT_FLOAT_EQ(m[1][1], 3.0f);
    EXPECT_FLOAT_EQ(m[2][2], 4.0f);
}

TEST(TransformComponent, RotationY_90Degrees) {
    TransformComponent t;
    t.rotation.y = glm::half_pi<float>();  // 90 degrees yaw
    auto m = t.getLocalMatrix();

    // After 90-degree Y rotation (column-major):
    // Column 0 = (cos90, 0, -sin90, 0) → (0, 0, -1, 0)
    // Column 2 = (sin90, 0, cos90, 0)  → (1, 0,  0, 0)
    EXPECT_NEAR(m[0][0], 0.0f, 1e-5f);   // cos(90) ≈ 0
    EXPECT_NEAR(m[0][2], -1.0f, 1e-5f);  // -sin(90) in column 0, row 2
}

TEST(TransformComponent, TRS_Composition) {
    TransformComponent t;
    t.position = glm::vec3(10.0f, 0.0f, 0.0f);
    t.scale = glm::vec3(2.0f);

    auto m = t.getLocalMatrix();

    // The composition should be T * R * S
    // A point at origin scaled by 2 then translated by 10 along X:
    glm::vec4 origin(0, 0, 0, 1);
    glm::vec4 result = m * origin;
    EXPECT_NEAR(result.x, 10.0f, 1e-5f);
    EXPECT_NEAR(result.y, 0.0f, 1e-5f);
    EXPECT_NEAR(result.z, 0.0f, 1e-5f);

    // A point at (1,0,0) should be scaled to (2,0,0) then translated to (12,0,0)
    glm::vec4 unitX(1, 0, 0, 1);
    glm::vec4 result2 = m * unitX;
    EXPECT_NEAR(result2.x, 12.0f, 1e-5f);
}

// ═══════════════════════════════════════════════════════════════
// Direction Vector Tests
// ═══════════════════════════════════════════════════════════════

TEST(TransformComponent, DefaultForward_NegativeZ) {
    TransformComponent t;
    auto fwd = t.getForward();
    TestHelpers::ExpectVec3Near(fwd, glm::vec3(0.0f, 0.0f, -1.0f));
}

TEST(TransformComponent, DefaultRight_PositiveX) {
    TransformComponent t;
    auto right = t.getRight();
    TestHelpers::ExpectVec3Near(right, glm::vec3(1.0f, 0.0f, 0.0f));
}

TEST(TransformComponent, DefaultUp_PositiveY) {
    TransformComponent t;
    auto up = t.getUp();
    TestHelpers::ExpectVec3Near(up, glm::vec3(0.0f, 1.0f, 0.0f));
}

TEST(TransformComponent, Forward_After90YawRotation) {
    TransformComponent t;
    t.rotation.y = glm::half_pi<float>();  // 90 degrees yaw
    auto fwd = t.getForward();

    // After 90 Y rotation, forward (-Z) should point toward -X
    TestHelpers::ExpectVec3Near(fwd, glm::vec3(-1.0f, 0.0f, 0.0f));
}

TEST(TransformComponent, Right_After90YawRotation) {
    TransformComponent t;
    t.rotation.y = glm::half_pi<float>();
    auto right = t.getRight();

    // After 90 Y rotation, right (+X) should point toward -Z
    TestHelpers::ExpectVec3Near(right, glm::vec3(0.0f, 0.0f, -1.0f));
}

// ═══════════════════════════════════════════════════════════════
// Dirty Flag Tests
// ═══════════════════════════════════════════════════════════════

TEST(TransformComponent, DirtyFlag_StartsDirty) {
    TransformComponent t;
    EXPECT_TRUE(t.isDirty);
}

TEST(TransformComponent, DirtyFlag_PositionConstruction) {
    TransformComponent t(glm::vec3(1, 2, 3));
    EXPECT_TRUE(t.isDirty);
    EXPECT_FLOAT_EQ(t.position.x, 1.0f);
}

// ═══════════════════════════════════════════════════════════════
// Constructor Tests
// ═══════════════════════════════════════════════════════════════

TEST(TransformComponent, PositionConstructor) {
    TransformComponent t(glm::vec3(5, 10, 15));
    EXPECT_FLOAT_EQ(t.position.x, 5.0f);
    EXPECT_FLOAT_EQ(t.position.y, 10.0f);
    EXPECT_FLOAT_EQ(t.position.z, 15.0f);
    // Scale should still default to 1
    TestHelpers::ExpectVec3Near(t.scale, glm::vec3(1.0f));
}

TEST(TransformComponent, FullConstructor) {
    glm::vec3 pos(1, 2, 3);
    glm::vec3 rot(0.1f, 0.2f, 0.3f);
    glm::vec3 scl(2, 3, 4);
    TransformComponent t(pos, rot, scl);

    TestHelpers::ExpectVec3Near(t.position, pos);
    TestHelpers::ExpectVec3Near(t.rotation, rot);
    TestHelpers::ExpectVec3Near(t.scale, scl);
}
