//
// Utf8.h - UTF-8 decoding and encoding
//

#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace Shoonyakasha {
namespace UI {

constexpr char32_t kReplacementCharacter = 0xFFFD;

/// Code points of a UTF-8 string. A malformed sequence becomes one U+FFFD:
/// a stray continuation or invalid lead byte, a sequence cut short by a
/// non-continuation byte or the end of the text, an overlong encoding, a
/// surrogate, or a value above U+10FFFF. Decoding resumes at the first byte
/// that is not part of the malformed sequence.
inline std::u32string decodeUtf8(std::string_view text) {
    std::u32string out;
    out.reserve(text.size());

    const size_t n = text.size();
    size_t i = 0;
    while (i < n) {
        const auto lead = static_cast<uint8_t>(text[i]);
        if (lead < 0x80) {
            out.push_back(lead);
            ++i;
            continue;
        }

        size_t length;
        char32_t codepoint;
        char32_t minimum;
        if ((lead & 0xE0) == 0xC0)      { length = 2; codepoint = lead & 0x1F; minimum = 0x80; }
        else if ((lead & 0xF0) == 0xE0) { length = 3; codepoint = lead & 0x0F; minimum = 0x800; }
        else if ((lead & 0xF8) == 0xF0) { length = 4; codepoint = lead & 0x07; minimum = 0x10000; }
        else {
            out.push_back(kReplacementCharacter);
            ++i;
            continue;
        }

        size_t k = 1;
        for (; k < length; ++k) {
            if (i + k >= n) break;
            const auto next = static_cast<uint8_t>(text[i + k]);
            if ((next & 0xC0) != 0x80) break;
            codepoint = (codepoint << 6) | (next & 0x3F);
        }
        i += k;

        if (k < length || codepoint < minimum || codepoint > 0x10FFFF ||
            (codepoint >= 0xD800 && codepoint <= 0xDFFF)) {
            out.push_back(kReplacementCharacter);
        } else {
            out.push_back(codepoint);
        }
    }
    return out;
}

/// The UTF-8 encoding of `text`. A surrogate or a value above U+10FFFF is
/// encoded as U+FFFD.
inline std::string encodeUtf8(std::u32string_view text) {
    std::string out;
    out.reserve(text.size());
    for (char32_t c : text) {
        if (c > 0x10FFFF || (c >= 0xD800 && c <= 0xDFFF)) c = kReplacementCharacter;
        if (c < 0x80) {
            out.push_back(static_cast<char>(c));
        } else if (c < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (c >> 6)));
            out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        } else if (c < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (c >> 12)));
            out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (c >> 18)));
            out.push_back(static_cast<char>(0x80 | ((c >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        }
    }
    return out;
}

} // namespace UI
} // namespace Shoonyakasha
