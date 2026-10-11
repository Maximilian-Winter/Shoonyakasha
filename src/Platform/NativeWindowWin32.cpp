//
// NativeWindowWin32.cpp - The window procedure GLFW's is wrapped in, and the
// IME through IMM32
//
// Windows bridges IMM32 to the Text Services Framework, so input methods
// built on either work through it.
//

#ifdef _WIN32

#include "Platform/NativeWindow.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <imm.h>

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include <cmath>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace Shoonyakasha {
namespace Platform {

namespace {

constexpr wchar_t kProperty[] = L"Shoonyakasha.NativeWindow";

/// One of the strings or arrays IMM32 keeps for the composition, as bytes.
std::vector<uint8_t> compositionBytes(HIMC context, DWORD index) {
    const LONG size = ImmGetCompositionStringW(context, index, nullptr, 0);
    if (size <= 0) return {};
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    ImmGetCompositionStringW(context, index, bytes.data(), static_cast<DWORD>(size));
    return bytes;
}

std::u16string compositionString(HIMC context, DWORD index) {
    const std::vector<uint8_t> bytes = compositionBytes(context, index);
    std::u16string text(bytes.size() / sizeof(char16_t), u'\0');
    std::memcpy(text.data(), bytes.data(), text.size() * sizeof(char16_t));
    return text;
}

class Win32NativeWindow final : public NativeWindow {
public:
    Win32NativeWindow(GLFWwindow* window, EventDispatcher* events)
        : m_hwnd(glfwGetWin32Window(window)), m_events(events) {
        if (!m_hwnd) return;
        SetPropW(m_hwnd, kProperty, this);
        m_previous = reinterpret_cast<WNDPROC>(
            SetWindowLongPtrW(m_hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&procedure)));
        ImmAssociateContextEx(m_hwnd, nullptr, 0);   // no IME until text input is active
    }

    ~Win32NativeWindow() override {
        if (!m_hwnd) return;
        if (GetWindowLongPtrW(m_hwnd, GWLP_WNDPROC) == reinterpret_cast<LONG_PTR>(&procedure)) {
            SetWindowLongPtrW(m_hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(m_previous));
        }
        RemovePropW(m_hwnd, kProperty);
        ImmAssociateContextEx(m_hwnd, nullptr, IACE_DEFAULT);
    }

    void setTextInputActive(bool active) override {
        if (active == m_active) return;
        m_active = active;
        m_rectSet = false;
        if (!m_hwnd) return;
        if (active) {
            ImmAssociateContextEx(m_hwnd, nullptr, IACE_DEFAULT);
            return;
        }
        if (HIMC context = ImmGetContext(m_hwnd)) {
            ImmNotifyIME(context, NI_COMPOSITIONSTR, CPS_CANCEL, 0);
            ImmReleaseContext(m_hwnd, context);
        }
        ImmAssociateContextEx(m_hwnd, nullptr, 0);
        publish({});
    }

    bool isTextInputActive() const override { return m_active; }

    void setTextInputRect(glm::vec2 min, glm::vec2 max) override {
        if (!m_hwnd || !m_active) return;
        const RECT rect{std::lround(min.x), std::lround(min.y), std::lround(max.x), std::lround(max.y)};
        if (m_rectSet && EqualRect(&rect, &m_rect)) return;
        HIMC context = ImmGetContext(m_hwnd);
        if (!context) return;
        // The composition window is not shown, but some input methods place
        // their candidate list by it.
        COMPOSITIONFORM composition{};
        composition.dwStyle = CFS_POINT;
        composition.ptCurrentPos = {rect.left, rect.top};
        ImmSetCompositionWindow(context, &composition);
        // Below the caret, kept off it.
        CANDIDATEFORM candidate{};
        candidate.dwIndex = 0;
        candidate.dwStyle = CFS_EXCLUDE;
        candidate.ptCurrentPos = {rect.left, rect.bottom};
        candidate.rcArea = rect;
        ImmSetCandidateWindow(context, &candidate);
        ImmReleaseContext(m_hwnd, context);
        m_rect = rect;
        m_rectSet = true;
    }

    int addMessageHook(MessageHook hook) override {
        m_hooks.emplace(m_nextHook, std::move(hook));
        return m_nextHook++;
    }

    void removeMessageHook(int id) override { m_hooks.erase(id); }

private:
    static LRESULT CALLBACK procedure(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
        auto* self = static_cast<Win32NativeWindow*>(GetPropW(hwnd, kProperty));
        if (!self) return DefWindowProcW(hwnd, message, wParam, lParam);
        return self->handle(message, wParam, lParam);
    }

    LRESULT handle(UINT message, WPARAM wParam, LPARAM lParam) {
        if (!m_hooks.empty()) {
            const auto hooks = m_hooks;   // a hook may remove itself
            for (const auto& [id, hook] : hooks) {
                if (const auto result = hook(message, wParam, lParam)) return static_cast<LRESULT>(*result);
            }
        }
        switch (message) {
        case WM_IME_SETCONTEXT:
            // The text field draws the composition; the IME draws only its
            // candidate list.
            if (wParam) lParam &= ~static_cast<LPARAM>(ISC_SHOWUICOMPOSITIONWINDOW);
            break;
        case WM_IME_STARTCOMPOSITION:
            return 0;
        case WM_IME_COMPOSITION:
            readComposition(lParam);
            return 0;   // so it does not come again as WM_IME_CHAR and WM_CHAR
        case WM_IME_ENDCOMPOSITION:
            publish({});
            return 0;
        default:
            break;
        }
        return CallWindowProcW(m_previous, m_hwnd, message, wParam, lParam);
    }

    /// Publishes the text committed, if any, and the composition as it is now.
    void readComposition(LPARAM changes) {
        HIMC context = ImmGetContext(m_hwnd);
        if (!context) return;
        if (changes & GCS_RESULTSTR) {
            for (char32_t c : imeComposition(compositionString(context, GCS_RESULTSTR), {}, {}, 0).text) {
                m_events->publish(CharEvent{c});
            }
        }
        const std::u16string text = compositionString(context, GCS_COMPSTR);
        const std::vector<uint8_t> attributes = compositionBytes(context, GCS_COMPATTR);
        const std::vector<uint8_t> clauseBytes = compositionBytes(context, GCS_COMPCLAUSE);
        std::vector<uint32_t> clauses(clauseBytes.size() / sizeof(uint32_t));
        std::memcpy(clauses.data(), clauseBytes.data(), clauses.size() * sizeof(uint32_t));
        const LONG cursor = ImmGetCompositionStringW(context, GCS_CURSORPOS, nullptr, 0);
        ImmReleaseContext(m_hwnd, context);
        publish(imeComposition(text, attributes, clauses, cursor >= 0 ? static_cast<size_t>(cursor) : text.size()));
    }

    void publish(TextComposition composition) {
        if (composition == m_composition) return;
        m_composition = composition;
        if (m_events) m_events->publish(TextCompositionEvent{std::move(composition)});
    }

    HWND m_hwnd = nullptr;
    WNDPROC m_previous = nullptr;
    EventDispatcher* m_events = nullptr;
    bool m_active = false;
    bool m_rectSet = false;
    RECT m_rect{};
    TextComposition m_composition;
    std::map<int, MessageHook> m_hooks;
    int m_nextHook = 1;
};

} // namespace

std::unique_ptr<NativeWindow> NativeWindow::create(GLFWwindow* window, EventDispatcher* events) {
    return std::make_unique<Win32NativeWindow>(window, events);
}

} // namespace Platform
} // namespace Shoonyakasha

#endif
