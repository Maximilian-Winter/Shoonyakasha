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
/// with a direction (UAX #9 rules P2 and P3), left to right without one;
/// AutoRightToLeft the same, right to left without one. FromLayout is for
/// UIText: its element's layout direction, which CanvasTextSystem resolves
/// before layout; layoutText reads it as Auto.
enum class TextDirection { Auto, LeftToRight, RightToLeft, AutoRightToLeft, FromLayout };

/// Horizontal lines run left to right and stack downwards. Vertical lines
/// are columns that run downwards, stacked from the right (Chinese and
/// Japanese) or from the left (Mongolian).
enum class TextOrientation { Horizontal, VerticalRightToLeft, VerticalLeftToRight };

struct TextStyle {
    float fontSize = 24.0f;       // ascent-to-descent height
    TextAlignH alignH = TextAlignH::Left;
    TextAlignV alignV = TextAlignV::Top;
    bool wrap = true;             // break lines at the box width, or for vertical text its height
    float lineSpacing = 1.0f;     // multiple of the font's line height
    TextDirection direction = TextDirection::Auto;
    TextOrientation orientation = TextOrientation::Horizontal;
    bool uprightDigits = false;   // vertical: runs of one or two digits upright side by side (tate-chu-yoko)
};

/// Code points [begin, end) laid out at their own size from their own fonts:
/// `fonts` indexes the fonts layoutText is given, the first preferred and
/// the rest used for code points it lacks. A size of 0 is the style's.
struct TextSpan {
    size_t begin = 0;
    size_t end = 0;
    std::vector<uint32_t> fonts;
    float fontSize = 0.0f;
};

struct PositionedGlyph {
    uint32_t font = 0;            // index into the fonts the text was laid out with
    int glyph = 0;
    glm::vec2 min{0.0f};          // quad covering the glyph's distance field,
    glm::vec2 max{0.0f};          // relative to the box's top-left corner
    size_t codepoint = 0;         // the first code point of its cluster
    size_t line = 0;
    bool sideways = false;        // vertical text: turned 90 degrees clockwise, min and max its turned box
};

/// For vertical text a line is a column: `baseline` is its centre's x,
/// `top` and `bottom` its left and right edges, `left` where its content
/// starts down it, and `width` its content's length.
struct TextLine {
    size_t firstCodepoint = 0;    // range in the laid-out text, '\n' and the space it broke at excluded
    size_t codepointCount = 0;
    float width = 0.0f;           // trailing spaces excluded
    float left = 0.0f;            // where its content starts, aligned, relative to the box; an empty line's caret
    float baseline = 0.0f;        // y relative to the box's top-left corner
    float top = 0.0f;             // the first font's ascent above the baseline,
    float bottom = 0.0f;          // and its descent below it, at the line's largest size
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
    std::vector<TextCluster> clusters;    // line by line, left to right; none for '\n', nor in vertical text
    glm::vec2 size{0.0f};                 // widest line by the height of all lines; vertical: all columns by the longest
    bool vertical = false;
};

/// Lays out `text` in a box of `boxSize` with `fonts`, the first preferred
/// and the rest used for code points it lacks. With wrap on, a line breaks
/// at the last opportunity before it would exceed the box width: a space,
/// which is dropped, or a place UI/LineBreak.h allows, such as after a
/// Tibetan tsheg, between ideographs or between Thai words. Without one it
/// breaks between clusters. '\n' always breaks and starts a new paragraph;
/// U+2028, a lone carriage return and the other mandatory breaks of UAX #14
/// end a line within the paragraph. Spaces a line
/// breaks at stay at its end in the paragraph's direction and do not count
/// towards its width or alignment. Text that does not fit is placed outside
/// the box rather than dropped; clipping is the renderer's concern. Control
/// characters other than '\n' and tab are not drawn; a tab is drawn as a
/// space.
///
/// With `spans`, the code points of each are laid out from its fonts at its
/// size; others from all of `fonts` at the style's size. A line is as tall
/// as the first font at the largest size on it, and lines are spaced by
/// their own heights.
///
/// Vertical text breaks and aligns in the same way down columns as the box
/// is tall: alignV places each column's content along it, and alignH the
/// columns as a block (Start: the side they start from). Chinese, Japanese
/// and Korean characters and fullwidth forms stand upright, shaped top to
/// bottom with the font's vertical forms; other characters, such as Latin,
/// digits and Mongolian, are turned 90 degrees clockwise, as the Unicode
/// vertical orientations (UAX #50) of those blocks give. With uprightDigits
/// a run of one or two ASCII digits stands upright side by side in one
/// character's height, squeezed to the column's width.
TextLayoutResult layoutText(const std::vector<const SdfFont*>& fonts, std::u32string_view text,
                            const TextStyle& style, const glm::vec2& boxSize,
                            const std::vector<TextSpan>& spans = {});

/// The same with one font.
TextLayoutResult layoutText(const SdfFont& font, std::u32string_view text,
                            const TextStyle& style, const glm::vec2& boxSize);

/// Is `c` set upright in vertical text?
bool uprightInVerticalText(char32_t c);

} // namespace UI
} // namespace Shoonyakasha
