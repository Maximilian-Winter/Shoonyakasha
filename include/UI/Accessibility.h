//
// Accessibility.h - What screen readers see of the canvas UI, and what they do
//
// Each frame AccessibilitySystem describes the screen canvases as a tree of
// nodes: role, name, value, state, bounds in window coordinates, and the
// actions each supports. A platform's accessibility provider reads only that
// snapshot, never the registry, and queues the actions a screen reader asks
// for; they are applied in the next frame. The snapshot and the queue are
// guarded, so the provider may be called on any thread.
//

#pragma once

#include "UI/UIComponents.h"

#include <entt/entt.hpp>
#include <glm/glm.hpp>

#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace Shoonyakasha {
namespace UI {

enum class AccessibleRole : uint8_t {
    Group,      // a canvas, or an element marked with UIAccessible
    Button,
    CheckBox,   // a toggle
    Slider,
    Edit,       // a text field
    Pane,       // a scroll view
    Text,
};

/// Names an element for screen readers, or hides it from them. Buttons and
/// toggles are named by their label, text by itself and text fields by their
/// placeholder without it.
struct UIAccessible {
    std::string name;                    // empty: the name the element has without it
    std::string description;
    bool hidden = false;                 // the element and its descendants are decoration
    std::optional<AccessibleRole> role;  // in place of the role its components give
};

struct AccessibleNode {
    uint32_t id = 0;                     // the element's entity; stable while it exists
    int parent = -1;                     // index into the snapshot's nodes; -1 for a canvas
    std::vector<int> children;           // in draw order
    AccessibleRole role = AccessibleRole::Group;
    std::string name;                    // UTF-8
    std::string description;
    Rect bounds;                         // window coordinates, clipped
    bool offscreen = false;              // clipped away entirely
    bool enabled = true;
    bool focusable = false;
    bool focused = false;

    // CheckBox.
    bool toggled = false;
    // Slider.
    float minimum = 0.0f;
    float maximum = 1.0f;
    float value = 0.0f;
    float smallChange = 0.1f;
    // Edit: the text (empty for a password), and the caret and selection in
    // code points.
    std::string text;
    bool readOnly = false;
    bool password = false;
    size_t selectionStart = 0;
    size_t selectionEnd = 0;
    // Pane: how far it is scrolled, 0 to 100 per axis, and whether it can be.
    glm::vec2 scrollPercent{0.0f};
    bool scrollsHorizontally = false;
    bool scrollsVertically = false;
    glm::vec2 viewPercent{100.0f};       // the share of the content in view
};

struct AccessibilitySnapshot {
    std::vector<AccessibleNode> nodes;   // canvases first, each followed by its tree in depth-first order
    uint64_t frame = 0;                  // counts snapshots
    int focused = -1;                    // the node with keyboard focus, or -1

    /// The node for element `id`, or -1.
    int find(uint32_t id) const {
        for (size_t i = 0; i < nodes.size(); ++i) {
            if (nodes[i].id == id) return static_cast<int>(i);
        }
        return -1;
    }
};

struct AccessibleAction {
    enum class Kind : uint8_t { Invoke, Toggle, SetValue, SetText, Select, Focus, Scroll };
    Kind kind = Kind::Invoke;
    uint32_t node = 0;                   // AccessibleNode::id
    float value = 0.0f;                  // SetValue
    std::string text;                    // SetText, UTF-8
    size_t selectionStart = 0;           // Select: code points of a text field; the caret goes to the end
    size_t selectionEnd = 0;
    glm::vec2 scrollPercent{0.0f};       // Scroll, 0 to 100 per axis; negative leaves an axis
};

/// The snapshot the provider reads and the actions it queues. Owned by
/// UIContext.
class AccessibilityState {
public:
    void publish(AccessibilitySnapshot snapshot) {
        std::lock_guard lock(m_mutex);
        m_snapshot = std::move(snapshot);
    }

    /// Calls `read` with the current snapshot, under the lock.
    template <typename Read>
    auto read(Read&& read) const {
        std::lock_guard lock(m_mutex);
        return read(m_snapshot);
    }

    void queue(AccessibleAction action) {
        std::lock_guard lock(m_mutex);
        m_actions.push_back(std::move(action));
    }

    std::vector<AccessibleAction> takeActions() {
        std::lock_guard lock(m_mutex);
        return std::exchange(m_actions, {});
    }

    /// Whether a snapshot is wanted: set while a provider exists, so nothing
    /// is built for no one.
    bool enabled = false;

private:
    mutable std::recursive_mutex m_mutex;
    AccessibilitySnapshot m_snapshot;
    std::vector<AccessibleAction> m_actions;
};

} // namespace UI
} // namespace Shoonyakasha
