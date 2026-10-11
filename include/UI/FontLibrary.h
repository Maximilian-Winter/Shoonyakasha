//
// FontLibrary.h - The SDF fonts the canvas UI draws with
//
// Text names one font. Code points it lacks are drawn from its fallbacks, in
// order, or, for a font with none of its own, from the library's default
// fallbacks. Rich text can also name fonts, and use a font's bold and italic
// fonts.
//

#pragma once

#include "UI/SdfFont.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Shoonyakasha {
namespace UI {

class FontLibrary {
public:
    /// Id of the font at `path`, loading it on first use. Returns 0 when the
    /// file cannot be read as a font. Paths are used as given.
    uint32_t load(const std::string& path) {
        if (auto it = m_ids.find(path); it != m_ids.end()) return it->second;

        auto font = std::make_unique<SdfFont>();
        if (!font->loadFromFile(path)) return 0;

        m_fonts.push_back(std::move(font));
        const auto id = static_cast<uint32_t>(m_fonts.size());
        m_ids.emplace(path, id);
        // Named after its file, without the extension, unless that is taken.
        const size_t slash = path.find_last_of("/\\");
        std::string stem = path.substr(slash == std::string::npos ? 0 : slash + 1);
        if (const size_t dot = stem.rfind('.'); dot != std::string::npos && dot > 0) stem.erase(dot);
        m_names.try_emplace(stem, id);
        return id;
    }

    /// The name rich text's <font=name> finds `font` by, as well as its file
    /// name without the extension.
    void setName(uint32_t font, const std::string& name) { m_names[name] = font; }

    /// The font named `name`, or 0.
    uint32_t find(const std::string& name) const {
        const auto it = m_names.find(name);
        return it != m_names.end() && get(it->second) ? it->second : 0;
    }

    /// The fonts rich text uses for `font` in bold, italic, and both; 0 for
    /// none, which is then drawn faux.
    void setStyles(uint32_t font, uint32_t bold, uint32_t italic, uint32_t boldItalic) {
        m_styles[font] = {bold, italic, boldItalic};
    }

    /// The font for `font` in bold and italic as asked, and what of them is
    /// left to draw faux: the bold italic font; or the bold or italic font,
    /// the other faux; or `font` itself, both faux.
    struct Styled {
        uint32_t font = 0;
        bool fauxBold = false;
        bool fauxItalic = false;
    };
    Styled styled(uint32_t font, bool bold, bool italic) const {
        const auto it = m_styles.find(font);
        const Styles s = it != m_styles.end() ? it->second : Styles{};
        if (bold && italic && get(s.boldItalic)) return {s.boldItalic, false, false};
        if (bold && get(s.bold)) return {s.bold, false, italic};
        if (italic && get(s.italic)) return {s.italic, bold, false};
        return {font, bold, italic};
    }

    /// The font with `id`, or null for 0 and unknown ids.
    const SdfFont* get(uint32_t id) const {
        return id >= 1 && id <= m_fonts.size() ? m_fonts[id - 1].get() : nullptr;
    }

    /// The fonts drawn from, in order, for code points `font` lacks.
    void setFallbacks(uint32_t font, std::vector<uint32_t> fallbacks) {
        m_fallbacks[font] = std::move(fallbacks);
    }

    /// Fallbacks for fonts that have none of their own.
    void setDefaultFallbacks(std::vector<uint32_t> fallbacks) { m_defaultFallbacks = std::move(fallbacks); }

    /// `font` followed by its fallbacks: loaded fonts only, each once. Empty
    /// when `font` itself is not loaded.
    std::vector<uint32_t> chain(uint32_t font) const {
        std::vector<uint32_t> out;
        if (!get(font)) return out;
        out.push_back(font);
        const auto own = m_fallbacks.find(font);
        const auto& fallbacks = own != m_fallbacks.end() ? own->second : m_defaultFallbacks;
        for (const uint32_t id : fallbacks) {
            if (get(id) && std::find(out.begin(), out.end(), id) == out.end()) out.push_back(id);
        }
        return out;
    }

    /// The fonts of `ids`, as layoutText takes them.
    std::vector<const SdfFont*> resolve(const std::vector<uint32_t>& ids) const {
        std::vector<const SdfFont*> out;
        for (const uint32_t id : ids) {
            if (const SdfFont* font = get(id)) out.push_back(font);
        }
        return out;
    }

private:
    struct Styles {
        uint32_t bold = 0;
        uint32_t italic = 0;
        uint32_t boldItalic = 0;
    };

    std::vector<std::unique_ptr<SdfFont>> m_fonts;  // id - 1
    std::unordered_map<std::string, uint32_t> m_ids;
    std::unordered_map<std::string, uint32_t> m_names;
    std::unordered_map<uint32_t, Styles> m_styles;
    std::unordered_map<uint32_t, std::vector<uint32_t>> m_fallbacks;
    std::vector<uint32_t> m_defaultFallbacks;
};

} // namespace UI
} // namespace Shoonyakasha
