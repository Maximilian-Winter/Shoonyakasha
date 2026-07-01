//
// FontLoader.cpp
//

#include "Resources/FontLoader.h"
#include "Vulkan/VulkanDevice.h"
#include "GPU/GPUResourceFactory.h"

// stb_truetype implementation in src/ThirdParty/stb_impl.cpp
#include <stb_truetype.h>

#include <fstream>
#include <iostream>
#include <vector>

namespace Shoonyakasha {

namespace {
constexpr int kFirstChar = 32;
constexpr int kNumChars = 96;  // ASCII 32..127
constexpr int kAtlasSize = 512;
}

FontLoader::FontLoader(VulkanDevice& device)
    : m_device(device)
{
}

FontLoader::~FontLoader() {
    for (auto& [key, font] : m_cache) {
        if (font.valid) {
            GPUResourceFactory::destroyTexture(m_device.getAllocator().getHandle(), m_device.getLogicalDevice(), font.atlas);
        }
    }
}

const BakedFont* FontLoader::getOrLoadFont(const std::string& path, float pixelHeight) {
    std::string key = path + "@" + std::to_string(pixelHeight);
    auto it = m_cache.find(key);
    if (it != m_cache.end()) {
        return it->second.valid ? &it->second : nullptr;
    }

    BakedFont font = bakeFont(path, pixelHeight);
    auto [inserted, _] = m_cache.emplace(key, std::move(font));
    return inserted->second.valid ? &inserted->second : nullptr;
}

BakedFont FontLoader::bakeFont(const std::string& path, float pixelHeight) {
    BakedFont font;

    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        std::cerr << "[FontLoader] Failed to open font file: " << path << std::endl;
        return font;
    }
    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<unsigned char> fontData(static_cast<size_t>(size));
    if (!file.read(reinterpret_cast<char*>(fontData.data()), size)) {
        std::cerr << "[FontLoader] Failed to read font file: " << path << std::endl;
        return font;
    }

    std::vector<unsigned char> bitmap(kAtlasSize * kAtlasSize, 0);
    std::vector<stbtt_bakedchar> chardata(kNumChars);

    int result = stbtt_BakeFontBitmap(
        fontData.data(), 0, pixelHeight,
        bitmap.data(), kAtlasSize, kAtlasSize,
        kFirstChar, kNumChars, chardata.data());

    if (result == 0) {
        std::cerr << "[FontLoader] stbtt_BakeFontBitmap failed for: " << path << std::endl;
        return font;
    }
    if (result < 0) {
        std::cerr << "[FontLoader] Warning: atlas too small for " << path
                  << " at size " << pixelHeight << " - some glyphs may be missing" << std::endl;
    }

    // Expand single-channel coverage to RGBA8 (rgb = white, a = coverage)
    // so the sprite shader (tintColor * texture) works unchanged for text.
    std::vector<uint8_t> rgba(static_cast<size_t>(kAtlasSize) * kAtlasSize * 4);
    for (size_t i = 0; i < bitmap.size(); ++i) {
        rgba[i * 4 + 0] = 255;
        rgba[i * 4 + 1] = 255;
        rgba[i * 4 + 2] = 255;
        rgba[i * 4 + 3] = bitmap[i];
    }

    font.atlas = GPUResourceFactory::createTexture2DWithData(
        m_device.getAllocator().getHandle(),
        m_device.getLogicalDevice(),
        m_device.getGraphicsQueue(),
        m_device.getCommandPool(),
        kAtlasSize, kAtlasSize,
        VK_FORMAT_R8G8B8A8_UNORM,
        rgba.data(),
        rgba.size(),
        false);

    font.atlas.sampler = GPUResourceFactory::createSampler(
        m_device.getLogicalDevice(),
        VK_FILTER_LINEAR, VK_FILTER_LINEAR,
        VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        1.0f,
        VK_SAMPLER_MIPMAP_MODE_LINEAR,
        static_cast<float>(font.atlas.mipLevels));
    font.atlas.exists = true;

    font.pixelHeight = pixelHeight;
    for (int i = 0; i < kNumChars; ++i) {
        const stbtt_bakedchar& c = chardata[i];
        GlyphInfo glyph;
        glyph.uvRect = glm::vec4(
            static_cast<float>(c.x0) / kAtlasSize,
            static_cast<float>(c.y0) / kAtlasSize,
            static_cast<float>(c.x1) / kAtlasSize,
            static_cast<float>(c.y1) / kAtlasSize);
        glyph.sizePixels = glm::vec2(c.x1 - c.x0, c.y1 - c.y0);
        glyph.offsetPixels = glm::vec2(c.xoff, c.yoff);
        glyph.advancePixels = c.xadvance;
        font.glyphs[static_cast<char>(kFirstChar + i)] = glyph;
    }

    font.valid = true;
    return font;
}

} // namespace Shoonyakasha
