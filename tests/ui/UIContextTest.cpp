//
// UIContextTest.cpp - the canvas UI without a renderer, and the shaders the
// renderer loads
//

#include <gtest/gtest.h>

#include "ECS/Core.h"
#include "FrameGraph/ShaderInterfaceValidator.h"
#include "UI/UIComponents.h"
#include "UI/UIContext.h"
#include "UI/UISystems.h"

#include <entt/entt.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

using namespace Shoonyakasha;
using namespace Shoonyakasha::UI;
namespace fs = std::filesystem;

namespace {

const fs::path kFont = fs::path(SHOONYAKASHA_SOURCE_DIR) / "assets" / "fonts" / "Roboto-Regular.ttf";
const fs::path kShaders =
    fs::path(SHOONYAKASHA_SOURCE_DIR) / "python" / "shoonyakasha" / "pipelines" / "default" / "shaders" / "ui";

std::vector<char> readFile(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

entt::entity makeOverlay(entt::registry& registry) {
    const auto canvas = registry.create();
    registry.emplace<UICanvas>(canvas);
    return canvas;
}

entt::entity addElement(entt::registry& registry, entt::entity parent) {
    const auto element = registry.create();
    UIRect rect;
    rect.anchorMin = rect.anchorMax = {0.0f, 0.0f};
    rect.pivot = {0.0f, 0.0f};
    rect.sizeDelta = {200.0f, 50.0f};
    registry.emplace<UIRect>(element, rect);
    registry.get_or_emplace<ECS::HierarchyComponent>(parent).addChild(element);
    registry.get_or_emplace<ECS::HierarchyComponent>(element).parent = parent;
    return element;
}

void update(entt::registry& registry, const FontLibrary& fonts) {
    const glm::vec2 screen(800.0f, 600.0f);
    CanvasLayoutSystem(&screen).update(registry, 0.0f);
    CanvasTextSystem(&fonts).update(registry, 0.0f);
}

} // namespace

TEST(UIContext, WithoutARendererBuildsDrawDataAndDropsGlyphUploads) {
    UIContext context;
    EXPECT_FALSE(context.hasRenderer());
    const uint32_t font = context.fonts().load(kFont.string());
    ASSERT_NE(font, 0u);

    entt::registry registry;
    const auto canvas = makeOverlay(registry);
    auto& text = registry.emplace<UIText>(addElement(registry, canvas));
    text.text = "Hi";
    text.font = font;
    update(registry, context.fonts());

    context.prepareFrame(registry, VK_NULL_HANDLE, 0);
    ASSERT_EQ(context.drawData().canvases.size(), 1u);
    EXPECT_EQ(context.drawData().indices.size(), 12u);   // two glyph quads
    EXPECT_EQ(context.glyphs().glyphCount(), 2u);
    EXPECT_TRUE(context.glyphs().pendingUploads().empty());
}

TEST(UIContext, ReportsOverlaysThatNoPassDrew) {
    UIContext context;
    entt::registry registry;
    context.prepareFrame(registry, VK_NULL_HANDLE, 0);
    EXPECT_FALSE(context.overlaysUndrawn());   // nothing to draw

    const auto canvas = makeOverlay(registry);
    registry.emplace<UIImage>(addElement(registry, canvas));
    update(registry, context.fonts());
    context.prepareFrame(registry, VK_NULL_HANDLE, 0);
    EXPECT_TRUE(context.overlaysUndrawn());

    registry.get<UICanvas>(canvas).mode = UICanvas::Mode::WorldTexture;
    context.prepareFrame(registry, VK_NULL_HANDLE, 0);
    EXPECT_FALSE(context.overlaysUndrawn());   // world canvases are not drawn by ui_canvas passes
}

TEST(UIShaders, MatchTheRendererInterface) {
    // The renderer binds one set: the glyph atlas at 0 and the batch image at
    // 1, and pushes the target size as a vec2.
    FrameGraph::ShaderInterfaceExpectation expected;
    expected.setCount = 1;
    expected.descriptors[{0, 0}].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    expected.descriptors[{0, 1}].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    expected.pushConstantSize = sizeof(glm::vec2);

    for (const char* name : {"ui.vert.spv", "ui.frag.spv"}) {
        SCOPED_TRACE(name);
        const auto code = readFile(kShaders / name);
        ASSERT_FALSE(code.empty());
        const auto problems = FrameGraph::validateShaderInterface(code.data(), code.size(), expected);
        EXPECT_TRUE(problems.empty()) << problems.front();
    }
}
