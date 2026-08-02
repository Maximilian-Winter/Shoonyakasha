//
// BufferLayoutCompilerTest.cpp - Tests for JSON buffer layout compilation
//
// Tier 1: Pure unit tests — no GPU context, no ECS
//

#include <gtest/gtest.h>
#include "FrameGraph/BufferLayoutCompiler.h"

using namespace Shoonyakasha;

// The packer speaks the shader-side type (17 members) rather than
// MaterialParam::Type (8), because that is what determines alignment. The
// std140/std430/scalar rules themselves live in FrameGraph/BufferFieldTypes.h
// and are shared with the production compiler.
using FieldType   = FrameGraph::BufferFieldType;
using PackingRule = FrameGraph::BufferPackingRule;

// ═══════════════════════════════════════════════════════════════
// parseType() Tests
// ═══════════════════════════════════════════════════════════════

TEST(BufferLayoutCompiler, ParseType_Float) {
    EXPECT_EQ(BufferLayoutCompiler::parseType("float"), FieldType::Float);
}

TEST(BufferLayoutCompiler, ParseType_Vec2) {
    EXPECT_EQ(BufferLayoutCompiler::parseType("vec2"), FieldType::Vec2);
}

TEST(BufferLayoutCompiler, ParseType_Vec3) {
    EXPECT_EQ(BufferLayoutCompiler::parseType("vec3"), FieldType::Vec3);
}

TEST(BufferLayoutCompiler, ParseType_Vec4) {
    EXPECT_EQ(BufferLayoutCompiler::parseType("vec4"), FieldType::Vec4);
}

TEST(BufferLayoutCompiler, ParseType_Mat3) {
    EXPECT_EQ(BufferLayoutCompiler::parseType("mat3"), FieldType::Mat3);
}

TEST(BufferLayoutCompiler, ParseType_Mat4) {
    EXPECT_EQ(BufferLayoutCompiler::parseType("mat4"), FieldType::Mat4);
}

TEST(BufferLayoutCompiler, ParseType_Int) {
    EXPECT_EQ(BufferLayoutCompiler::parseType("int"), FieldType::Int);
}

TEST(BufferLayoutCompiler, ParseType_UInt) {
    EXPECT_EQ(BufferLayoutCompiler::parseType("uint"), FieldType::UInt);
}

TEST(BufferLayoutCompiler, ParseType_Unknown_Throws) {
    EXPECT_THROW(BufferLayoutCompiler::parseType("bogus"), std::runtime_error);
}

// ═══════════════════════════════════════════════════════════════
// getTypeSize() Tests
// ═══════════════════════════════════════════════════════════════

TEST(BufferLayoutCompiler, TypeSize_Float) { EXPECT_EQ(BufferLayoutCompiler::nativeSize(FieldType::Float), 4u); }
TEST(BufferLayoutCompiler, TypeSize_Vec2)  { EXPECT_EQ(BufferLayoutCompiler::nativeSize(FieldType::Vec2), 8u); }
TEST(BufferLayoutCompiler, TypeSize_Vec3)  { EXPECT_EQ(BufferLayoutCompiler::nativeSize(FieldType::Vec3), 12u); }
TEST(BufferLayoutCompiler, TypeSize_Vec4)  { EXPECT_EQ(BufferLayoutCompiler::nativeSize(FieldType::Vec4), 16u); }
TEST(BufferLayoutCompiler, TypeSize_Mat3)  { EXPECT_EQ(BufferLayoutCompiler::nativeSize(FieldType::Mat3), 36u); }
TEST(BufferLayoutCompiler, TypeSize_Mat4)  { EXPECT_EQ(BufferLayoutCompiler::nativeSize(FieldType::Mat4), 64u); }
TEST(BufferLayoutCompiler, TypeSize_Int)   { EXPECT_EQ(BufferLayoutCompiler::nativeSize(FieldType::Int), 4u); }
TEST(BufferLayoutCompiler, TypeSize_UInt)  { EXPECT_EQ(BufferLayoutCompiler::nativeSize(FieldType::UInt), 4u); }

// ═══════════════════════════════════════════════════════════════
// getTypeAlignment() Tests
// ═══════════════════════════════════════════════════════════════

// Scalar packing — aligned to natural size (4 for <=4 bytes, 8 for <=8, 16 for larger)
TEST(BufferLayoutCompiler, Alignment_Scalar_Float) {
    EXPECT_EQ(BufferLayoutCompiler::baseAlignment(FieldType::Float, PackingRule::Scalar), 4u);
}

// Scalar layout (VK_EXT_scalar_block_layout) aligns to the *component* type, so
// every float-based vector and matrix aligns to 4. Not a typo: the old
// size-bucket rule here matched neither scalar nor std140.
TEST(BufferLayoutCompiler, Alignment_Scalar_Vec2) {
    EXPECT_EQ(BufferLayoutCompiler::baseAlignment(FieldType::Vec2, PackingRule::Scalar), 4u);
}

TEST(BufferLayoutCompiler, Alignment_Scalar_Vec3) {
    EXPECT_EQ(BufferLayoutCompiler::baseAlignment(FieldType::Vec3, PackingRule::Scalar), 4u);
}

TEST(BufferLayoutCompiler, Alignment_Scalar_Vec4) {
    EXPECT_EQ(BufferLayoutCompiler::baseAlignment(FieldType::Vec4, PackingRule::Scalar), 4u);
}

TEST(BufferLayoutCompiler, Alignment_Scalar_Mat4) {
    EXPECT_EQ(BufferLayoutCompiler::baseAlignment(FieldType::Mat4, PackingRule::Scalar), 4u);
}

// Std140 packing — vec3 aligned to 16, vec2 to 8
TEST(BufferLayoutCompiler, Alignment_Std140_Float) {
    EXPECT_EQ(BufferLayoutCompiler::baseAlignment(FieldType::Float, PackingRule::Std140), 4u);
}

TEST(BufferLayoutCompiler, Alignment_Std140_Vec2) {
    EXPECT_EQ(BufferLayoutCompiler::baseAlignment(FieldType::Vec2, PackingRule::Std140), 8u);
}

TEST(BufferLayoutCompiler, Alignment_Std140_Vec3) {
    // vec3 aligns to 16 in std140!
    EXPECT_EQ(BufferLayoutCompiler::baseAlignment(FieldType::Vec3, PackingRule::Std140), 16u);
}

TEST(BufferLayoutCompiler, Alignment_Std140_Vec4) {
    EXPECT_EQ(BufferLayoutCompiler::baseAlignment(FieldType::Vec4, PackingRule::Std140), 16u);
}

TEST(BufferLayoutCompiler, Alignment_Std140_Mat3) {
    EXPECT_EQ(BufferLayoutCompiler::baseAlignment(FieldType::Mat3, PackingRule::Std140), 16u);
}

TEST(BufferLayoutCompiler, Alignment_Std140_Mat4) {
    EXPECT_EQ(BufferLayoutCompiler::baseAlignment(FieldType::Mat4, PackingRule::Std140), 16u);
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

// Refuses to guess. The two JSON front-ends previously defaulted an
// unrecognised value differently — scalar here, std140 in FrameGraphJson.
TEST(BufferLayoutCompiler, ParsePackingRule_Unknown_Throws) {
    EXPECT_THROW(BufferLayoutCompiler::parsePackingRule("unknown"), std::runtime_error);
}

TEST(BufferLayoutCompiler, ParsePackingRule_PushConstant) {
    EXPECT_EQ(BufferLayoutCompiler::parsePackingRule("push_constant"), PackingRule::PushConstant);
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

TEST(BufferLayoutCompiler, Compile_DefaultPacking_IsStd140) {
    BufferLayoutCompiler compiler;

    // No "packing" key -> std140, matching FrameGraphJson.cpp. This class used
    // to default to scalar, so identical JSON compiled two different ways
    // depending on which front-end read it.
    nlohmann::json layout = {
        {"fields", {
            {{"name", "val"}, {"type", "float"}, {"source", "const.1"}}
        }}
    };

    auto result = compiler.compile("test", layout);
    // std140 rounds the block to 16
    EXPECT_EQ(result.fields[0].offset, 0u);
    EXPECT_EQ(result.totalSize, 16u);
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

// ═══════════════════════════════════════════════════════════════
// std430 — previously zero behavioural coverage
//
// std430 had no distinct implementation at all: it shared the std140 alignment
// table but not its matrix handling, and in the production compiler it fell into
// a branch that applied no alignment whatsoever. These cases pin the places it
// does and does not differ from std140.
// ═══════════════════════════════════════════════════════════════

namespace {
nlohmann::json layoutOf(const char* packing,
                        std::initializer_list<std::pair<const char*, const char*>> fields,
                        int arrayCount = 0) {
    nlohmann::json js = nlohmann::json::array();
    for (auto& [name, type] : fields) {
        nlohmann::json f = {{"name", name}, {"type", type}, {"source", "const.0"}};
        if (arrayCount > 0) f["arrayCount"] = arrayCount;
        js.push_back(f);
    }
    return nlohmann::json{{"packing", packing}, {"fields", js}};
}
} // namespace

// The single sharpest difference between the two rules: std140 rounds every
// array element up to 16 bytes, std430 does not.
TEST(BufferLayoutCompiler, Compile_FloatArray_Std430_Stride4) {
    BufferLayoutCompiler compiler;
    auto r = compiler.compile("t", layoutOf("std430", {{"x", "float"}}, 4));
    EXPECT_EQ(r.fields[0].arrayStride, 4u);
    EXPECT_EQ(r.totalSize, 16u);
}

TEST(BufferLayoutCompiler, Compile_FloatArray_Std140_Stride16) {
    BufferLayoutCompiler compiler;
    auto r = compiler.compile("t", layoutOf("std140", {{"x", "float"}}, 4));
    EXPECT_EQ(r.fields[0].arrayStride, 16u);
    EXPECT_EQ(r.totalSize, 64u);
}

// The most common misconception is that std430 "packs tighter" and therefore
// makes mat3 36 bytes. It does not: mat3 is three vec3 columns at 16-byte
// stride under both rules.
TEST(BufferLayoutCompiler, Compile_Mat3_Std430_Occupies48) {
    BufferLayoutCompiler compiler;
    auto r = compiler.compile("t", layoutOf("std430", {{"rot", "mat3"}, {"scale", "float"}}));
    EXPECT_EQ(r.fields[0].offset, 0u);
    EXPECT_EQ(r.fields[0].size, 48u);
    EXPECT_EQ(r.fields[0].columnStride, 16u);
    EXPECT_EQ(r.fields[1].offset, 48u);
}

// std430 does not relax vec3 either: base alignment is 16 in both. The expected
// values match the std140 case.
TEST(BufferLayoutCompiler, Compile_Vec3_Std430_StillAligns16) {
    BufferLayoutCompiler compiler;
    auto r = compiler.compile("t", layoutOf("std430", {{"a", "float"}, {"b", "vec3"}}));
    EXPECT_EQ(r.fields[0].offset, 0u);
    EXPECT_EQ(r.fields[1].offset, 16u);
    EXPECT_EQ(r.fields[1].size, 12u) << "vec3 occupies 12; its tail padding belongs to the next field";
}

// mat2 is the one type whose column stride genuinely differs.
TEST(BufferLayoutCompiler, Compile_Mat2_ColumnStrideDiffersBetweenRules) {
    BufferLayoutCompiler compiler;

    auto s430 = compiler.compile("t", layoutOf("std430", {{"m", "mat2"}}));
    EXPECT_EQ(s430.fields[0].columnStride, 8u);
    EXPECT_EQ(s430.fields[0].size, 16u);

    auto s140 = compiler.compile("t", layoutOf("std140", {{"m", "mat2"}}));
    EXPECT_EQ(s140.fields[0].columnStride, 16u);
    EXPECT_EQ(s140.fields[0].size, 32u);
}

// std430 rounds the block to the largest member alignment; std140 always to 16.
// For an SSBO this value is also the array-of-structs element stride.
TEST(BufferLayoutCompiler, Compile_Std430_BlockRoundsToMaxMemberAlignment) {
    BufferLayoutCompiler compiler;

    auto withVec4 = compiler.compile("t", layoutOf("std430", {{"a", "vec4"}, {"b", "float"}}));
    EXPECT_EQ(withVec4.totalSize, 32u) << "cursor 20, max member alignment 16";

    auto scalarsOnly = compiler.compile("t", layoutOf("std430", {{"a", "float"}, {"b", "float"}}));
    EXPECT_EQ(scalarsOnly.totalSize, 8u) << "max member alignment 4, so no rounding to 16";
}

// An explicitly declared offset of 0 used to be indistinguishable from "unset".
TEST(BufferLayoutCompiler, Compile_ExplicitOffsetIsHonoured) {
    BufferLayoutCompiler compiler;
    nlohmann::json layout = {
        {"packing", "std140"},
        {"fields", {
            {{"name", "a"}, {"type", "float"}, {"source", "const.0"}, {"offset", 0}},
            {{"name", "b"}, {"type", "float"}, {"source", "const.1"}, {"offset", 64}}
        }}
    };
    auto r = compiler.compile("t", layout);
    EXPECT_EQ(r.fields[0].offset, 0u);
    EXPECT_EQ(r.fields[1].offset, 64u);
    EXPECT_EQ(r.totalSize, 80u);
}

// Types the dot-path resolver cannot produce still pack correctly; they are
// flagged so the resolver leaves them zeroed rather than writing a float into
// them, which is what the old silent default did on every draw.
TEST(BufferLayoutCompiler, Compile_UnresolvableType_PacksButIsFlagged) {
    BufferLayoutCompiler compiler;
    auto r = compiler.compile("t", layoutOf("std430", {{"ids", "uvec4"}, {"f", "float"}}));
    EXPECT_EQ(r.fields[0].offset, 0u);
    EXPECT_EQ(r.fields[0].size, 16u);
    EXPECT_FALSE(r.fields[0].resolvable);
    EXPECT_TRUE(r.fields[1].resolvable);
}

// push_constant is std430: plain `layout(push_constant)` in Vulkan GLSL uses
// std430, and every shipped shader declares it that way.
TEST(BufferLayoutCompiler, Compile_PushConstant_FollowsStd430) {
    BufferLayoutCompiler compiler;
    auto pc  = compiler.compile("t", layoutOf("push_constant", {{"x", "float"}}, 4));
    auto s43 = compiler.compile("t", layoutOf("std430",        {{"x", "float"}}, 4));
    EXPECT_EQ(pc.fields[0].arrayStride, s43.fields[0].arrayStride);
    EXPECT_EQ(pc.totalSize, s43.totalSize);
}
