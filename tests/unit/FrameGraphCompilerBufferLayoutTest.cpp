// tests/unit/FrameGraphCompilerBufferLayoutTest.cpp
//
// Golden byte offsets for the buffer layouts that ship in the example pipelines.
//
// FrameGraphCompiler::compileBufferLayouts is the production packer: the offsets it
// produces are the contract between the JSON, the GLSL that reads the buffer, and
// DotPathResolver, which writes into it. Until now it had no test coverage at all
// (tests/unit/BufferLayoutCompilerTest.cpp exercises a separate header that the
// engine does not call on any live path).
//
// These cases were written against the *current* implementation and confirmed green
// before any packing changes were made. They exist so that the upcoming std140/std430
// correctness work can be shown to be behaviour-preserving for shipped content: if an
// assertion here moves, real pipelines changed layout and the shaders no longer agree
// with the buffers.
//
// Sources, verbatim field lists:
//   examples/declarative_sponza_test/pbr_ibl_pipeline_v3.json
//   examples/full_showcase/showcase_pipeline.json

#include <gtest/gtest.h>
#include "Vulkan/FrameGraph/FrameGraph.h"

using namespace Shoonyakasha::FrameGraph;

namespace {

BufferFieldDesc field(const char* name, BufferFieldType type, uint32_t arrayCount = 1) {
    BufferFieldDesc f;
    f.name = name;
    f.type = type;
    f.arrayCount = arrayCount;
    return f;
}

/// Compile one layout and hand back the result. Takes the layout by value so each
/// test reads as a single self-contained description of a shipped buffer.
CompiledBufferLayout compileOne(BufferLayoutDesc desc) {
    FrameGraphCompiler compiler;
    std::unordered_map<std::string, CompiledBufferLayout> out;
    compiler.compileBufferLayouts({std::move(desc)}, out);

    auto it = out.begin();
    EXPECT_NE(it, out.end()) << "layout was dropped during compilation";
    return it == out.end() ? CompiledBufferLayout{} : it->second;
}

/// Assert the full offset vector in one go, so a failure reports every field rather
/// than stopping at the first.
void expectOffsets(const CompiledBufferLayout& layout,
                   const std::vector<uint32_t>& expected) {
    ASSERT_EQ(layout.fields.size(), expected.size());
    for (size_t i = 0; i < expected.size(); ++i) {
        EXPECT_EQ(layout.fields[i].offset, expected[i])
            << "field[" << i << "] '" << layout.fields[i].name << "'";
    }
}

} // namespace

// ── CameraUBO — std140, six 16-byte-aligned members ──────────────────────────
// 4 mat4 (64 each) then 2 vec4 (16 each). No padding anywhere: every member is
// already a multiple of its own alignment.
TEST(FrameGraphCompilerBufferLayout, CameraUBO_Std140) {
    BufferLayoutDesc desc;
    desc.name = "CameraUBO";
    desc.usage = BufferUsageType::UniformBuffer;
    desc.packing = BufferPackingRule::Std140;
    desc.fields = {
        field("view",     BufferFieldType::Mat4),
        field("proj",     BufferFieldType::Mat4),
        field("invView",  BufferFieldType::Mat4),
        field("invProj",  BufferFieldType::Mat4),
        field("position", BufferFieldType::Vec4),
        field("params",   BufferFieldType::Vec4),
    };

    auto layout = compileOne(desc);
    expectOffsets(layout, {0, 64, 128, 192, 256, 272});
    EXPECT_EQ(layout.totalSize, 288u);
}

// ── LightsUBO — std140, scalar header then four vec4[16] arrays ───────────────
// The interesting part: a uint followed by three floats packs tightly at 0/4/8/12,
// then the first array member is bumped to 16 by the vec4 alignment. Array stride
// is 16 under std140's round-up-to-vec4 rule, so each array occupies 256 bytes.
TEST(FrameGraphCompilerBufferLayout, LightsUBO_Std140_WithVec4Arrays) {
    BufferLayoutDesc desc;
    desc.name = "LightsUBO";
    desc.usage = BufferUsageType::UniformBuffer;
    desc.packing = BufferPackingRule::Std140;
    desc.fields = {
        field("lightCount",           BufferFieldType::UInt),
        field("padding1",             BufferFieldType::Float),
        field("padding2",             BufferFieldType::Float),
        field("padding3",             BufferFieldType::Float),
        field("lightsPositionType",   BufferFieldType::Vec4, 16),
        field("lightsColorIntensity", BufferFieldType::Vec4, 16),
        field("lightsDirectionRange", BufferFieldType::Vec4, 16),
        field("lightsAttenuation",    BufferFieldType::Vec4, 16),
    };

    auto layout = compileOne(desc);
    expectOffsets(layout, {0, 4, 8, 12, 16, 272, 528, 784});
    EXPECT_EQ(layout.totalSize, 1040u);

    // Stride is what the shader's array indexing depends on; assert it explicitly.
    EXPECT_EQ(layout.fields[4].arrayStride, 16u);
    EXPECT_EQ(layout.fields[7].arrayStride, 16u);
}

// ── MaterialPushConstants — the per-draw push constant block ──────────────────
// Declared "scalar" in JSON. Every member here happens to sit on its natural
// alignment, so std140, std430 and true scalar layout all agree — which is why
// this block is safe ground while the packing rules are corrected.
// 104 bytes, comfortably inside the 128-byte guaranteed push constant limit.
TEST(FrameGraphCompilerBufferLayout, MaterialPushConstants_Scalar) {
    BufferLayoutDesc desc;
    desc.name = "MaterialPushConstants";
    desc.usage = BufferUsageType::PushConstant;
    desc.packing = BufferPackingRule::Scalar;
    desc.fields = {
        field("model",            BufferFieldType::Mat4),
        field("baseColorFactor",  BufferFieldType::Vec4),
        field("metallicFactor",   BufferFieldType::Float),
        field("roughnessFactor",  BufferFieldType::Float),
        field("hasNormalMap",     BufferFieldType::Float),
        field("hasMetalRoughMap", BufferFieldType::Float),
        field("alphaCutoff",      BufferFieldType::Float),
        field("padding1",         BufferFieldType::Float),
    };

    auto layout = compileOne(desc);
    expectOffsets(layout, {0, 64, 80, 84, 88, 92, 96, 100});
    EXPECT_EQ(layout.totalSize, 104u);
    EXPECT_LE(layout.totalSize, 128u) << "exceeds the guaranteed push constant limit";
}

// ── particleSSBO — std430 array-of-structs element ────────────────────────────
// totalSize doubles as the element stride for the SSBO array, so it is the value
// the compute shader's indexing depends on.
TEST(FrameGraphCompilerBufferLayout, ParticleSSBO_Std430) {
    BufferLayoutDesc desc;
    desc.name = "particleSSBO";
    desc.usage = BufferUsageType::StorageBuffer;
    desc.packing = BufferPackingRule::Std430;
    desc.fields = {
        field("position", BufferFieldType::Vec4),
        field("velocity", BufferFieldType::Vec4),
    };

    auto layout = compileOne(desc);
    expectOffsets(layout, {0, 16});
    EXPECT_EQ(layout.totalSize, 32u);
}

// ── SpritePerDraw — the second shipped push constant block ────────────────────
TEST(FrameGraphCompilerBufferLayout, SpritePerDraw_Scalar) {
    BufferLayoutDesc desc;
    desc.name = "SpritePerDraw";
    desc.usage = BufferUsageType::PushConstant;
    desc.packing = BufferPackingRule::Scalar;
    desc.fields = {
        field("model",       BufferFieldType::Mat4),
        field("tintColor",   BufferFieldType::Vec4),
        field("uvRect",      BufferFieldType::Vec4),
        field("screenSpace", BufferFieldType::Float),
        field("padding1",    BufferFieldType::Float),
        field("padding2",    BufferFieldType::Float),
        field("padding3",    BufferFieldType::Float),
    };

    auto layout = compileOne(desc);
    expectOffsets(layout, {0, 64, 80, 96, 100, 104, 108});
    EXPECT_EQ(layout.totalSize, 112u);
    EXPECT_LE(layout.totalSize, 128u) << "exceeds the guaranteed push constant limit";
}

// ── particleSimParams — std140 UBO mixing scalars and vec4s ───────────────────
// The one shipped layout where std140 padding is actually load-bearing: the two
// vec4s force alignment bumps that the scalar members around them do not.
TEST(FrameGraphCompilerBufferLayout, ParticleSimParams_Std140) {
    BufferLayoutDesc desc;
    desc.name = "particleSimParams";
    desc.usage = BufferUsageType::UniformBuffer;
    desc.packing = BufferPackingRule::Std140;
    desc.fields = {
        field("deltaTime",      BufferFieldType::Float),
        field("gravity",        BufferFieldType::Float),
        field("particleCount",  BufferFieldType::UInt),
        field("boundaryRadius", BufferFieldType::Float),
        field("attractorPos",   BufferFieldType::Vec4),
        field("wind",           BufferFieldType::Vec4),
        field("damping",        BufferFieldType::Float),
        field("spawnHeight",    BufferFieldType::Float),
        field("padding1",       BufferFieldType::Float),
        field("padding2",       BufferFieldType::Float),
    };

    auto layout = compileOne(desc);
    expectOffsets(layout, {0, 4, 8, 12, 16, 32, 48, 52, 56, 60});
    EXPECT_EQ(layout.totalSize, 64u);
}

// ── Guard: a descriptor_set layout carries no fields and no size ──────────────
TEST(FrameGraphCompilerBufferLayout, DescriptorSetLayout_HasNoSize) {
    BufferLayoutDesc desc;
    desc.name = "MaterialTextures";
    desc.usage = BufferUsageType::DescriptorSet;

    TextureBindingDesc tex;
    tex.name = "albedoMap";
    tex.binding = 0;
    desc.textures = {tex};

    auto layout = compileOne(desc);
    EXPECT_EQ(layout.totalSize, 0u);
    EXPECT_TRUE(layout.fields.empty());
    EXPECT_EQ(layout.textures.size(), 1u);
}
