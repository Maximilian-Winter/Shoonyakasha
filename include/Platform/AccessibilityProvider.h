//
// AccessibilityProvider.h - Screen readers' view of a window's canvas UI
//
// On Windows a UI Automation provider: the window answers WM_GETOBJECT with
// a fragment root whose children are the snapshot's canvases and elements
// (UI/Accessibility.h), with the Invoke, Toggle, RangeValue, Value, Text and
// Scroll patterns as their roles have them. It reads only the snapshot, and
// queues what a screen reader does for AccessibilitySystem to apply. After
// each snapshot it raises the events for what changed: focus, toggle
// states, values, names and the tree's structure. On other platforms there
// is none.
//

#pragma once

#include <cstdint>
#include <memory>
#include <optional>

namespace Shoonyakasha {

namespace UI { class AccessibilityState; }

namespace Platform {

class AccessibilityProvider {
public:
    /// The provider for window `nativeWindow` (NativeWindow::handle), or
    /// null where there is none. `state` must outlive it.
    static std::unique_ptr<AccessibilityProvider> create(void* nativeWindow, UI::AccessibilityState& state);

    /// Lets go of the window; screen readers still holding its elements are
    /// told they are gone.
    virtual ~AccessibilityProvider() = default;

    /// A message of the window, from NativeWindow's message hook: answers
    /// UI Automation's WM_GETOBJECT, and enables the snapshot from then on.
    virtual std::optional<intptr_t> handleMessage(uint32_t message, uintptr_t wParam, intptr_t lParam) = 0;

    /// Raises the events for what changed since the last call. Called on
    /// the window's thread after each snapshot.
    virtual void snapshotPublished() = 0;
};

} // namespace Platform
} // namespace Shoonyakasha
