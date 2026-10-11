//
// TextShaper.cpp - Turning code points into positioned glyphs with HarfBuzz
//

#include "UI/TextShaper.h"

#include <hb.h>

#include <algorithm>
#include <string>

namespace Shoonyakasha {
namespace UI {

namespace {

bool isControl(char32_t c) {
    return (c < 0x20 && c != U'\n') || (c >= 0x7F && c <= 0x9F) || c == 0x2028 || c == 0x2029;
}

/// Characters that belong with whatever precedes them.
bool followsPrevious(hb_unicode_funcs_t* unicode, char32_t c) {
    const hb_script_t script = hb_unicode_script(unicode, c);
    if (script == HB_SCRIPT_COMMON || script == HB_SCRIPT_INHERITED || script == HB_SCRIPT_UNKNOWN) return true;
    switch (hb_unicode_general_category(unicode, c)) {
    case HB_UNICODE_GENERAL_CATEGORY_NON_SPACING_MARK:
    case HB_UNICODE_GENERAL_CATEGORY_SPACING_MARK:
    case HB_UNICODE_GENERAL_CATEGORY_ENCLOSING_MARK:
        return true;
    default:
        return false;
    }
}

} // namespace

std::vector<ShapedGlyph> shapeText(const std::vector<const SdfFont*>& fonts, std::u32string_view text,
                                   size_t begin, size_t end, const std::vector<uint8_t>* levels,
                                   const std::vector<const std::vector<uint32_t>*>* chains,
                                   const std::vector<VerticalForm>* vertical) {
    std::vector<ShapedGlyph> out;
    if (fonts.empty() || !fonts[0] || !fonts[0]->valid() || begin >= end || end > text.size()) return out;
    if (chains && chains->size() < end) chains = nullptr;
    if (vertical && vertical->size() < end) vertical = nullptr;
    auto formOf = [&](size_t i) { return vertical ? (*vertical)[begin + i] : VerticalForm::Sideways; };

    hb_unicode_funcs_t* unicode = hb_unicode_funcs_get_default();

    // What HarfBuzz is given: the text with tabs as spaces, other controls
    // as zero-width spaces (drawn as nothing), and code points no font has
    // as their first font's replacement.
    std::u32string shaped(text);

    // The font and script of each code point in [begin, end).
    const size_t n = end - begin;
    std::vector<uint32_t> fontOf(n, 0);
    std::vector<hb_script_t> scriptOf(n, HB_SCRIPT_COMMON);

    // The fonts code point i may draw from: its chain, or all of them.
    auto chainOf = [&](size_t i) { return chains ? (*chains)[begin + i] : nullptr; };
    auto usable = [&](uint32_t f) { return f < fonts.size() && fonts[f] && fonts[f]->valid(); };
    auto inChain = [&](uint32_t f, const std::vector<uint32_t>* chain) {
        return !chain || std::find(chain->begin(), chain->end(), f) != chain->end();
    };
    auto firstFontWith = [&](char32_t c, const std::vector<uint32_t>* chain) -> int {
        if (chain) {
            for (const uint32_t f : *chain) {
                if (usable(f) && fonts[f]->findGlyph(c)) return static_cast<int>(f);
            }
            return -1;
        }
        for (size_t f = 0; f < fonts.size(); ++f) {
            if (usable(static_cast<uint32_t>(f)) && fonts[f]->findGlyph(c)) return static_cast<int>(f);
        }
        return -1;
    };

    for (size_t i = 0; i < n; ++i) {
        char32_t& c = shaped[begin + i];
        if (c == U'\t') c = U' ';
        else if (isControl(c)) c = 0x200B;

        const std::vector<uint32_t>* chain = chainOf(i);
        const bool follows = followsPrevious(unicode, c);
        if (follows && i > 0 && inChain(fontOf[i - 1], chain) && fonts[fontOf[i - 1]]->findGlyph(c)) {
            fontOf[i] = fontOf[i - 1];
        } else if (c == 0x200B && i > 0) {
            fontOf[i] = fontOf[i - 1];
        } else {
            const int f = firstFontWith(c, chain);
            const uint32_t lead = chain && !chain->empty() && usable(chain->front()) ? chain->front() : 0;
            if (f >= 0) {
                fontOf[i] = static_cast<uint32_t>(f);
            } else if (c != 0x200B) {
                c = fonts[lead]->findGlyph(0xFFFD) ? char32_t{0xFFFD} : char32_t{'?'};
                fontOf[i] = lead;
            } else {
                fontOf[i] = lead;
            }
        }
        scriptOf[i] = hb_unicode_script(unicode, c);
    }

    // Common and inherited characters take the script before them, or the
    // first real script after them at the start.
    hb_script_t current = HB_SCRIPT_COMMON;
    for (size_t i = 0; i < n; ++i) {
        const bool real = scriptOf[i] != HB_SCRIPT_COMMON && scriptOf[i] != HB_SCRIPT_INHERITED &&
                          scriptOf[i] != HB_SCRIPT_UNKNOWN;
        if (real) {
            current = scriptOf[i];
        } else if (current != HB_SCRIPT_COMMON) {
            scriptOf[i] = current;
        }
    }
    for (size_t i = n; i-- > 0;) {
        if (scriptOf[i] != HB_SCRIPT_COMMON && scriptOf[i] != HB_SCRIPT_INHERITED &&
            scriptOf[i] != HB_SCRIPT_UNKNOWN) {
            current = scriptOf[i];
        } else {
            scriptOf[i] = current;
        }
    }

    if (levels && levels->size() < n) levels = nullptr;
    auto levelOf = [&](size_t i) { return levels ? (*levels)[i] : uint8_t{0}; };

    hb_buffer_t* buffer = hb_buffer_create();
    for (size_t runStart = 0; runStart < n;) {
        size_t runEnd = runStart + 1;
        while (runEnd < n && fontOf[runEnd] == fontOf[runStart] && scriptOf[runEnd] == scriptOf[runStart] &&
               levelOf(runEnd) == levelOf(runStart) && formOf(runEnd) == formOf(runStart)) {
            ++runEnd;
        }
        const bool upright = formOf(runStart) == VerticalForm::Upright;

        const SdfFont& font = *fonts[fontOf[runStart]];
        hb_buffer_clear_contents(buffer);
        hb_buffer_add_utf32(buffer, reinterpret_cast<const uint32_t*>(shaped.data()),
                            static_cast<int>(shaped.size()), static_cast<unsigned int>(begin + runStart),
                            static_cast<int>(runEnd - runStart));
        const hb_script_t script = scriptOf[runStart] == HB_SCRIPT_COMMON ? HB_SCRIPT_LATIN : scriptOf[runStart];
        hb_direction_t direction = hb_script_get_horizontal_direction(script);
        if (direction == HB_DIRECTION_INVALID) direction = HB_DIRECTION_LTR;
        if (levels) direction = (levelOf(runStart) & 1u) ? HB_DIRECTION_RTL : HB_DIRECTION_LTR;
        if (upright) direction = HB_DIRECTION_TTB;
        hb_buffer_set_script(buffer, script);
        hb_buffer_set_direction(buffer, direction);
        hb_buffer_set_language(buffer, hb_language_get_default());
        hb_shape(static_cast<hb_font_t*>(font.shapingFont()), buffer, nullptr, 0);

        unsigned int count = 0;
        const hb_glyph_info_t* infos = hb_buffer_get_glyph_infos(buffer, &count);
        const hb_glyph_position_t* positions = hb_buffer_get_glyph_positions(buffer, &count);
        const float scale = font.unitScale();
        for (unsigned int g = 0; g < count; ++g) {
            ShapedGlyph glyph;
            glyph.font = fontOf[runStart];
            glyph.glyph = static_cast<int>(infos[g].codepoint);
            glyph.cluster = infos[g].cluster;
            glyph.advance = static_cast<float>(positions[g].x_advance) * scale;
            glyph.offset = glm::vec2(static_cast<float>(positions[g].x_offset),
                                     -static_cast<float>(positions[g].y_offset)) * scale;
            glyph.verticalAdvance = -static_cast<float>(positions[g].y_advance) * scale;
            glyph.upright = upright;
            out.push_back(glyph);
        }
        runStart = runEnd;
    }
    hb_buffer_destroy(buffer);
    return out;
}

} // namespace UI
} // namespace Shoonyakasha
