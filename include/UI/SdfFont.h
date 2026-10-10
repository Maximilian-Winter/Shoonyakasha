//
// SdfFont.h - A TrueType font rasterised as signed distance fields
//
// Every glyph is rasterised at kBakeSize pixels, with kPadding pixels of
// distance field around it. Text of any size is drawn by scaling those
// bitmaps by fontSize / kBakeSize. Metrics are returned at the bake size.
//
// A pixel value of kOnEdge is the glyph outline; values rise inside the glyph
// and fall outside it by kPixelDistScale per bake-size pixel, reaching 0 at
// the edge of the padding.
//
// Sizes follow stbtt_ScaleForPixelHeight: a font size is the distance from
// the font's ascent to its descent.
//

#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Shoonyakasha {
namespace UI {

class SdfFont {
public:
    static constexpr float kBakeSize = 48.0f;
    static constexpr int kPadding = 6;
    static constexpr uint8_t kOnEdge = 180;
    static constexpr float kPixelDistScale = 30.0f;  // kOnEdge / kPadding

    /// Placement of a glyph's distance-field bitmap relative to the pen
    /// position on the baseline, y down, at the bake size. Empty for glyphs
    /// with no outline, such as the space.
    struct GlyphBox {
        glm::vec2 offset{0.0f};
        glm::ivec2 size{0};
        bool empty() const { return size.x <= 0 || size.y <= 0; }
    };

    struct Bitmap {
        glm::ivec2 size{0};
        std::vector<uint8_t> pixels;  // size.x * size.y, row-major, top row first
    };

    SdfFont();
    ~SdfFont();
    SdfFont(SdfFont&&) noexcept;
    SdfFont& operator=(SdfFont&&) noexcept;

    /// Reads the first font in a .ttf or .otf file. False when the file cannot
    /// be read or is not a font.
    bool loadFromFile(const std::string& path);
    bool loadFromMemory(std::vector<uint8_t> data);

    bool valid() const;

    /// Glyph index for a code point; 0 when the font has no glyph for it.
    int findGlyph(char32_t codepoint) const;

    /// Glyph to draw for a code point: its own glyph, else U+FFFD, else '?',
    /// else glyph 0 (the font's missing-glyph box).
    int glyphForCodepoint(char32_t codepoint) const;

    float ascent() const { return m_ascent; }    // above the baseline, positive
    float descent() const { return m_descent; }  // below the baseline, negative
    float lineGap() const { return m_lineGap; }

    float advance(int glyph) const;
    float kerning(int leftGlyph, int rightGlyph) const;
    GlyphBox glyphBox(int glyph) const;

    /// The glyph's distance field, matching glyphBox(glyph).size. Empty for
    /// glyphs with no outline.
    Bitmap rasterize(int glyph) const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
    float m_scale = 0.0f;
    float m_ascent = 0.0f;
    float m_descent = 0.0f;
    float m_lineGap = 0.0f;
};

} // namespace UI
} // namespace Shoonyakasha
