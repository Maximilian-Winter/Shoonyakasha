//
// SharedBufferRegistryTest.cpp - Tests for cross-graph buffer/image sharing
//
// Tier 1: Pure unit tests — no GPU context needed
//

#include <gtest/gtest.h>
#include "FrameGraph/SharedBufferRegistry.h"
#include "TestHelpers.h"

using namespace Shoonyakasha::FrameGraph;

TEST(SharedBufferRegistry, RegisterBuffer_HasReturnsTrue) {
    SharedBufferRegistry reg;
    SharedBufferEntry entry;
    entry.size = 1024;

    reg.registerBuffer("particles", entry);
    EXPECT_TRUE(reg.hasBuffer("particles"));
}

TEST(SharedBufferRegistry, GetBuffer_ReturnsCorrectData) {
    SharedBufferRegistry reg;
    SharedBufferEntry entry;
    entry.size = 2048;
    entry.elementCount = 100;
    entry.elementStride = 16;
    entry.producerGraph = "compute";

    reg.registerBuffer("data", entry);

    const auto* result = reg.getBuffer("data");
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->size, 2048u);
    EXPECT_EQ(result->elementCount, 100u);
    EXPECT_EQ(result->elementStride, 16u);
    EXPECT_EQ(result->producerGraph, "compute");
}

TEST(SharedBufferRegistry, HasBuffer_Unregistered_False) {
    SharedBufferRegistry reg;
    EXPECT_FALSE(reg.hasBuffer("nonexistent"));
}

TEST(SharedBufferRegistry, GetBuffer_Unregistered_Null) {
    SharedBufferRegistry reg;
    EXPECT_EQ(reg.getBuffer("nonexistent"), nullptr);
}

TEST(SharedBufferRegistry, UnregisterBuffer_RemovesEntry) {
    SharedBufferRegistry reg;
    SharedBufferEntry entry;
    entry.size = 512;

    reg.registerBuffer("temp", entry);
    EXPECT_TRUE(reg.hasBuffer("temp"));

    reg.unregisterBuffer("temp");
    EXPECT_FALSE(reg.hasBuffer("temp"));
}

TEST(SharedBufferRegistry, GetVersion_IncrementOnReRegister) {
    SharedBufferRegistry reg;
    SharedBufferEntry entry;
    entry.version = 0;

    reg.registerBuffer("data", entry);
    EXPECT_EQ(reg.getVersion("data"), 0u);

    entry.version = 1;
    reg.registerBuffer("data", entry);
    EXPECT_EQ(reg.getVersion("data"), 1u);
}

TEST(SharedBufferRegistry, GetVersion_Unregistered_ReturnsZero) {
    SharedBufferRegistry reg;
    EXPECT_EQ(reg.getVersion("nonexistent"), 0u);
}

TEST(SharedBufferRegistry, RegisterImage_HasReturnsTrue) {
    SharedBufferRegistry reg;
    SharedImageEntry entry;
    entry.format = VK_FORMAT_R8G8B8A8_SRGB;
    entry.extent = {1920, 1080};

    reg.registerImage("colorTarget", entry);
    EXPECT_TRUE(reg.hasImage("colorTarget"));
}

TEST(SharedBufferRegistry, GetImage_ReturnsCorrectData) {
    SharedBufferRegistry reg;
    SharedImageEntry entry;
    entry.format = VK_FORMAT_D32_SFLOAT;
    entry.extent = {1024, 768};
    entry.producerGraph = "shadow";

    reg.registerImage("depth", entry);

    const auto* result = reg.getImage("depth");
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->format, VK_FORMAT_D32_SFLOAT);
    EXPECT_EQ(result->extent.width, 1024u);
    EXPECT_EQ(result->extent.height, 768u);
    EXPECT_EQ(result->producerGraph, "shadow");
}

TEST(SharedBufferRegistry, UnregisterImage_RemovesEntry) {
    SharedBufferRegistry reg;
    SharedImageEntry entry;

    reg.registerImage("target", entry);
    EXPECT_TRUE(reg.hasImage("target"));

    reg.unregisterImage("target");
    EXPECT_FALSE(reg.hasImage("target"));
}

TEST(SharedBufferRegistry, HasImage_Unregistered_False) {
    SharedBufferRegistry reg;
    EXPECT_FALSE(reg.hasImage("nonexistent"));
}

TEST(SharedBufferRegistry, Clear_RemovesAll) {
    SharedBufferRegistry reg;
    SharedBufferEntry buf;
    SharedImageEntry img;

    reg.registerBuffer("buf1", buf);
    reg.registerBuffer("buf2", buf);
    reg.registerImage("img1", img);

    reg.clear();

    EXPECT_FALSE(reg.hasBuffer("buf1"));
    EXPECT_FALSE(reg.hasBuffer("buf2"));
    EXPECT_FALSE(reg.hasImage("img1"));
}

TEST(SharedBufferRegistry, BufferAndImage_IndependentNamespaces) {
    SharedBufferRegistry reg;
    SharedBufferEntry buf;
    buf.size = 100;
    SharedImageEntry img;
    img.format = VK_FORMAT_R8_UNORM;

    // Same name, different types
    reg.registerBuffer("foo", buf);
    reg.registerImage("foo", img);

    EXPECT_TRUE(reg.hasBuffer("foo"));
    EXPECT_TRUE(reg.hasImage("foo"));

    // Remove buffer, image stays
    reg.unregisterBuffer("foo");
    EXPECT_FALSE(reg.hasBuffer("foo"));
    EXPECT_TRUE(reg.hasImage("foo"));
}
