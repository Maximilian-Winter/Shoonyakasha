//
// TextLayout.h - Breaking text into lines and placing glyph quads
//
// Layout is done in the units of the rect the text sits in (canvas units).
// Text is shaped with HarfBuzz (UI/TextShaper.h), so glyphs, their positions
// and kerning are the font's own, and lines break between clusters, never
// inside one. The first font gives the line metrics.
//
// Each paragraph goes through the Unicode bidirectional algorithm (UAX #9,
// UI/Bidi.h): lines are broken in typing order, and each line's runs are
// then put in display order, so Arabic or Hebrew read right to left with
// Latin words and numbers inside them left to right.
//

#pragma once

#include "UI/SdfFont.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <string_view>
#include <vector>

namespace Shoonyakasha {
namespace UI {

/// Start and End are the left and right of a left-to-right paragraph, and
/// the right and left of a right-to-left one.
enum class TextAlignH { Left, Center, Right, Start, End };
enum class TextAlignV { Top, Middle, Bottom };

/// A paragraph's direction. Auto takes it from the paragraph's first letter
/// with a direction (UAX #9 rules P2 and P3), left to right without one.
enum class TextDirection { Auto, LeftToRight, RightToLeft };

struct TextStyle {
    float fontSize = 24.0f;       // ascent-to-descent height
    TextAlignH alignH = TextAlignH::Left;
    TextAlignV alignV = TextAlignV::Top;
    bool wrap = true;             // break lines at the box width
    float lineSpacing = 1.0f;     // multiple of the font's line height
    TextDirection direction = TextDirection::Auto;
};

struct PositionedGlyph {
    uint32_t font = 0;            // index into the fonts the text was laid out with
    int glyph = 0;
    glm::vec2 min{0.0f};          // quad covering the glyph's distance field,
    glm::vec2 max{0.0f};          // relative to the box's top-left corner
};

struct TextLine {
    size_t firstCodepoint = 0;    // range in the laid-out text, '\n' and the space it broke at excluded
    size_t codepointCount = 0;
    float width = 0.0f;           // trailing spaces excluded
    float baseline = 0.0f;        // y relative to the box's top-left corner
    bool rightToLeft = false;     // its paragraph's direction
};

/// The smallest unit text is edited and broken in: one or more code points
/// drawn as one or more glyphs, such as a Devanagari conjunct.
struct TextCluster {
    size_t firstCodepoint = 0;
    size_t codepointCount = 0;
    size_t line = 0;
    float x = 0.0f;               // its left edge relative to the box, aligned
    float width = 0.0f;
    uint8_t level = 0;            // its bidi embedding level: odd reads right to left
    bool rightToLeft() const { return (level & 1u) != 0; }
};

struct TextLayoutResult {
    std::vector<PositionedGlyph> glyphs;  // line by line, left to right; glyphs with no outline omitted
    std::vector<TextLine> lines;
    std::vector<TextCluster> clusters;    // line by line, left to right; none for '\n'
    glm::vec2 size{0.0f};                 // widest line by the height of all lines
};

/// Lays out `text` in a box of `boxSize` with `fonts`, the first preferred
/// and the rest used for code points it lacks. With wrap on, a line breaks
/// at the last opportunity before it would exceed the box width: a space,
/// which is dropped, or a place UI/LineBreak.h allows, such as after a
/// Tibetan tsheg or between ideographs. Without one it breaks between
/// clusters. '\n' always breaks and starts a new paragraph. Spaces a line
/// breaks at stay at its end in the paragraph's direction and do not count
/// towards its width or alignment. Text that does not fit is placed outside
/// the box rather than dropped; clipping is the renderer's concern. Control
/// characters other than '\n' and tab are not drawn; a tab is drawn as a
/// space.
TextLayoutResult layoutText(const std::vector<const SdfFont*>& fonts, std::u32string_view text,
                            const TextStyle& style, const glm::vec2& boxSize);

/// The same with one font.
TextLayoutResult layoutText(const SdfFont& font, std::u32string_view text,
                            const TextStyle& style, const glm::vec2& boxSize);

} // namespace UI
} // namespace Shoonyakasha
