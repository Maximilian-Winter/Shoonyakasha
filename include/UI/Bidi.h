//
// Bidi.h - The Unicode bidirectional algorithm (UAX #9) for one paragraph
//
// A thin wrapper over SheenBidi. It resolves an embedding level for each
// code point of a paragraph (even: left to right, odd: right to left) and
// gives each line's runs of one level in display order, left to right.
//

#pragma once

#include "UI/TextLayout.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

namespace Shoonyakasha {
namespace UI {

struct BidiRun {
    size_t begin = 0;     // code points [begin, end) of the text
    size_t end = 0;
    uint8_t level = 0;
};

class BidiParagraph {
public:
    /// Resolves text[begin, end), one paragraph: it should hold no '\n'.
    /// Other paragraph separators inside it count as spaces.
    BidiParagraph(std::u32string_view text, size_t begin, size_t end, TextDirection direction);
    ~BidiParagraph();
    BidiParagraph(BidiParagraph&&) noexcept;
    BidiParagraph& operator=(BidiParagraph&&) noexcept;

    /// 0 for a left-to-right paragraph, 1 for a right-to-left one.
    uint8_t baseLevel() const { return m_baseLevel; }
    bool rightToLeft() const { return (m_baseLevel & 1u) != 0; }

    /// The level of each code point of the paragraph, from its first.
    const std::vector<uint8_t>& levels() const { return m_levels; }

    /// The runs of text[lineBegin, lineEnd), a line of the paragraph, in
    /// display order (UAX #9 rules L1 and L2). Whitespace at the line's end
    /// takes the paragraph's level.
    std::vector<BidiRun> lineRuns(size_t lineBegin, size_t lineEnd) const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
    size_t m_begin = 0;
    uint8_t m_baseLevel = 0;
    std::vector<uint8_t> m_levels;
};

} // namespace UI
} // namespace Shoonyakasha
