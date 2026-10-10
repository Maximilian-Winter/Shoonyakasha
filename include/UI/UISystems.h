//
// UISystems.h - ECS systems of the canvas UI
//
// They run after TransformSystem and CameraSystem (priority 0), in the order
// input, layout, text. Each sets its priority in its constructor, before
// SystemManager::addSystem sorts.
//

#pragma once

#include "ECS/CameraRay.h"
#include "ECS/Systems.h"

#include <entt/entt.hpp>
#include <glm/glm.hpp>

#include <optional>

namespace Shoonyakasha {
namespace UI {

class FontLibrary;

/// Lays out every canvas each frame. Elements no longer under a canvas have
/// visibleInHierarchy cleared and canvas set to null.
class CanvasLayoutSystem : public ECS::ISystem {
public:
    static constexpr int kPriority = 51;

    /// `screenSize` is read on each update; the owner keeps it current.
    /// `fonts` measures text for layout groups and size fitters.
    explicit CanvasLayoutSystem(const glm::vec2* screenSize, const FontLibrary* fonts = nullptr);

    void update(entt::registry& registry, float deltaTime) override;

private:
    const glm::vec2* m_screenSize;
    const FontLibrary* m_fonts;
};

struct UIPointerState;

/// Pointer input for the canvas UI. Runs before CanvasLayoutSystem, so it
/// hit-tests the rects laid out the frame before, and writes:
/// - UIInteractable state and its one-frame clicked and valueChanged edges,
///   for the left mouse button;
/// - UIToggle and UISlider values;
/// - the UIPointerState given to it.
/// An element pressed keeps the pointer until release: a slider follows a
/// drag outside its rect, and a release elsewhere is not a click. While the
/// mouse is captured the pointer is the centre of the screen. It also shows
/// toggles' checkmarks and places sliders' fills and handles from their
/// values, each frame.
class CanvasInputSystem : public ECS::ISystem {
public:
    static constexpr int kPriority = 50;

    /// `screenSize` is in framebuffer pixels and `windowSize` in the window
    /// coordinates the cursor position comes in; they differ on high-DPI
    /// displays. Both are read on each update. `windowSize` may be null when
    /// the two are the same.
    CanvasInputSystem(const glm::vec2* screenSize, const glm::vec2* windowSize, UIPointerState* pointer);

    void update(entt::registry& registry, float deltaTime) override;

private:
    void updateDrag(entt::registry& registry, glm::vec2 pixel, const std::optional<ECS::Ray>& ray,
                    bool down, bool pressedNow, entt::entity hitElement, float deltaTime);
    bool dragScrollbar(entt::registry& registry, entt::entity bar, glm::vec2 pixel,
                       const std::optional<ECS::Ray>& ray, bool pressedNow);

    const glm::vec2* m_screenSize;
    const glm::vec2* m_windowSize;
    UIPointerState* m_pointer;
    entt::entity m_captured = entt::null;
    bool m_wasDown = false;

    // Dragging a scroll view's content.
    entt::entity m_dragView = entt::null;   // the view under the press, until release
    bool m_dragging = false;                // the pointer has moved past the threshold
    glm::vec2 m_pressPosition{0.0f};        // canvas units, on the view's canvas
    glm::vec2 m_lastDragPosition{0.0f};
    glm::vec2 m_dragStartOffset{0.0f};
    // Dragging a scrollbar's handle: where along it the handle was taken, or
    // negative after a press on the track.
    float m_barGrab = -1.0f;
};

/// Lays out each UIText of a laid-out element into its UITextCache, in the
/// element's rect, when the text, font, style or rect size changed.
class CanvasTextSystem : public ECS::ISystem {
public:
    static constexpr int kPriority = 52;

    explicit CanvasTextSystem(const FontLibrary* fonts);

    void update(entt::registry& registry, float deltaTime) override;

private:
    const FontLibrary* m_fonts;
};

} // namespace UI
} // namespace Shoonyakasha
