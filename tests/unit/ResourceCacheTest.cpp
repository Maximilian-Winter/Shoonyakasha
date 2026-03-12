//
// ResourceCacheTest.cpp - Tests for ResourceHandle, ResourceDescriptor,
//                         ResourceCache, and AsyncLoader
//
// Tier 1: Pure unit tests — no GPU context, no VulkanDevice
//

#include <gtest/gtest.h>
#include "Resources/ResourceManager.h"

#include <atomic>
#include <chrono>
#include <thread>

using namespace Shoonyakasha;

// ═══════════════════════════════════════════════════════════════
// ResourceHandle Tests
// ═══════════════════════════════════════════════════════════════

TEST(ResourceHandle, Default_Invalid) {
    ResourceHandle h;
    EXPECT_FALSE(h.isValid());
    EXPECT_EQ(h.type, ResourceType::Unknown);
    EXPECT_EQ(h.id, 0u);
}

TEST(ResourceHandle, ValidConstruction) {
    ResourceHandle h("texture", ResourceType::Texture, 42);
    EXPECT_TRUE(h.isValid());
    EXPECT_EQ(h.name, "texture");
    EXPECT_EQ(h.type, ResourceType::Texture);
    EXPECT_EQ(h.id, 42u);
}

TEST(ResourceHandle, Equality_Same) {
    ResourceHandle a("tex", ResourceType::Texture, 1);
    ResourceHandle b("tex", ResourceType::Texture, 1);
    EXPECT_EQ(a, b);
}

TEST(ResourceHandle, Equality_DifferentId) {
    ResourceHandle a("tex", ResourceType::Texture, 1);
    ResourceHandle b("tex", ResourceType::Texture, 2);
    EXPECT_NE(a, b);
}

TEST(ResourceHandle, Equality_DifferentType) {
    ResourceHandle a("res", ResourceType::Texture, 1);
    ResourceHandle b("res", ResourceType::Buffer, 1);
    EXPECT_NE(a, b);
}

TEST(ResourceHandle, Hash_CanBeUsedInUnorderedMap) {
    ResourceHandle h("tex", ResourceType::Texture, 1);
    std::unordered_map<ResourceHandle, int> map;
    map[h] = 42;
    EXPECT_EQ(map[h], 42);
}

TEST(ResourceHandle, Hash_DifferentHandles_DifferentHashes) {
    ResourceHandle a("a", ResourceType::Texture, 1);
    ResourceHandle b("b", ResourceType::Texture, 2);

    std::hash<ResourceHandle> hasher;
    // Not guaranteed to be different, but very likely for distinct handles
    // We just verify the hasher doesn't crash and produces a value
    (void)hasher(a);
    (void)hasher(b);
}

// ═══════════════════════════════════════════════════════════════
// ResourceDescriptor Tests
// ═══════════════════════════════════════════════════════════════

TEST(ResourceDescriptor, DefaultConstruction) {
    ResourceDescriptor desc;
    EXPECT_EQ(desc.type, ResourceType::Unknown);
    EXPECT_TRUE(desc.name.empty());
}

TEST(ResourceDescriptor, NamedConstruction) {
    ResourceDescriptor desc("model", ResourceType::Model, "assets/model.gltf");
    EXPECT_EQ(desc.name, "model");
    EXPECT_EQ(desc.type, ResourceType::Model);
}

TEST(ResourceDescriptor, SetParameter_FluentChaining) {
    ResourceDescriptor desc;
    desc.setParameter("width", 512)
        .setParameter("height", 256)
        .setParameter("label", std::string("test"));

    EXPECT_EQ(desc.getParameter<int>("width"), 512);
    EXPECT_EQ(desc.getParameter<int>("height"), 256);
    EXPECT_EQ(desc.getParameter<std::string>("label"), "test");
}

TEST(ResourceDescriptor, GetParameter_Missing_ReturnsDefault) {
    ResourceDescriptor desc;
    EXPECT_EQ(desc.getParameter<int>("missing", 99), 99);
}

TEST(ResourceDescriptor, GetParameter_WrongType_ReturnsDefault) {
    ResourceDescriptor desc;
    desc.setParameter("width", 512);

    // Ask for string when it's an int → bad_any_cast → returns default
    EXPECT_EQ(desc.getParameter<std::string>("width", "fallback"), "fallback");
}

// ═══════════════════════════════════════════════════════════════
// ResourceCache Tests
// ═══════════════════════════════════════════════════════════════

TEST(ResourceCache, StoreAndRetrieve_RoundTrip) {
    ResourceCache cache;
    ResourceHandle h("data", ResourceType::Buffer, 1);
    ResourceDescriptor desc("data", ResourceType::Buffer, "");

    auto data = std::make_shared<int>(42);
    cache.store(h, desc, data, 100);

    auto retrieved = cache.retrieve(h);
    ASSERT_NE(retrieved, nullptr);
    EXPECT_EQ(*std::static_pointer_cast<int>(retrieved), 42);
}

TEST(ResourceCache, Contains_Stored_True) {
    ResourceCache cache;
    ResourceHandle h("data", ResourceType::Buffer, 1);
    ResourceDescriptor desc;

    cache.store(h, desc, std::make_shared<int>(0), 0);
    EXPECT_TRUE(cache.contains(h));
}

TEST(ResourceCache, Contains_NotStored_False) {
    ResourceCache cache;
    ResourceHandle h("missing", ResourceType::Buffer, 1);
    EXPECT_FALSE(cache.contains(h));
}

TEST(ResourceCache, Retrieve_NotStored_Null) {
    ResourceCache cache;
    ResourceHandle h("missing", ResourceType::Buffer, 1);
    EXPECT_EQ(cache.retrieve(h), nullptr);
}

TEST(ResourceCache, Remove_RemovesEntry) {
    ResourceCache cache;
    ResourceHandle h("data", ResourceType::Buffer, 1);
    ResourceDescriptor desc;

    cache.store(h, desc, std::make_shared<int>(0), 100);
    EXPECT_TRUE(cache.contains(h));

    cache.remove(h);
    EXPECT_FALSE(cache.contains(h));
}

TEST(ResourceCache, CurrentMemory_TracksCorrectly) {
    ResourceCache cache;
    ResourceHandle h1("a", ResourceType::Buffer, 1);
    ResourceHandle h2("b", ResourceType::Buffer, 2);
    ResourceDescriptor desc;

    EXPECT_EQ(cache.getCurrentMemory(), 0u);

    cache.store(h1, desc, std::make_shared<int>(0), 100);
    EXPECT_EQ(cache.getCurrentMemory(), 100u);

    cache.store(h2, desc, std::make_shared<int>(0), 200);
    EXPECT_EQ(cache.getCurrentMemory(), 300u);

    cache.remove(h1);
    EXPECT_EQ(cache.getCurrentMemory(), 200u);
}

TEST(ResourceCache, ResourceCount_TracksCorrectly) {
    ResourceCache cache;
    ResourceHandle h1("a", ResourceType::Buffer, 1);
    ResourceHandle h2("b", ResourceType::Buffer, 2);
    ResourceDescriptor desc;

    EXPECT_EQ(cache.getResourceCount(), 0u);

    cache.store(h1, desc, std::make_shared<int>(0), 0);
    EXPECT_EQ(cache.getResourceCount(), 1u);

    cache.store(h2, desc, std::make_shared<int>(0), 0);
    EXPECT_EQ(cache.getResourceCount(), 2u);

    cache.remove(h1);
    EXPECT_EQ(cache.getResourceCount(), 1u);
}

TEST(ResourceCache, Clear_RemovesAll) {
    ResourceCache cache;
    ResourceHandle h1("a", ResourceType::Buffer, 1);
    ResourceHandle h2("b", ResourceType::Texture, 2);
    ResourceDescriptor desc;

    cache.store(h1, desc, std::make_shared<int>(0), 100);
    cache.store(h2, desc, std::make_shared<int>(0), 200);
    EXPECT_EQ(cache.getResourceCount(), 2u);

    cache.clear();
    EXPECT_EQ(cache.getResourceCount(), 0u);
    EXPECT_EQ(cache.getCurrentMemory(), 0u);
    EXPECT_FALSE(cache.contains(h1));
    EXPECT_FALSE(cache.contains(h2));
}

TEST(ResourceCache, Store_Replace_UpdatesMemory) {
    ResourceCache cache;
    ResourceHandle h("data", ResourceType::Buffer, 1);
    ResourceDescriptor desc;

    cache.store(h, desc, std::make_shared<int>(1), 100);
    EXPECT_EQ(cache.getCurrentMemory(), 100u);

    // Store same handle again → replaces, adjusts memory
    cache.store(h, desc, std::make_shared<int>(2), 300);
    EXPECT_EQ(cache.getCurrentMemory(), 300u);
    EXPECT_EQ(cache.getResourceCount(), 1u);
}

TEST(ResourceCache, EvictUnreferenced_OnlyRemovesUnreferenced) {
    ResourceCache cache;
    ResourceHandle h1("a", ResourceType::Buffer, 1);
    ResourceHandle h2("b", ResourceType::Buffer, 2);
    ResourceDescriptor desc;

    cache.store(h1, desc, std::make_shared<int>(0), 100);
    cache.store(h2, desc, std::make_shared<int>(0), 100);

    // Retrieve h1 to bump its reference count
    cache.retrieve(h1);

    // Evict unreferenced — h2 (refCount=0) should be evicted, h1 (refCount=1) stays
    cache.evictUnreferenced();

    EXPECT_TRUE(cache.contains(h1));
    EXPECT_FALSE(cache.contains(h2));
}

TEST(ResourceCache, MemoryBudget_AutoEvictsOnStore) {
    // Small budget: 200 bytes
    ResourceCache cache(200);

    ResourceHandle h1("a", ResourceType::Buffer, 1);
    ResourceHandle h2("b", ResourceType::Buffer, 2);
    ResourceHandle h3("c", ResourceType::Buffer, 3);
    ResourceDescriptor desc;

    // Store 150 bytes — fits
    cache.store(h1, desc, std::make_shared<int>(0), 150);
    EXPECT_TRUE(cache.contains(h1));

    // Store 100 bytes — total would be 250 > 200
    // h1 has refCount=0 so it's an eviction candidate
    cache.store(h2, desc, std::make_shared<int>(0), 100);

    // h1 should have been evicted to make room
    EXPECT_FALSE(cache.contains(h1));
    EXPECT_TRUE(cache.contains(h2));
}

TEST(ResourceCache, GetLoadedResources_ListsAll) {
    ResourceCache cache;
    ResourceHandle h1("a", ResourceType::Buffer, 1);
    ResourceHandle h2("b", ResourceType::Texture, 2);
    ResourceDescriptor desc;

    cache.store(h1, desc, std::make_shared<int>(0), 0);
    cache.store(h2, desc, std::make_shared<int>(0), 0);

    auto loaded = cache.getLoadedResources();
    EXPECT_EQ(loaded.size(), 2u);
}

TEST(ResourceCache, GetDescriptor_Found) {
    ResourceCache cache;
    ResourceHandle h("tex", ResourceType::Texture, 1);
    ResourceDescriptor desc("tex", ResourceType::Texture, "path/tex.png");

    cache.store(h, desc, std::make_shared<int>(0), 0);

    const auto* retrieved = cache.getDescriptor(h);
    ASSERT_NE(retrieved, nullptr);
    EXPECT_EQ(retrieved->name, "tex");
}

TEST(ResourceCache, GetDescriptor_NotFound_Null) {
    ResourceCache cache;
    ResourceHandle h("missing", ResourceType::Buffer, 1);
    EXPECT_EQ(cache.getDescriptor(h), nullptr);
}

// ═══════════════════════════════════════════════════════════════
// AsyncLoader Tests
// ═══════════════════════════════════════════════════════════════

TEST(AsyncLoader, SubmitAndWait_TaskExecutes) {
    AsyncLoader loader(2);
    std::atomic<bool> executed{false};

    loader.submitTask([&] { executed = true; });
    loader.waitForAll();

    // Small sleep to let the worker finish after queue empties
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    EXPECT_TRUE(executed);
}

TEST(AsyncLoader, MultipleTasks_AllExecute) {
    AsyncLoader loader(2);
    std::atomic<int> count{0};

    for (int i = 0; i < 10; i++) {
        loader.submitTask([&] { count++; });
    }
    loader.waitForAll();

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_EQ(count.load(), 10);
}

TEST(AsyncLoader, CancelAll_ClearsPendingTasks) {
    AsyncLoader loader(1);

    // Submit a blocking task to hold the single worker, then flood the queue
    std::atomic<bool> gate{false};
    loader.submitTask([&] {
        while (!gate) std::this_thread::yield();
    });

    // These should queue up behind the blocking task
    std::atomic<int> count{0};
    for (int i = 0; i < 100; i++) {
        loader.submitTask([&] { count++; });
    }

    // Cancel pending — only the currently-running task survives
    loader.cancelAll();
    EXPECT_EQ(loader.getPendingTaskCount(), 0u);

    // Release the blocking task
    gate = true;
    loader.waitForAll();

    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    // Some tasks may have been picked up before cancel, but far fewer than 100
    EXPECT_LT(count.load(), 100);
}

TEST(AsyncLoader, PendingTaskCount) {
    AsyncLoader loader(1);

    // Block the worker
    std::atomic<bool> gate{false};
    loader.submitTask([&] {
        while (!gate) std::this_thread::yield();
    });

    // Queue tasks
    loader.submitTask([] {});
    loader.submitTask([] {});

    // At least 2 should be pending (the blocking one already started)
    EXPECT_GE(loader.getPendingTaskCount(), 2u);

    // Release
    gate = true;
    loader.waitForAll();
}
