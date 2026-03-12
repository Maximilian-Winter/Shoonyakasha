//
// RenderComponentsTest.cpp - Tests for MaterialComponentV5, MeshComponent, RenderableTagComponent
//
// Tier 2: ECS data components — no GPU (uses dummy handles for validity)
//

#include <gtest/gtest.h>
#include "ECS/RenderComponents.h"
#include "TestHelpers.h"

using namespace Shoonyakasha;

// ═══════════════════════════════════════════════════════════════
// MaterialComponentV5 — Parameter Access
// ═══════════════════════════════════════════════════════════════

TEST(MaterialComponentV5, SetParam_GetParam_Float_RoundTrip) {
    MaterialComponentV5 mat;
    mat.setParam("roughness", 0.5f);
    EXPECT_FLOAT_EQ(mat.getParam<float>("roughness"), 0.5f);
}

TEST(MaterialComponentV5, SetParam_GetParam_Vec4_RoundTrip) {
    MaterialComponentV5 mat;
    glm::vec4 color(1.0f, 0.0f, 0.0f, 1.0f);
    mat.setParam("baseColorFactor", color);
    TestHelpers::ExpectVec4Near(mat.getParam<glm::vec4>("baseColorFactor"), color);
}

TEST(MaterialComponentV5, SetParam_GetParam_Mat4_RoundTrip) {
    MaterialComponentV5 mat;
    glm::mat4 m(2.0f);
    mat.setParam("transform", m);
    TestHelpers::ExpectMat4Near(mat.getParam<glm::mat4>("transform"), m);
}

TEST(MaterialComponentV5, HasParam_Exists_True) {
    MaterialComponentV5 mat;
    mat.setParam("metallic", 1.0f);
    EXPECT_TRUE(mat.hasParam("metallic"));
}

TEST(MaterialComponentV5, HasParam_Missing_False) {
    MaterialComponentV5 mat;
    EXPECT_FALSE(mat.hasParam("nonexistent"));
}

TEST(MaterialComponentV5, GetParam_Missing_ReturnsDefault) {
    MaterialComponentV5 mat;
    EXPECT_FLOAT_EQ(mat.getParam<float>("missing", 42.0f), 42.0f);
}

TEST(MaterialComponentV5, SetParam_Overwrite) {
    MaterialComponentV5 mat;
    mat.setParam("roughness", 0.5f);
    mat.setParam("roughness", 0.8f);
    EXPECT_FLOAT_EQ(mat.getParam<float>("roughness"), 0.8f);
}

// ═══════════════════════════════════════════════════════════════
// MaterialComponentV5 — Alpha Mode Classification
// ═══════════════════════════════════════════════════════════════

TEST(MaterialComponentV5, DefaultAlphaMode_Opaque) {
    MaterialComponentV5 mat;
    EXPECT_EQ(mat.alphaMode, AlphaMode::Opaque);
}

TEST(MaterialComponentV5, IsOpaque) {
    MaterialComponentV5 mat;
    mat.alphaMode = AlphaMode::Opaque;
    EXPECT_TRUE(mat.isOpaque());
    EXPECT_FALSE(mat.isMasked());
    EXPECT_FALSE(mat.isTransparent());
}

TEST(MaterialComponentV5, IsMasked) {
    MaterialComponentV5 mat;
    mat.alphaMode = AlphaMode::Mask;
    EXPECT_FALSE(mat.isOpaque());
    EXPECT_TRUE(mat.isMasked());
    EXPECT_FALSE(mat.isTransparent());
}

TEST(MaterialComponentV5, IsTransparent) {
    MaterialComponentV5 mat;
    mat.alphaMode = AlphaMode::Blend;
    EXPECT_FALSE(mat.isOpaque());
    EXPECT_FALSE(mat.isMasked());
    EXPECT_TRUE(mat.isTransparent());
}

TEST(MaterialComponentV5, IsOpaqueOrMasked_Opaque) {
    MaterialComponentV5 mat;
    mat.alphaMode = AlphaMode::Opaque;
    EXPECT_TRUE(mat.isOpaqueOrMasked());
}

TEST(MaterialComponentV5, IsOpaqueOrMasked_Mask) {
    MaterialComponentV5 mat;
    mat.alphaMode = AlphaMode::Mask;
    EXPECT_TRUE(mat.isOpaqueOrMasked());
}

TEST(MaterialComponentV5, IsOpaqueOrMasked_Blend_False) {
    MaterialComponentV5 mat;
    mat.alphaMode = AlphaMode::Blend;
    EXPECT_FALSE(mat.isOpaqueOrMasked());
}

// ═══════════════════════════════════════════════════════════════
// MaterialComponentV5 — Texture Access
// ═══════════════════════════════════════════════════════════════

TEST(MaterialComponentV5, GetTexture_NotFound_Null) {
    MaterialComponentV5 mat;
    EXPECT_EQ(mat.getTexture("albedoMap"), nullptr);
}

TEST(MaterialComponentV5, HasTexture_NoTexture_False) {
    MaterialComponentV5 mat;
    EXPECT_FALSE(mat.hasTexture("normalMap"));
}

TEST(MaterialComponentV5, HasTexture_ExistsButNotMarked_False) {
    MaterialComponentV5 mat;
    GPUTexture tex{};
    tex.exists = false;
    mat.textures["albedo"] = tex;
    EXPECT_FALSE(mat.hasTexture("albedo"));
}

TEST(MaterialComponentV5, HasTexture_ExistsAndMarked_True) {
    MaterialComponentV5 mat;
    GPUTexture tex{};
    tex.exists = true;
    mat.textures["albedo"] = tex;
    EXPECT_TRUE(mat.hasTexture("albedo"));
}

// ═══════════════════════════════════════════════════════════════
// MaterialComponentV5 — Default Values
// ═══════════════════════════════════════════════════════════════

TEST(MaterialComponentV5, DefaultAlphaCutoff) {
    MaterialComponentV5 mat;
    EXPECT_FLOAT_EQ(mat.alphaCutoff, 0.5f);
}

TEST(MaterialComponentV5, DefaultDoubleSided_False) {
    MaterialComponentV5 mat;
    EXPECT_FALSE(mat.doubleSided);
}

// ═══════════════════════════════════════════════════════════════
// MeshComponent Tests
// ═══════════════════════════════════════════════════════════════

TEST(MeshComponent, Default_Invalid) {
    MeshComponent mesh;
    EXPECT_FALSE(mesh.isValid());
    EXPECT_FALSE(mesh.hasIndices());
}

TEST(MeshComponent, HasIndices_WithValidIndexBuffer) {
    MeshComponent mesh;
    mesh.indexBuffer.buffer = TestHelpers::dummyVkBuffer();
    mesh.indexCount = 36;
    EXPECT_TRUE(mesh.hasIndices());
}

TEST(MeshComponent, HasIndices_NoIndexCount_False) {
    MeshComponent mesh;
    mesh.indexBuffer.buffer = TestHelpers::dummyVkBuffer();
    mesh.indexCount = 0;
    EXPECT_FALSE(mesh.hasIndices());
}

TEST(MeshComponent, IsValid_WithVertexBuffer) {
    MeshComponent mesh;
    mesh.vertexBuffer.buffer = TestHelpers::dummyVkBuffer();
    mesh.vertexCount = 100;
    EXPECT_TRUE(mesh.isValid());
}

TEST(MeshComponent, IsValid_NoVertexCount_False) {
    MeshComponent mesh;
    mesh.vertexBuffer.buffer = TestHelpers::dummyVkBuffer();
    mesh.vertexCount = 0;
    EXPECT_FALSE(mesh.isValid());
}

// ═══════════════════════════════════════════════════════════════
// RenderableTagComponent Tests
// ═══════════════════════════════════════════════════════════════

TEST(RenderableTagComponent, Default_ShouldRender_True) {
    RenderableTagComponent tag;
    EXPECT_TRUE(tag.shouldRender());
}

TEST(RenderableTagComponent, NotVisible_ShouldRender_False) {
    RenderableTagComponent tag;
    tag.visible = false;
    EXPECT_FALSE(tag.shouldRender());
}

TEST(RenderableTagComponent, DefaultValues) {
    RenderableTagComponent tag;
    EXPECT_TRUE(tag.visible);
    EXPECT_TRUE(tag.castShadows);
    EXPECT_TRUE(tag.receiveShadows);
    EXPECT_EQ(tag.renderLayerMask, 0xFF);
    EXPECT_EQ(tag.sortKey, 0u);
}
