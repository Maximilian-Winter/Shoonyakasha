//
// RichText.h - Markup in UIText: colour, weight, slant, size, font, links
// and lines
//
// Text with UIText::richText set is read as markup:
//
//   <color=#RRGGBB> or <color=#RRGGBBAA>   colour
//   <b>, <i>                               bold and italic
//   <size=N>                               font size in canvas units
//   <font=name>                            a font by FontLibrary name
//   <link=id>                              a span the pointer can click
//   <u>, <s>                               underline and strikethrough
//
// each closed by </name>. Tags nest; a closing tag ends the most recent open
// tag of its name, and tags still open at the end run to it. "&lt;" and
// "&gt;" stand for '<' and '>'. A tag that is not one of these, has a value
// it should not or lacks one it needs, or closes nothing open, is text.
// Values may be in double quotes.
//

#pragma once

#include "UI/TextLayout.h"

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Shoonyakasha {
namespace UI {

class FontLibrary;

/// What the tags open at a point of the text set.
struct RichTextStyle {
    bool hasColor = false;
    glm::vec4 color{1.0f};
    bool bold = false;
    bool italic = false;
    bool underline = false;
    bool strikethrough = false;
    float size = 0.0f;        // 0: the text's own size
    std::string font;         // empty: the text's own font
    int link = -1;            // index into the links; -1: none

    bool operator==(const RichTextStyle&) const = default;
};

/// Code points [begin, end) of the text, in one style.
struct RichTextSpan {
    size_t begin = 0;
    size_t end = 0;
    RichTextStyle style;
};

struct RichText {
    std::u32string text;                 // without the markup
    std::vector<RichTextSpan> spans;     // in order, covering the text, one per change of style
    std::vector<std::string> links;      // the id of each <link>, in order of opening
};

RichText parseRichText(std::u32string_view markup);

/// How a stretch of laid-out text is drawn.
struct TextRun {
    size_t begin = 0;         // code points [begin, end) of the plain text
    size_t end = 0;
    RichTextStyle style;
    float fontSize = 0.0f;    // resolved
    bool fauxBold = false;    // bold with no bold font: drawn thicker
    bool fauxItalic = false;  // italic with no italic font: drawn slanted
};

/// What a UIText is laid out and drawn from.
struct PreparedText {
    std::u32string text;               // without the markup
    std::vector<uint32_t> fonts;       // FontLibrary ids; spans and PositionedGlyph::font index it
    std::vector<TextSpan> spans;       // for layoutText, covering the text; empty for plain text
    std::vector<TextRun> runs;         // one per span; empty for plain text
    std::vector<std::string> links;
};

/// Plain `text` in `font`'s chain; or, with `rich`, its markup resolved: a
/// `<font>` name FontLibrary does not know keeps the text's font, and bold or
/// italic take the font's bold or italic font where one is set
/// (FontLibrary::setStyles), and are drawn faux otherwise.
PreparedText prepareText(std::string_view text, bool rich, uint32_t font, float fontSize, const FontLibrary& fonts);

/// The run that code point `index` is in, or null.
const TextRun* runAt(const std::vector<TextRun>& runs, size_t index);

/// The link under `point`, relative to the laid-out box's top-left corner:
/// the index into the links of the cluster there, or -1.
int linkAt(const TextLayoutResult& layout, const std::vector<TextRun>& runs, glm::vec2 point);

} // namespace UI
} // namespace Shoonyakasha
