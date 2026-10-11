//
// GlyphCacheTest.cpp - Glyph atlas pages: growth and least-recently-used eviction
//
// Tier 1: pure CPU. Glyphs are rasterised from Roboto; copies of one glyph
// filed under different font ids fill the pages.
//

#include <gtest/gtest.h>

#include "UI/GlyphCache.h"

#include <algorithm>
#include <string>

using namespace Shoonyakasha::UI;

namespace {

const std::string kRoboto = std::string(SHOONYAKASHA_SOURCE_DIR) + "/assets/fonts/Roboto-Regular.ttf";

constexpr int kPage = 160;  // a few glyphs per page

struct Fixture {
    SdfFont font;
    bool loaded = font.loadFromFile(kRoboto);
    int q = font.findGlyph(U'q');
    uint32_t nextId = 1000;

    /// Adds copies of 'q' until `cache` has `pages` pages and the last one is
    /// full. Returns the font ids added, by page.
    std::vector<std::vector<uint32_t>> fill(GlyphCache& cache, uint32_t pages) {
        std::vector<std::vector<uint32_t>> ids(pages);
        while (true) {
            const auto entry = cache.findOrAdd(nextId, q, font);
            if (!entry) break;
            if (entry->page >= pages) break;
            ids[entry->page].push_back(nextId++);
            if (cache.pageCount() > pages) break;
        }
        return ids;
    }
};

} // namespace

TEST(GlyphCache, StartsWithOnePage) {
    GlyphCache cache(kPage, 3);
    EXPECT_EQ(cache.pageCount(), 1u);
    EXPECT_EQ(cache.maxPages(), 3);
}

TEST(GlyphCache, AddsPagesUpToTheMaximum) {
    Fixture f;
    ASSERT_TRUE(f.loaded);
    GlyphCache cache(kPage, 3);

    const auto ids = f.fill(cache, 3);
    EXPECT_EQ(cache.pageCount(), 3u);
    for (const auto& page : ids) EXPECT_FALSE(page.empty());

    // Every page was drawn from in this frame, so nothing can be evicted.
    EXPECT_FALSE(cache.findOrAdd(f.nextId, f.q, f.font).has_value());
    EXPECT_EQ(cache.pageCount(), 3u);
    EXPECT_EQ(cache.evictions(), 0u);

    // Each upload names the page its glyph went to.
    ASSERT_EQ(cache.pendingUploads().size(), ids[0].size() + ids[1].size() + ids[2].size());
    EXPECT_EQ(cache.pendingUploads().front().page, 0u);
    EXPECT_EQ(cache.pendingUploads().back().page, 2u);
}

TEST(GlyphCache, EvictsTheLeastRecentlyUsedPage) {
    Fixture f;
    GlyphCache cache(kPage, 3);
    const auto ids = f.fill(cache, 3);
    cache.clearPendingUploads();

    // Pages 0 and 2 are drawn from in later frames; page 1 is the oldest.
    cache.beginFrame();
    ASSERT_EQ(cache.findOrAdd(ids[0][0], f.q, f.font)->page, 0u);
    cache.beginFrame();
    ASSERT_EQ(cache.findOrAdd(ids[2][0], f.q, f.font)->page, 2u);
    EXPECT_LT(cache.pageLastUsed(1), cache.pageLastUsed(0));
    EXPECT_LT(cache.pageLastUsed(0), cache.pageLastUsed(2));

    const size_t before = cache.glyphCount();
    const auto entry = cache.findOrAdd(f.nextId, f.q, f.font);
    ASSERT_TRUE(entry.has_value());
    EXPECT_EQ(entry->page, 1u);
    EXPECT_EQ(cache.evictions(), 1u);
    EXPECT_EQ(cache.generation(), 0u);
    EXPECT_EQ(cache.glyphCount(), before - ids[1].size() + 1);
    ASSERT_EQ(cache.pendingClears().size(), 1u);
    EXPECT_EQ(cache.pendingClears()[0], 1u);

    // The other pages keep their glyphs: finding them queues nothing.
    cache.clearPendingUploads();
    EXPECT_EQ(cache.findOrAdd(ids[0].back(), f.q, f.font)->page, 0u);
    EXPECT_EQ(cache.findOrAdd(ids[2].back(), f.q, f.font)->page, 2u);
    EXPECT_TRUE(cache.pendingUploads().empty());
}

TEST(GlyphCache, EvictedGlyphsComeBackWhenDrawnAgain) {
    Fixture f;
    GlyphCache cache(kPage, 1);
    const auto ids = f.fill(cache, 1);
    cache.clearPendingUploads();
    const uint32_t evicted = ids[0].front();

    cache.beginFrame();
    ASSERT_TRUE(cache.findOrAdd(f.nextId++, f.q, f.font).has_value());  // evicts the page
    EXPECT_EQ(cache.evictions(), 1u);
    cache.clearPendingUploads();

    // In the same frame the page is in use, but it has room for the glyph again.
    const auto back = cache.findOrAdd(evicted, f.q, f.font);
    ASSERT_TRUE(back.has_value());
    EXPECT_EQ(back->page, 0u);
    ASSERT_EQ(cache.pendingUploads().size(), 1u);
    EXPECT_EQ(cache.evictions(), 1u);
}

TEST(GlyphCache, EvictionDropsTheUploadsQueuedForThePage) {
    Fixture f;
    GlyphCache cache(kPage, 2);
    const auto ids = f.fill(cache, 2);

    // Page 1 is drawn from later, so page 0 is evicted, along with the uploads
    // still queued for it.
    cache.beginFrame();
    cache.findOrAdd(ids[1][0], f.q, f.font);
    ASSERT_EQ(cache.findOrAdd(f.nextId, f.q, f.font)->page, 0u);

    const auto& uploads = cache.pendingUploads();
    const size_t onPage0 = std::count_if(uploads.begin(), uploads.end(),
                                         [](const GlyphCache::Upload& u) { return u.page == 0; });
    EXPECT_EQ(onPage0, 1u);  // only the new glyph
    EXPECT_EQ(uploads.size(), ids[1].size() + 1);
}

TEST(GlyphCache, ResetQueuesEveryPageForClearing) {
    Fixture f;
    GlyphCache cache(kPage, 2);
    f.fill(cache, 2);
    cache.reset();

    EXPECT_EQ(cache.glyphCount(), 0u);
    EXPECT_TRUE(cache.pendingUploads().empty());
    EXPECT_EQ(cache.pendingClears(), (std::vector<uint32_t>{0, 1}));
    EXPECT_EQ(cache.pageCount(), 2u);
    EXPECT_EQ(cache.generation(), 1u);
}
