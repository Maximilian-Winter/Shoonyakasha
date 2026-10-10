//
// UIBatchBuilderTest.cpp - Draw data from laid-out canvases, and CanvasTextSystem
//
// Tier 2: EnTT registry, no GPU. Textures are fake handles; only their
// identity and size matter here.
//

#include <gtest/gtest.h>

#include "UI/UIBatchBuilder.h"
#include "UI/CanvasLayout.h"
#include "UI/UISystems.h"
#include "ECS/Core.h"

#include <cstdint>
#include <string>

using namespace Shoonyakasha;
using namespace Shoonyakasha::UI;

namespace {

const std::string kRoboto = std::string(SHOONYAKASHA_SOURCE_DIR) + "/assets/fonts/Roboto-Regular.ttf";

GPUTexture fakeTexture(uintptr_t id, uint32_t width = 64, uint32_t height = 32) {
    GPUTexture t;
    t.view = reinterpret_cast<VkImageView>(id);
    t.sampler = reinterpret_cast<VkSampler>(id + 1000);
    t.width = width;
    t.height = height;
    return t;
}

struct Fixture {
    entt::registry registry;
    FontLibrary fonts;
    GlyphCache glyphs{512};
    UIDrawData data;
    entt::entity canvas = entt::null;
    glm::vec2 screen{800, 600};

    explicit Fixture(float scale = 1.0f) {
        UICanvas c;
        c.scaleFactor = scale;
        canvas = addCanvas(c);
    }

    entt::entity addCanvas(const UICanvas& c) {
        const entt::entity e = registry.create();
        registry.emplace<UICanvas>(e, c);
        return e;
    }

    entt::entity add(entt::entity parent, glm::vec2 position, glm::vec2 size) {
        const entt::entity e = registry.create();
        UIRect r;
        r.anchorMin = r.anchorMax = {0, 0};
        r.pivot = {0, 0};
        r.anchoredPosition = position;
        r.sizeDelta = size;
        registry.emplace<UIRect>(e, r);
        registry.get_or_emplace<ECS::HierarchyComponent>(parent).addChild(e);
        registry.get_or_emplace<ECS::HierarchyComponent>(e).parent = parent;
        return e;
    }

    entt::entity image(entt::entity parent, glm::vec2 position, glm::vec2 size, GPUTexture texture = {}) {
        const entt::entity e = add(parent, position, size);
        UIImage img;
        img.texture = texture;
        registry.emplace<UIImage>(e, img);
        return e;
    }

    void build() {
        for (const entt::entity c : registry.view<UICanvas>()) layoutCanvas(registry, c, screen);
        CanvasTextSystem(&fonts).update(registry, 0.0f);
        buildDrawData(registry, fonts, glyphs, data);
    }

    UIDrawMode mode(size_t vertex) const { return static_cast<UIDrawMode>(data.vertices.at(vertex).mode); }
};

void expectVec2Near(glm::vec2 a, glm::vec2 b) {
    EXPECT_NEAR(a.x, b.x, 1e-4f);
    EXPECT_NEAR(a.y, b.y, 1e-4f);
}

} // namespace

TEST(UIBatchBuilder, PackColorPutsRedInTheLowByte) {
    EXPECT_EQ(packColor({1, 0, 0, 1}), 0xFF0000FFu);
    EXPECT_EQ(packColor({0, 0, 1, 0.5f}), 0x80FF0000u);
}

TEST(UIBatchBuilder, SolidImageIsOneScaledQuad) {
    Fixture f(2.0f);
    f.image(f.canvas, {10, 20}, {30, 40});
    f.build();

    ASSERT_EQ(f.data.canvases.size(), 1u);
    ASSERT_EQ(f.data.batches.size(), 1u);
    EXPECT_EQ(f.data.batches[0].image, VK_NULL_HANDLE);
    EXPECT_EQ(f.data.batches[0].indexCount, 6u);
    EXPECT_EQ(f.data.batches[0].scissor, glm::ivec4(0, 0, 800, 600));
    ASSERT_EQ(f.data.vertices.size(), 4u);
    expectVec2Near(f.data.vertices[0].position, {20, 40});
    expectVec2Near(f.data.vertices[2].position, {80, 120});
    EXPECT_EQ(f.mode(0), UIDrawMode::Solid);
}

TEST(UIBatchBuilder, KeepsPainterOrderAcrossTextures) {
    Fixture f;
    const GPUTexture a = fakeTexture(0x10), b = fakeTexture(0x20);
    f.image(f.canvas, {0, 0}, {50, 50}, a);
    f.image(f.canvas, {10, 10}, {50, 50}, a);
    f.image(f.canvas, {20, 20}, {50, 50}, b);
    f.image(f.canvas, {30, 30}, {50, 50}, a);
    f.build();

    ASSERT_EQ(f.data.batches.size(), 3u);
    EXPECT_EQ(f.data.batches[0].image, a.view);
    EXPECT_EQ(f.data.batches[0].indexCount, 12u);
    EXPECT_EQ(f.data.batches[0].sampler, a.sampler);
    EXPECT_EQ(f.data.batches[1].image, b.view);
    EXPECT_EQ(f.data.batches[2].image, a.view);
    EXPECT_EQ(f.data.batches[2].firstIndex, 18u);
}

TEST(UIBatchBuilder, SolidQuadsJoinTexturedBatches) {
    Fixture f;
    const GPUTexture a = fakeTexture(0x10);
    f.image(f.canvas, {0, 0}, {50, 50});
    f.image(f.canvas, {0, 0}, {50, 50}, a);
    f.image(f.canvas, {0, 0}, {50, 50});
    f.build();

    ASSERT_EQ(f.data.batches.size(), 1u);
    EXPECT_EQ(f.data.batches[0].image, a.view);
    EXPECT_EQ(f.mode(0), UIDrawMode::Solid);
    EXPECT_EQ(f.mode(4), UIDrawMode::Image);
}

TEST(UIBatchBuilder, ClipStartsABatchWithItsScissor) {
    Fixture f(2.0f);
    const entt::entity clipper = f.add(f.canvas, {10, 10}, {100, 50});
    f.registry.emplace<UIClip>(clipper);
    f.image(clipper, {0, 0}, {300, 300});
    f.image(f.canvas, {0, 0}, {20, 20});
    f.build();

    ASSERT_EQ(f.data.batches.size(), 2u);
    EXPECT_EQ(f.data.batches[0].scissor, glm::ivec4(20, 20, 200, 100));
    EXPECT_EQ(f.data.batches[1].scissor, glm::ivec4(0, 0, 800, 600));
}

TEST(UIBatchBuilder, HiddenAndFullyClippedElementsAreSkipped) {
    Fixture f;
    const entt::entity hidden = f.image(f.canvas, {0, 0}, {50, 50});
    f.registry.get<UIRect>(hidden).visible = false;
    const entt::entity clipper = f.add(f.canvas, {0, 0}, {10, 10});
    f.registry.emplace<UIClip>(clipper);
    f.image(clipper, {100, 100}, {20, 20});
    f.build();

    EXPECT_TRUE(f.data.vertices.empty());
    ASSERT_EQ(f.data.canvases.size(), 1u);
    EXPECT_EQ(f.data.canvases[0].batchCount, 0u);
}

TEST(UIBatchBuilder, NineSliceGeometryAndUvs) {
    Fixture f;
    const entt::entity e = f.add(f.canvas, {100, 100}, {200, 80});
    UIPanel panel;
    panel.texture = fakeTexture(0x30, 64, 32);
    panel.border = {8, 4, 16, 8};
    f.registry.emplace<UIPanel>(e, panel);
    f.build();

    ASSERT_EQ(f.data.vertices.size(), 36u);
    // Top-left corner quad.
    expectVec2Near(f.data.vertices[0].position, {100, 100});
    expectVec2Near(f.data.vertices[2].position, {108, 104});
    expectVec2Near(f.data.vertices[2].uv, {8.0f / 64.0f, 4.0f / 32.0f});
    // Bottom-right corner quad is the last one.
    expectVec2Near(f.data.vertices[32].position, {284, 172});
    expectVec2Near(f.data.vertices[32].uv, {1.0f - 16.0f / 64.0f, 1.0f - 8.0f / 32.0f});
    expectVec2Near(f.data.vertices[34].position, {300, 180});
    expectVec2Near(f.data.vertices[34].uv, {1, 1});
}

TEST(UIBatchBuilder, NineSliceWithoutCentre) {
    Fixture f;
    const entt::entity e = f.add(f.canvas, {0, 0}, {100, 100});
    UIPanel panel;
    panel.texture = fakeTexture(0x30);
    panel.border = {8, 8, 8, 8};
    panel.fillCenter = false;
    f.registry.emplace<UIPanel>(e, panel);
    f.build();

    EXPECT_EQ(f.data.vertices.size(), 32u);
}

TEST(UIBatchBuilder, NineSliceBordersShrinkWhenTheyOverlap) {
    Fixture f;
    const entt::entity e = f.add(f.canvas, {0, 0}, {10, 100});
    UIPanel panel;
    panel.texture = fakeTexture(0x30);
    panel.border = {8, 8, 8, 8};
    f.registry.emplace<UIPanel>(e, panel);
    f.build();

    // The centre column has no width, so its three quads are dropped; the
    // left column is 5 wide.
    ASSERT_EQ(f.data.vertices.size(), 24u);
    expectVec2Near(f.data.vertices[2].position, {5, 8});
}

TEST(UIBatchBuilder, UntexturedPanelIsOneSolidQuad) {
    Fixture f;
    const entt::entity e = f.add(f.canvas, {0, 0}, {100, 100});
    UIPanel panel;
    panel.border = {8, 8, 8, 8};
    f.registry.emplace<UIPanel>(e, panel);
    f.build();

    ASSERT_EQ(f.data.vertices.size(), 4u);
    EXPECT_EQ(f.mode(0), UIDrawMode::Solid);
}

TEST(UIBatchBuilder, CanvasesOrderedOverlaysBySortOrderThenWorld) {
    Fixture f;  // overlay, sortOrder 0
    UICanvas world;
    world.mode = UICanvas::Mode::WorldTexture;
    world.pixelSize = {256, 128};
    const entt::entity worldCanvas = f.addCanvas(world);
    UICanvas back;
    back.sortOrder = -3;
    const entt::entity backCanvas = f.addCanvas(back);

    f.image(worldCanvas, {0, 0}, {10, 10});
    f.image(f.canvas, {0, 0}, {10, 10}, fakeTexture(0x10));
    f.image(backCanvas, {0, 0}, {10, 10}, fakeTexture(0x20));
    f.build();

    ASSERT_EQ(f.data.canvases.size(), 3u);
    EXPECT_EQ(f.data.canvases[0].canvas, backCanvas);
    EXPECT_EQ(f.data.canvases[1].canvas, f.canvas);
    EXPECT_EQ(f.data.canvases[2].canvas, worldCanvas);
    EXPECT_EQ(f.data.canvases[2].targetSize, glm::vec2(256, 128));
    for (uint32_t i = 0; i < 3; ++i) {
        EXPECT_EQ(f.data.canvases[i].firstBatch, i);
        EXPECT_EQ(f.data.canvases[i].batchCount, 1u);
    }
    // Batches never span canvases, even when they could merge.
    EXPECT_EQ(f.data.batches[2].scissor, glm::ivec4(0, 0, 256, 128));
}

TEST(UIBatchBuilder, TextQuadsAndGlyphUploads) {
    Fixture f;
    const entt::entity e = f.add(f.canvas, {0, 0}, {400, 100});
    UIText text;
    text.text = "abca b";
    text.font = f.fonts.load(kRoboto);
    ASSERT_NE(text.font, 0u);
    f.registry.emplace<UIText>(e, text);
    f.build();

    EXPECT_EQ(f.data.vertices.size(), 5u * 4u);  // the space has no quad
    EXPECT_EQ(f.mode(0), UIDrawMode::Text);
    EXPECT_EQ(f.glyphs.pendingUploads().size(), 3u);  // a, b, c
    EXPECT_EQ(f.data.batches.size(), 1u);

    f.glyphs.clearPendingUploads();
    f.build();
    EXPECT_TRUE(f.glyphs.pendingUploads().empty());
    EXPECT_EQ(f.data.vertices.size(), 20u);
}

TEST(UIBatchBuilder, FullAtlasIsResetOnce) {
    Fixture f;
    const uint32_t font = f.fonts.load(kRoboto);
    const SdfFont& sdf = *f.fonts.get(font);

    // Fill the atlas with copies of 'q' filed under other font ids, so every
    // slot left over is smaller than a 'q'.
    const int q = sdf.findGlyph(U'q');
    for (uint32_t other = 1000; f.glyphs.findOrAdd(other, q, sdf); ++other) {}
    ASSERT_FALSE(f.glyphs.findOrAdd(font, q, sdf).has_value())
        << "the 512 atlas should be full";

    const entt::entity e = f.add(f.canvas, {0, 0}, {400, 100});
    UIText text;
    text.text = "qq";
    text.font = font;
    f.registry.emplace<UIText>(e, text);
    f.build();

    EXPECT_EQ(f.glyphs.generation(), 1u);
    EXPECT_EQ(f.data.vertices.size(), 8u);
    EXPECT_EQ(f.glyphs.glyphCount(), 1u);
}

// ── CanvasTextSystem ────────────────────────────────────────────

TEST(CanvasTextSystem, LaysOutAgainOnlyWhenInputsChange) {
    Fixture f;
    const entt::entity e = f.add(f.canvas, {0, 0}, {400, 100});
    UIText text;
    text.text = "Tara";
    text.font = f.fonts.load(kRoboto);
    f.registry.emplace<UIText>(e, text);
    f.build();

    auto& cache = f.registry.get<UITextCache>(e);
    ASSERT_EQ(cache.layout.glyphs.size(), 4u);

    cache.layout.glyphs.clear();  // marks whether the next update lays out again
    f.build();
    EXPECT_TRUE(f.registry.get<UITextCache>(e).layout.glyphs.empty());

    f.registry.get<UIText>(e).text = "Vajrayogini";
    f.build();
    EXPECT_EQ(f.registry.get<UITextCache>(e).layout.glyphs.size(), 11u);

    f.registry.get<UITextCache>(e).layout.glyphs.clear();
    f.registry.get<UIRect>(e).sizeDelta = {50, 100};
    f.build();
    EXPECT_FALSE(f.registry.get<UITextCache>(e).layout.glyphs.empty());
}

TEST(CanvasTextSystem, PrioritySetAtConstruction) {
    CanvasTextSystem system(nullptr);
    EXPECT_EQ(system.priority, CanvasTextSystem::kPriority);
}
