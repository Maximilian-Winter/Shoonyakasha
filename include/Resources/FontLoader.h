//
// FontLoader.h - Bakes TTF fonts into a glyph atlas texture via stb_truetype
//
// The atlas is baked as RGBA8 (rgb = white, alpha = glyph coverage) so
// text quads can be drawn with the exact same sprite shader used for
// textured sprites/UI panels (tintColor * texture).
//

#pragma once

#include "GPU/GPUTypes.h"
#include <glm/glm.hpp>
#include <string>
#include <unordered_map>

namespace Shoonyakasha {

class VulkanDevice;

struct GlyphInfo {
    glm::vec4 uvRect{0.0f};       // u0, v0, u1, v1 in the atlas [0, 1]
    glm::vec2 sizePixels{0.0f};   // glyph quad size at the baked pixel height
    glm::vec2 offsetPixels{0.0f}; // bearing: offset from the pen/baseline to the quad's top-left
    float advancePixels = 0.0f;   // how far to move the pen after this glyph
};

struct BakedFont {
    GPUTexture atlas;
    float pixelHeight = 0.0f;
    std::unordered_map<char, GlyphInfo> glyphs;  // ASCII 32..126
    bool valid = false;
};

class FontLoader {
public:
    explicit FontLoader(VulkanDevice& device);
    ~FontLoader();

    FontLoader(const FontLoader&) = delete;
    FontLoader& operator=(const FontLoader&) = delete;

    // Load (or fetch from cache) a font baked at the given pixel height.
    // Returns nullptr if the font file couldn't be read or baked.
    const BakedFont* getOrLoadFont(const std::string& path, float pixelHeight);

private:
    VulkanDevice& m_device;
    std::unordered_map<std::string, BakedFont> m_cache;  // keyed by "path@pixelHeight"

    BakedFont bakeFont(const std::string& path, float pixelHeight);
};

} // namespace Shoonyakasha
