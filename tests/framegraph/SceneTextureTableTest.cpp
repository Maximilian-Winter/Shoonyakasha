//
// SceneTextureTableTest.cpp - the scene's textures by slot
//
// Covers which textures get slots, the entity.material.textures.<name>.slot
// dot-path, and the bindlessTextures capability.
//
// Tier 1: no GPU context; image views and samplers are dummy handles.
//

#include <gtest/gtest.h>

#include "ECS/RenderComponents.h"
#include "FrameGraph/DotPathResolver.h"
#include "GPU/GpuDeleteQueue.h"
#include "Vulkan/FrameGraph/FrameGraph.h"
#include "Vulkan/FrameGraph/FrameGraphJson.h"
#include "Vulkan/SceneTextureTable.h"
#include "../TestHelpers.h"

#include <nlohmann/json.hpp>

using namespace Shoonyakasha;
using namespace Shoonyakasha::FrameGraph;
using nlohmann::json;

namespace {

VkImageView view(uintptr_t n) { return reinterpret_cast<VkImageView>(n); }

GPUTexture texture(uintptr_t n, bool exists = true) {
    GPUTexture t;
    t.view = view(n);
    t.sampler = TestHelpers::dummyVkSampler();
    t.exists = exists;
    return t;
}

entt::entity addEntity(entt::registry& registry, std::vector<std::pair<std::string, GPUTexture>> textures,
                       bool visible = true, bool validMesh = true) {
    const auto e = registry.create();
    MeshComponent mesh;
    if (validMesh) mesh.vertexBuffer = borrowBuffer(GPUBuffer{TestHelpers::dummyVkBuffer(), nullptr, 64});
    mesh.vertexCount = 3;
    registry.emplace<MeshComponent>(e, std::move(mesh));
    MaterialComponentV5 material;
    for (auto& [name, t] : textures) material.textures[name] = t;
    registry.emplace<MaterialComponentV5>(e, std::move(material));
    registry.emplace<RenderableTagComponent>(e).visible = visible;
    return e;
}

} // namespace

TEST(SceneTextureTable, OneSlotPerViewOfRenderedMeshes) {
    entt::registry registry;
    const auto a = addEntity(registry, {{"albedoMap", texture(10)}});
    const auto b = addEntity(registry, {{"albedoMap", texture(10)}});                  // shares a's view
    const auto c = addEntity(registry, {{"normalMap", texture(20)}});
    const auto missing = addEntity(registry, {{"albedoMap", texture(30, false)}});     // a fallback
    const auto hidden = addEntity(registry, {{"albedoMap", texture(40)}}, false);
    const auto noMesh = addEntity(registry, {{"albedoMap", texture(50)}}, true, false);

    SceneTextureTable table;
    table.build(registry, 16);
    ASSERT_EQ(table.slots().size(), 2u);
    const auto slot = [&](entt::entity e, const char* name) {
        return table.slotOf(registry.get<MaterialComponentV5>(e).getTexture(name));
    };
    EXPECT_EQ(slot(a, "albedoMap"), slot(b, "albedoMap"));
    EXPECT_NE(slot(a, "albedoMap"), slot(c, "normalMap"));
    EXPECT_EQ(table.slots()[slot(c, "normalMap")].view, view(20));
    EXPECT_EQ(slot(missing, "albedoMap"), SceneTextureTable::kNoTexture);
    EXPECT_EQ(slot(hidden, "albedoMap"), SceneTextureTable::kNoTexture);
    EXPECT_EQ(slot(noMesh, "albedoMap"), SceneTextureTable::kNoTexture);
    EXPECT_EQ(slot(a, "emissiveMap"), SceneTextureTable::kNoTexture);   // the material has none
}

TEST(SceneTextureTable, SlotsStayWhileTheSceneDoesAndStopAtCapacity) {
    entt::registry registry;
    for (uintptr_t i = 1; i <= 5; ++i) addEntity(registry, {{"albedoMap", texture(i)}});
    SceneTextureTable table;
    table.build(registry, 16);
    const auto first = table.slots();
    table.build(registry, 16);
    EXPECT_EQ(table.slots(), first);

    table.build(registry, 3);
    EXPECT_EQ(table.slots().size(), 3u);
    size_t without = 0;
    for (auto [e, material] : registry.view<MaterialComponentV5>().each()) {
        if (table.slotOf(material.getTexture("albedoMap")) == SceneTextureTable::kNoTexture) ++without;
    }
    EXPECT_EQ(without, 2u);

    table.build(registry, 0);
    EXPECT_TRUE(table.slots().empty());
}

TEST(SceneTextureTable, SlotDotPath) {
    entt::registry registry;
    const auto e = addEntity(registry, {{"albedoMap", texture(7)}, {"normalMap", texture(8, false)}});
    SceneTextureTable table;
    table.build(registry, 16);
    const uint32_t albedo = table.slotOf(registry.get<MaterialComponentV5>(e).getTexture("albedoMap"));
    ASSERT_NE(albedo, SceneTextureTable::kNoTexture);

    DotPathResolver resolver;
    SceneContext scene;
    const std::string path = "entity.material.textures.albedoMap.slot";
    const CompiledPath compiled = resolver.compile(path);
    EXPECT_EQ(compiled.op, CompiledPath::Op::MaterialTextureSlot);

    // Without a table every slot reads as none.
    EXPECT_EQ(resolver.resolve(compiled, path, scene, e, registry).tryAs<uint32_t>(), SceneTextureTable::kNoTexture);

    scene.sceneTextures = &table;
    EXPECT_EQ(resolver.resolve(compiled, path, scene, e, registry).tryAs<uint32_t>(), albedo);
    EXPECT_EQ(resolver.resolve(path, scene, e, registry).tryAs<uint32_t>(), albedo);
    EXPECT_EQ(resolver.resolve("entity.material.textures.normalMap.slot", scene, e, registry).tryAs<uint32_t>(),
              SceneTextureTable::kNoTexture);
    EXPECT_EQ(resolver.resolve("entity.material.textures.aoMap.slot", scene, e, registry).tryAs<uint32_t>(),
              SceneTextureTable::kNoTexture);
}

TEST(SceneTextureTable, BindlessTexturesCapability) {
    const json graph = {{"version", 1}, {"name", "bindless_test"},
                        {"passes", {{{"name", "Draw"}, {"type", "compute"}, {"hasSideEffects", true},
                                     {"requires", {"bindlessTextures"}},
                                     {"execution", {{"type", "compute_dispatch"}, {"dispatch", {{"x", 1}}}}}}}}};
    PipelineCapabilities none;
    none.bindlessTextures = false;
    FrameGraphBuilder without;
    loadGraphFromJson(without, graph, none);
    EXPECT_TRUE(without.isUnavailablePass("Draw"));
    FrameGraphBuilder with;
    loadGraphFromJson(with, graph);
    EXPECT_FALSE(with.isUnavailablePass("Draw"));
}
