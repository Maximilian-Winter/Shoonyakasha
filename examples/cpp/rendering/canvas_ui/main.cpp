//
// Canvas UI example - a screen overlay canvas on the default pipeline
//
// A HUD drawn by the canvas UI over a lit box: translucent panels, an image,
// colour swatches, Unicode text in two fonts, a clip rect cutting off text
// that overflows its panel, and a frame counter that changes every frame.
// Beside the box, a world canvas: a texture the UI renders into, shown on a
// quad in the scene, with a button that counts clicks. Bottom left, a toggle
// spins the box, a slider sets its angle and a button resets both.
//
// Usage:
//     CanvasUIExample [--screenshot path.png]
//
// With --screenshot it switches the world canvas's resolution at frame 60,
// moves and clicks a scripted pointer through the widgets, saves frame 120
// to the path and closes.
//
// Keys: WASD/Q/E + right mouse to fly, R to switch the world canvas between
// 1024x512 and 512x256 pixels. Left mouse uses the widgets; with the mouse
// captured (ESC), the centre of the screen is the pointer.
//

#include "App/ApplicationBase.h"
#include "Core/AssetPaths.h"
#include "ECS/CameraController.h"
#include "ECS/Core.h"
#include "ECS/RenderComponents.h"
#include "Resources/Sprite2DManager.h"
#include "UI/UIComponents.h"
#include "UI/UIContext.h"
#include "UI/UIWidgets.h"
#include "Vulkan/VulkanSwapChain.h"
#include "Vulkan/VulkanWindow.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cmath>
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

        // A terminal in the world, left of the box: a canvas of 1024x512
        // canvas units on a 1.6 x 0.8 quad, turned towards the camera.
        m_terminal = getUIContext().createWorldCanvas(registry, {1024.0f, 512.0f}, {1.6f, 0.8f}, 2.0f);
        auto& terminal = registry.get<UICanvas>(m_terminal);
        terminal.scaleMode = UICanvas::ScaleMode::ScaleWithScreen;
        terminal.referenceSize = {1024.0f, 512.0f};
        terminal.clearColor = srgb(0.03f, 0.06f, 0.1f);
        auto& transform = registry.get<ECS::TransformComponent>(m_terminal);
        transform.position = {-1.5f, 0.9f, 0.3f};
        transform.rotation = {0.0f, 0.45f, 0.0f};

        const auto heading = element(m_terminal, {0, 0}, {1, 0}, {0, 0}, {40, 30}, {-80, 80});
        text(heading, playfair, "World canvas", 64.0f, srgb(0.95f, 0.85f, 0.55f));
        const auto body = element(m_terminal, {0, 0}, {1, 1}, {0, 0}, {40, 130}, {-80, -170});
        text(body, roboto, "Rendered into its own texture every frame, then shown on a quad by the "
                           "default pipeline as emission. R switches its resolution.", 34.0f,
             srgb(0.85f, 0.9f, 0.95f));
        m_terminalCounter = element(m_terminal, {0, 1}, {1, 1}, {0, 1}, {40, -30}, {-80, 50});
        text(m_terminalCounter, roboto, "", 40.0f, srgb(0.6f, 0.95f, 0.7f));
        m_terminalButton = createButton(registry, m_terminal, "Click me", roboto, {260.0f, 70.0f});
        place(m_terminalButton, {1, 1}, {-40, -110});

        // Bottom-left settings: spin the box, set its angle, reset it.
        const auto settings = element(m_canvas, {0, 1}, {0, 1}, {0, 1}, {24, -24}, {340, 170});
        registry.emplace<UIPanel>(settings).color = srgb(0.08f, 0.09f, 0.12f, 0.82f);
        m_spin = createToggle(registry, settings, "Spin the box", roboto, false, {300.0f, 28.0f});
        place(m_spin, {0, 0}, {16, 16});
        m_angle = createSlider(registry, settings, 0.0f, 360.0f, 0.0f, {300.0f, 24.0f});
        place(m_angle, {0, 0}, {16, 66});
        m_reset = createButton(registry, settings, "Reset", roboto, {140.0f, 40.0f});
        place(m_reset, {0, 0}, {16, 112});

        // Box.gltf's mesh entity, turned by the slider.
        const auto box = registry.view<MeshComponent, ECS::TransformComponent>();
        for (const auto entity : box) {
            if (entity != m_terminal) m_box = entity;
        }
    }

    void onUpdate(float dt) override {
        ++m_frame;
        auto& registry = getRegistry();
        if (!m_screenshot.empty()) scriptPointer();

        registry.get<UIText>(m_counter).text = "frame " + std::to_string(m_frame);
        const glm::vec2 pixels = registry.get<UICanvas>(m_terminal).pixelSize;
        if (registry.get<UIInteractable>(m_terminalButton).clicked) ++m_clicks;
        registry.get<UIText>(m_terminalCounter).text =
            std::to_string(static_cast<int>(pixels.x)) + "x" + std::to_string(static_cast<int>(pixels.y)) +
            " pixels, " + std::to_string(m_clicks) + " clicks";
        if (!m_screenshot.empty() && m_frame == 60) toggleResolution();

        // The widgets' edges are from the input pass of the frame before.
        auto& angle = registry.get<UISlider>(m_angle);
        if (registry.get<UIInteractable>(m_reset).clicked) {
            angle.value = 0.0f;
            registry.get<UIToggle>(m_spin).isOn = false;
        }
        if (registry.get<UIToggle>(m_spin).isOn && !registry.get<UIInteractable>(m_angle).pressed) {
            angle.value = std::fmod(angle.value + 45.0f * dt, 360.0f);
        }
        if (m_box != entt::null) {
            auto& transform = registry.get<ECS::TransformComponent>(m_box);
            transform.rotation.y = glm::radians(angle.value);
            transform.isDirty = true;
        }
    }

    void onKeyPressed(int keyCode) override {
        if (keyCode == GLFW_KEY_R) toggleResolution();
    }

    void onPostRender() override {
        if (!m_screenshot.empty() && m_frame == 120) {
            captureScreenshot(m_screenshot);
            glfwSetWindowShouldClose(getWindow().getWindow(), GLFW_TRUE);
        }
    }

private:
    /// With --screenshot: a pointer that turns spinning on, drags the angle
    /// slider, clicks the world canvas's button twice, then rests on Reset.
    /// It goes through the same input path as the mouse.
    void scriptPointer() {
        if (m_frame < 65) return;
        auto at = [&](entt::entity e, float across = 0.5f) {
            m_scriptPixel = pixelOf(e, across);
        };
        bool down = false;
        if (m_frame <= 67) { at(m_spin); down = m_frame == 66; }
        else if (m_frame <= 86) { at(m_angle, 0.1f + 0.65f * std::min((m_frame - 75) / 10.0f, 1.0f)); down = m_frame >= 75 && m_frame <= 85; }
        else if (m_frame <= 94) { at(m_terminalButton); down = m_frame == 91 || m_frame == 93; }
        else { at(m_reset); }

        int width = 0, height = 0;
        glfwGetWindowSize(getWindow().getWindow(), &width, &height);
        const glm::vec2 screen = screenSize();
        for (auto [entity, state] : getRegistry().view<ECS::InputStateComponent>().each()) {
            state.mousePosition = m_scriptPixel * glm::vec2(width, height) / screen;
            state.mouseButtons[GLFW_MOUSE_BUTTON_LEFT] = down;
            state.mouseCaptured = false;
        }
    }

    glm::vec2 screenSize() {
        const VkExtent2D extent = getSwapChain().getSwapChainExtent();
        return {static_cast<float>(extent.width), static_cast<float>(extent.height)};
    }

    /// Screen pixel of a point `across` the way along the middle of an
    /// element's last laid-out rect, on a screen or a world canvas.
    glm::vec2 pixelOf(entt::entity e, float across) {
        auto& registry = getRegistry();
        const auto& element = registry.get<UIRect>(e);
        const auto& canvas = registry.get<UICanvas>(element.canvas);
        const glm::vec2 point(glm::mix(element.rect.min.x, element.rect.max.x, across),
                              (element.rect.min.y + element.rect.max.y) * 0.5f);
        if (canvas.mode == UICanvas::Mode::ScreenOverlay) return point * canvas.scale;

        const glm::vec2 uv = point / canvas.size;
        const glm::mat4& world = registry.get<ECS::TransformComponent>(element.canvas).worldMatrix;
        const auto& camera = registry.get<ECS::CameraComponent>(getCameraEntity());
        const glm::vec4 clip = camera.projectionMatrix * camera.viewMatrix * world *
                               glm::vec4(uv.x - 0.5f, 0.5f - uv.y, 0.0f, 1.0f);
        const glm::vec2 ndc = glm::vec2(clip) / clip.w;
        return glm::vec2((ndc.x + 1.0f) * 0.5f, (1.0f - ndc.y) * 0.5f) * screenSize();
    }

    /// Puts an element's corner given by `anchor` at `offset` from the same
    /// corner of its parent.
    void place(entt::entity e, glm::vec2 anchor, glm::vec2 offset) {
        auto& rect = getRegistry().get<UIRect>(e);
        rect.anchorMin = rect.anchorMax = rect.pivot = anchor;
        rect.anchoredPosition = offset;
    }

    void toggleResolution() {
        auto& canvas = getRegistry().get<UICanvas>(m_terminal);
        canvas.pixelSize = canvas.pixelSize.x > 600.0f ? glm::vec2(512.0f, 256.0f) : glm::vec2(1024.0f, 512.0f);
    }

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
    entt::entity m_terminal = entt::null;
    entt::entity m_terminalCounter = entt::null;
    entt::entity m_terminalButton = entt::null;
    entt::entity m_spin = entt::null;
    entt::entity m_angle = entt::null;
    entt::entity m_reset = entt::null;
    entt::entity m_box = entt::null;
    int m_frame = 0;
    int m_clicks = 0;
    glm::vec2 m_scriptPixel{0.0f};
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
