//
// LineBreak.h - Where a line of text may break
//
// A small, table-driven part of Unicode line breaking (UAX #14), enough for
// the scripts the canvas UI shapes:
// - after spaces, which are dropped at the break;
// - after the Tibetan tsheg and shad, and after hyphens, which stay at the
//   end of the line;
// - before and after CJK ideographs, kana and Hangul syllables, which are
//   written without spaces;
// - never before closing punctuation, the ideographic comma and full stop,
//   small kana and the shad, and never after opening punctuation, so these
//   stay with the character they belong to.
// Thai, Lao, Khmer and Myanmar, which need a dictionary to find words, are
// not covered: they break only at spaces.
//

#pragma once

namespace Shoonyakasha {
namespace UI {

/// Space and tab: a line may break at them, and they are dropped there.
bool isBreakingSpace(char32_t c);

/// May a line break between a character `before` and the character `after`
/// that follows it (the last code point of one cluster and the first of the
/// next)? Not at a space: that break is at the space itself.
bool canBreakBetween(char32_t before, char32_t after);

} // namespace UI
} // namespace Shoonyakasha
