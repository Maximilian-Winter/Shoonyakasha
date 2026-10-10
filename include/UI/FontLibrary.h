//
// FontLibrary.h - The SDF fonts the canvas UI draws with
//
// Text names one font. Code points it lacks are drawn from its fallbacks, in
// order, or, for a font with none of its own, from the library's default
// fallbacks.
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
        return id;
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
    std::vector<std::unique_ptr<SdfFont>> m_fonts;  // id - 1
    std::unordered_map<std::string, uint32_t> m_ids;
    std::unordered_map<uint32_t, std::vector<uint32_t>> m_fallbacks;
    std::vector<uint32_t> m_defaultFallbacks;
};

} // namespace UI
} // namespace Shoonyakasha
