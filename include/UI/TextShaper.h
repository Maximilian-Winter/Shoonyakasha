//
// TextShaper.h - Turning code points into positioned glyphs with HarfBuzz
//
// Text is split into runs of one script and one font and each run is shaped:
// conjuncts, reordered vowel signs and stacked marks come out as the font
// draws them, with its kerning applied. Positions are at the bake size
// (SdfFont::kBakeSize), y down.
//

#pragma once

#include "UI/SdfFont.h"

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace Shoonyakasha {
namespace UI {

struct ShapedGlyph {
    uint32_t font = 0;      // index into the fonts given to shapeText
    int glyph = 0;          // that font's glyph id
    uint32_t cluster = 0;   // index in the text of the first code point of its cluster
    float advance = 0.0f;   // pen movement, bake-size pixels
    glm::vec2 offset{0.0f}; // from the pen, bake-size pixels, y down
};

/// Shapes text[begin, end), the rest of `text` serving as context. Each code
/// point goes to the first font in `fonts` that has it; spaces, punctuation
/// and combining marks stay with the font and script of what precedes them.
/// A tab is shaped as a space and other control characters as nothing. A
/// code point no font has becomes the first font's U+FFFD, or '?'. Runs are
/// returned in text order; a right-to-left run's glyphs are in visual order.
/// Empty when `fonts` is empty or its first font is not loaded.
std::vector<ShapedGlyph> shapeText(const std::vector<const SdfFont*>& fonts, std::u32string_view text,
                                   size_t begin, size_t end);

} // namespace UI
} // namespace Shoonyakasha
