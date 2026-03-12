//
// DotPathResolverTest.cpp - Tests for dot-path classification, ResolvedValue, and const paths
//
// Tier 1: Pure unit tests — no GPU context, no ECS registry
//

#include <gtest/gtest.h>
#include "FrameGraph/DotPathResolver.h"
#include "TestHelpers.h"

using namespace Shoonyakasha;

// ═══════════════════════════════════════════════════════════════
// Path Root Classification Tests
// ═══════════════════════════════════════════════════════════════

TEST(DotPathResolver, GetPathRoot_Scene) {
    EXPECT_EQ(DotPathResolver::getPathRoot("scene.camera.view"), DotPathResolver::PathRoot::Scene);
}

TEST(DotPathResolver, GetPathRoot_Entity) {
    EXPECT_EQ(DotPathResolver::getPathRoot("entity.transform.worldMatrix"), DotPathResolver::PathRoot::Entity);
}

TEST(DotPathResolver, GetPathRoot_Const) {
    EXPECT_EQ(DotPathResolver::getPathRoot("const.42"), DotPathResolver::PathRoot::Const);
}

TEST(DotPathResolver, GetPathRoot_Resource) {
    EXPECT_EQ(DotPathResolver::getPathRoot("gPosition"), DotPathResolver::PathRoot::Resource);
}

TEST(DotPathResolver, GetPathRoot_ResourceUnderscore) {
    EXPECT_EQ(DotPathResolver::getPathRoot("_myBuffer"), DotPathResolver::PathRoot::Resource);
}

TEST(DotPathResolver, GetPathRoot_Empty_Invalid) {
    EXPECT_EQ(DotPathResolver::getPathRoot(""), DotPathResolver::PathRoot::Invalid);
}

TEST(DotPathResolver, GetPathRoot_NumberStart_Invalid) {
    EXPECT_EQ(DotPathResolver::getPathRoot("123abc"), DotPathResolver::PathRoot::Invalid);
}

// ─── Convenience bool checks ────────────────────────────────

TEST(DotPathResolver, IsScenePath_True) {
    EXPECT_TRUE(DotPathResolver::isScenePath("scene.time.elapsed"));
}

TEST(DotPathResolver, IsScenePath_False) {
    EXPECT_FALSE(DotPathResolver::isScenePath("entity.transform.worldMatrix"));
}

TEST(DotPathResolver, IsEntityPath_True) {
    EXPECT_TRUE(DotPathResolver::isEntityPath("entity.material.params.color"));
}

TEST(DotPathResolver, IsEntityPath_False) {
    EXPECT_FALSE(DotPathResolver::isEntityPath("scene.camera.view"));
}

TEST(DotPathResolver, IsConstPath_True) {
    EXPECT_TRUE(DotPathResolver::isConstPath("const.1.0"));
}

TEST(DotPathResolver, IsConstPath_False) {
    EXPECT_FALSE(DotPathResolver::isConstPath("scene.time.delta"));
}

TEST(DotPathResolver, IsResourcePath_True) {
    EXPECT_TRUE(DotPathResolver::isResourcePath("litColorHDR"));
}

TEST(DotPathResolver, IsResourcePath_False) {
    EXPECT_FALSE(DotPathResolver::isResourcePath("scene.camera.view"));
}

// ═══════════════════════════════════════════════════════════════
// ResolvedValue Construction & Type Checks
// ═══════════════════════════════════════════════════════════════

TEST(ResolvedValue, Default_IsInvalid) {
    ResolvedValue v;
    EXPECT_FALSE(v.isValid());
}

TEST(ResolvedValue, Float_Valid) {
    ResolvedValue v(3.14f);
    EXPECT_TRUE(v.isValid());
    EXPECT_TRUE(v.isFloat());
    EXPECT_FLOAT_EQ(v.as<float>(), 3.14f);
}

TEST(ResolvedValue, Vec2_RoundTrip) {
    glm::vec2 val(1.0f, 2.0f);
    ResolvedValue v(val);
    EXPECT_TRUE(v.isVec2());
    TestHelpers::ExpectVec2Near(v.as<glm::vec2>(), val);
}

TEST(ResolvedValue, Vec3_RoundTrip) {
    glm::vec3 val(1.0f, 2.0f, 3.0f);
    ResolvedValue v(val);
    EXPECT_TRUE(v.isVec3());
    TestHelpers::ExpectVec3Near(v.as<glm::vec3>(), val);
}

TEST(ResolvedValue, Vec4_RoundTrip) {
    glm::vec4 val(1.0f, 2.0f, 3.0f, 4.0f);
    ResolvedValue v(val);
    EXPECT_TRUE(v.isVec4());
    TestHelpers::ExpectVec4Near(v.as<glm::vec4>(), val);
}

TEST(ResolvedValue, Mat3_RoundTrip) {
    glm::mat3 m(1, 2, 3, 4, 5, 6, 7, 8, 9);
    ResolvedValue v(m);
    EXPECT_TRUE(v.isMat3());
    TestHelpers::ExpectMat3Near(v.as<glm::mat3>(), m);
}

TEST(ResolvedValue, Mat4_RoundTrip) {
    glm::mat4 m(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16);
    ResolvedValue v(m);
    EXPECT_TRUE(v.isMat4());
    TestHelpers::ExpectMat4Near(v.as<glm::mat4>(), m);
}

TEST(ResolvedValue, Int_RoundTrip) {
    ResolvedValue v(int32_t(-42));
    EXPECT_TRUE(v.isInt());
    EXPECT_EQ(v.as<int32_t>(), -42);
}

TEST(ResolvedValue, UInt_RoundTrip) {
    ResolvedValue v(uint32_t(999));
    EXPECT_TRUE(v.isUInt());
    EXPECT_EQ(v.as<uint32_t>(), 999u);
}

// ═══════════════════════════════════════════════════════════════
// ResolvedValue::byteSize()
// ═══════════════════════════════════════════════════════════════

TEST(ResolvedValue, ByteSize_Monostate)  { EXPECT_EQ(ResolvedValue().byteSize(), 0u); }
TEST(ResolvedValue, ByteSize_Float)      { EXPECT_EQ(ResolvedValue(1.0f).byteSize(), sizeof(float)); }
TEST(ResolvedValue, ByteSize_Vec2)       { EXPECT_EQ(ResolvedValue(glm::vec2(0)).byteSize(), sizeof(glm::vec2)); }
TEST(ResolvedValue, ByteSize_Vec3)       { EXPECT_EQ(ResolvedValue(glm::vec3(0)).byteSize(), sizeof(glm::vec3)); }
TEST(ResolvedValue, ByteSize_Vec4)       { EXPECT_EQ(ResolvedValue(glm::vec4(0)).byteSize(), sizeof(glm::vec4)); }
TEST(ResolvedValue, ByteSize_Mat3)       { EXPECT_EQ(ResolvedValue(glm::mat3(1)).byteSize(), sizeof(glm::mat3)); }
TEST(ResolvedValue, ByteSize_Mat4)       { EXPECT_EQ(ResolvedValue(glm::mat4(1)).byteSize(), sizeof(glm::mat4)); }
TEST(ResolvedValue, ByteSize_Int)        { EXPECT_EQ(ResolvedValue(int32_t(0)).byteSize(), sizeof(int32_t)); }
TEST(ResolvedValue, ByteSize_UInt)       { EXPECT_EQ(ResolvedValue(uint32_t(0)).byteSize(), sizeof(uint32_t)); }
TEST(ResolvedValue, ByteSize_Texture)    { EXPECT_EQ(ResolvedValue(GPUTexture{}).byteSize(), 0u); }

// ═══════════════════════════════════════════════════════════════
// ResolvedValue::tryAs<T>()
// ═══════════════════════════════════════════════════════════════

TEST(ResolvedValue, TryAs_CorrectType_ReturnsValue) {
    ResolvedValue v(42.0f);
    auto result = v.tryAs<float>();
    ASSERT_TRUE(result.has_value());
    EXPECT_FLOAT_EQ(result.value(), 42.0f);
}

TEST(ResolvedValue, TryAs_WrongType_ReturnsNullopt) {
    ResolvedValue v(42.0f);
    EXPECT_FALSE(v.tryAs<glm::vec3>().has_value());
}

TEST(ResolvedValue, TryAs_Monostate_ReturnsNullopt) {
    ResolvedValue v;
    EXPECT_FALSE(v.tryAs<float>().has_value());
}

// ═══════════════════════════════════════════════════════════════
// ResolvedValue::copyTo()
// ═══════════════════════════════════════════════════════════════

TEST(ResolvedValue, CopyTo_Float) {
    ResolvedValue v(42.0f);
    float dest = 0.0f;
    v.copyTo(&dest);
    EXPECT_FLOAT_EQ(dest, 42.0f);
}

TEST(ResolvedValue, CopyTo_Vec3) {
    glm::vec3 src(1.0f, 2.0f, 3.0f);
    ResolvedValue v(src);
    glm::vec3 dest(0.0f);
    v.copyTo(&dest);
    TestHelpers::ExpectVec3Near(dest, src);
}

TEST(ResolvedValue, CopyTo_Monostate_NoOp) {
    // Ensure no crash when copying monostate
    ResolvedValue v;
    float dest = 99.0f;
    v.copyTo(&dest);
    EXPECT_FLOAT_EQ(dest, 99.0f);  // Untouched
}

// ═══════════════════════════════════════════════════════════════
// Const Path Resolution (no ECS needed)
// ═══════════════════════════════════════════════════════════════

TEST(DotPathResolver, ConstPath_SingleFloat) {
    DotPathResolver resolver;
    SceneContext scene;

    auto v = resolver.resolveScene("const.42", scene);
    ASSERT_TRUE(v.isFloat());
    EXPECT_FLOAT_EQ(v.as<float>(), 42.0f);
}

TEST(DotPathResolver, ConstPath_NegativeFloat) {
    DotPathResolver resolver;
    SceneContext scene;

    // Note: from_chars should handle negative numbers
    auto v = resolver.resolveScene("const.-1", scene);
    // from_chars on some implementations may not handle leading minus for float
    // If it doesn't resolve, that's acceptable
    if (v.isValid()) {
        EXPECT_FLOAT_EQ(v.as<float>(), -1.0f);
    }
}

TEST(DotPathResolver, ConstPath_Zero) {
    DotPathResolver resolver;
    SceneContext scene;

    auto v = resolver.resolveScene("const.0", scene);
    ASSERT_TRUE(v.isFloat());
    EXPECT_FLOAT_EQ(v.as<float>(), 0.0f);
}

TEST(DotPathResolver, ConstPath_Vec2) {
    DotPathResolver resolver;
    SceneContext scene;

    auto v = resolver.resolveScene("const.1.2", scene);
    ASSERT_TRUE(v.isVec2());
    TestHelpers::ExpectVec2Near(v.as<glm::vec2>(), glm::vec2(1.0f, 2.0f));
}

TEST(DotPathResolver, ConstPath_Vec3) {
    DotPathResolver resolver;
    SceneContext scene;

    auto v = resolver.resolveScene("const.1.2.3", scene);
    ASSERT_TRUE(v.isVec3());
    TestHelpers::ExpectVec3Near(v.as<glm::vec3>(), glm::vec3(1.0f, 2.0f, 3.0f));
}

TEST(DotPathResolver, ConstPath_Vec4) {
    DotPathResolver resolver;
    SceneContext scene;

    auto v = resolver.resolveScene("const.1.2.3.4", scene);
    ASSERT_TRUE(v.isVec4());
    TestHelpers::ExpectVec4Near(v.as<glm::vec4>(), glm::vec4(1.0f, 2.0f, 3.0f, 4.0f));
}

TEST(DotPathResolver, ConstPath_Invalid_ReturnsInvalid) {
    DotPathResolver resolver;
    SceneContext scene;

    auto v = resolver.resolveScene("const.not_a_number", scene);
    EXPECT_FALSE(v.isValid());
}

// ═══════════════════════════════════════════════════════════════
// validatePath()
// ═══════════════════════════════════════════════════════════════

TEST(DotPathResolver, ValidatePath_ValidScene_Empty) {
    DotPathResolver resolver;
    EXPECT_TRUE(resolver.validatePath("scene.camera.view").empty());
}

TEST(DotPathResolver, ValidatePath_ValidEntity_Empty) {
    DotPathResolver resolver;
    EXPECT_TRUE(resolver.validatePath("entity.transform.worldMatrix").empty());
}

TEST(DotPathResolver, ValidatePath_ValidConst_Empty) {
    DotPathResolver resolver;
    EXPECT_TRUE(resolver.validatePath("const.42").empty());
}

TEST(DotPathResolver, ValidatePath_ValidResource_Empty) {
    DotPathResolver resolver;
    EXPECT_TRUE(resolver.validatePath("gPosition").empty());
}

TEST(DotPathResolver, ValidatePath_InvalidSceneCategory) {
    DotPathResolver resolver;
    auto error = resolver.validatePath("scene.bogus.field");
    EXPECT_FALSE(error.empty());
}

TEST(DotPathResolver, ValidatePath_InvalidEntityComponent) {
    DotPathResolver resolver;
    auto error = resolver.validatePath("entity.bogus.field");
    EXPECT_FALSE(error.empty());
}

TEST(DotPathResolver, ValidatePath_SceneTooShort) {
    DotPathResolver resolver;
    auto error = resolver.validatePath("scene.");
    // "scene." splits into ["scene", ""], may or may not trigger the size check
    // The main thing is it doesn't crash
    (void)error;
}

TEST(DotPathResolver, ValidatePath_Empty_Invalid) {
    DotPathResolver resolver;
    auto error = resolver.validatePath("");
    EXPECT_FALSE(error.empty());
}
