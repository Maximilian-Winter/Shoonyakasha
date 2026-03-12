//
// GPUTypesTest.cpp - Tests for GPU type wrappers
//
// Tier 1: Pure unit tests — no GPU context, no EnTT
//

#include <gtest/gtest.h>
#include "GPU/GPUTypes.h"
#include "TestHelpers.h"

using namespace Shoonyakasha;

// ═══════════════════════════════════════════════════════════════
// MaterialParam Factory + Round-Trip Tests
// ═══════════════════════════════════════════════════════════════

TEST(MaterialParam, FromFloat_RoundTrip) {
    auto p = MaterialParam::from(3.14f);
    EXPECT_EQ(p.type, MaterialParam::Type::Float);
    EXPECT_FLOAT_EQ(p.as<float>(), 3.14f);
}

TEST(MaterialParam, FromVec2_RoundTrip) {
    glm::vec2 v(1.0f, 2.0f);
    auto p = MaterialParam::from(v);
    EXPECT_EQ(p.type, MaterialParam::Type::Vec2);
    TestHelpers::ExpectVec2Near(p.as<glm::vec2>(), v);
}

TEST(MaterialParam, FromVec3_RoundTrip) {
    glm::vec3 v(1.0f, 2.0f, 3.0f);
    auto p = MaterialParam::from(v);
    EXPECT_EQ(p.type, MaterialParam::Type::Vec3);
    TestHelpers::ExpectVec3Near(p.as<glm::vec3>(), v);
}

TEST(MaterialParam, FromVec4_RoundTrip) {
    glm::vec4 v(1.0f, 2.0f, 3.0f, 4.0f);
    auto p = MaterialParam::from(v);
    EXPECT_EQ(p.type, MaterialParam::Type::Vec4);
    TestHelpers::ExpectVec4Near(p.as<glm::vec4>(), v);
}

TEST(MaterialParam, FromMat3_RoundTrip) {
    glm::mat3 m(
        1, 2, 3,
        4, 5, 6,
        7, 8, 9
    );
    auto p = MaterialParam::from(m);
    EXPECT_EQ(p.type, MaterialParam::Type::Mat3);
    TestHelpers::ExpectMat3Near(p.as<glm::mat3>(), m);
}

TEST(MaterialParam, FromMat4_RoundTrip) {
    glm::mat4 m(
        1, 2, 3, 4,
        5, 6, 7, 8,
        9, 10, 11, 12,
        13, 14, 15, 16
    );
    auto p = MaterialParam::from(m);
    EXPECT_EQ(p.type, MaterialParam::Type::Mat4);
    TestHelpers::ExpectMat4Near(p.as<glm::mat4>(), m);
}

TEST(MaterialParam, FromInt_RoundTrip) {
    auto p = MaterialParam::from(int32_t(-42));
    EXPECT_EQ(p.type, MaterialParam::Type::Int);
    EXPECT_EQ(p.as<int32_t>(), -42);
}

TEST(MaterialParam, FromUInt_RoundTrip) {
    auto p = MaterialParam::from(uint32_t(999));
    EXPECT_EQ(p.type, MaterialParam::Type::UInt);
    EXPECT_EQ(p.as<uint32_t>(), 999u);
}

// ═══════════════════════════════════════════════════════════════
// MaterialParam ByteSize Tests
// ═══════════════════════════════════════════════════════════════

TEST(MaterialParam, ByteSize_Float) { EXPECT_EQ(MaterialParam::from(1.0f).byteSize(), 4u); }
TEST(MaterialParam, ByteSize_Vec2)  { EXPECT_EQ(MaterialParam::from(glm::vec2(0)).byteSize(), 8u); }
TEST(MaterialParam, ByteSize_Vec3)  { EXPECT_EQ(MaterialParam::from(glm::vec3(0)).byteSize(), 12u); }
TEST(MaterialParam, ByteSize_Vec4)  { EXPECT_EQ(MaterialParam::from(glm::vec4(0)).byteSize(), 16u); }
TEST(MaterialParam, ByteSize_Mat3)  { EXPECT_EQ(MaterialParam::from(glm::mat3(1)).byteSize(), 36u); }
TEST(MaterialParam, ByteSize_Mat4)  { EXPECT_EQ(MaterialParam::from(glm::mat4(1)).byteSize(), 64u); }
TEST(MaterialParam, ByteSize_Int)   { EXPECT_EQ(MaterialParam::from(int32_t(0)).byteSize(), 4u); }
TEST(MaterialParam, ByteSize_UInt)  { EXPECT_EQ(MaterialParam::from(uint32_t(0)).byteSize(), 4u); }

// ═══════════════════════════════════════════════════════════════
// MaterialParam RawData
// ═══════════════════════════════════════════════════════════════

TEST(MaterialParam, RawData_NotNull) {
    auto p = MaterialParam::from(1.0f);
    EXPECT_NE(p.rawData(), nullptr);
}

TEST(MaterialParam, RawData_ConstNotNull) {
    const auto p = MaterialParam::from(1.0f);
    EXPECT_NE(p.rawData(), nullptr);
}

TEST(MaterialParam, RawData_ContainsCorrectBytes) {
    float val = 42.0f;
    auto p = MaterialParam::from(val);
    float readBack;
    std::memcpy(&readBack, p.rawData(), sizeof(float));
    EXPECT_FLOAT_EQ(readBack, val);
}

// ═══════════════════════════════════════════════════════════════
// GPUBuffer Tests
// ═══════════════════════════════════════════════════════════════

TEST(GPUBuffer, DefaultInvalid) {
    GPUBuffer buf{};
    EXPECT_FALSE(buf.isValid());
    EXPECT_EQ(buf.buffer, VK_NULL_HANDLE);
    EXPECT_EQ(buf.allocation, nullptr);
    EXPECT_EQ(buf.size, 0u);
}

TEST(GPUBuffer, ValidWithDummyHandle) {
    GPUBuffer buf{};
    buf.buffer = TestHelpers::dummyVkBuffer();
    buf.size = 1024;
    EXPECT_TRUE(buf.isValid());
    EXPECT_EQ(buf.size, 1024u);
}

TEST(GPUBuffer, Reset_ClearsAll) {
    GPUBuffer buf{};
    buf.buffer = TestHelpers::dummyVkBuffer();
    buf.size = 1024;
    EXPECT_TRUE(buf.isValid());

    buf.reset();
    EXPECT_FALSE(buf.isValid());
    EXPECT_EQ(buf.buffer, VK_NULL_HANDLE);
    EXPECT_EQ(buf.allocation, nullptr);
    EXPECT_EQ(buf.size, 0u);
}

// ═══════════════════════════════════════════════════════════════
// GPUTexture Tests
// ═══════════════════════════════════════════════════════════════

TEST(GPUTexture, DefaultInvalid) {
    GPUTexture tex{};
    EXPECT_FALSE(tex.isValid());
    EXPECT_FALSE(tex.exists);
    EXPECT_EQ(tex.width, 0u);
    EXPECT_EQ(tex.height, 0u);
    EXPECT_EQ(tex.format, VK_FORMAT_UNDEFINED);
}

TEST(GPUTexture, ValidRequiresAllThreeHandles) {
    GPUTexture tex{};
    // Only image — not valid
    tex.image = TestHelpers::dummyVkImage();
    EXPECT_FALSE(tex.isValid());

    // Image + view — still not valid
    tex.view = TestHelpers::dummyVkImageView();
    EXPECT_FALSE(tex.isValid());

    // All three — valid
    tex.sampler = TestHelpers::dummyVkSampler();
    EXPECT_TRUE(tex.isValid());
}

TEST(GPUTexture, Reset_ClearsAll) {
    GPUTexture tex{};
    tex.image = TestHelpers::dummyVkImage();
    tex.view = TestHelpers::dummyVkImageView();
    tex.sampler = TestHelpers::dummyVkSampler();
    tex.format = VK_FORMAT_R8G8B8A8_SRGB;
    tex.width = 512;
    tex.height = 512;
    tex.mipLevels = 10;
    tex.exists = true;
    EXPECT_TRUE(tex.isValid());

    tex.reset();
    EXPECT_FALSE(tex.isValid());
    EXPECT_FALSE(tex.exists);
    EXPECT_EQ(tex.width, 0u);
    EXPECT_EQ(tex.height, 0u);
    EXPECT_EQ(tex.mipLevels, 1u);
    EXPECT_EQ(tex.format, VK_FORMAT_UNDEFINED);
}

// ═══════════════════════════════════════════════════════════════
// Enum Conversion Tests
// ═══════════════════════════════════════════════════════════════

TEST(AlphaMode, ToString) {
    EXPECT_STREQ(toString(AlphaMode::Opaque), "Opaque");
    EXPECT_STREQ(toString(AlphaMode::Mask), "Mask");
    EXPECT_STREQ(toString(AlphaMode::Blend), "Blend");
}

TEST(MaterialParamType, ToString) {
    EXPECT_STREQ(toString(MaterialParam::Type::Float), "float");
    EXPECT_STREQ(toString(MaterialParam::Type::Vec2), "vec2");
    EXPECT_STREQ(toString(MaterialParam::Type::Vec3), "vec3");
    EXPECT_STREQ(toString(MaterialParam::Type::Vec4), "vec4");
    EXPECT_STREQ(toString(MaterialParam::Type::Mat3), "mat3");
    EXPECT_STREQ(toString(MaterialParam::Type::Mat4), "mat4");
    EXPECT_STREQ(toString(MaterialParam::Type::Int), "int");
    EXPECT_STREQ(toString(MaterialParam::Type::UInt), "uint");
}

TEST(IndexType, ToVkIndexType) {
    EXPECT_EQ(toVkIndexType(IndexType::UInt16), VK_INDEX_TYPE_UINT16);
    EXPECT_EQ(toVkIndexType(IndexType::UInt32), VK_INDEX_TYPE_UINT32);
}

// ═══════════════════════════════════════════════════════════════
// MaterialParam Default Construction
// ═══════════════════════════════════════════════════════════════

TEST(MaterialParam, DefaultConstruction_TypeIsFloat) {
    MaterialParam p;
    EXPECT_EQ(p.type, MaterialParam::Type::Float);
}

TEST(MaterialParam, DefaultConstruction_DataIsZeroed) {
    MaterialParam p;
    // Default data array should be zero-initialized
    EXPECT_FLOAT_EQ(p.as<float>(), 0.0f);
}
