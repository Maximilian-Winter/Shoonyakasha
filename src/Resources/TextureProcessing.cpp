//
// TextureProcessing.cpp - resizing, mip chains and block compression of 8-bit RGBA images
//

#include "Resources/TextureProcessing.h"

#include <stb_dxt.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <thread>

namespace Shoonyakasha {

namespace {

const std::array<float, 256>& srgbToLinearTable() {
    static const std::array<float, 256> table = [] {
        std::array<float, 256> t{};
        for (int i = 0; i < 256; ++i) {
            const float c = static_cast<float>(i) / 255.0f;
            t[i] = c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
        }
        return t;
    }();
    return table;
}

// Linear to 8-bit sRGB through a table of 4096 steps.
uint8_t linearToSrgb(float linear) {
    constexpr int kSteps = 4095;
    static const std::array<uint8_t, kSteps + 1> table = [] {
        std::array<uint8_t, kSteps + 1> t{};
        for (int i = 0; i <= kSteps; ++i) {
            const float c = static_cast<float>(i) / kSteps;
            const float s = c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
            t[i] = static_cast<uint8_t>(std::lround(s * 255.0f));
        }
        return t;
    }();
    return table[static_cast<size_t>(std::lround(std::clamp(linear, 0.0f, 1.0f) * kSteps))];
}

} // namespace

RgbaImage halveImage(const RgbaImage& image, bool srgb) {
    RgbaImage out;
    out.width = std::max(image.width / 2, 1u);
    out.height = std::max(image.height / 2, 1u);
    out.texels.resize(static_cast<size_t>(out.width) * out.height * 4);
    const auto& toLinear = srgbToLinearTable();
    for (uint32_t y = 0; y < out.height; ++y) {
        const uint32_t y0 = std::min(2 * y, image.height - 1), y1 = std::min(2 * y + 1, image.height - 1);
        for (uint32_t x = 0; x < out.width; ++x) {
            const uint32_t x0 = std::min(2 * x, image.width - 1), x1 = std::min(2 * x + 1, image.width - 1);
            const size_t sources[4] = {(static_cast<size_t>(y0) * image.width + x0) * 4,
                                       (static_cast<size_t>(y0) * image.width + x1) * 4,
                                       (static_cast<size_t>(y1) * image.width + x0) * 4,
                                       (static_cast<size_t>(y1) * image.width + x1) * 4};
            const size_t target = (static_cast<size_t>(y) * out.width + x) * 4;
            for (int c = 0; c < 4; ++c) {
                if (srgb && c < 3) {
                    float sum = 0.0f;
                    for (size_t s : sources) sum += toLinear[image.texels[s + c]];
                    out.texels[target + c] = linearToSrgb(sum * 0.25f);
                } else {
                    uint32_t sum = 0;
                    for (size_t s : sources) sum += image.texels[s + c];
                    out.texels[target + c] = static_cast<uint8_t>((sum + 2) / 4);
                }
            }
        }
    }
    return out;
}

RgbaImage fitImage(RgbaImage image, uint32_t maxSize, bool srgb) {
    while (maxSize > 0 && (image.width > maxSize || image.height > maxSize)) image = halveImage(image, srgb);
    return image;
}

std::vector<RgbaImage> mipChain(RgbaImage image, bool srgb) {
    std::vector<RgbaImage> levels;
    levels.push_back(std::move(image));
    while (levels.back().width > 1 || levels.back().height > 1) levels.push_back(halveImage(levels.back(), srgb));
    return levels;
}

bool hasTransparency(const RgbaImage& image) {
    for (size_t i = 3; i < image.texels.size(); i += 4) {
        if (image.texels[i] < 255) return true;
    }
    return false;
}

uint32_t blockBytes(BlockFormat format) {
    return format == BlockFormat::BC1 || format == BlockFormat::BC4 ? 8u : 16u;
}

std::vector<uint8_t> compressBlocks(const RgbaImage& image, BlockFormat format) {
    const uint32_t blocksX = (image.width + 3) / 4, blocksY = (image.height + 3) / 4;
    const uint32_t bytes = blockBytes(format);
    std::vector<uint8_t> out(static_cast<size_t>(blocksX) * blocksY * bytes);

    // Rows of blocks are independent: spread them over the CPU's threads.
    auto compressRows = [&](uint32_t firstRow, uint32_t endRow) {
        uint8_t rgba[16 * 4], channels[16 * 2];
        for (uint32_t by = firstRow; by < endRow; ++by) {
            for (uint32_t bx = 0; bx < blocksX; ++bx) {
                for (uint32_t ty = 0; ty < 4; ++ty) {
                    for (uint32_t tx = 0; tx < 4; ++tx) {
                        const uint32_t x = std::min(bx * 4 + tx, image.width - 1);
                        const uint32_t y = std::min(by * 4 + ty, image.height - 1);
                        const uint8_t* texel = &image.texels[(static_cast<size_t>(y) * image.width + x) * 4];
                        const uint32_t i = ty * 4 + tx;
                        for (int c = 0; c < 4; ++c) rgba[i * 4 + c] = texel[c];
                        channels[i * 2] = texel[0];
                        channels[i * 2 + 1] = texel[1];
                    }
                }
                uint8_t* block = &out[(static_cast<size_t>(by) * blocksX + bx) * bytes];
                switch (format) {
                    case BlockFormat::BC1: stb_compress_dxt_block(block, rgba, 0, STB_DXT_HIGHQUAL); break;
                    case BlockFormat::BC3: stb_compress_dxt_block(block, rgba, 1, STB_DXT_HIGHQUAL); break;
                    case BlockFormat::BC4: {
                        uint8_t red[16];
                        for (int i = 0; i < 16; ++i) red[i] = rgba[i * 4];
                        stb_compress_bc4_block(block, red);
                        break;
                    }
                    case BlockFormat::BC5: stb_compress_bc5_block(block, channels); break;
                }
            }
        }
    };

    const uint32_t threads =
        std::min(std::max(std::thread::hardware_concurrency(), 1u), std::max(blocksY / 16, 1u));
    std::vector<std::thread> workers;
    for (uint32_t t = 1; t < threads; ++t) {
        workers.emplace_back(compressRows, blocksY * t / threads, blocksY * (t + 1) / threads);
    }
    compressRows(0, blocksY / threads);
    for (auto& worker : workers) worker.join();
    return out;
}

} // namespace Shoonyakasha
