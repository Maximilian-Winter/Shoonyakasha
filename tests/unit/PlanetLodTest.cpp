//
// PlanetLodTest.cpp - the native level of detail of shoonyakasha.planet
//
// Tier 1: CPU only, no GPU.
//
// The cases covered: the roots taking slots and jobs, splitting near the
// camera once heights are back and drawing children only when all four are
// built, merging and releasing unused subtrees, a full pool, the draw lists'
// layout, culling behind the horizon, the sun's rebuild queue skipping slots
// that have a job, and dropping a tree. The Python test
// tests/python/test_planet_native_lod.py compares it with the Python version
// frame by frame.
//

#include <gtest/gtest.h>

#include "Planet/PlanetLod.h"

#include <cmath>
#include <set>

using namespace Shoonyakasha::Planet;

namespace {

constexpr double RADIUS = 100000.0;
constexpr int SLOTS = 64;
constexpr int JOBS = 16;
constexpr int VERTICES = 357;

LodBody planetBody() {
    LodBody body;
    body.index = 0;
    body.occluder = RADIUS - 2000.0;
    body.ocean = true;
    return body;
}

LodView viewAt(const Vec3& position, const Vec3& forward) {
    LodView view;
    view.position = position;
    view.forward = forward;
    view.fovDegrees = 70.0;
    view.aspect = 16.0 / 9.0;
    return view;
}

// Heights for every job of this frame, as BuildChunks writes them: (low, high, stamp, 0) per slot.
void answerJobs(PlanetLod& lod, std::vector<float>& rows, float low = 0.0f, float high = 10.0f) {
    for (const LodJob& job : lod.jobs()) {
        const size_t slot = static_cast<size_t>(job.b[0]);
        rows[slot * 4 + 0] = low;
        rows[slot * 4 + 1] = high;
        rows[slot * 4 + 2] = static_cast<float>(job.b[2]);
    }
    lod.clearJobs();
    lod.takeHeights(rows.data(), rows.size() / 4);
}

} // namespace

TEST(PlanetLod, RootsTakeSlotsAndJobs) {
    PlanetLod lod(SLOTS, JOBS, VERTICES, 0.05);
    lod.createTree(0, RADIUS, -2000.0, 6000.0, 12, 1.25, 0.05);
    ASSERT_EQ(lod.jobs().size(), 6u);
    EXPECT_EQ(lod.used(), 6);
    EXPECT_EQ(lod.pending(), 6u);
    std::set<int> slots;
    for (size_t k = 0; k < 6; ++k) {
        const LodJob& job = lod.jobs()[k];
        EXPECT_EQ(job.a[0], static_cast<double>(k));                // face
        EXPECT_EQ(job.a[1], 0.0);                                  // level
        EXPECT_EQ(job.b[2], static_cast<double>(k + 1));          // stamp
        slots.insert(static_cast<int>(job.b[0]));
    }
    EXPECT_EQ(slots, (std::set<int>{0, 1, 2, 3, 4, 5}));
    std::vector<float> rows(SLOTS * 4, 0.0f);
    answerJobs(lod, rows);
    EXPECT_EQ(lod.pending(), 0u);
}

TEST(PlanetLod, SplitsNearTheCameraAndDrawsChildrenWhenAllAreBuilt) {
    PlanetLod lod(256, JOBS, VERTICES, 0.05);
    lod.createTree(0, RADIUS, 0.0, 10.0, 12, 1.25, 0.05);
    std::vector<float> rows(256 * 4, 0.0f);
    answerJobs(lod, rows);
    const Vec3 above = cubeSphereDirection(4, 0.01, 0.01);
    const LodView view = viewAt({above[0] * (RADIUS + 1000.0), above[1] * (RADIUS + 1000.0),
                                 above[2] * (RADIUS + 1000.0)},
                                {-above[0], -above[1], -above[2]});
    lod.update(view, {planetBody()}, 1, false, false);
    // The roots near the camera want their children: they are missing and get jobs, up to the
    // frame's budget; the roots are still drawn.
    ASSERT_GE(lod.missing(0), 4u);
    EXPECT_EQ(lod.jobs().size(), std::min<size_t>(lod.missing(0), JOBS));
    EXPECT_EQ(lod.selectedCount(0), 6u);
    const auto drawnFaceRoot = [&](int face) {
        for (const LodNodeInfo& node : lod.selected(0)) {
            if (node.face == face && node.level == 0) return true;
        }
        return false;
    };
    int64_t frame = 1;
    while (lod.missing(0) > 0 && frame < 20) {
        answerJobs(lod, rows);
        lod.update(view, {planetBody()}, ++frame, false, false);
        if (!drawnFaceRoot(4)) break;
    }
    // Once its four children are built the +Z root is drawn through them.
    EXPECT_FALSE(drawnFaceRoot(4));
    int deepest = 0;
    for (const LodNodeInfo& node : lod.selected(0)) {
        deepest = std::max(deepest, node.level);
        EXPECT_GE(node.slot, 0);
    }
    EXPECT_GE(deepest, 1);
}

TEST(PlanetLod, ReleasesSubtreesUnusedForTheEvictionFrames) {
    PlanetLod lod(256, JOBS, VERTICES, 0.05, 3);
    lod.createTree(0, RADIUS, 0.0, 10.0, 12, 1.25, 0.05);
    std::vector<float> rows(256 * 4, 0.0f);
    answerJobs(lod, rows);
    const Vec3 above = cubeSphereDirection(4, 0.0, 0.0);
    const LodView near = viewAt({0.0, 0.0, RADIUS + 1000.0}, {0.0, 0.0, -1.0});
    for (int64_t frame = 1; frame <= 6; ++frame) {
        lod.update(near, {planetBody()}, frame, false, false);
        answerJobs(lod, rows);
    }
    const int usedNear = lod.used();
    EXPECT_GT(usedNear, 6);
    const LodView far = viewAt({0.0, 0.0, RADIUS * 20.0}, {0.0, 0.0, -1.0});
    for (int64_t frame = 7; frame <= 12; ++frame) {
        lod.update(far, {planetBody()}, frame, false, false);
        answerJobs(lod, rows);
    }
    EXPECT_EQ(lod.used(), 6);
    EXPECT_EQ(lod.nodes(0).size(), 6u);
    (void)above;
}

TEST(PlanetLod, AFullPoolBuildsNothingMore) {
    PlanetLod lod(8, JOBS, VERTICES, 0.05);
    lod.createTree(0, RADIUS, 0.0, 10.0, 12, 1.25, 0.05);
    std::vector<float> rows(8 * 4, 0.0f);
    answerJobs(lod, rows);
    const LodView view = viewAt({0.0, 0.0, RADIUS + 100.0}, {0.0, 0.0, -1.0});
    lod.update(view, {planetBody()}, 1, false, false);
    EXPECT_EQ(lod.used(), 8);
    EXPECT_EQ(lod.jobs().size(), 2u);
    answerJobs(lod, rows);
    lod.update(view, {planetBody()}, 2, false, false);
    EXPECT_TRUE(lod.jobs().empty());
    EXPECT_EQ(lod.selectedCount(0), 6u);     // the +Z root still drawn: not all four children built
}

TEST(PlanetLod, DrawListsHoldACountThenFiveWordsPerChunk) {
    PlanetLod lod(SLOTS, JOBS, VERTICES, 0.05);
    lod.createTree(0, RADIUS, -100.0, 10.0, 12, 1.25, 0.05);
    std::vector<float> rows(SLOTS * 4, 0.0f);
    answerJobs(lod, rows, -100.0f, 10.0f);
    const LodView view = viewAt({0.0, 0.0, RADIUS * 3.0}, {0.0, 0.0, -1.0});
    lod.update(view, {planetBody()}, 1, false, false);
    lod.writeDraws(view, {planetBody()}, 0, 25000.0, 2304);
    const auto& draws = lod.chunkDraws();
    const uint32_t count = draws[0];
    ASSERT_EQ(draws.size(), 4 + count * 5);
    EXPECT_EQ(count + lod.culled(), 6u);
    EXPECT_LT(count, 6u);                    // the far side of the planet is behind its horizon
    for (uint32_t k = 0; k < count; ++k) {
        const uint32_t* command = &draws[4 + k * 5];
        EXPECT_EQ(command[0], 2304u);
        EXPECT_EQ(command[1], 1u);
        EXPECT_EQ(command[2], 0u);
        EXPECT_EQ(command[3], command[4] * VERTICES);
    }
    EXPECT_EQ(lod.oceanDraws()[0], count);   // every chunk reaches below sea level
    EXPECT_EQ(lod.shadowDraws()[0], 0u);     // none within the shadows' reach
    // The terrain's own colours: red below 0.
    const float* instance = &lod.instances()[static_cast<size_t>(draws[4 + 4]) * 12];
    EXPECT_EQ(instance[4], -1.0f);
}

TEST(PlanetLod, HorizonCullsWhatTheSphereHides) {
    const Vec3 camera{0.0, 0.0, RADIUS + 1000.0};
    EXPECT_TRUE(hiddenByHorizon({0.0, 0.0, -RADIUS}, 1000.0, camera, RADIUS - 100.0));
    EXPECT_FALSE(hiddenByHorizon({0.0, 0.0, RADIUS}, 1000.0, camera, RADIUS - 100.0));
    EXPECT_FALSE(hiddenByHorizon({0.0, 0.0, -RADIUS}, 1000.0, {0.0, 0.0, RADIUS - 500.0}, RADIUS));
}

TEST(PlanetLod, RebuildQueueSkipsSlotsWithAJobThisFrame) {
    PlanetLod lod(SLOTS, JOBS, VERTICES, 0.05);
    lod.createTree(0, RADIUS, 0.0, 10.0, 12, 1.25, 0.05);
    // The roots' first jobs are still this frame's: queueing every built chunk adds none.
    const LodView view = viewAt({0.0, 0.0, RADIUS * 5.0}, {0.0, 0.0, -1.0});
    lod.update(view, {planetBody()}, 1, false, true);
    EXPECT_EQ(lod.jobs().size(), 6u);
    EXPECT_EQ(lod.rebuildQueued(), 0u);
    std::vector<float> rows(SLOTS * 4, 0.0f);
    answerJobs(lod, rows);
    lod.update(view, {planetBody()}, 2, false, true);
    EXPECT_EQ(lod.jobs().size(), 6u);        // each root built again once
    EXPECT_EQ(lod.pending(), 6u);
}

TEST(PlanetLod, DroppingATreeReleasesItsSlots) {
    PlanetLod lod(SLOTS, JOBS, VERTICES, 0.05);
    lod.createTree(0, RADIUS, 0.0, 10.0, 12, 1.25, 0.05);
    lod.createTree(1, RADIUS / 4.0, 0.0, 10.0, 12, 1.25, 0.05);
    EXPECT_EQ(lod.used(), 12);
    lod.dropTree(0);
    EXPECT_FALSE(lod.hasTree(0));
    EXPECT_TRUE(lod.hasTree(1));
    EXPECT_EQ(lod.used(), 6);
    EXPECT_EQ(lod.pending(), 6u);
}
