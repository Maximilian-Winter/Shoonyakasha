//
// ImageFiles.cpp - .hdr and .exr writers (include/Compute/ImageFiles.h)
//

#include "Compute/ImageFiles.h"

#include "Compute/Compute.h"
#include "Compute/Npy.h"
#include "GPU/VkFormatUtils.h"
#include "Vulkan/FrameGraph/FrameGraphJson.h"

#include <glm/gtc/packing.hpp>
#include <stb_image_write.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace Shoonyakasha::Compute {

namespace {

void checkSize(uint32_t width, uint32_t height, uint32_t channels) {
    if (width == 0 || height == 0) throw std::invalid_argument("image file: the image is empty");
    if (channels < 1 || channels > 4) {
        throw std::invalid_argument("image file: " + std::to_string(channels) + " channels; 1 to 4 are written");
    }
}

/// Moves `temp` over `path`, removing `temp` when that fails.
void replace(const std::string& temp, const std::string& path) {
    std::error_code ec;
    std::filesystem::rename(temp, path, ec);
    if (ec) {
        std::filesystem::remove(temp, ec);
        throw std::runtime_error("image file: cannot replace '" + path + "'");
    }
}

void put32(std::string& out, uint32_t v) {
    for (int i = 0; i < 4; ++i) out += static_cast<char>((v >> (8 * i)) & 0xff);
}

void put64(std::string& out, uint64_t v) {
    for (int i = 0; i < 8; ++i) out += static_cast<char>((v >> (8 * i)) & 0xff);
}

void putFloat(std::string& out, float f) {
    uint32_t bits;
    std::memcpy(&bits, &f, 4);
    put32(out, bits);
}

/// One header attribute: name, type, size, value.
void attribute(std::string& out, const char* name, const char* type, const std::string& value) {
    out += name;
    out += '\0';
    out += type;
    out += '\0';
    put32(out, static_cast<uint32_t>(value.size()));
    out += value;
}

float srgbToLinear(float c) {
    return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

} // namespace

void saveHdr(const std::string& path, const float* texels, uint32_t width, uint32_t height, uint32_t channels) {
    checkSize(width, height, channels);
    // stb_image_write reads two channels as grey and alpha; write R and G
    // with blue zero instead.
    std::vector<float> rgb;
    if (channels == 2) {
        rgb.resize(size_t(width) * height * 3, 0.0f);
        for (size_t i = 0; i < size_t(width) * height; ++i) {
            rgb[i * 3] = texels[i * 2];
            rgb[i * 3 + 1] = texels[i * 2 + 1];
        }
        texels = rgb.data();
        channels = 3;
    }
    const std::string temp = path + ".partial";
    if (!stbi_write_hdr(temp.c_str(), int(width), int(height), int(channels), texels)) {
        std::error_code ec;
        std::filesystem::remove(temp, ec);
        throw std::runtime_error("image file: cannot write '" + temp + "'");
    }
    replace(temp, path);
}

void saveExr(const std::string& path, const float* texels, uint32_t width, uint32_t height, uint32_t channels,
             ExrPixelType type) {
    checkSize(width, height, channels);
    static const char* const names[4][4] = {{"Y"}, {"R", "G"}, {"R", "G", "B"}, {"R", "G", "B", "A"}};

    // Channels are listed, and stored in each scanline, in alphabetical
    // order of their names; `order` maps that position to the texel's.
    std::vector<uint32_t> order(channels);
    for (uint32_t c = 0; c < channels; ++c) order[c] = c;
    std::sort(order.begin(), order.end(),
              [&](uint32_t a, uint32_t b) { return std::strcmp(names[channels - 1][a], names[channels - 1][b]) < 0; });

    const uint32_t pixelType = type == ExrPixelType::Half ? 1 : 2;
    const uint32_t valueSize = type == ExrPixelType::Half ? 2 : 4;

    std::string chlist;
    for (uint32_t c : order) {
        chlist += names[channels - 1][c];
        chlist += '\0';
        put32(chlist, pixelType);
        chlist += std::string(4, '\0');   // pLinear and three reserved bytes
        put32(chlist, 1);                 // x sampling
        put32(chlist, 1);                 // y sampling
    }
    chlist += '\0';

    std::string box;
    put32(box, 0);
    put32(box, 0);
    put32(box, width - 1);
    put32(box, height - 1);
    std::string one, centre;
    putFloat(one, 1.0f);
    putFloat(centre, 0.0f);
    putFloat(centre, 0.0f);

    std::string file = std::string("\x76\x2f\x31\x01", 4);   // magic
    put32(file, 2);                                          // version 2, single-part scanline
    attribute(file, "channels", "chlist", chlist);
    attribute(file, "compression", "compression", std::string(1, '\0'));
    attribute(file, "dataWindow", "box2i", box);
    attribute(file, "displayWindow", "box2i", box);
    attribute(file, "lineOrder", "lineOrder", std::string(1, '\0'));
    attribute(file, "pixelAspectRatio", "float", one);
    attribute(file, "screenWindowCenter", "v2f", centre);
    attribute(file, "screenWindowWidth", "float", one);
    file += '\0';

    // Without compression each block is one scanline: its y, its byte
    // count, then every channel's values for the row.
    const uint64_t rowBytes = uint64_t(width) * channels * valueSize;
    const uint64_t firstBlock = file.size() + uint64_t(height) * 8;
    for (uint32_t y = 0; y < height; ++y) put64(file, firstBlock + y * (8 + rowBytes));
    file.reserve(file.size() + height * (8 + rowBytes));
    for (uint32_t y = 0; y < height; ++y) {
        put32(file, y);
        put32(file, static_cast<uint32_t>(rowBytes));
        const float* row = texels + size_t(y) * width * channels;
        for (uint32_t c : order) {
            for (uint32_t x = 0; x < width; ++x) {
                const float v = row[size_t(x) * channels + c];
                if (type == ExrPixelType::Half) {
                    const uint16_t h = glm::packHalf1x16(v);
                    file += static_cast<char>(h & 0xff);
                    file += static_cast<char>(h >> 8);
                } else {
                    putFloat(file, v);
                }
            }
        }
    }

    const std::string temp = path + ".partial";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out) throw std::runtime_error("image file: cannot write '" + temp + "'");
        out.write(file.data(), static_cast<std::streamsize>(file.size()));
        if (!out) throw std::runtime_error("image file: writing '" + temp + "' failed");
    }
    replace(temp, path);
}

std::vector<float> imageTexels(const Image& image, uint32_t mip, uint32_t layer, uint32_t& channels) {
    if (!image.valid()) throw std::invalid_argument("image file: the image is empty or released");
    if (mip >= image.mipLevels()) {
        throw std::invalid_argument("image file: mip level " + std::to_string(mip) + " of an image with " +
                                    std::to_string(image.mipLevels()) + " level(s)");
    }
    const uint32_t slices = image.depth(mip) > 1 ? image.depth(mip) : image.layers();
    if (layer >= slices) {
        throw std::invalid_argument("image file: layer " + std::to_string(layer) + " of an image with " +
                                    std::to_string(slices) + " layer(s) or slice(s) at mip " + std::to_string(mip));
    }

    const VkFormat format = FrameGraph::JsonUtils::stringToFormat(image.format());
    const size_t texel = image.texelSize();
    const size_t count = size_t(image.width(mip)) * image.height(mip);
    std::vector<uint8_t> bytes(image.byteSize(mip));
    image.download(bytes.data(), bytes.size(), mip);
    const uint8_t* src = bytes.data() + layer * count * texel;

    auto read16 = [&](size_t i) { uint16_t v; std::memcpy(&v, src + i * 2, 2); return v; };
    auto read32 = [&](size_t i) { uint32_t v; std::memcpy(&v, src + i * 4, 4); return v; };
    std::vector<float> out;

    switch (format) {
        case VK_FORMAT_R16_SFLOAT: case VK_FORMAT_R16G16_SFLOAT: case VK_FORMAT_R16G16B16_SFLOAT:
        case VK_FORMAT_R16G16B16A16_SFLOAT:
            channels = static_cast<uint32_t>(texel / 2);
            out.resize(count * channels);
            for (size_t i = 0; i < out.size(); ++i) out[i] = glm::unpackHalf1x16(read16(i));
            return out;
        case VK_FORMAT_R32_SFLOAT: case VK_FORMAT_R32G32_SFLOAT: case VK_FORMAT_R32G32B32_SFLOAT:
        case VK_FORMAT_R32G32B32A32_SFLOAT:
            channels = static_cast<uint32_t>(texel / 4);
            out.resize(count * channels);
            std::memcpy(out.data(), src, out.size() * 4);
            return out;
        case VK_FORMAT_R16_UNORM: case VK_FORMAT_R16G16_UNORM: case VK_FORMAT_R16G16B16A16_UNORM:
            channels = static_cast<uint32_t>(texel / 2);
            out.resize(count * channels);
            for (size_t i = 0; i < out.size(); ++i) out[i] = read16(i) / 65535.0f;
            return out;
        case VK_FORMAT_R8_UNORM: case VK_FORMAT_R8G8_UNORM: case VK_FORMAT_R8G8B8_UNORM:
        case VK_FORMAT_R8G8B8A8_UNORM: case VK_FORMAT_B8G8R8A8_UNORM:
        case VK_FORMAT_R8G8B8A8_SRGB: case VK_FORMAT_B8G8R8A8_SRGB: {
            channels = static_cast<uint32_t>(texel);
            const bool bgra = format == VK_FORMAT_B8G8R8A8_UNORM || format == VK_FORMAT_B8G8R8A8_SRGB;
            const bool srgb = format == VK_FORMAT_R8G8B8A8_SRGB || format == VK_FORMAT_B8G8R8A8_SRGB;
            out.resize(count * channels);
            for (size_t i = 0; i < count; ++i) {
                for (uint32_t c = 0; c < channels; ++c) {
                    const uint32_t from = (bgra && c < 3) ? 2 - c : c;
                    const float v = src[i * channels + from] / 255.0f;
                    out[i * channels + c] = (srgb && c < 3) ? srgbToLinear(v) : v;
                }
            }
            return out;
        }
        case VK_FORMAT_B10G11R11_UFLOAT_PACK32:
            channels = 3;
            out.resize(count * 3);
            for (size_t i = 0; i < count; ++i) unpackB10G11R11(read32(i), &out[i * 3]);
            return out;
        case VK_FORMAT_A2B10G10R10_UNORM_PACK32:
            channels = 4;
            out.resize(count * 4);
            for (size_t i = 0; i < count; ++i) unpackA2B10G10R10(read32(i), &out[i * 4]);
            return out;
        default:
            throw std::invalid_argument("image file: '" + image.format() + "' texels are not written as floats");
    }
}

void saveImage(const std::string& path, const Image& image, uint32_t mip, uint32_t layer, ExrPixelType type) {
    const std::string extension = std::filesystem::path(path).extension().string();
    if (extension != ".hdr" && extension != ".exr" && extension != ".npy") {
        throw std::invalid_argument("image file: '" + path + "' is not an .hdr, .exr or .npy path");
    }
    uint32_t channels = 0;
    const std::vector<float> texels = imageTexels(image, mip, layer, channels);
    const uint32_t width = image.width(mip), height = image.height(mip);
    if (extension == ".hdr") {
        saveHdr(path, texels.data(), width, height, channels);
    } else if (extension == ".exr") {
        saveExr(path, texels.data(), width, height, channels, type);
    } else {
        saveNpy(path, texels.data(), texels.size() * sizeof(float), "<f4", {height, width, channels});
    }
}

} // namespace Shoonyakasha::Compute
