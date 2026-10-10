//
// TextLayout.h - Breaking text into lines and placing glyph quads
//
// Layout is done in the units of the rect the text sits in (canvas units).
// Text is shaped with HarfBuzz (UI/TextShaper.h), so glyphs, their positions
// and kerning are the font's own, and lines break between clusters, never
// inside one. The first font gives the line metrics.
//

#pragma once

#include "UI/SdfFont.h"

#include <glm/glm.hpp>

#include <cstdint>
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
};

/// The smallest unit text is edited and broken in: one or more code points
/// drawn as one or more glyphs, such as a Devanagari conjunct.
struct TextCluster {
    size_t firstCodepoint = 0;
    size_t codepointCount = 0;
    size_t line = 0;
    float x = 0.0f;               // its left edge relative to the box, aligned
    float width = 0.0f;
};

struct TextLayoutResult {
    std::vector<PositionedGlyph> glyphs;  // in line order; glyphs with no outline omitted
    std::vector<TextLine> lines;
    std::vector<TextCluster> clusters;    // in line order; none for '\n'
    glm::vec2 size{0.0f};                 // widest line by the height of all lines
};

/// Lays out `text` in a box of `boxSize` with `fonts`, the first preferred
/// and the rest used for code points it lacks. With wrap on, a line breaks
/// at the last opportunity before it would exceed the box width: a space,
/// which is dropped, or a place UI/LineBreak.h allows, such as after a
/// Tibetan tsheg or between ideographs. Without one it breaks between
/// clusters. '\n' always breaks. Text that
/// does not fit is placed outside the box rather than dropped; clipping is the
/// renderer's concern. Control characters other than '\n' and tab are not
/// drawn; a tab is drawn as a space.
TextLayoutResult layoutText(const std::vector<const SdfFont*>& fonts, std::u32string_view text,
                            const TextStyle& style, const glm::vec2& boxSize);

/// The same with one font.
TextLayoutResult layoutText(const SdfFont& font, std::u32string_view text,
                            const TextStyle& style, const glm::vec2& boxSize);

} // namespace UI
} // namespace Shoonyakasha
