//
// StreamingTest.cpp - GPU component tables, buffer streams, and the
// "components" scene source
//
// Tier 1: no GPU.
//

#include <gtest/gtest.h>

#include "ECS/GpuComponents.h"
#include "Facade/EngineAPI.h"
#include "Vulkan/FrameGraph/FrameGraph.h"
#include "Vulkan/FrameGraph/FrameGraphJson.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

using namespace Shoonyakasha;

namespace {

std::vector<float> rowsOf(const ECS::GpuComponentTables::Table& table) {
    std::vector<float> out(table.rows.size() / sizeof(float));
    std::memcpy(out.data(), table.rows.data(), table.rows.size());
    return out;
}

} // namespace

TEST(GpuComponentTables, RowsAreSetReplacedAndRemoved) {
    entt::registry registry;
    const auto a = registry.create(), b = registry.create(), c = registry.create();
    ECS::GpuComponentTables tables;

    const float rowA[2] = {1, 2}, rowB[2] = {3, 4}, rowC[2] = {5, 6};
    EXPECT_EQ(tables.set(a, "P", rowA, sizeof rowA), "");
    EXPECT_EQ(tables.set(b, "P", rowB, sizeof rowB), "");
    EXPECT_EQ(tables.set(c, "P", rowC, sizeof rowC), "");
    const auto* table = tables.find("P");
    ASSERT_NE(table, nullptr);
    EXPECT_EQ(table->stride, 8u);
    EXPECT_EQ(rowsOf(*table), (std::vector<float>{1, 2, 3, 4, 5, 6}));

    const float again[2] = {9, 9};
    EXPECT_EQ(tables.set(b, "P", again, sizeof again), "");          // replaced in place
    EXPECT_EQ(rowsOf(*table), (std::vector<float>{1, 2, 9, 9, 5, 6}));

    EXPECT_TRUE(tables.remove(a, "P"));                               // the last row moves into its place
    EXPECT_EQ(rowsOf(*table), (std::vector<float>{5, 6, 9, 9}));
    EXPECT_FALSE(tables.remove(a, "P"));
    EXPECT_FALSE(tables.remove(a, "Missing"));

    const float wide[3] = {0, 0, 0};
    EXPECT_NE(tables.set(a, "P", wide, sizeof wide).find("rows of 8 bytes"), std::string::npos);
    EXPECT_NE(tables.set(a, "P", wide, 0), "");
    EXPECT_EQ(tables.find("Missing"), nullptr);
}

TEST(GpuComponentTables, DestroyedEntitiesDropOut) {
    entt::registry registry;
    std::vector<entt::entity> entities;
    for (int i = 0; i < 6; ++i) entities.push_back(registry.create());
    std::vector<float> rows = {0, 1, 2, 3, 4, 5};
    ECS::GpuComponentTables tables;
    EXPECT_EQ(tables.setMany("P", entities.data(), entities.size(), rows.data(), sizeof(float)), "");

    registry.destroy(entities[1]);
    registry.destroy(entities[4]);
    registry.destroy(entities[5]);
    tables.dropInvalid(registry);

    const auto* table = tables.find("P");
    ASSERT_EQ(table->count(), 3u);
    auto left = rowsOf(*table);
    std::sort(left.begin(), left.end());
    EXPECT_EQ(left, (std::vector<float>{0, 2, 3}));
    for (size_t row = 0; row < table->count(); ++row) {                // the index follows the moves
        EXPECT_EQ(table->index.at(table->entities[row]), row);
    }
}

TEST(BufferStream, CoveredWritesAreReplaced) {
    Facade::BufferStream stream("B");
    const uint8_t a[4] = {1, 1, 1, 1}, b[2] = {2, 2}, c[8] = {3, 3, 3, 3, 3, 3, 3, 3};
    stream.write(a, 4, 0);
    stream.write(b, 2, 8);
    EXPECT_EQ(stream.pendingBytes(), 6u);
    stream.write(c, 8, 0);       // covers the first write, not the second
    EXPECT_EQ(stream.pendingBytes(), 10u);

    const auto writes = stream.take();
    ASSERT_EQ(writes.size(), 2u);
    EXPECT_EQ(writes[0].offset, 8u);
    EXPECT_EQ(writes[1].offset, 0u);
    EXPECT_EQ(writes[1].bytes.size(), 8u);
    EXPECT_EQ(stream.pendingBytes(), 0u);
    EXPECT_TRUE(stream.take().empty());

    stream.report(2, "");
    EXPECT_EQ(stream.applied(), 2u);
    stream.report(1, "too big");
    EXPECT_EQ(stream.applied(), 3u);
    EXPECT_EQ(stream.error(), "too big");
}

TEST(BufferStream, WritesFromManyThreads) {
    Facade::BufferStream stream("B");
    std::vector<std::thread> threads;
    for (uint32_t t = 0; t < 8; ++t) {
        threads.emplace_back([&stream, t] {
            for (uint32_t i = 0; i < 1000; ++i) {
                const uint32_t value = t * 1000 + i;
                stream.write(&value, sizeof value, t * sizeof value);   // each thread its own slot
            }
        });
    }
    for (auto& thread : threads) thread.join();
    const auto writes = stream.take();
    ASSERT_EQ(writes.size(), 8u);            // each slot's writes replaced by its last
    for (const auto& write : writes) {
        uint32_t value = 0;
        std::memcpy(&value, write.bytes.data(), sizeof value);
        EXPECT_EQ(value % 1000, 999u);
        EXPECT_EQ(write.offset, (value / 1000) * sizeof value);
    }
}

TEST(ComponentsSource, ParsedAndChecked) {
    using nlohmann::json;
    json pipeline = {
        {"bufferLayouts", {{"Particles", {{"usage", "storage_buffer"}, {"elementCount", 64},
                                          {"source", {{"type", "scene"}, {"data", "components"},
                                                      {"component", "Particle"}}},
                                          {"fields", json::array({{{"name", "p"}, {"type", "vec4"}}})}}}}},
        {"resources", json::array()},
        {"passes", json::array()},
    };
    FrameGraph::FrameGraphBuilder builder;
    FrameGraph::loadGraphFromJson(builder, pipeline);
    const auto* layout = builder.getBufferLayout("Particles");
    ASSERT_NE(layout, nullptr);
    EXPECT_EQ(layout->initConfig.sceneData, "components");
    EXPECT_EQ(layout->initConfig.sceneComponent, "Particle");

    pipeline["bufferLayouts"]["Particles"]["source"].erase("component");
    FrameGraph::FrameGraphBuilder second;
    EXPECT_THROW(FrameGraph::loadGraphFromJson(second, pipeline), std::runtime_error);
}
