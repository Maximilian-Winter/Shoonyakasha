//
// Bidi.cpp - The Unicode bidirectional algorithm (UAX #9) for one paragraph
//

#include "UI/Bidi.h"

#include <SheenBidi/SheenBidi.h>

#include <algorithm>
#include <string>

namespace Shoonyakasha {
namespace UI {

namespace {

/// Characters of bidi class B other than '\n': SheenBidi would end the
/// paragraph at them.
bool isParagraphSeparator(char32_t c) {
    return c == U'\r' || (c >= 0x1C && c <= 0x1E) || c == 0x85 || c == 0x2029;
}

/// Whether `c` may make text other than left to right: a letter of a
/// right-to-left script from Hebrew on, or a directional formatting
/// character. Text without any is all at level 0 in a left-to-right
/// paragraph.
bool mayChangeDirection(char32_t c) {
    return c >= 0x0590 && (c <= 0x08FF ||                      // Hebrew to Arabic Extended-A
                           c == 0x200F || c == 0x061C ||           // RLM, ALM
                           (c >= 0x202A && c <= 0x202E) ||        // embeddings and overrides
                           (c >= 0x2066 && c <= 0x2069) ||        // isolates
                           (c >= 0xFB1D && c <= 0xFDFF) ||        // Hebrew and Arabic presentation forms
                           (c >= 0xFE70 && c <= 0xFEFF) ||
                           (c >= 0x10800 && c <= 0x10FFF) ||      // historic right-to-left scripts
                           (c >= 0x1E800 && c <= 0x1EFFF));       // Adlam, Arabic mathematical symbols
}

} // namespace

struct BidiParagraph::Impl {
    std::u32string text;                     // the paragraph, separators as spaces
    SBAlgorithmRef algorithm = nullptr;
    SBParagraphRef paragraph = nullptr;

    ~Impl() {
        if (paragraph) SBParagraphRelease(paragraph);
        if (algorithm) SBAlgorithmRelease(algorithm);
    }
};

BidiParagraph::BidiParagraph(std::u32string_view text, size_t begin, size_t end, TextDirection direction)
    : m_impl(std::make_unique<Impl>()), m_begin(begin) {
    end = std::min(end, text.size());
    begin = std::min(begin, end);
    m_begin = begin;
    m_impl->text.assign(text.substr(begin, end - begin));
    for (char32_t& c : m_impl->text) {
        if (isParagraphSeparator(c)) c = U' ';
    }
    const size_t length = m_impl->text.size();
    m_levels.assign(length, direction == TextDirection::RightToLeft ? 1 : 0);
    m_baseLevel = direction == TextDirection::RightToLeft ? 1 : 0;
    if (length == 0) return;
    const bool fallbackLeftToRight = direction != TextDirection::RightToLeft &&
                                     direction != TextDirection::AutoRightToLeft;
    if (fallbackLeftToRight && std::none_of(m_impl->text.begin(), m_impl->text.end(), mayChangeDirection)) {
        return;
    }

    SBCodepointSequence sequence{SBStringEncodingUTF32, m_impl->text.data(), length};
    m_impl->algorithm = SBAlgorithmCreate(&sequence);
    if (!m_impl->algorithm) return;
    const SBLevel base = direction == TextDirection::LeftToRight     ? SBLevel{0}
                       : direction == TextDirection::RightToLeft     ? SBLevel{1}
                       : direction == TextDirection::AutoRightToLeft ? SBLevelDefaultRTL
                                                                     : SBLevelDefaultLTR;
    m_impl->paragraph = SBAlgorithmCreateParagraph(m_impl->algorithm, 0, length, base);
    if (!m_impl->paragraph) return;

    m_baseLevel = SBParagraphGetBaseLevel(m_impl->paragraph);
    const size_t resolved = std::min<size_t>(SBParagraphGetLength(m_impl->paragraph), length);
    const SBLevel* levels = SBParagraphGetLevelsPtr(m_impl->paragraph);
    std::fill(m_levels.begin(), m_levels.end(), m_baseLevel);
    std::copy(levels, levels + resolved, m_levels.begin());
}

BidiParagraph::~BidiParagraph() = default;
BidiParagraph::BidiParagraph(BidiParagraph&&) noexcept = default;
BidiParagraph& BidiParagraph::operator=(BidiParagraph&&) noexcept = default;

std::vector<BidiRun> BidiParagraph::lineRuns(size_t lineBegin, size_t lineEnd) const {
    std::vector<BidiRun> runs;
    const size_t length = m_levels.size();
    const size_t first = std::clamp(lineBegin, m_begin, m_begin + length) - m_begin;
    const size_t last = std::clamp(lineEnd, m_begin + first, m_begin + length) - m_begin;
    if (first >= last) return runs;

    SBLineRef line = m_impl->paragraph ? SBParagraphCreateLine(m_impl->paragraph, first, last - first) : nullptr;
    if (!line) {
        runs.push_back({m_begin + first, m_begin + last, m_baseLevel});
        return runs;
    }
    const SBUInteger count = SBLineGetRunCount(line);
    const SBRun* sbRuns = SBLineGetRunsPtr(line);
    for (SBUInteger r = 0; r < count; ++r) {
        runs.push_back({m_begin + sbRuns[r].offset, m_begin + sbRuns[r].offset + sbRuns[r].length,
                        static_cast<uint8_t>(sbRuns[r].level)});
    }
    SBLineRelease(line);
    return runs;
}

} // namespace UI
} // namespace Shoonyakasha
