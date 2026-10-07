//
// RenderStatsTest.cpp - frame rate windows, GPU timestamp resolution, draw counting
//
// Tier 1: the device-free parts of RenderStats.h. The collector itself needs
// a device and timestamp queries.
//

#include <gtest/gtest.h>

#include "Vulkan/FrameGraph/RenderStats.h"

using namespace Shoonyakasha::FrameGraph;

namespace {

constexpr uint32_t kNone = UINT32_MAX;

FrameStats frame(uint64_t number, double cpuMs, std::vector<std::pair<std::string, double>> passes,
                 uint32_t drawsPerPass = 1) {
    FrameStats f;
    f.frame = number;
    f.cpuRecordMs = cpuMs;
    for (auto& [name, gpu] : passes) {
        PassStats p;
        p.name = name;
        p.cpuMs = cpuMs / passes.size();
        p.gpuMs = gpu;
        p.gpuValid = gpu >= 0.0;
        p.draws.drawCalls = drawsPerPass;
        p.draws.vertices = drawsPerPass * 3;
        f.passes.push_back(p);
        f.draws += p.draws;
        if (p.gpuValid) f.gpuMs += gpu;
    }
    f.gpuValid = f.gpuMs > 0.0;
    return f;
}

} // namespace

// ── Window ──────────────────────────────────────────────────────

TEST(RenderStatsWindow, ClosesAfterTheWindowWithRateAndWorstFrame) {
    RenderStatsWindow window(100.0);
    RenderStats stats;
    for (int i = 0; i < 9; ++i) EXPECT_FALSE(window.addInterval(10.0, stats));
    EXPECT_TRUE(window.addInterval(20.0, stats));    // 110 ms over 10 frames
    EXPECT_NEAR(stats.fps, 10 * 1000.0 / 110.0, 1e-9);
    EXPECT_NEAR(stats.frameTimeMs, 11.0, 1e-9);
    EXPECT_DOUBLE_EQ(stats.frameTimeMaxMs, 20.0);

    // The next window starts afresh.
    for (int i = 0; i < 9; ++i) window.addInterval(10.0, stats);
    EXPECT_TRUE(window.addInterval(10.0, stats));
    EXPECT_DOUBLE_EQ(stats.frameTimeMaxMs, 10.0);
}

TEST(RenderStatsWindow, AveragesTheFramesOfTheWindow) {
    RenderStatsWindow window(100.0);
    RenderStats stats;
    window.addFrame(frame(1, 2.0, {{"GBuffer", 3.0}, {"Lighting", 1.0}}, 10), stats);
    window.addFrame(frame(2, 4.0, {{"GBuffer", 5.0}, {"Lighting", 1.0}}, 20), stats);
    EXPECT_EQ(stats.latest.frame, 2u);
    EXPECT_TRUE(window.addInterval(100.0, stats));

    const auto& avg = stats.average;
    EXPECT_DOUBLE_EQ(avg.cpuRecordMs, 3.0);
    EXPECT_DOUBLE_EQ(avg.gpuMs, 5.0);
    EXPECT_EQ(avg.draws.drawCalls, 30u);              // (20 + 40) / 2
    ASSERT_EQ(avg.passes.size(), 2u);
    EXPECT_EQ(avg.passes[0].name, "GBuffer");
    EXPECT_DOUBLE_EQ(avg.passes[0].gpuMs, 4.0);
    EXPECT_EQ(avg.passes[0].draws.drawCalls, 15u);
}

TEST(RenderStatsWindow, GpuAveragesSkipFramesWithoutResults) {
    RenderStatsWindow window(100.0);
    RenderStats stats;
    window.addFrame(frame(1, 1.0, {{"A", 2.0}}), stats);
    window.addFrame(frame(2, 1.0, {{"A", -1.0}}), stats);   // no GPU result
    window.addInterval(100.0, stats);
    EXPECT_TRUE(stats.average.gpuValid);
    EXPECT_DOUBLE_EQ(stats.average.passes[0].gpuMs, 2.0);
    EXPECT_DOUBLE_EQ(stats.average.gpuMs, 2.0);
}

TEST(RenderStatsWindow, ADifferentPassListRestartsThePassAverages) {
    RenderStatsWindow window(100.0);
    RenderStats stats;
    window.addFrame(frame(1, 1.0, {{"Old", 9.0}}), stats);
    window.addFrame(frame(2, 1.0, {{"New", 1.0}, {"Other", 1.0}}), stats);
    window.addInterval(100.0, stats);
    ASSERT_EQ(stats.average.passes.size(), 2u);
    EXPECT_EQ(stats.average.passes[0].name, "New");
    EXPECT_DOUBLE_EQ(stats.average.passes[0].gpuMs, 1.0);
}

// ── GPU times ───────────────────────────────────────────────────

TEST(ResolveGpuTimes, PassTimesAreDeltasBetweenConsecutiveEnds) {
    FrameStats f = frame(1, 1.0, {{"A", -1}, {"B", -1}, {"C", -1}});
    // Query 0 is the start; 1, 2, 3 the ends of A, B, C. 1 tick = 1000 ns.
    const std::vector<uint64_t> ts{1000, 3000, 3500, 7500};
    const std::vector<bool> ok(4, true);
    resolveGpuTimes(f, ts, ok, {1, 2, 3}, {0, 1, 2}, {0}, 1000.0, ~uint64_t{0});

    EXPECT_NEAR(f.passes[0].gpuMs, 2.0, 1e-9);
    EXPECT_NEAR(f.passes[1].gpuMs, 0.5, 1e-9);
    EXPECT_NEAR(f.passes[2].gpuMs, 4.0, 1e-9);
    EXPECT_TRUE(f.gpuValid);
    EXPECT_NEAR(f.gpuMs, 6.5, 1e-9);
}

TEST(ResolveGpuTimes, UnavailableQueriesLeaveTimesInvalid) {
    FrameStats f = frame(1, 1.0, {{"A", -1}, {"B", -1}});
    const std::vector<uint64_t> ts{0, 10, 20};
    const std::vector<bool> ok{true, true, false};
    resolveGpuTimes(f, ts, ok, {1, 2}, {0, 1}, {0}, 1.0, ~uint64_t{0});
    EXPECT_TRUE(f.passes[0].gpuValid);
    EXPECT_FALSE(f.passes[1].gpuValid);

    FrameStats g = frame(1, 1.0, {{"A", -1}});
    resolveGpuTimes(g, ts, ok, {kNone}, {kNone}, {}, 1.0, ~uint64_t{0});
    EXPECT_FALSE(g.passes[0].gpuValid);
    EXPECT_FALSE(g.gpuValid);
}

TEST(ResolveGpuTimes, CountersThatWrapWithinTheirValidBits) {
    // 32 valid bits: the end reads lower than the start.
    FrameStats f = frame(1, 1.0, {{"A", -1}});
    const std::vector<uint64_t> ts{0xFFFFFF00ull, 0x00000100ull};
    resolveGpuTimes(f, ts, {true, true}, {1}, {0}, {0}, 1e6, 0xFFFFFFFFull);
    EXPECT_NEAR(f.passes[0].gpuMs, 512.0, 1e-9);   // 0x200 ticks of 1 ms
    EXPECT_NEAR(f.gpuMs, 512.0, 1e-9);
}

TEST(ResolveGpuTimes, SeveralCommandBuffersSpanEarliestStartToLatestEnd) {
    // Compute batch starts at 100, graphics at 50; last end at 400.
    FrameStats f = frame(1, 1.0, {{"Sim", -1}, {"Draw", -1}});
    const std::vector<uint64_t> ts{100, 300, 50, 400};
    resolveGpuTimes(f, ts, std::vector<bool>(4, true), {1, 3}, {0, 2}, {0, 2}, 1e6, ~uint64_t{0});
    EXPECT_NEAR(f.passes[0].gpuMs, 200.0, 1e-9);
    EXPECT_NEAR(f.passes[1].gpuMs, 350.0, 1e-9);
    EXPECT_NEAR(f.gpuMs, 350.0, 1e-9);
}

// ── Draw counting ───────────────────────────────────────────────

TEST(DrawCounting, CountsOnlyWhileAPassIsActive) {
    countDraw(100);   // no active pass: dropped
    DrawCounts counts;
    RenderStatsCounting::active = &counts;
    countDraw(36, 10);
    countDraw(3);
    countDispatch();
    RenderStatsCounting::active = nullptr;
    countDraw(100);
    EXPECT_EQ(counts.drawCalls, 2u);
    EXPECT_EQ(counts.dispatches, 1u);
    EXPECT_EQ(counts.vertices, 363u);
}

// ── Summary ─────────────────────────────────────────────────────

TEST(RenderStatsSummary, ListsTheMostExpensivePassesFirst) {
    RenderStatsWindow window(100.0);
    RenderStats stats;
    stats.gpuTiming = true;
    window.addFrame(frame(1, 1.0, {{"Cheap", 0.1}, {"Expensive", 4.0}, {"Middle", 1.0}}), stats);
    window.addInterval(100.0, stats);

    const std::string text = formatRenderStats(stats, 2);
    EXPECT_NE(text.find("10.0 FPS"), std::string::npos) << text;
    EXPECT_LT(text.find("Expensive"), text.find("Middle")) << text;
    EXPECT_EQ(text.find("Cheap"), std::string::npos) << text;
}
