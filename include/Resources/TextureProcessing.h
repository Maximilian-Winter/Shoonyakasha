//
// TextureProcessing.h - resizing, mip chains and block compression of 8-bit RGBA images
//

#pragma once

#include <cstdint>
#include <vector>

namespace Shoonyakasha {

/// An image of 8-bit RGBA texels, rows top first.
struct RgbaImage {
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint8_t> texels;   // width * height * 4
};

/// Half the size in each direction (rounded down, at least 1), each texel
/// the average of the 2 x 2 it covers; past an odd edge the last row or
/// column repeats. With `srgb`, red, green and blue average in linear light.
RgbaImage halveImage(const RgbaImage& image, bool srgb);

/// `image` halved until neither side exceeds `maxSize`; unchanged when
/// `maxSize` is 0 or it already fits.
RgbaImage fitImage(RgbaImage image, uint32_t maxSize, bool srgb);

/// `image` and its halvings, down to 1 x 1.
std::vector<RgbaImage> mipChain(RgbaImage image, bool srgb);

/// Whether any texel's alpha is below 255.
bool hasTransparency(const RgbaImage& image);

/// Block-compressed formats compressBlocks writes.
enum class BlockFormat {
    BC1,   // RGB, 8 bytes a block
    BC3,   // RGBA, 16 bytes a block
    BC4,   // R, 8 bytes a block
    BC5,   // R and G, 16 bytes a block
};

/// Bytes of one 4 x 4 block of `format`.
uint32_t blockBytes(BlockFormat format);

/// `image` as 4 x 4 blocks of `format`, rows of blocks top first, as Vulkan
/// copies them into a block-compressed image. Blocks past the image's edge
/// repeat its last row and column.
std::vector<uint8_t> compressBlocks(const RgbaImage& image, BlockFormat format);

} // namespace Shoonyakasha
