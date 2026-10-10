//
// SdfFont.cpp - A TrueType font rasterised as signed distance fields
//

#include "UI/SdfFont.h"

#include <stb_truetype.h>

#include <cstring>
#include <fstream>
#include <iterator>

namespace Shoonyakasha {
namespace UI {

struct SdfFont::Impl {
    std::vector<uint8_t> data;  // stbtt_fontinfo points into this
    stbtt_fontinfo info{};
};

SdfFont::SdfFont() = default;
SdfFont::~SdfFont() = default;
SdfFont::SdfFont(SdfFont&&) noexcept = default;
SdfFont& SdfFont::operator=(SdfFont&&) noexcept = default;

bool SdfFont::loadFromFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    return loadFromMemory(std::move(data));
}

bool SdfFont::loadFromMemory(std::vector<uint8_t> data) {
    m_impl.reset();
    if (data.empty()) return false;

    auto impl = std::make_unique<Impl>();
    impl->data = std::move(data);

    const int offset = stbtt_GetFontOffsetForIndex(impl->data.data(), 0);
    if (offset < 0 || !stbtt_InitFont(&impl->info, impl->data.data(), offset)) return false;

    m_scale = stbtt_ScaleForPixelHeight(&impl->info, kBakeSize);
    int ascent = 0, descent = 0, lineGap = 0;
    stbtt_GetFontVMetrics(&impl->info, &ascent, &descent, &lineGap);
    m_ascent = static_cast<float>(ascent) * m_scale;
    m_descent = static_cast<float>(descent) * m_scale;
    m_lineGap = static_cast<float>(lineGap) * m_scale;

    m_impl = std::move(impl);
    return true;
}

bool SdfFont::valid() const {
    return m_impl != nullptr;
}

int SdfFont::findGlyph(char32_t codepoint) const {
    if (!m_impl) return 0;
    return stbtt_FindGlyphIndex(&m_impl->info, static_cast<int>(codepoint));
}

int SdfFont::glyphForCodepoint(char32_t codepoint) const {
    for (const char32_t candidate : {codepoint, char32_t{0xFFFD}, char32_t{'?'}}) {
        if (const int glyph = findGlyph(candidate)) return glyph;
    }
    return 0;
}

float SdfFont::advance(int glyph) const {
    if (!m_impl) return 0.0f;
    int advanceWidth = 0, leftSideBearing = 0;
    stbtt_GetGlyphHMetrics(&m_impl->info, glyph, &advanceWidth, &leftSideBearing);
    return static_cast<float>(advanceWidth) * m_scale;
}

float SdfFont::kerning(int leftGlyph, int rightGlyph) const {
    if (!m_impl) return 0.0f;
    return static_cast<float>(stbtt_GetGlyphKernAdvance(&m_impl->info, leftGlyph, rightGlyph)) * m_scale;
}

SdfFont::GlyphBox SdfFont::glyphBox(int glyph) const {
    GlyphBox box;
    if (!m_impl) return box;

    // The same box stbtt_GetGlyphSDF computes, so it matches rasterize().
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    stbtt_GetGlyphBitmapBoxSubpixel(&m_impl->info, glyph, m_scale, m_scale, 0.0f, 0.0f, &x0, &y0, &x1, &y1);
    if (x0 == x1 || y0 == y1) return box;

    box.offset = glm::vec2(static_cast<float>(x0 - kPadding), static_cast<float>(y0 - kPadding));
    box.size = glm::ivec2(x1 - x0 + 2 * kPadding, y1 - y0 + 2 * kPadding);
    return box;
}

SdfFont::Bitmap SdfFont::rasterize(int glyph) const {
    Bitmap bitmap;
    if (!m_impl) return bitmap;

    int width = 0, height = 0, xoff = 0, yoff = 0;
    unsigned char* pixels = stbtt_GetGlyphSDF(&m_impl->info, m_scale, glyph, kPadding, kOnEdge,
                                              kPixelDistScale, &width, &height, &xoff, &yoff);
    if (!pixels) return bitmap;

    bitmap.size = glm::ivec2(width, height);
    bitmap.pixels.assign(pixels, pixels + static_cast<size_t>(width) * static_cast<size_t>(height));
    stbtt_FreeSDF(pixels, nullptr);
    return bitmap;
}

} // namespace UI
} // namespace Shoonyakasha
