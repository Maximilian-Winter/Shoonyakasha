//
// LineBreak.cpp - Where a line of text may break
//

#include "UI/LineBreak.h"

#include <algorithm>
#include <iterator>

namespace Shoonyakasha {
namespace UI {

namespace {

template<size_t N>
bool contains(const char32_t (&set)[N], char32_t c) {
    return std::find(std::begin(set), std::end(set), c) != std::end(set);
}

bool inRange(char32_t c, char32_t first, char32_t last) {
    return c >= first && c <= last;
}

/// Written without spaces between words: a line may break on either side.
bool isIdeographic(char32_t c) {
    return inRange(c, 0x2E80, 0x2FDF) ||    // CJK radicals, Kangxi radicals
           inRange(c, 0x3040, 0x309F) ||    // Hiragana
           inRange(c, 0x30A0, 0x30FF) ||    // Katakana
           inRange(c, 0x3100, 0x312F) ||    // Bopomofo
           inRange(c, 0x31F0, 0x31FF) ||    // Katakana phonetic extensions
           inRange(c, 0x3400, 0x4DBF) ||    // CJK extension A
           inRange(c, 0x4E00, 0x9FFF) ||    // CJK unified ideographs
           inRange(c, 0xAC00, 0xD7AF) ||    // Hangul syllables
           inRange(c, 0xF900, 0xFAFF) ||    // CJK compatibility ideographs
           inRange(c, 0x20000, 0x3FFFF);    // CJK extensions B onwards
}

/// Stays with the character before it.
bool noBreakBefore(char32_t c) {
    static constexpr char32_t kSet[] = {
        U')', U']', U'}', U',', U'.', U';', U':', U'!', U'?', U'%',
        0x2019, 0x201D, 0x2026,                                       // ’ ” …
        0x3001, 0x3002, 0x3009, 0x300B, 0x300D, 0x300F, 0x3011, 0x3015,   // 、。〉》」』】〕
        0x3017, 0x3019, 0x301F, 0x3005, 0x309D, 0x309E, 0x30FB, 0x30FC,   // 〗〙〟々ゝゞ・ー
        0x30FD, 0x30FE,                                               // ヽヾ
        0x3041, 0x3043, 0x3045, 0x3047, 0x3049, 0x3063, 0x3083, 0x3085,   // small hiragana
        0x3087, 0x308E, 0x3095, 0x3096,
        0x30A1, 0x30A3, 0x30A5, 0x30A7, 0x30A9, 0x30C3, 0x30E3, 0x30E5,   // small katakana
        0x30E7, 0x30EE, 0x30F5, 0x30F6,
        0xFF01, 0xFF09, 0xFF0C, 0xFF0E, 0xFF1A, 0xFF1B, 0xFF1F, 0xFF3D, 0xFF5D,   // fullwidth ! ) , . : ; ? ] }
        0x0F0D, 0x0F0E, 0x0F0F, 0x0F10, 0x0F11, 0x0F12, 0x0F14,       // Tibetan shad and its kin
    };
    return contains(kSet, c);
}

/// Stays with the character after it.
bool noBreakAfter(char32_t c) {
    static constexpr char32_t kSet[] = {
        U'(', U'[', U'{', 0x2018, 0x201C,                             // ( [ { ‘ “
        0x3008, 0x300A, 0x300C, 0x300E, 0x3010, 0x3014, 0x3016, 0x3018, 0x301D,   // 〈《「『【〔〖〘〝
        0xFF08, 0xFF3B, 0xFF5B,                                       // fullwidth ( [ {
        0x0F0C,                                                       // Tibetan non-breaking tsheg
    };
    return contains(kSet, c);
}

/// A line may break after it; it stays at the end of the line.
bool breakAfter(char32_t c) {
    static constexpr char32_t kSet[] = {
        U'-', 0x2010, 0x2012, 0x2013,                                 // hyphens, figure and en dash
        0x0F0B,                                                       // Tibetan tsheg
        0x0F0D, 0x0F0E, 0x0F0F, 0x0F10, 0x0F11, 0x0F12, 0x0F14,       // Tibetan shad and its kin
    };
    return contains(kSet, c);
}

} // namespace

bool isBreakingSpace(char32_t c) {
    return c == U' ' || c == U'\t';
}

bool canBreakBetween(char32_t before, char32_t after) {
    if (isBreakingSpace(before) || isBreakingSpace(after)) return false;
    if (noBreakBefore(after) || noBreakAfter(before)) return false;
    return breakAfter(before) || isIdeographic(before) || isIdeographic(after);
}

} // namespace UI
} // namespace Shoonyakasha
