//
// GlyphCache.h - Where each glyph's distance field lives in the glyph atlas
//
// The CPU side of the atlas: it rasterises glyphs on first use, packs them,
// and queues their bitmaps for upload. The GPU texture copies the queued
// uploads and then clears the queue.
//

#pragma once

#include "UI/GlyphAtlasPacker.h"
#include "UI/SdfFont.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

namespace Shoonyakasha {
namespace UI {

class GlyphCache {
public:
    static constexpr int kDefaultSize = 2048;

    struct Entry {
        glm::vec2 uvMin{0.0f};
        glm::vec2 uvMax{0.0f};
    };

    struct Upload {
        glm::ivec2 position{0};
        SdfFont::Bitmap bitmap;
    };

    explicit GlyphCache(int size = kDefaultSize) : m_size(size), m_packer(size, size, 1) {}

    /// Atlas UVs of `glyph` from font `fontId`, rasterising and queueing it if
    /// it is not in the atlas yet. Nothing when the glyph has no outline or
    /// the atlas is full.
    std::optional<Entry> findOrAdd(uint32_t fontId, int glyph, const SdfFont& font) {
        const uint64_t key = (static_cast<uint64_t>(fontId) << 32) | static_cast<uint32_t>(glyph);
        if (auto it = m_entries.find(key); it != m_entries.end()) return it->second;

        SdfFont::Bitmap bitmap = font.rasterize(glyph);
        if (bitmap.pixels.empty()) return std::nullopt;

        const auto position = m_packer.insert(bitmap.size.x, bitmap.size.y);
        if (!position) return std::nullopt;

        const float size = static_cast<float>(m_size);
        Entry entry;
        entry.uvMin = glm::vec2(*position) / size;
        entry.uvMax = glm::vec2(*position + bitmap.size) / size;
        m_entries.emplace(key, entry);
        m_uploads.push_back({*position, std::move(bitmap)});
        return entry;
    }

    /// Forgets every glyph. The atlas texture keeps its old pixels, but no
    /// entry refers to them any more.
    void reset() {
        m_entries.clear();
        m_uploads.clear();
        m_packer.reset();
        ++m_generation;
    }

    const std::vector<Upload>& pendingUploads() const { return m_uploads; }
    void clearPendingUploads() { m_uploads.clear(); }

    int size() const { return m_size; }
    size_t glyphCount() const { return m_entries.size(); }
    uint64_t generation() const { return m_generation; }  // incremented by reset()

private:
    int m_size;
    GlyphAtlasPacker m_packer;
    std::unordered_map<uint64_t, Entry> m_entries;
    std::vector<Upload> m_uploads;
    uint64_t m_generation = 0;
};

} // namespace UI
} // namespace Shoonyakasha
