//
// UISystems.h - ECS systems of the canvas UI
//
// They run after TransformSystem and CameraSystem (priority 0), in the order
// input, layout, text. Each sets its priority in its constructor, before
// SystemManager::addSystem sorts.
//

#pragma once

#include "ECS/CameraController.h"
#include "ECS/CameraRay.h"
#include "ECS/Systems.h"

#include <entt/entt.hpp>
#include <glm/glm.hpp>

#include <array>
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
struct UIFocusState;
struct UIClipboard;
struct UIPointerDepth;

/// Where an elastic scroll view shows a drag to `offset` canvas units along
/// an axis it can move over [0, range]: unchanged within it, and past an end
/// by a distance that grows ever more slowly and never reaches `viewSize`.
float elasticOffset(float offset, float range, float viewSize);

/// The drag offset that elasticOffset shows at `shown`, which must be less
/// than `viewSize` past an end.
float elasticDragOffset(float shown, float range, float viewSize);

/// One step of a critically damped spring from `current` towards `target`:
/// it gets there in roughly `smoothTime` seconds and never passes it.
/// `velocity`, in units per second, is carried from step to step.
float springTowards(float current, float target, float& velocity, float smoothTime, float dt);

/// Pointer input for the canvas UI. Runs before CanvasLayoutSystem, so it
/// hit-tests the rects laid out the frame before, and writes:
/// - UIInteractable state and its one-frame clicked and valueChanged edges,
///   for the left mouse button;
/// - UIToggle and UISlider values;
/// - the focused UITextField's text, caret and selection, from the keyboard
///   and the pointer;
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
    /// the two are the same. Without `focus`, the keyboard and gamepad are
    /// not used; without `clipboard`, text fields cannot copy or paste.
    /// With a valid `depth`, world canvases behind the scene under the
    /// pointer are not hit.
    CanvasInputSystem(const glm::vec2* screenSize, const glm::vec2* windowSize, UIPointerState* pointer,
                      UIFocusState* focus = nullptr, UIClipboard* clipboard = nullptr,
                      const UIPointerDepth* depth = nullptr);

    /// A world canvas counts as in front of the scene under the pointer up to
    /// this far behind it, in world units, plus kOcclusionMarginPerUnit of
    /// its distance: the canvas's own quad is in the depth buffer too.
    static constexpr float kOcclusionMargin = 0.02f;
    static constexpr float kOcclusionMarginPerUnit = 0.01f;

    /// Seconds between two presses that make a double-click.
    static constexpr float kDoubleClickTime = 0.4f;

    void update(entt::registry& registry, float deltaTime) override;

private:
    void updateFocus(entt::registry& registry, const ECS::InputStateComponent& input, bool pressedNow,
                     entt::entity hovered);
    void pointTextField(entt::registry& registry, entt::entity field, glm::vec2 pixel,
                        const std::optional<ECS::Ray>& ray, bool pressedNow, bool shift);
    void editTextField(entt::registry& registry, entt::entity field, const ECS::InputStateComponent& input);
    void updateDrag(entt::registry& registry, glm::vec2 pixel, const std::optional<ECS::Ray>& ray,
                    bool down, bool pressedNow, entt::entity hitElement, float deltaTime);
    bool dragScrollbar(entt::registry& registry, entt::entity bar, glm::vec2 pixel,
                       const std::optional<ECS::Ray>& ray, bool pressedNow);

    const glm::vec2* m_screenSize;
    const glm::vec2* m_windowSize;
    UIPointerState* m_pointer;
    const UIPointerDepth* m_depth;
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

    UIFocusState* m_focus;
    std::array<bool, GLFW_GAMEPAD_BUTTON_LAST + 1> m_lastPadButtons{};  // for press edges

    // Text fields.
    UIClipboard* m_clipboard;
    float m_time = 0.0f;                        // seconds of updates, for double-clicks
    float m_lastPressTime = -1.0f;
    entt::entity m_lastPressField = entt::null;
    bool m_selectingWords = false;              // a double-click selected a word; dragging leaves it
    entt::entity m_lastFocused = entt::null;    // focused at the end of the last update
};

/// Lays out each UIText of a laid-out element into its UITextCache, in the
/// element's rect, when the text, font, style or rect size changed. Before
/// that it shows each UITextField's text, its mask or its placeholder in the
/// field's text element; after it, it moves the text to keep the caret in
/// view and places the caret and selection, setting their rects for this
/// frame as well as their anchors for the next.
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
