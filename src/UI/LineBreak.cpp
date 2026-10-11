//
// LineBreak.cpp - Where a line of text may break, and where words are
//

#include "UI/LineBreak.h"
#include "UI/WordDictionary.h"

#include <graphemebreak.h>
#include <linebreak.h>
#include <wordbreak.h>

#include <string>

namespace Shoonyakasha {
namespace UI {

namespace {

static_assert(sizeof(utf32_t) == sizeof(char32_t), "libunibreak reads char32_t text as utf32_t");

// The language libunibreak tailors line breaking for: none of its
// languages, so UAX #14's defaults hold, and "strict", so small kana and the
// prolonged sound mark (class CJ) do not start a line, the default of UAX #14.
constexpr const char* kLineBreakLanguage = "und-strict";

void initialise() {
    static const bool done = [] {
        init_linebreak();
        init_wordbreak();
        init_graphemebreak();
        return true;
    }();
    (void)done;
}

const utf32_t* units(std::u32string_view s) {
    return reinterpret_cast<const utf32_t*>(s.data());
}

/// For each code point of `s`, whether a grapheme cluster ends after it.
std::vector<bool> graphemeEnds(std::u32string_view s) {
    std::vector<char> brks(s.size());
    set_graphemebreaks_utf32(units(s), s.size(), nullptr, brks.data());
    std::vector<bool> ends(s.size());
    for (size_t i = 0; i < s.size(); ++i) ends[i] = brks[i] == GRAPHEMEBREAK_BREAK;
    return ends;
}

/// Calls `f(begin, end, starts)` for each run of a script split by a
/// dictionary in `s`, with where words start inside it (offsets from the
/// run's start); `starts` is null for a script without a word list.
template<typename F>
void forEachDictionaryRun(std::u32string_view s, F&& f) {
    for (size_t i = 0; i < s.size();) {
        const auto script = dictionaryScriptOf(s[i]);
        if (!script) {
            ++i;
            continue;
        }
        size_t end = i + 1;
        while (end < s.size() && dictionaryScriptOf(s[end]) == script) ++end;
        const std::u32string_view run = s.substr(i, end - i);
        if (const WordDictionary* dictionary = wordDictionary(*script)) {
            const std::vector<bool> ends = graphemeEnds(run);
            const std::vector<size_t> starts = dictionary->split(run, &ends);
            f(i, end, &starts);
        } else {
            f(i, end, nullptr);
        }
        i = end;
    }
}

} // namespace

std::vector<LineBreakAfter> lineBreaks(std::u32string_view text, size_t begin, size_t end) {
    std::vector<LineBreakAfter> out;
    if (end > text.size()) end = text.size();
    if (begin >= end) return out;
    initialise();
    const std::u32string_view s = text.substr(begin, end - begin);

    std::vector<char> brks(s.size());
    set_linebreaks_utf32(units(s), s.size(), kLineBreakLanguage, brks.data());
    out.resize(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        out[i] = brks[i] == LINEBREAK_MUSTBREAK  ? LineBreakAfter::Mandatory
               : brks[i] == LINEBREAK_ALLOWBREAK ? LineBreakAfter::Allowed
                                                 : LineBreakAfter::None;
    }
    out.back() = LineBreakAfter::Mandatory;

    forEachDictionaryRun(s, [&](size_t runBegin, size_t, const std::vector<size_t>* starts) {
        if (!starts) return;
        for (const size_t start : *starts) {
            LineBreakAfter& b = out[runBegin + start - 1];
            if (b == LineBreakAfter::None) b = LineBreakAfter::Allowed;
        }
    });
    return out;
}

std::vector<bool> wordBoundaries(std::u32string_view text) {
    std::vector<bool> out;
    if (text.empty()) return out;
    initialise();

    std::vector<char> brks(text.size());
    set_wordbreaks_utf32(units(text), text.size(), nullptr, brks.data());
    out.resize(text.size());
    for (size_t i = 0; i < text.size(); ++i) out[i] = brks[i] == WORDBREAK_BREAK;

    // Inside a run of a dictionary script, the words are the dictionary's;
    // without one, the run is one word.
    forEachDictionaryRun(text, [&](size_t runBegin, size_t runEnd, const std::vector<size_t>* starts) {
        for (size_t i = runBegin; i + 1 < runEnd; ++i) out[i] = false;
        if (!starts) return;
        for (const size_t start : *starts) out[runBegin + start - 1] = true;
    });
    out.back() = true;
    return out;
}

bool isBreakingSpace(char32_t c) {
    return c == U' ' || c == U'\t';
}

} // namespace UI
} // namespace Shoonyakasha
