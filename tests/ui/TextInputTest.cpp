//
// TextInputTest.cpp - Reading IMM32's composition, and where the input
// method's candidate list goes
//
// Tier 1: pure functions and an EnTT registry, no window or GPU.
//

#include <gtest/gtest.h>

#include "ECS/Core.h"
#include "Platform/NativeWindow.h"
#include "UI/UIComponents.h"
#include "UI/UIHitTest.h"

#include <glm/gtc/matrix_transform.hpp>

#include <cstdint>
#include <vector>

using namespace Shoonyakasha;
using namespace Shoonyakasha::UI;

// ─── imeComposition ─────────────────────────────────────────────

TEST(ImeComposition, ClausesCursorAndTheActiveClause) {
    // "abc" converted and active, "def" converted.
    const std::vector<uint8_t> attributes{3, 3, 3, 2, 2, 2};
    const std::vector<uint32_t> clauses{0, 3, 6};
    const TextComposition c = Platform::imeComposition(u"abcdef", attributes, clauses, 3);
    EXPECT_EQ(c.text, U"abcdef");
    EXPECT_EQ(c.caret, 3u);
    ASSERT_EQ(c.clauses.size(), 2u);
    EXPECT_EQ(c.clauses[0], (TextCompositionClause{0, 3, true}));
    EXPECT_EQ(c.clauses[1], (TextCompositionClause{3, 6, false}));
}

TEST(ImeComposition, CountsInCodePointsAcrossSurrogatePairs) {
    // a, U+1F600 as two UTF-16 units, b.
    const std::u16string text{u'a', 0xD83D, 0xDE00, u'b'};
    const std::vector<uint8_t> attributes{0, 1, 1, 0};
    const std::vector<uint32_t> clauses{0, 1, 3, 4};
    const TextComposition c = Platform::imeComposition(text, attributes, clauses, 3);
    EXPECT_EQ(c.text, (std::u32string{U'a', 0x1F600, U'b'}));
    EXPECT_EQ(c.caret, 2u);
    ASSERT_EQ(c.clauses.size(), 3u);
    EXPECT_EQ(c.clauses[1], (TextCompositionClause{1, 2, true}));
    EXPECT_EQ(c.clauses[2], (TextCompositionClause{2, 3, false}));
}

TEST(ImeComposition, MalformedClausesAreLeftOut) {
    const std::vector<uint8_t> attributes{0, 0, 0};
    const std::vector<uint32_t> notToTheEnd{0, 2};
    const std::vector<uint32_t> notRising{0, 2, 2, 3};
    EXPECT_TRUE(Platform::imeComposition(u"abc", attributes, notToTheEnd, 3).clauses.empty());
    EXPECT_TRUE(Platform::imeComposition(u"abc", attributes, notRising, 3).clauses.empty());
    EXPECT_TRUE(Platform::imeComposition(u"abc", {}, {}, 99).clauses.empty());
    EXPECT_EQ(Platform::imeComposition(u"abc", {}, {}, 99).caret, 3u);   // clamped to the end

    const std::u16string unpaired{u'a', 0xDC00};
    EXPECT_EQ(Platform::imeComposition(unpaired, {}, {}, 0).text, (std::u32string{U'a', 0xFFFD}));
    EXPECT_TRUE(Platform::imeComposition(u"", {}, {}, 0).empty());
}

// ─── textInputRect ──────────────────────────────────────────────

namespace {

/// A field whose caret is placed at `caret` (canvas units) on `canvas`.
entt::entity fieldWithCaret(entt::registry& registry, entt::entity canvas, Rect caret) {
    const entt::entity caretElement = registry.create();
    auto& rect = registry.emplace<UIRect>(caretElement);
    rect.canvas = canvas;
    rect.rect = caret;
    const entt::entity field = registry.create();
    registry.emplace<UITextField>(field).caretElement = caretElement;
    return field;
}

} // namespace

TEST(TextInputRect, AScreenCanvasCaretGoesThroughTheCanvasScaleToWindowCoordinates) {
    entt::registry registry;
    const entt::entity canvas = registry.create();
    registry.emplace<UICanvas>(canvas).scale = 2.0f;
    const entt::entity field = fieldWithCaret(registry, canvas, {{100.0f, 50.0f}, {102.0f, 70.0f}});

    // A high-DPI window: 800 x 600 pixels in 400 x 300 window coordinates.
    const auto rect = textInputRect(registry, field, {800.0f, 600.0f}, {400.0f, 300.0f});
    ASSERT_TRUE(rect.has_value());
    EXPECT_NEAR(rect->min.x, 100.0f, 1e-3f);
    EXPECT_NEAR(rect->min.y, 50.0f, 1e-3f);
    EXPECT_NEAR(rect->max.x, 102.0f, 1e-3f);
    EXPECT_NEAR(rect->max.y, 70.0f, 1e-3f);

    // Off the screen there is none, and nor for a field with no caret.
    registry.get<UIRect>(registry.get<UITextField>(field).caretElement).rect = {{600.0f, 10.0f}, {602.0f, 20.0f}};
    EXPECT_FALSE(textInputRect(registry, field, {800.0f, 600.0f}, {400.0f, 300.0f}).has_value());
    EXPECT_FALSE(textInputRect(registry, canvas, {800.0f, 600.0f}, {400.0f, 300.0f}).has_value());
}

TEST(TextInputRect, AWorldCanvasCaretIsProjectedThroughTheMainCamera) {
    entt::registry registry;
    const glm::vec2 screen{800.0f, 600.0f};

    // A canvas of 200 x 100 units on a 2 x 1 quad at the origin, facing +Z.
    const entt::entity canvas = registry.create();
    auto& c = registry.emplace<UICanvas>(canvas);
    c.mode = UICanvas::Mode::WorldTexture;
    c.size = {200.0f, 100.0f};
    registry.emplace<ECS::TransformComponent>(canvas).worldMatrix =
        glm::scale(glm::mat4(1.0f), glm::vec3(2.0f, 1.0f, 1.0f));

    const entt::entity cameraEntity = registry.create();
    auto& camera = registry.emplace<ECS::CameraComponent>(cameraEntity);
    camera.isMainCamera = true;
    camera.projectionMatrix = glm::perspective(glm::radians(60.0f), screen.x / screen.y, 0.1f, 100.0f);
    camera.viewMatrix = glm::lookAt(glm::vec3(0.0f, 0.0f, 3.0f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));

    // A caret at the canvas's centre is at the screen's centre.
    const entt::entity field = fieldWithCaret(registry, canvas, {{99.0f, 40.0f}, {101.0f, 60.0f}});
    const auto rect = textInputRect(registry, field, screen, screen);
    ASSERT_TRUE(rect.has_value());
    EXPECT_NEAR((rect->min.x + rect->max.x) * 0.5f, 400.0f, 0.5f);
    EXPECT_NEAR((rect->min.y + rect->max.y) * 0.5f, 300.0f, 0.5f);
    EXPECT_LT(rect->min.y, rect->max.y);   // y down, as the canvas's

    // With the camera turned away, the caret is behind it.
    camera.viewMatrix = glm::lookAt(glm::vec3(0.0f, 0.0f, 3.0f), glm::vec3(0.0f, 0.0f, 6.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    EXPECT_FALSE(textInputRect(registry, field, screen, screen).has_value());

    // And without a main camera there is nowhere to put it.
    camera.isMainCamera = false;
    EXPECT_FALSE(textInputRect(registry, field, screen, screen).has_value());
}
