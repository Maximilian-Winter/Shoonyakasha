//
// TextComposition.h - Text an input method is composing
//
// An input method (IME) builds text in steps before the user commits it:
// typed kana that are then converted to kanji, for example. Until it is
// committed the text is shown where it will go but is not part of it.
//

#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace Shoonyakasha {

/// A part of the composition the input method converts as one, such as a
/// word. Indices are in code points of TextComposition::text.
struct TextCompositionClause {
    size_t begin = 0;
    size_t end = 0;
    bool active = false;   // the clause being converted

    bool operator==(const TextCompositionClause&) const = default;
};

struct TextComposition {
    std::u32string text;                         // empty: nothing is being composed
    size_t caret = 0;                            // code-point index into text
    std::vector<TextCompositionClause> clauses;  // in order; may be empty

    bool empty() const { return text.empty(); }

    bool operator==(const TextComposition&) const = default;
};

} // namespace Shoonyakasha
