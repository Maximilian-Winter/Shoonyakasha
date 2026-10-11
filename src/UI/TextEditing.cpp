//
// TextEditing.cpp - Editing a line of text by code point and cluster
//

#include "UI/TextEditing.h"
#include "UI/LineBreak.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace Shoonyakasha {
namespace UI {

namespace {

enum class Kind { Space, Punctuation, Word };

Kind kindOf(char32_t c) {
    if (isBreakingSpace(c) || c == 0x00A0 || c == 0x3000 || (c >= 0x2000 && c <= 0x200A)) return Kind::Space;
    if (c < 0x80) {
        const bool word = (c >= U'0' && c <= U'9') || (c >= U'A' && c <= U'Z') || (c >= U'a' && c <= U'z') ||
                          c == U'_';
        return word ? Kind::Word : Kind::Punctuation;
    }
    const bool punctuation =
        (c >= 0x00A1 && c <= 0x00BF && c != 0x00AA && c != 0x00B5 && c != 0x00BA) ||
        c == 0x00D7 || c == 0x00F7 ||
        (c >= 0x2010 && c <= 0x205E) ||           // general punctuation
        c == 0x0964 || c == 0x0965 ||             // Devanagari danda and double danda
        (c >= 0x0F04 && c <= 0x0F14) ||           // Tibetan marks, tsheg and shad
        (c >= 0x3001 && c <= 0x3003) || (c >= 0x3008 && c <= 0x3011) ||
        (c >= 0x3014 && c <= 0x301F) ||           // CJK punctuation and brackets
        (c >= 0xFF01 && c <= 0xFF0F) || (c >= 0xFF1A && c <= 0xFF20);
    return punctuation ? Kind::Punctuation : Kind::Word;
}

} // namespace

std::vector<size_t> caretStops(const std::vector<TextCluster>& clusters, size_t length) {
    std::vector<size_t> stops{0, length};
    if (clusters.empty()) {
        for (size_t i = 1; i < length; ++i) stops.push_back(i);
    } else {
        for (const TextCluster& c : clusters) {
            if (c.firstCodepoint < length) stops.push_back(c.firstCodepoint);
        }
    }
    std::sort(stops.begin(), stops.end());
    stops.erase(std::unique(stops.begin(), stops.end()), stops.end());
    return stops;
}

namespace {

float leadingEdge(const TextCluster& c) { return c.rightToLeft() ? c.x + c.width : c.x; }
float trailingEdge(const TextCluster& c) { return c.rightToLeft() ? c.x : c.x + c.width; }

/// Where the caret stands for `index`, given the cluster starting there and
/// the one ending there (either may be null), and the line it is on.
std::pair<float, size_t> caretPlace(const TextCluster* after, const TextCluster* before) {
    if (after && before && after->line == before->line && before->level > after->level) {
        return {trailingEdge(*before), before->line};
    }
    if (after) return {leadingEdge(*after), after->line};
    if (before) return {trailingEdge(*before), before->line};
    return {0.0f, 0};
}

} // namespace

float caretX(const TextLayoutResult& layout, size_t index) {
    const TextCluster* after = nullptr;
    const TextCluster* before = nullptr;
    const TextCluster* last = nullptr;   // in text order
    for (const TextCluster& c : layout.clusters) {
        const size_t end = c.firstCodepoint + c.codepointCount;
        if (index > c.firstCodepoint && index < end) return leadingEdge(c);   // inside: not a caret stop
        if (c.firstCodepoint == index) after = &c;
        if (end == index) before = &c;
        if (!last || c.firstCodepoint > last->firstCodepoint) last = &c;
    }
    if (!after && !before && last && index > last->firstCodepoint) before = last;   // past the end
    return caretPlace(after, before).first;
}

std::vector<std::pair<size_t, float>> caretPositions(const TextLayoutResult& layout, size_t length) {
    std::unordered_map<size_t, const TextCluster*> starting, ending;
    for (const TextCluster& c : layout.clusters) {
        starting[c.firstCodepoint] = &c;
        ending[c.firstCodepoint + c.codepointCount] = &c;
    }
    struct Stop {
        size_t index;
        float x;
        size_t line;
    };
    std::vector<Stop> stops;
    for (const size_t index : caretStops(layout.clusters, length)) {
        const auto a = starting.find(index);
        const auto b = ending.find(index);
        const auto [x, line] = caretPlace(a != starting.end() ? a->second : nullptr,
                                          b != ending.end() ? b->second : nullptr);
        stops.push_back({index, x, line});
    }
    std::stable_sort(stops.begin(), stops.end(), [](const Stop& a, const Stop& b) {
        return a.line != b.line ? a.line < b.line : a.x < b.x;
    });
    std::vector<std::pair<size_t, float>> out;
    out.reserve(stops.size());
    for (const Stop& s : stops) out.emplace_back(s.index, s.x);
    return out;
}

size_t caretAt(const TextLayoutResult& layout, size_t length, float x) {
    size_t best = 0;
    float bestDistance = -1.0f;
    for (const auto& [index, at] : caretPositions(layout, length)) {
        const float distance = std::abs(at - x);
        if (bestDistance < 0.0f || distance < bestDistance) {
            best = std::min(index, length);
            bestDistance = distance;
        }
    }
    return best;
}

// ─── TextEditBuffer ─────────────────────────────────────────────

std::u32string TextEditBuffer::selectedText() const {
    return text.substr(selectionStart(), selectionEnd() - selectionStart());
}

void TextEditBuffer::moveTo(size_t index, bool extend) {
    caret = std::min(index, text.size());
    if (!extend) anchor = caret;
}

// With no stops, every code point is one.

size_t TextEditBuffer::previousStop(size_t index) const {
    if (stops.empty()) return index > 0 ? std::min(index - 1, text.size()) : 0;
    const auto it = std::lower_bound(stops.begin(), stops.end(), index);
    return it == stops.begin() ? 0 : *(it - 1);
}

size_t TextEditBuffer::nextStop(size_t index) const {
    if (stops.empty()) return std::min(index + 1, text.size());
    const auto it = std::upper_bound(stops.begin(), stops.end(), index);
    return it == stops.end() ? text.size() : *it;
}

size_t TextEditBuffer::leftStop(size_t index) const {
    const auto it = std::find(visual.begin(), visual.end(), index);
    if (it == visual.end()) return previousStop(index);
    return it == visual.begin() ? index : *(it - 1);
}

size_t TextEditBuffer::rightStop(size_t index) const {
    const auto it = std::find(visual.begin(), visual.end(), index);
    if (it == visual.end()) return nextStop(index);
    return it + 1 == visual.end() ? index : *(it + 1);
}

namespace {

/// The largest stop at or below `index`, and the smallest at or above it.
size_t stopAtOrBelow(const std::vector<size_t>& stops, size_t index) {
    if (stops.empty()) return index;
    const auto it = std::upper_bound(stops.begin(), stops.end(), index);
    return it == stops.begin() ? 0 : *(it - 1);
}

size_t stopAtOrAbove(const std::vector<size_t>& stops, size_t index, size_t length) {
    if (stops.empty()) return std::min(index, length);
    const auto it = std::lower_bound(stops.begin(), stops.end(), index);
    return it == stops.end() ? length : *it;
}

} // namespace

size_t TextEditBuffer::previousWord(size_t index) const {
    size_t i = std::min(index, text.size());
    while (i > 0 && kindOf(text[i - 1]) == Kind::Space) --i;
    if (i > 0) {
        const Kind kind = kindOf(text[i - 1]);
        while (i > 0 && kindOf(text[i - 1]) == kind) --i;
    }
    return stopAtOrBelow(stops, i);
}

size_t TextEditBuffer::nextWord(size_t index) const {
    const size_t n = text.size();
    size_t i = std::min(index, n);
    if (i < n && kindOf(text[i]) != Kind::Space) {
        const Kind kind = kindOf(text[i]);
        while (i < n && kindOf(text[i]) == kind) ++i;
    }
    while (i < n && kindOf(text[i]) == Kind::Space) ++i;
    return stopAtOrAbove(stops, i, n);
}

std::pair<size_t, size_t> TextEditBuffer::wordAt(size_t index) const {
    const size_t n = text.size();
    if (n == 0) return {0, 0};
    const size_t at = std::min(index, n - 1);
    const Kind kind = kindOf(text[at]);
    size_t start = at;
    while (start > 0 && kindOf(text[start - 1]) == kind) --start;
    size_t end = at + 1;
    while (end < n && kindOf(text[end]) == kind) ++end;
    return {stopAtOrBelow(stops, start), stopAtOrAbove(stops, end, n)};
}

void TextEditBuffer::erase(size_t start, size_t end) {
    text.erase(start, end - start);
    visual.clear();
    // Stops inside the range go; those after it move back.
    std::vector<size_t> kept;
    kept.reserve(stops.size());
    for (const size_t s : stops) {
        if (s <= start) kept.push_back(s);
        else if (s >= end) kept.push_back(s - (end - start));
    }
    kept.erase(std::unique(kept.begin(), kept.end()), kept.end());
    stops = std::move(kept);
    caret = anchor = start;
}

bool TextEditBuffer::insert(std::u32string_view insertion, size_t maxLength) {
    const size_t start = selectionStart();
    const size_t end = selectionEnd();
    const size_t remaining = text.size() - (end - start);
    size_t count = insertion.size();
    if (maxLength > 0) count = remaining >= maxLength ? 0 : std::min(count, maxLength - remaining);
    if (count == 0 && start == end) return false;

    erase(start, end);
    text.insert(start, insertion.substr(0, count));
    visual.clear();
    if (!stops.empty()) {
        // Until the text is laid out again, the inserted code points are a
        // stop each.
        for (size_t& s : stops) {
            if (s > start) s += count;
        }
        for (size_t i = 1; i <= count; ++i) stops.push_back(start + i);
        std::sort(stops.begin(), stops.end());
        stops.erase(std::unique(stops.begin(), stops.end()), stops.end());
    }
    caret = anchor = start + count;
    return true;
}

bool TextEditBuffer::backspace(bool word) {
    if (hasSelection()) {
        erase(selectionStart(), selectionEnd());
        return true;
    }
    if (caret == 0) return false;
    erase(word ? previousWord(caret) : previousStop(caret), caret);
    return true;
}

bool TextEditBuffer::deleteForward(bool word) {
    if (hasSelection()) {
        erase(selectionStart(), selectionEnd());
        return true;
    }
    if (caret >= text.size()) return false;
    erase(caret, word ? nextWord(caret) : nextStop(caret));
    return true;
}

} // namespace UI
} // namespace Shoonyakasha
