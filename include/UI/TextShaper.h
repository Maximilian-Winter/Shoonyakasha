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

/// How a code point is set in vertical text.
enum class VerticalForm : uint8_t {
    Sideways,           // shaped horizontally, its line turned 90 degrees clockwise
    Upright,            // shaped top to bottom
    UprightHorizontal,  // shaped horizontally and set upright, as digits in tate-chu-yoko
};

struct ShapedGlyph {
    uint32_t font = 0;      // index into the fonts given to shapeText
    int glyph = 0;          // that font's glyph id
    uint32_t cluster = 0;   // index in the text of the first code point of its cluster
    float advance = 0.0f;   // pen movement, bake-size pixels
    glm::vec2 offset{0.0f}; // from the pen, bake-size pixels, y down
    float verticalAdvance = 0.0f;  // shaped top to bottom: pen movement down, bake-size pixels
    bool upright = false;   // shaped top to bottom; `offset` places its horizontal origin
};

/// Shapes text[begin, end), the rest of `text` serving as context. Each code
/// point goes to the first font in `fonts` that has it; spaces, punctuation
/// and combining marks stay with the font and script of what precedes them.
/// A tab is shaped as a space, and other control characters and the line
/// and paragraph separators (U+2028, U+2029) as nothing. A
/// code point no font has becomes the first font's U+FFFD, or '?'. Runs are
/// returned in text order; a right-to-left run's glyphs are in visual order.
/// Empty when `fonts` is empty or its first font is not loaded.
///
/// With `levels`, the bidi embedding level of each code point of
/// [begin, end), runs also end where the level changes, and each is shaped
/// in its level's direction (odd: right to left, mirroring brackets).
/// Without, each run takes its script's direction.
///
/// With `chains`, one entry per code point of `text`, a code point whose
/// entry is not null draws from the fonts its entry lists, indices into
/// `fonts`, in that order, and the first of them gives its replacement.
///
/// With `vertical`, one entry per code point of `text`, runs also end where
/// the form changes, and Upright runs are shaped top to bottom, which takes
/// the fonts' vertical forms (the vert and vrt2 features) and vertical
/// advances; the pen then runs down a line through the glyphs' centres.
std::vector<ShapedGlyph> shapeText(const std::vector<const SdfFont*>& fonts, std::u32string_view text,
                                   size_t begin, size_t end, const std::vector<uint8_t>* levels = nullptr,
                                   const std::vector<const std::vector<uint32_t>*>* chains = nullptr,
                                   const std::vector<VerticalForm>* vertical = nullptr);

} // namespace UI
} // namespace Shoonyakasha
