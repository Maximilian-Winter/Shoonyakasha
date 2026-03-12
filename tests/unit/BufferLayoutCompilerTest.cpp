//
// BufferLayoutCompilerTest.cpp - Tests for JSON buffer layout compilation
//
// Tier 1: Pure unit tests — no GPU context, no ECS
//

#include <gtest/gtest.h>
#include "FrameGraph/BufferLayoutCompiler.h"

using namespace Shoonyakasha;

// ═══════════════════════════════════════════════════════════════
// parseType() Tests
// ═══════════════════════════════════════════════════════════════

TEST(BufferLayoutCompiler, ParseType_Float) {
    EXPECT_EQ(BufferLayoutCompiler::parseType("float"), MaterialParam::Type::Float);
}

TEST(BufferLayoutCompiler, ParseType_Vec2) {
    EXPECT_EQ(BufferLayoutCompiler::parseType("vec2"), MaterialParam::Type::Vec2);
}

TEST(BufferLayoutCompiler, ParseType_Vec3) {
    EXPECT_EQ(BufferLayoutCompiler::parseType("vec3"), MaterialParam::Type::Vec3);
}

TEST(BufferLayoutCompiler, ParseType_Vec4) {
    EXPECT_EQ(BufferLayoutCompiler::parseType("vec4"), MaterialParam::Type::Vec4);
}

TEST(BufferLayoutCompiler, ParseType_Mat3) {
    EXPECT_EQ(BufferLayoutCompiler::parseType("mat3"), MaterialParam::Type::Mat3);
}

TEST(BufferLayoutCompiler, ParseType_Mat4) {
    EXPECT_EQ(BufferLayoutCompiler::parseType("mat4"), MaterialParam::Type::Mat4);
}

TEST(BufferLayoutCompiler, ParseType_Int) {
    EXPECT_EQ(BufferLayoutCompiler::parseType("int"), MaterialParam::Type::Int);
}

TEST(BufferLayoutCompiler, ParseType_UInt) {
    EXPECT_EQ(BufferLayoutCompiler::parseType("uint"), MaterialParam::Type::UInt);
}

TEST(BufferLayoutCompiler, ParseType_Unknown_Throws) {
    EXPECT_THROW(BufferLayoutCompiler::parseType("bogus"), std::runtime_error);
}

// ═══════════════════════════════════════════════════════════════
// getTypeSize() Tests
// ═══════════════════════════════════════════════════════════════

TEST(BufferLayoutCompiler, TypeSize_Float) { EXPECT_EQ(BufferLayoutCompiler::getTypeSize(MaterialParam::Type::Float), 4u); }
TEST(BufferLayoutCompiler, TypeSize_Vec2)  { EXPECT_EQ(BufferLayoutCompiler::getTypeSize(MaterialParam::Type::Vec2), 8u); }
TEST(BufferLayoutCompiler, TypeSize_Vec3)  { EXPECT_EQ(BufferLayoutCompiler::getTypeSize(MaterialParam::Type::Vec3), 12u); }
TEST(BufferLayoutCompiler, TypeSize_Vec4)  { EXPECT_EQ(BufferLayoutCompiler::getTypeSize(MaterialParam::Type::Vec4), 16u); }
TEST(BufferLayoutCompiler, TypeSize_Mat3)  { EXPECT_EQ(BufferLayoutCompiler::getTypeSize(MaterialParam::Type::Mat3), 36u); }
TEST(BufferLayoutCompiler, TypeSize_Mat4)  { EXPECT_EQ(BufferLayoutCompiler::getTypeSize(MaterialParam::Type::Mat4), 64u); }
TEST(BufferLayoutCompiler, TypeSize_Int)   { EXPECT_EQ(BufferLayoutCompiler::getTypeSize(MaterialParam::Type::Int), 4u); }
TEST(BufferLayoutCompiler, TypeSize_UInt)  { EXPECT_EQ(BufferLayoutCompiler::getTypeSize(MaterialParam::Type::UInt), 4u); }

// ═══════════════════════════════════════════════════════════════
// getTypeAlignment() Tests
// ═══════════════════════════════════════════════════════════════

// Scalar packing — aligned to natural size (4 for <=4 bytes, 8 for <=8, 16 for larger)
TEST(BufferLayoutCompiler, Alignment_Scalar_Float) {
    EXPECT_EQ(BufferLayoutCompiler::getTypeAlignment(MaterialParam::Type::Float, PackingRule::Scalar), 4u);
}

TEST(BufferLayoutCompiler, Alignment_Scalar_Vec2) {
    EXPECT_EQ(BufferLayoutCompiler::getTypeAlignment(MaterialParam::Type::Vec2, PackingRule::Scalar), 8u);
}

TEST(BufferLayoutCompiler, Alignment_Scalar_Vec3) {
    EXPECT_EQ(BufferLayoutCompiler::getTypeAlignment(MaterialParam::Type::Vec3, PackingRule::Scalar), 16u);
}

TEST(BufferLayoutCompiler, Alignment_Scalar_Vec4) {
    EXPECT_EQ(BufferLayoutCompiler::getTypeAlignment(MaterialParam::Type::Vec4, PackingRule::Scalar), 16u);
}

TEST(BufferLayoutCompiler, Alignment_Scalar_Mat4) {
    EXPECT_EQ(BufferLayoutCompiler::getTypeAlignment(MaterialParam::Type::Mat4, PackingRule::Scalar), 16u);
}

// Std140 packing — vec3 aligned to 16, vec2 to 8
TEST(BufferLayoutCompiler, Alignment_Std140_Float) {
    EXPECT_EQ(BufferLayoutCompiler::getTypeAlignment(MaterialParam::Type::Float, PackingRule::Std140), 4u);
}

TEST(BufferLayoutCompiler, Alignment_Std140_Vec2) {
    EXPECT_EQ(BufferLayoutCompiler::getTypeAlignment(MaterialParam::Type::Vec2, PackingRule::Std140), 8u);
}

TEST(BufferLayoutCompiler, Alignment_Std140_Vec3) {
    // vec3 aligns to 16 in std140!
    EXPECT_EQ(BufferLayoutCompiler::getTypeAlignment(MaterialParam::Type::Vec3, PackingRule::Std140), 16u);
}

TEST(BufferLayoutCompiler, Alignment_Std140_Vec4) {
    EXPECT_EQ(BufferLayoutCompiler::getTypeAlignment(MaterialParam::Type::Vec4, PackingRule::Std140), 16u);
}

TEST(BufferLayoutCompiler, Alignment_Std140_Mat3) {
    EXPECT_EQ(BufferLayoutCompiler::getTypeAlignment(MaterialParam::Type::Mat3, PackingRule::Std140), 16u);
}

TEST(BufferLayoutCompiler, Alignment_Std140_Mat4) {
    EXPECT_EQ(BufferLayoutCompiler::getTypeAlignment(MaterialParam::Type::Mat4, PackingRule::Std140), 16u);
}

// ═══════════════════════════════════════════════════════════════
// parsePackingRule() Tests
// ═══════════════════════════════════════════════════════════════

TEST(BufferLayoutCompiler, ParsePackingRule_Std140) {
    EXPECT_EQ(BufferLayoutCompiler::parsePackingRule("std140"), PackingRule::Std140);
}

TEST(BufferLayoutCompiler, ParsePackingRule_Std430) {
    EXPECT_EQ(BufferLayoutCompiler::parsePackingRule("std430"), PackingRule::Std430);
}

TEST(BufferLayoutCompiler, ParsePackingRule_Scalar) {
    EXPECT_EQ(BufferLayoutCompiler::parsePackingRule("scalar"), PackingRule::Scalar);
}

TEST(BufferLayoutCompiler, ParsePackingRule_Unknown_DefaultsScalar) {
    EXPECT_EQ(BufferLayoutCompiler::parsePackingRule("unknown"), PackingRule::Scalar);
}

// ═══════════════════════════════════════════════════════════════
// compile() Tests — Single Field Scalar
// ═══════════════════════════════════════════════════════════════

TEST(BufferLayoutCompiler, Compile_SingleFloat_Scalar) {
    BufferLayoutCompiler compiler;

    nlohmann::json layout = {
        {"packing", "scalar"},
        {"fields", {{
            {"name", "value"},
            {"type", "float"},
            {"source", "const.1"}
        }}}
    };

    auto result = compiler.compile("test", layout);
    EXPECT_EQ(result.name, "test");
    ASSERT_EQ(result.fields.size(), 1u);
    EXPECT_EQ(result.fields[0].offset, 0u);
    EXPECT_EQ(result.fields[0].size, 4u);
    EXPECT_EQ(result.totalSize, 4u);
}

// ═══════════════════════════════════════════════════════════════
// compile() Tests — Std140 Alignment Padding
// ═══════════════════════════════════════════════════════════════

TEST(BufferLayoutCompiler, Compile_Vec3ThenFloat_Std140_PaddingCorrect) {
    BufferLayoutCompiler compiler;

    // In std140: vec3 aligns to 16, takes 12 bytes,
    // then float aligns to 4, so offset = 12 (no extra padding beyond vec3's 12 bytes)
    // Actually: vec3 at offset 0 (align=16, 0 is aligned), size=12.
    // currentOffset = 0 + 12 = 12
    // float align=4, 12 is aligned to 4. So float at offset 12, size=4.
    // currentOffset = 12 + 4 = 16
    // totalSize aligned to 16 (std140 final alignment) = 16
    nlohmann::json layout = {
        {"packing", "std140"},
        {"fields", {
            {{"name", "pos"}, {"type", "vec3"}, {"source", "const.1.2.3"}},
            {{"name", "scale"}, {"type", "float"}, {"source", "const.1"}}
        }}
    };

    auto result = compiler.compile("test", layout);
    ASSERT_EQ(result.fields.size(), 2u);
    EXPECT_EQ(result.fields[0].offset, 0u);     // vec3 at 0
    EXPECT_EQ(result.fields[1].offset, 12u);    // float at 12 (after vec3's 12 bytes)
    EXPECT_EQ(result.totalSize, 16u);            // 16 aligned to 16
}

TEST(BufferLayoutCompiler, Compile_FloatThenVec3_Std140_PaddingCorrect) {
    BufferLayoutCompiler compiler;

    // float at offset 0 (align=4), size=4. currentOffset=4.
    // vec3 align=16 (std140!), so offset = 16. size=12. currentOffset = 28.
    // totalSize aligned to 16 = 32
    nlohmann::json layout = {
        {"packing", "std140"},
        {"fields", {
            {{"name", "scale"}, {"type", "float"}, {"source", "const.1"}},
            {{"name", "pos"}, {"type", "vec3"}, {"source", "const.1.2.3"}}
        }}
    };

    auto result = compiler.compile("test", layout);
    ASSERT_EQ(result.fields.size(), 2u);
    EXPECT_EQ(result.fields[0].offset, 0u);     // float at 0
    EXPECT_EQ(result.fields[1].offset, 16u);    // vec3 padded to 16 (std140 alignment)
    EXPECT_EQ(result.totalSize, 32u);            // 28 rounded up to 32
}

// ═══════════════════════════════════════════════════════════════
// compile() Tests — Mat3 Std140 Effective Size
// ═══════════════════════════════════════════════════════════════

TEST(BufferLayoutCompiler, Compile_Mat3_Std140_EffectiveSize48) {
    BufferLayoutCompiler compiler;

    // mat3 in std140 is stored as 3 vec4s = 48 bytes
    // mat3 at offset 0, effectiveSize = 48
    // currentOffset = 48
    // Then a float at align=4: offset=48, size=4. currentOffset=52
    // totalSize aligned to 16 = 64
    nlohmann::json layout = {
        {"packing", "std140"},
        {"fields", {
            {{"name", "rotation"}, {"type", "mat3"}, {"source", "const.0"}},
            {{"name", "scale"}, {"type", "float"}, {"source", "const.1"}}
        }}
    };

    auto result = compiler.compile("test", layout);
    ASSERT_EQ(result.fields.size(), 2u);
    EXPECT_EQ(result.fields[0].offset, 0u);     // mat3 at 0
    EXPECT_EQ(result.fields[1].offset, 48u);    // float after mat3's 48-byte effective size
    EXPECT_EQ(result.totalSize, 64u);            // 52 rounded up to 64
}

TEST(BufferLayoutCompiler, Compile_Mat3_Scalar_NativeSize36) {
    BufferLayoutCompiler compiler;

    // mat3 in scalar packing uses native 36 bytes
    // mat3 at offset 0, size=36. currentOffset=36
    // float at align=4: offset=36, size=4. currentOffset=40
    // totalSize aligned to 4 (scalar final alignment) = 40
    nlohmann::json layout = {
        {"packing", "scalar"},
        {"fields", {
            {{"name", "rotation"}, {"type", "mat3"}, {"source", "const.0"}},
            {{"name", "scale"}, {"type", "float"}, {"source", "const.1"}}
        }}
    };

    auto result = compiler.compile("test", layout);
    ASSERT_EQ(result.fields.size(), 2u);
    EXPECT_EQ(result.fields[0].offset, 0u);
    EXPECT_EQ(result.fields[1].offset, 36u);    // After 36-byte mat3
    EXPECT_EQ(result.totalSize, 40u);
}

// ═══════════════════════════════════════════════════════════════
// compile() Tests — Source Classification
// ═══════════════════════════════════════════════════════════════

TEST(BufferLayoutCompiler, Compile_SourceClassification_Scene) {
    BufferLayoutCompiler compiler;

    nlohmann::json layout = {
        {"fields", {
            {{"name", "view"}, {"type", "mat4"}, {"source", "scene.camera.view"}}
        }}
    };

    auto result = compiler.compile("test", layout);
    EXPECT_TRUE(result.hasSceneSources);
    EXPECT_FALSE(result.hasEntitySources);
    EXPECT_FALSE(result.hasConstSources);
}

TEST(BufferLayoutCompiler, Compile_SourceClassification_Entity) {
    BufferLayoutCompiler compiler;

    nlohmann::json layout = {
        {"fields", {
            {{"name", "model"}, {"type", "mat4"}, {"source", "entity.transform.worldMatrix"}}
        }}
    };

    auto result = compiler.compile("test", layout);
    EXPECT_FALSE(result.hasSceneSources);
    EXPECT_TRUE(result.hasEntitySources);
    EXPECT_FALSE(result.hasConstSources);
}

TEST(BufferLayoutCompiler, Compile_SourceClassification_Const) {
    BufferLayoutCompiler compiler;

    nlohmann::json layout = {
        {"fields", {
            {{"name", "zero"}, {"type", "float"}, {"source", "const.0"}}
        }}
    };

    auto result = compiler.compile("test", layout);
    EXPECT_FALSE(result.hasSceneSources);
    EXPECT_FALSE(result.hasEntitySources);
    EXPECT_TRUE(result.hasConstSources);
}

TEST(BufferLayoutCompiler, Compile_SourceClassification_Mixed) {
    BufferLayoutCompiler compiler;

    nlohmann::json layout = {
        {"fields", {
            {{"name", "view"}, {"type", "mat4"}, {"source", "scene.camera.view"}},
            {{"name", "model"}, {"type", "mat4"}, {"source", "entity.transform.worldMatrix"}},
            {{"name", "zero"}, {"type", "float"}, {"source", "const.0"}}
        }}
    };

    auto result = compiler.compile("test", layout);
    EXPECT_TRUE(result.hasSceneSources);
    EXPECT_TRUE(result.hasEntitySources);
    EXPECT_TRUE(result.hasConstSources);
}

// ═══════════════════════════════════════════════════════════════
// compile() Tests — Missing fields array
// ═══════════════════════════════════════════════════════════════

TEST(BufferLayoutCompiler, Compile_NoFieldsArray_Throws) {
    BufferLayoutCompiler compiler;
    nlohmann::json layout = {{"packing", "scalar"}};
    EXPECT_THROW(compiler.compile("test", layout), std::runtime_error);
}

// ═══════════════════════════════════════════════════════════════
// compileAll() Tests
// ═══════════════════════════════════════════════════════════════

TEST(BufferLayoutCompiler, CompileAll_MultipleLayouts) {
    BufferLayoutCompiler compiler;

    nlohmann::json layouts = {
        {"CameraUBO", {
            {"packing", "std140"},
            {"fields", {
                {{"name", "view"}, {"type", "mat4"}, {"source", "scene.camera.view"}},
                {{"name", "proj"}, {"type", "mat4"}, {"source", "scene.camera.projection"}}
            }}
        }},
        {"PushConst", {
            {"packing", "scalar"},
            {"fields", {
                {{"name", "model"}, {"type", "mat4"}, {"source", "entity.transform.worldMatrix"}}
            }}
        }}
    };

    auto result = compiler.compileAll(layouts);
    EXPECT_EQ(result.size(), 2u);
    EXPECT_TRUE(result.count("CameraUBO"));
    EXPECT_TRUE(result.count("PushConst"));

    // CameraUBO: 2 mat4s in std140 = offset 0 + 64 = 64, totalSize aligned to 16 = 128
    EXPECT_EQ(result.at("CameraUBO").fields.size(), 2u);
    EXPECT_EQ(result.at("CameraUBO").fields[0].offset, 0u);
    EXPECT_EQ(result.at("CameraUBO").fields[1].offset, 64u);
    EXPECT_EQ(result.at("CameraUBO").totalSize, 128u);

    // PushConst: 1 mat4 in scalar = offset 0, size 64, totalSize = 64
    EXPECT_EQ(result.at("PushConst").fields.size(), 1u);
    EXPECT_EQ(result.at("PushConst").totalSize, 64u);
}

// ═══════════════════════════════════════════════════════════════
// compile() Tests — Default Packing (scalar when omitted)
// ═══════════════════════════════════════════════════════════════

TEST(BufferLayoutCompiler, Compile_DefaultPacking_IsScalar) {
    BufferLayoutCompiler compiler;

    // No "packing" key → defaults to "scalar"
    nlohmann::json layout = {
        {"fields", {
            {{"name", "val"}, {"type", "float"}, {"source", "const.1"}}
        }}
    };

    auto result = compiler.compile("test", layout);
    // Scalar packing: float aligned to 4, totalSize aligned to 4
    EXPECT_EQ(result.fields[0].offset, 0u);
    EXPECT_EQ(result.totalSize, 4u);
}

// ═══════════════════════════════════════════════════════════════
// compile() Tests — Mat4 pairs in std140
// ═══════════════════════════════════════════════════════════════

TEST(BufferLayoutCompiler, Compile_TwoMat4s_Std140) {
    BufferLayoutCompiler compiler;

    nlohmann::json layout = {
        {"packing", "std140"},
        {"fields", {
            {{"name", "view"}, {"type", "mat4"}, {"source", "scene.camera.view"}},
            {{"name", "proj"}, {"type", "mat4"}, {"source", "scene.camera.projection"}}
        }}
    };

    auto result = compiler.compile("test", layout);
    ASSERT_EQ(result.fields.size(), 2u);
    EXPECT_EQ(result.fields[0].offset, 0u);     // First mat4 at 0
    EXPECT_EQ(result.fields[1].offset, 64u);    // Second mat4 at 64
    EXPECT_EQ(result.totalSize, 128u);           // 128 aligned to 16
}
