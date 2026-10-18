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
#include "UI/UIWidgets.h"
#include "ECS/Core.h"

#include <cstdint>
#include <stdexcept>
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

    UIDrawMode mode(size_t vertex) const { return static_cast<UIDrawMode>(data.vertices.at(vertex).mode & 0xFFu); }
    uint32_t page(size_t vertex) const { return data.vertices.at(vertex).mode >> 8; }

    /// The first vertex drawn with `drawMode`.
    const UIVertex& first(UIDrawMode drawMode) const {
        for (const auto& v : data.vertices) {
            if (static_cast<UIDrawMode>(v.mode & 0xFFu) == drawMode) return v;
        }
        throw std::runtime_error("no vertex with that mode");
    }
};

void expectVec2Near(glm::vec2 a, glm::vec2 b) {
    EXPECT_NEAR(a.x, b.x, 1e-4f);
    EXPECT_NEAR(a.y, b.y, 1e-4f);
}

} // namespace

TEST(ToolkitGeometry, LocalCoordinatesScaleAndClipAndKeepBatchOrder) {
    Fixture f(2.0f);
    auto parent = f.add(f.canvas, {10, 20}, {50, 40});
    f.registry.emplace<UIClip>(parent);
    for (int n = 0; n < 2; ++n) {
        auto e = f.add(parent, {5, 6}, {30, 20});
        f.registry.emplace<UIGeometry>(e,
            std::vector<glm::vec2>{{0, 0}, {10, 0}, {0, 10}},
            std::vector<uint32_t>{0, 1, 2}, std::vector<glm::vec4>(3, glm::vec4(1)));
    }
    f.build();
    ASSERT_EQ(f.data.vertices.size(), 6u);
    ASSERT_EQ(f.data.batches.size(), 1u);
    expectVec2Near(f.data.vertices[0].position, {30, 52});
    EXPECT_EQ(f.data.batches[0].scissor, glm::ivec4(20, 40, 100, 80));
    EXPECT_EQ(f.data.indices[3], 3u);
    EXPECT_EQ(f.mode(0), UIDrawMode::Solid);
    f.registry.get<UIRect>(parent).visible = false;
    f.build();
    EXPECT_TRUE(f.data.vertices.empty());
}

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
    world.clearColor = {0.1f, 0.2f, 0.3f, 0.5f};
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
    EXPECT_EQ(f.data.canvases[2].clearColor, glm::vec4(0.1f, 0.2f, 0.3f, 0.5f));
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

TEST(UIBatchBuilder, FullAtlasEvictsAPageNotDrawnFromThisFrame) {
    Fixture f;
    f.glyphs = GlyphCache(512, 1);
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

    EXPECT_EQ(f.glyphs.evictions(), 1u);
    EXPECT_EQ(f.glyphs.generation(), 0u);
    EXPECT_EQ(f.data.vertices.size(), 8u);
    EXPECT_EQ(f.glyphs.glyphCount(), 1u);
}

TEST(UIBatchBuilder, AtlasIsResetOnceWhenEveryPageIsInUse) {
    Fixture f;
    f.glyphs = GlyphCache(512, 2);
    const uint32_t font = f.fonts.load(kRoboto);
    const SdfFont& sdf = *f.fonts.get(font);
    const int a = sdf.findGlyph(U'a'), b = sdf.findGlyph(U'b'), q = sdf.findGlyph(U'q');

    // 'a' on the first page and 'b' on the second, both pages filled up with
    // copies of 'q' under other font ids.
    ASSERT_EQ(f.glyphs.findOrAdd(font, a, sdf)->page, 0u);
    uint32_t other = 1000;
    while (f.glyphs.pageCount() < 2) ASSERT_TRUE(f.glyphs.findOrAdd(other++, q, sdf));
    ASSERT_EQ(f.glyphs.findOrAdd(font, b, sdf)->page, 1u);
    while (f.glyphs.findOrAdd(other, q, sdf)) ++other;

    // Drawing "abq" uses both pages before 'q' needs room, so neither can be
    // evicted; the atlas is reset and all three are placed again.
    const entt::entity e = f.add(f.canvas, {0, 0}, {400, 100});
    UIText text;
    text.text = "abq";
    text.font = font;
    f.registry.emplace<UIText>(e, text);
    f.build();

    EXPECT_EQ(f.glyphs.generation(), 1u);
    EXPECT_EQ(f.glyphs.glyphCount(), 3u);
    EXPECT_EQ(f.data.vertices.size(), 12u);
    EXPECT_EQ(f.glyphs.pendingClears().size(), 2u);  // both pages
}

TEST(UIBatchBuilder, TextVerticesNameTheirAtlasPage) {
    Fixture f;
    const uint32_t font = f.fonts.load(kRoboto);
    const SdfFont& sdf = *f.fonts.get(font);
    const int q = sdf.findGlyph(U'q');
    for (uint32_t other = 1000; f.glyphs.pageCount() < 2; ++other) ASSERT_TRUE(f.glyphs.findOrAdd(other, q, sdf));

    // The first page is full for a 'q', so it goes on the second.
    const entt::entity e = f.add(f.canvas, {0, 0}, {400, 100});
    UIText text;
    text.text = "q";
    text.font = font;
    f.registry.emplace<UIText>(e, text);
    f.build();

    ASSERT_EQ(f.data.vertices.size(), 4u);
    EXPECT_EQ(f.mode(0), UIDrawMode::Text);
    EXPECT_EQ(f.page(0), 1u);
    EXPECT_EQ(f.glyphs.pendingUploads().back().page, 1u);
}

// ── Tint targets ────────────────────────────────────────────────

namespace {

struct TintState {
    const char* name;
    void (*apply)(UIInteractable&);
};

const TintState kTintStates[] = {
    {"normal", [](UIInteractable&) {}},
    {"hovered", [](UIInteractable& i) { i.hovered = true; }},
    {"pressed", [](UIInteractable& i) { i.hovered = i.pressed = true; }},
    {"focused", [](UIInteractable& i) { i.focused = true; }},
    {"disabled", [](UIInteractable& i) { i.enabled = false; }},
};

} // namespace

TEST(UIBatchBuilder, AButtonsLabelTakesTheStateColour) {
    for (const auto& state : kTintStates) {
        Fixture f;
        const uint32_t font = f.fonts.load(kRoboto);
        const entt::entity button = createButton(f.registry, f.canvas, "Go", font);
        auto& interactable = f.registry.get<UIInteractable>(button);
        state.apply(interactable);
        f.build();

        const entt::entity label = interactable.tintTargets.at(0);
        const glm::vec4 expected = f.registry.get<UIText>(label).color * interactable.stateColor();
        EXPECT_EQ(f.first(UIDrawMode::Text).color, packColor(expected)) << state.name;
        EXPECT_EQ(f.first(UIDrawMode::Solid).color,
                  packColor(f.registry.get<UIPanel>(button).color * interactable.stateColor())) << state.name;
    }
}

TEST(UIBatchBuilder, AToggleAndASliderTintTheirParts) {
    for (const auto& state : kTintStates) {
        Fixture f;
        const uint32_t font = f.fonts.load(kRoboto);
        const entt::entity toggle = createToggle(f.registry, f.canvas, "On", font, true);
        auto& toggleState = f.registry.get<UIInteractable>(toggle);
        state.apply(toggleState);
        f.build();

        // Drawn in order: the box, the checkmark, then the label.
        const auto& toggleTargets = toggleState.tintTargets;
        ASSERT_EQ(toggleTargets.size(), 3u);
        const glm::vec4 tint = toggleState.stateColor();
        EXPECT_EQ(f.data.vertices.at(0).color, packColor(f.registry.get<UIPanel>(toggleTargets[0]).color * tint))
            << state.name;
        EXPECT_EQ(f.data.vertices.at(4).color, packColor(f.registry.get<UIImage>(toggleTargets[1]).color * tint))
            << state.name;
        EXPECT_EQ(f.first(UIDrawMode::Text).color, packColor(f.registry.get<UIText>(toggleTargets[2]).color * tint))
            << state.name;
    }

    for (const auto& state : kTintStates) {
        Fixture f;
        const entt::entity slider = createSlider(f.registry, f.canvas, 0.0f, 1.0f, 0.5f);
        auto& sliderState = f.registry.get<UIInteractable>(slider);
        state.apply(sliderState);
        f.build();

        // Drawn in order: the track, which keeps its colour, the fill and the handle.
        const auto& s = f.registry.get<UISlider>(slider);
        const glm::vec4 tint = sliderState.stateColor();
        ASSERT_EQ(f.data.vertices.size(), 12u);
        EXPECT_EQ(f.data.vertices[4].color, packColor(f.registry.get<UIImage>(s.fill).color * tint)) << state.name;
        EXPECT_EQ(f.data.vertices[8].color, packColor(f.registry.get<UIPanel>(s.handle).color * tint)) << state.name;
    }
}

TEST(UIBatchBuilder, ATargetOfTwoInteractablesTakesBothColours) {
    Fixture f;
    const entt::entity target = f.image(f.canvas, {0, 0}, {10, 10});
    const entt::entity a = f.add(f.canvas, {20, 0}, {10, 10});
    const entt::entity b = f.add(f.canvas, {40, 0}, {10, 10});
    auto& first = f.registry.emplace<UIInteractable>(a);
    first.normalColor = {0.5f, 1.0f, 1.0f, 1.0f};
    first.tintTargets = {target};
    auto& second = f.registry.emplace<UIInteractable>(b);
    second.normalColor = {1.0f, 0.5f, 1.0f, 1.0f};
    second.tintTargets = {target, f.registry.create()};  // an element that draws nothing is fine
    f.build();

    ASSERT_EQ(f.data.vertices.size(), 4u);
    EXPECT_EQ(f.data.vertices[0].color, packColor({0.5f, 0.5f, 1.0f, 1.0f}));
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
