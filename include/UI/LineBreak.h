//
// LineBreak.h - Where a line of text may break, and where words are
//
// Line breaks follow the Unicode line breaking algorithm (UAX #14) and word
// boundaries the Unicode word boundary rules (UAX #29), both from
// libunibreak. Thai, Lao, Khmer and Myanmar, which those rules do not split,
// are split into words by UI/WordDictionary.h, for both.
//

#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace Shoonyakasha {
namespace UI {

enum class LineBreakAfter : uint8_t {
    None,        // the line may not break here
    Allowed,     // it may
    Mandatory,   // it must: after U+2028, a lone carriage return, a vertical tab and the like
};

/// For each code point of text[begin, end), whether a line may break after
/// it. A line may break after a space, never before one; after a hyphen,
/// the Tibetan tsheg and shad; between ideographs; at word ends in Thai,
/// Lao, Khmer and Myanmar; never before closing punctuation or after opening
/// punctuation. The break after the last code point is that of the end of
/// the text: Mandatory.
std::vector<LineBreakAfter> lineBreaks(std::u32string_view text, size_t begin, size_t end);

/// For each code point of `text`, whether a word, a run of spaces or a
/// piece of punctuation ends after it. True after the last.
std::vector<bool> wordBoundaries(std::u32string_view text);

/// Space and tab: a line may break at them, and they are dropped there.
bool isBreakingSpace(char32_t c);

} // namespace UI
} // namespace Shoonyakasha
