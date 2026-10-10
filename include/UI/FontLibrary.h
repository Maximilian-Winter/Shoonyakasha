//
// FontLibrary.h - The SDF fonts the canvas UI draws with
//

#pragma once

#include "UI/SdfFont.h"

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

private:
    std::vector<std::unique_ptr<SdfFont>> m_fonts;  // id - 1
    std::unordered_map<std::string, uint32_t> m_ids;
};

} // namespace UI
} // namespace Shoonyakasha
