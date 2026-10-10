//
// Canvas UI example - a screen overlay canvas on the default pipeline
//
// A HUD drawn by the canvas UI over a lit box: translucent panels, an image,
// colour swatches, Unicode text in two fonts, a clip rect cutting off text
// that overflows its panel, and a frame counter that changes every frame.
//
// Usage:
//     CanvasUIExample [--screenshot path.png]
//
// With --screenshot it saves frame 120 to the path and closes.
//
// Keys: WASD/Q/E + right mouse to fly.
//

#include "App/ApplicationBase.h"
#include "Core/AssetPaths.h"
#include "ECS/Core.h"
#include "Resources/Sprite2DManager.h"
#include "UI/UIComponents.h"
#include "UI/UIContext.h"
#include "Vulkan/VulkanWindow.h"

#include <GLFW/glfw3.h>

#include <cstring>
#include <string>

using namespace Shoonyakasha;
using namespace Shoonyakasha::UI;

namespace {

glm::vec4 srgb(float r, float g, float b, float a = 1.0f) { return {r, g, b, a}; }

} // namespace

class CanvasUIExample : public ApplicationBase {
public:
    CanvasUIExample(const ApplicationConfig& config, std::string screenshot)
        : ApplicationBase(config), m_screenshot(std::move(screenshot)) {}

protected:
    void onInit() override {
        createCamera({0.0f, 1.2f, 4.0f}, 60.0f, 4.0f, 0.05f, 100.0f);
        createDirectionalLight({-0.4f, -0.7f, -0.5f}, glm::vec3(1.0f), 3.0f);
        loadGltfScene("models/Box.gltf");

        auto& registry = getRegistry();
        auto& fonts = getUIContext().fonts();
        const uint32_t roboto = fonts.load(AssetPaths::locate("fonts/Roboto-Regular.ttf").string());
        const uint32_t playfair = fonts.load(AssetPaths::locate("fonts/PlayfairDisplay.ttf").string());

        // A canvas laid out at 1280x720 and scaled to the window.
        m_canvas = registry.create();
        UICanvas canvas;
        canvas.scaleMode = UICanvas::ScaleMode::ScaleWithScreen;
        canvas.referenceSize = {1280.0f, 720.0f};
        registry.emplace<UICanvas>(m_canvas, canvas);

        // Top-left panel with a title, a line of text and an image.
        const auto panel = element(m_canvas, {0, 0}, {0, 0}, {0, 0}, {24, 24}, {420, 220});
        registry.emplace<UIPanel>(panel).color = srgb(0.08f, 0.09f, 0.12f, 0.82f);

        const auto title = element(panel, {0, 0}, {1, 0}, {0, 0}, {16, 12}, {-32, 44});
        text(title, playfair, "Śūnyākāśa", 34.0f, srgb(0.95f, 0.85f, 0.55f));

        const auto line = element(panel, {0, 0}, {1, 0}, {0, 0}, {16, 62}, {-32, 28});
        text(line, roboto, "Canvas UI — Привет, мир", 20.0f, srgb(0.9f, 0.92f, 0.95f));

        const auto picture = element(panel, {0, 0}, {0, 0}, {0, 0}, {16, 100}, {150, 103});
        auto& image = registry.emplace<UIImage>(picture);
        image.texture = getSprite2DManager().loadTexture(AssetPaths::locate("textures/panel.png").string());

        const auto swatches = element(panel, {0, 0}, {0, 0}, {0, 0}, {184, 100}, {220, 103});
        const glm::vec4 colours[] = {srgb(0.85f, 0.25f, 0.2f), srgb(0.95f, 0.7f, 0.2f, 0.6f),
                                     srgb(0.25f, 0.65f, 0.35f), srgb(0.2f, 0.45f, 0.85f, 0.4f)};
        for (int i = 0; i < 4; ++i) {
            const auto swatch = element(swatches, {0, 0}, {0, 0}, {0, 0}, {i * 55.0f, 0}, {50, 103});
            registry.emplace<UIImage>(swatch).color = colours[i];
        }

        // Bottom-right panel that clips text longer than it is.
        const auto clipped = element(m_canvas, {1, 1}, {1, 1}, {1, 1}, {-24, -24}, {300, 90});
        registry.emplace<UIPanel>(clipped).color = srgb(0.1f, 0.1f, 0.1f, 0.7f);
        registry.emplace<UIClip>(clipped);
        const auto longText = element(clipped, {0, 0}, {0, 0}, {0, 0}, {12, 10}, {600, 80});
        text(longText, roboto, "This line runs on well past the right edge of its panel", 22.0f,
             srgb(1.0f, 1.0f, 1.0f), false);

        // Frame counter, top right.
        m_counter = element(m_canvas, {1, 0}, {1, 0}, {1, 0}, {-24, 24}, {260, 36});
        text(m_counter, roboto, "frame 0", 26.0f, srgb(0.6f, 0.95f, 0.7f));
        registry.get<UIText>(m_counter).alignH = TextAlignH::Right;
    }

    void onUpdate(float /*dt*/) override {
        ++m_frame;
        getRegistry().get<UIText>(m_counter).text = "frame " + std::to_string(m_frame);
    }

    void onPostRender() override {
        if (!m_screenshot.empty() && m_frame == 120) {
            captureScreenshot(m_screenshot);
            glfwSetWindowShouldClose(getWindow().getWindow(), GLFW_TRUE);
        }
    }

private:
    entt::entity element(entt::entity parent, glm::vec2 anchorMin, glm::vec2 anchorMax, glm::vec2 pivot,
                         glm::vec2 position, glm::vec2 size) {
        auto& registry = getRegistry();
        const auto e = registry.create();
        UIRect rect;
        rect.anchorMin = anchorMin;
        rect.anchorMax = anchorMax;
        rect.pivot = pivot;
        rect.anchoredPosition = position;
        rect.sizeDelta = size;
        registry.emplace<UIRect>(e, rect);
        registry.get_or_emplace<ECS::HierarchyComponent>(parent).addChild(e);
        registry.get_or_emplace<ECS::HierarchyComponent>(e).parent = parent;
        return e;
    }

    void text(entt::entity e, uint32_t font, const std::string& value, float size, glm::vec4 color,
              bool wrap = true) {
        auto& t = getRegistry().emplace<UIText>(e);
        t.text = value;
        t.font = font;
        t.fontSize = size;
        t.color = color;
        t.wrap = wrap;
    }

    std::string m_screenshot;
    entt::entity m_canvas = entt::null;
    entt::entity m_counter = entt::null;
    int m_frame = 0;
};

int main(int argc, char** argv) {
    std::string screenshot;
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::strcmp(argv[i], "--screenshot") == 0) screenshot = argv[i + 1];
    }

    ApplicationConfig config;
    config.title = "Canvas UI";
    config.width = 1280;
    config.height = 720;
    config.logFile = "canvas_ui.log";

    CanvasUIExample app(config, screenshot);
    app.run();
    return 0;
}
