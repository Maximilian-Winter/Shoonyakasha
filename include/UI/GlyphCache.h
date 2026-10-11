//
// GlyphCache.h - Where each glyph's distance field lives in the glyph atlas
//
// The CPU side of the atlas: it rasterises glyphs on first use, packs them
// into pages, and queues their bitmaps for upload. The atlas starts with one
// page and adds pages up to maxPages(). When every page is full, the page
// least recently drawn from is cleared and its glyphs are forgotten; they are
// rasterised again when next drawn. The GPU texture clears the queued pages,
// copies the queued uploads, and then clears both queues.
//

#pragma once

#include "UI/GlyphAtlasPacker.h"
#include "UI/SdfFont.h"

#include <glm/glm.hpp>

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <optional>
#include <unordered_map>
#include <vector>

namespace Shoonyakasha {
namespace UI {

class GlyphCache {
public:
    static constexpr int kDefaultSize = 2048;
    static constexpr int kDefaultMaxPages = 4;

    struct Entry {
        glm::vec2 uvMin{0.0f};
        glm::vec2 uvMax{0.0f};
        uint32_t page = 0;
    };

    struct Upload {
        glm::ivec2 position{0};
        uint32_t page = 0;
        SdfFont::Bitmap bitmap;
    };

    /// Pages are `size` texels square.
    explicit GlyphCache(int size = kDefaultSize, int maxPages = kDefaultMaxPages)
        : m_size(size), m_maxPages(std::max(maxPages, 1)) {
        addPage();
    }

    /// Starts a frame. Pages drawn from in the current frame are never
    /// evicted, so the glyphs placed so far stay where they are.
    void beginFrame() { ++m_frame; }

    /// Atlas UVs and page of `glyph` from font `fontId`, rasterising and
    /// queueing it if it is not in the atlas yet. Nothing when the glyph has
    /// no outline, or when no page has room and every page was drawn from in
    /// this frame.
    std::optional<Entry> findOrAdd(uint32_t fontId, int glyph, const SdfFont& font) {
        const uint64_t key = (static_cast<uint64_t>(fontId) << 32) | static_cast<uint32_t>(glyph);
        if (auto it = m_entries.find(key); it != m_entries.end()) {
            m_pages[it->second.page].lastUsed = m_frame;
            return it->second;
        }

        SdfFont::Bitmap bitmap = font.rasterize(glyph);
        if (bitmap.pixels.empty()) return std::nullopt;
        if (bitmap.size.x > m_size || bitmap.size.y > m_size) return std::nullopt;

        std::optional<glm::ivec2> position;
        uint32_t page = 0;
        for (; page < m_pages.size(); ++page) {
            position = m_pages[page].packer.insert(bitmap.size.x, bitmap.size.y);
            if (position) break;
        }
        if (!position && static_cast<int>(m_pages.size()) < m_maxPages) {
            page = addPage();
            position = m_pages[page].packer.insert(bitmap.size.x, bitmap.size.y);
        }
        if (!position) {
            const auto oldest = std::min_element(m_pages.begin(), m_pages.end(),
                [](const Page& a, const Page& b) { return a.lastUsed < b.lastUsed; });
            if (oldest->lastUsed == m_frame) return std::nullopt;
            page = static_cast<uint32_t>(oldest - m_pages.begin());
            evict(page);
            position = m_pages[page].packer.insert(bitmap.size.x, bitmap.size.y);
            if (!position) return std::nullopt;
        }

        const float size = static_cast<float>(m_size);
        Entry entry;
        entry.uvMin = glm::vec2(*position) / size;
        entry.uvMax = glm::vec2(*position + bitmap.size) / size;
        entry.page = page;
        m_entries.emplace(key, entry);
        m_pages[page].lastUsed = m_frame;
        m_uploads.push_back({*position, page, std::move(bitmap)});
        return entry;
    }

    /// Forgets every glyph and queues every page to be cleared. The number
    /// of pages stays.
    void reset() {
        m_entries.clear();
        m_uploads.clear();
        m_clears.clear();
        for (uint32_t page = 0; page < m_pages.size(); ++page) {
            m_pages[page].packer.reset();
            m_clears.push_back(page);
        }
        ++m_generation;
    }

    const std::vector<Upload>& pendingUploads() const { return m_uploads; }
    void clearPendingUploads() { m_uploads.clear(); }

    /// Pages whose glyphs were forgotten since the queue was last cleared.
    /// Their texels must be cleared before the pending uploads are copied.
    const std::vector<uint32_t>& pendingClears() const { return m_clears; }
    void clearPendingClears() { m_clears.clear(); }

    int size() const { return m_size; }
    int maxPages() const { return m_maxPages; }
    uint32_t pageCount() const { return static_cast<uint32_t>(m_pages.size()); }
    uint64_t pageLastUsed(uint32_t page) const { return m_pages.at(page).lastUsed; }
    uint64_t frame() const { return m_frame; }
    size_t glyphCount() const { return m_entries.size(); }
    uint64_t generation() const { return m_generation; }  // incremented by reset()
    uint64_t evictions() const { return m_evictions; }    // pages cleared to make room

private:
    struct Page {
        GlyphAtlasPacker packer;
        uint64_t lastUsed = 0;  // frame
    };

    uint32_t addPage() {
        m_pages.push_back({GlyphAtlasPacker(m_size, m_size, 1), m_frame});
        return static_cast<uint32_t>(m_pages.size() - 1);
    }

    void evict(uint32_t page) {
        for (auto it = m_entries.begin(); it != m_entries.end();) {
            it = it->second.page == page ? m_entries.erase(it) : std::next(it);
        }
        m_uploads.erase(std::remove_if(m_uploads.begin(), m_uploads.end(),
                                       [page](const Upload& u) { return u.page == page; }),
                        m_uploads.end());
        m_pages[page].packer.reset();
        if (std::find(m_clears.begin(), m_clears.end(), page) == m_clears.end()) m_clears.push_back(page);
        ++m_evictions;
    }

    int m_size;
    int m_maxPages;
    std::vector<Page> m_pages;
    std::unordered_map<uint64_t, Entry> m_entries;
    std::vector<Upload> m_uploads;
    std::vector<uint32_t> m_clears;
    uint64_t m_frame = 0;
    uint64_t m_generation = 0;
    uint64_t m_evictions = 0;
};

} // namespace UI
} // namespace Shoonyakasha
