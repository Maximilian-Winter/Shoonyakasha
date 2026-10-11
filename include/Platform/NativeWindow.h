//
// NativeWindow.h - What the engine needs from the window beyond GLFW
//
// Input method (IME) composition, and a hook on the window's messages. On
// Windows it replaces GLFW's window procedure with one that handles the IME
// messages and the hooks, and passes every other message on to GLFW's. On
// other platforms it does nothing.
//
// The IME is attached to the window only while text input is active, so keys
// pressed for anything else never open an input method's candidate list.
// While attached, what it composes is published as TextCompositionEvent and
// what it commits as CharEvent, once per character.
//

#pragma once

#include "Core/EventSystem.h"
#include "Core/TextComposition.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string_view>

struct GLFWwindow;

namespace Shoonyakasha {
namespace Platform {

class NativeWindow {
public:
    /// The implementation for this platform, for `window`, publishing to
    /// `events`. Both must outlive it.
    static std::unique_ptr<NativeWindow> create(GLFWwindow* window, EventDispatcher* events);

    /// Gives the window back GLFW's procedure and its default input context.
    virtual ~NativeWindow() = default;

    /// Attaches the IME while text input is active. Making it inactive
    /// cancels a composition in progress.
    virtual void setTextInputActive(bool active) = 0;
    virtual bool isTextInputActive() const = 0;

    /// The caret, in window coordinates (top-left origin, as the cursor's
    /// position comes): the IME shows its candidate list beside it.
    virtual void setTextInputRect(glm::vec2 min, glm::vec2 max) = 0;

    /// Sees each message the window gets, before the IME handling and GLFW;
    /// on Windows the message, WPARAM and LPARAM. Returning a value handles
    /// the message, and the value is the window procedure's result.
    using MessageHook = std::function<std::optional<intptr_t>(uint32_t message, uintptr_t wParam, intptr_t lParam)>;
    virtual int addMessageHook(MessageHook hook) = 0;
    virtual void removeMessageHook(int id) = 0;

    /// The platform's window: an HWND on Windows; null elsewhere.
    virtual void* handle() const = 0;
};

/// IMM32's attribute of a composition character (ATTR_* in imm.h).
enum class ImeAttribute : uint8_t {
    Input = 0,
    TargetConverted = 1,
    Converted = 2,
    TargetNotConverted = 3,
    InputError = 4,
    FixedConverted = 5,
};

/// The composition from what IMM32 reports: `text` in UTF-16; one attribute
/// per UTF-16 unit (GCS_COMPATTR); the clauses' start offsets in UTF-16
/// units followed by the text's length (GCS_COMPCLAUSE); and the cursor in
/// UTF-16 units (GCS_CURSORPOS). A clause is active where its first
/// character's attribute is a target. Missing or malformed clause offsets
/// leave the clauses empty.
TextComposition imeComposition(std::u16string_view text, std::span<const uint8_t> attributes,
                               std::span<const uint32_t> clauseOffsets, size_t cursor);

} // namespace Platform
} // namespace Shoonyakasha
