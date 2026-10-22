//
// TextureProcessingTest.cpp - resizing, mip chains and block compression of RGBA images
//

#include <gtest/gtest.h>

#include "Resources/TextureProcessing.h"

#include <array>
#include <cstdlib>

using namespace Shoonyakasha;

namespace {

RgbaImage solid(uint32_t width, uint32_t height, std::array<uint8_t, 4> rgba) {
    RgbaImage image{width, height, {}};
    for (uint32_t i = 0; i < width * height; ++i) image.texels.insert(image.texels.end(), rgba.begin(), rgba.end());
    return image;
}

std::array<uint8_t, 4> texel(const RgbaImage& image, uint32_t x, uint32_t y) {
    const uint8_t* t = &image.texels[(static_cast<size_t>(y) * image.width + x) * 4];
    return {t[0], t[1], t[2], t[3]};
}

void setTexel(RgbaImage& image, uint32_t x, uint32_t y, std::array<uint8_t, 4> rgba) {
    for (int c = 0; c < 4; ++c) image.texels[(static_cast<size_t>(y) * image.width + x) * 4 + c] = rgba[c];
}

// The colour of texel `index` (0..15) of a BC1 block, 8-bit RGB.
std::array<int, 3> decodeBc1(const uint8_t* block, int index) {
    const uint16_t c0 = static_cast<uint16_t>(block[0] | block[1] << 8);
    const uint16_t c1 = static_cast<uint16_t>(block[2] | block[3] << 8);
    auto rgb = [](uint16_t c) {
        return std::array<int, 3>{(c >> 11) * 255 / 31, ((c >> 5) & 63) * 255 / 63, (c & 31) * 255 / 31};
    };
    const auto a = rgb(c0), b = rgb(c1);
    const uint32_t bits = static_cast<uint32_t>(block[4] | block[5] << 8 | block[6] << 16 | block[7] << 24);
    const int i = (bits >> (2 * index)) & 3;
    std::array<int, 3> out{};
    for (int k = 0; k < 3; ++k) {
        if (i == 0) out[k] = a[k];
        else if (i == 1) out[k] = b[k];
        else if (c0 > c1) out[k] = i == 2 ? (2 * a[k] + b[k]) / 3 : (a[k] + 2 * b[k]) / 3;
        else out[k] = i == 2 ? (a[k] + b[k]) / 2 : 0;
    }
    return out;
}

// The value of texel `index` (0..15) of a BC4 block.
int decodeBc4(const uint8_t* block, int index) {
    const int a0 = block[0], a1 = block[1];
    uint64_t bits = 0;
    for (int i = 0; i < 6; ++i) bits |= static_cast<uint64_t>(block[2 + i]) << (8 * i);
    const int i = static_cast<int>((bits >> (3 * index)) & 7);
    if (i == 0) return a0;
    if (i == 1) return a1;
    if (a0 > a1) return ((8 - i) * a0 + (i - 1) * a1) / 7;
    if (i == 6) return 0;
    if (i == 7) return 255;
    return ((6 - i) * a0 + (i - 1) * a1) / 5;
}

} // namespace

TEST(TextureProcessing, HalvingAveragesTwoByTwo) {
    RgbaImage image = solid(2, 2, {0, 0, 0, 0});
    setTexel(image, 0, 0, {100, 200, 40, 255});
    setTexel(image, 1, 0, {100, 0, 40, 255});
    setTexel(image, 0, 1, {0, 200, 40, 255});
    setTexel(image, 1, 1, {0, 0, 40, 255});
    const RgbaImage half = halveImage(image, false);
    ASSERT_EQ(half.width, 1u);
    ASSERT_EQ(half.height, 1u);
    EXPECT_EQ(texel(half, 0, 0), (std::array<uint8_t, 4>{50, 100, 40, 255}));
}

TEST(TextureProcessing, SrgbHalvingAveragesLinearLight) {
    RgbaImage image = solid(2, 1, {0, 0, 0, 0});
    setTexel(image, 1, 0, {255, 255, 255, 255});
    // Black and white average to linear 0.5, sRGB 188; alpha stays linear.
    const auto t = texel(halveImage(image, true), 0, 0);
    EXPECT_EQ(t[0], 188);
    EXPECT_EQ(t[3], 128);
    EXPECT_EQ(texel(halveImage(image, false), 0, 0)[0], 128);
}

TEST(TextureProcessing, FitHalvesUntilBothSidesFit) {
    const RgbaImage fitted = fitImage(solid(4096, 1024, {1, 2, 3, 4}), 1024, false);
    EXPECT_EQ(fitted.width, 1024u);
    EXPECT_EQ(fitted.height, 256u);
    EXPECT_EQ(fitImage(solid(300, 200, {1, 2, 3, 4}), 0, false).width, 300u);
    EXPECT_EQ(fitImage(solid(300, 200, {1, 2, 3, 4}), 512, false).width, 300u);
}

TEST(TextureProcessing, MipChainEndsAtOneByOne) {
    const auto mips = mipChain(solid(8, 3, {9, 9, 9, 9}), false);
    ASSERT_EQ(mips.size(), 4u);   // 8x3, 4x1, 2x1, 1x1
    EXPECT_EQ(mips[1].width, 4u);
    EXPECT_EQ(mips[1].height, 1u);
    EXPECT_EQ(mips[3].width, 1u);
    EXPECT_EQ(texel(mips[3], 0, 0), (std::array<uint8_t, 4>{9, 9, 9, 9}));
}

TEST(TextureProcessing, TransparencyIsAnyAlphaBelowOpaque) {
    RgbaImage image = solid(3, 3, {10, 20, 30, 255});
    EXPECT_FALSE(hasTransparency(image));
    setTexel(image, 2, 2, {10, 20, 30, 254});
    EXPECT_TRUE(hasTransparency(image));
}

TEST(TextureProcessing, BlocksCoverTheImageInRows) {
    EXPECT_EQ(compressBlocks(solid(5, 5, {0, 0, 0, 255}), BlockFormat::BC1).size(), 4u * 8u);
    EXPECT_EQ(compressBlocks(solid(5, 5, {0, 0, 0, 255}), BlockFormat::BC5).size(), 4u * 16u);
    EXPECT_EQ(compressBlocks(solid(1, 1, {0, 0, 0, 255}), BlockFormat::BC3).size(), 16u);

    // Left block red, right block blue: the blocks come left to right.
    RgbaImage image = solid(8, 4, {255, 0, 0, 255});
    for (uint32_t y = 0; y < 4; ++y)
        for (uint32_t x = 4; x < 8; ++x) setTexel(image, x, y, {0, 0, 255, 255});
    const auto blocks = compressBlocks(image, BlockFormat::BC1);
    ASSERT_EQ(blocks.size(), 16u);
    for (int i = 0; i < 16; ++i) {
        const auto left = decodeBc1(blocks.data(), i), right = decodeBc1(blocks.data() + 8, i);
        EXPECT_GT(left[0], 240);
        EXPECT_LT(left[2], 16);
        EXPECT_LT(right[0], 16);
        EXPECT_GT(right[2], 240);
    }
}

TEST(TextureProcessing, Bc5HoldsRedAndGreen) {
    const auto blocks = compressBlocks(solid(4, 4, {200, 60, 0, 255}), BlockFormat::BC5);
    ASSERT_EQ(blocks.size(), 16u);
    for (int i = 0; i < 16; ++i) {
        EXPECT_NEAR(decodeBc4(blocks.data(), i), 200, 1);
        EXPECT_NEAR(decodeBc4(blocks.data() + 8, i), 60, 1);
    }
}
