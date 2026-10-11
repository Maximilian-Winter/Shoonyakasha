//
// TextEditing.h - Editing a line of text by code point and cluster
//
// Indices are code points. The caret stands between clusters (TextCluster),
// never inside one, so moving over or deleting a Devanagari conjunct or a
// letter with its marks takes one step.
//

#pragma once

#include "UI/TextLayout.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Shoonyakasha {
namespace UI {

/// Where the caret may stand in a text of `length` code points laid out as
/// `clusters`: 0, `length` and each cluster's start, ascending. Without
/// clusters, every code point.
std::vector<size_t> caretStops(const std::vector<TextCluster>& clusters, size_t length);

/// The caret's x for `index` in a laid-out line, relative to the box: the
/// left edge of the cluster it stands before, or the right edge of the last
/// cluster at the end. 0 for a text with no clusters.
float caretX(const TextLayoutResult& layout, size_t index);

/// The caret stop nearest to `x` in a laid-out line of `length` code points.
size_t caretAt(const TextLayoutResult& layout, size_t length, float x);

/// A line of text with a caret and a selection, and its edits.
struct TextEditBuffer {
    std::u32string text;
    size_t caret = 0;
    size_t anchor = 0;               // the selection runs between the anchor and the caret
    std::vector<size_t> stops;       // see caretStops; empty: every code point is one

    bool hasSelection() const { return caret != anchor; }
    size_t selectionStart() const { return caret < anchor ? caret : anchor; }
    size_t selectionEnd() const { return caret < anchor ? anchor : caret; }
    std::u32string selectedText() const;

    /// Moves the caret to `index`; without `extend` the selection collapses
    /// to it.
    void moveTo(size_t index, bool extend);

    /// The stop before and after `index`, or `index` at either end.
    size_t previousStop(size_t index) const;
    size_t nextStop(size_t index) const;

    /// Word jumps: back to the start of the word before `index`, and on to
    /// the start of the next word. Words are runs of letters and digits, or
    /// of punctuation, separated by spaces.
    size_t previousWord(size_t index) const;
    size_t nextWord(size_t index) const;

    /// The run of the same kind (word, punctuation or spaces) at `index`.
    std::pair<size_t, size_t> wordAt(size_t index) const;

    /// Replaces the selection with as much of `insertion` as keeps the text
    /// within `maxLength` code points (0: no limit), and puts the caret after
    /// it. Returns whether the text changed.
    bool insert(std::u32string_view insertion, size_t maxLength = 0);

    /// Removes the selection, or the cluster (`word`: the word) before or
    /// after the caret. Returns whether the text changed.
    bool backspace(bool word = false);
    bool deleteForward(bool word = false);

private:
    /// Removes [start, end) and leaves the caret at start.
    void erase(size_t start, size_t end);
};

} // namespace UI
} // namespace Shoonyakasha
