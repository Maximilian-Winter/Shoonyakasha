//
// TextLayout.h - Breaking text into lines and placing glyph quads
//
// Layout is done in the units of the rect the text sits in (canvas units).
// There is no shaping: each code point maps to one glyph, with pair kerning.
//

#pragma once

#include "UI/SdfFont.h"

#include <glm/glm.hpp>

#include <string_view>
#include <vector>

namespace Shoonyakasha {
namespace UI {

enum class TextAlignH { Left, Center, Right };
enum class TextAlignV { Top, Middle, Bottom };

struct TextStyle {
    float fontSize = 24.0f;       // ascent-to-descent height
    TextAlignH alignH = TextAlignH::Left;
    TextAlignV alignV = TextAlignV::Top;
    bool wrap = true;             // break lines at the box width
    float lineSpacing = 1.0f;     // multiple of the font's line height
};

struct PositionedGlyph {
    int glyph = 0;
    glm::vec2 min{0.0f};          // quad covering the glyph's distance field,
    glm::vec2 max{0.0f};          // relative to the box's top-left corner
};

struct TextLine {
    size_t firstCodepoint = 0;    // range in the laid-out text, '\n' excluded
    size_t codepointCount = 0;
    float width = 0.0f;           // trailing spaces excluded
    float baseline = 0.0f;        // y relative to the box's top-left corner
};

struct TextLayoutResult {
    std::vector<PositionedGlyph> glyphs;  // in text order; glyphs with no outline omitted
    std::vector<TextLine> lines;
    glm::vec2 size{0.0f};                 // widest line by the height of all lines
};

/// Lays out `text` in a box of `boxSize`. With wrap on, a line breaks at the
/// last space before it would exceed the box width, or between characters
/// when a word is wider than the box. '\n' always breaks. Text that does not
/// fit is placed outside the box rather than dropped; clipping is the
/// renderer's concern. Control characters other than '\n' are skipped.
TextLayoutResult layoutText(const SdfFont& font, std::u32string_view text,
                            const TextStyle& style, const glm::vec2& boxSize);

} // namespace UI
} // namespace Shoonyakasha
