//
// UIAutomationWin32.cpp - The canvas UI as UI Automation elements
//
// The window's fragment root has the snapshot's canvases as children, and
// each element answers from the snapshot by its node's id, so an element a
// screen reader keeps refers to the same widget from frame to frame and
// reports it gone once it is. The provider asks for COM threading, and the
// window's thread is a single-threaded apartment, so UI Automation calls it
// on that thread while the window pumps its messages; the snapshot is read
// under its lock all the same.
//

#ifdef _WIN32

#include "Platform/AccessibilityProvider.h"
#include "UI/Accessibility.h"
#include "UI/Utf8.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <ole2.h>
#include <UIAutomation.h>

#include <algorithm>
#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace Shoonyakasha {
namespace Platform {

namespace {

using UI::AccessibilitySnapshot;
using UI::AccessibleAction;
using UI::AccessibleNode;
using UI::AccessibleRole;

// ─── Strings and values ─────────────────────────────────────────

std::wstring wide(const std::string& utf8) {
    if (utf8.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    std::wstring out(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), out.data(), n);
    return out;
}

std::string narrow(const wchar_t* text) {
    if (!text || !*text) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, -1, out.data(), n, nullptr, nullptr);
    out.resize(static_cast<size_t>(std::max(n - 1, 0)));
    return out;
}

void setString(VARIANT* v, const std::string& utf8) {
    const std::wstring w = wide(utf8);
    v->vt = VT_BSTR;
    v->bstrVal = SysAllocStringLen(w.data(), static_cast<UINT>(w.size()));
}

void setBool(VARIANT* v, bool value) {
    v->vt = VT_BOOL;
    v->boolVal = value ? VARIANT_TRUE : VARIANT_FALSE;
}

void setInt(VARIANT* v, int value) {
    v->vt = VT_I4;
    v->lVal = value;
}

template <typename Interface>
HRESULT give(Interface* object, Interface** out) {
    if (!out) return E_INVALIDARG;
    *out = object;
    if (object) object->AddRef();
    return S_OK;
}

long controlType(AccessibleRole role) {
    switch (role) {
    case AccessibleRole::Button:   return UIA_ButtonControlTypeId;
    case AccessibleRole::CheckBox: return UIA_CheckBoxControlTypeId;
    case AccessibleRole::Slider:   return UIA_SliderControlTypeId;
    case AccessibleRole::Edit:     return UIA_EditControlTypeId;
    case AccessibleRole::Pane:     return UIA_PaneControlTypeId;
    case AccessibleRole::Text:     return UIA_TextControlTypeId;
    default:                       return UIA_GroupControlTypeId;
    }
}

// ─── What the elements share ────────────────────────────────────

class RootElement;
class NodeElement;

/// The window and the state, shared by every element so that elements a
/// screen reader still holds after the provider is gone find it gone.
struct Core {
    HWND hwnd = nullptr;
    std::mutex mutex;                                   // guards state, root and elements
    UI::AccessibilityState* state = nullptr;            // null once the provider is gone
    RootElement* root = nullptr;                        // one reference held
    std::map<uint32_t, NodeElement*> elements;          // one reference each held

    /// Calls `use` with the snapshot and the node `id` and its index, under
    /// the snapshot's lock. False when the provider or the node is gone.
    template <typename Use>
    bool withNode(uint32_t id, Use&& use) {
        UI::AccessibilityState* s = nullptr;
        {
            std::lock_guard lock(mutex);
            s = state;
        }
        if (!s) return false;
        return s->read([&](const AccessibilitySnapshot& snapshot) {
            const int index = snapshot.find(id);
            if (index < 0) return false;
            use(snapshot, snapshot.nodes[static_cast<size_t>(index)], index);
            return true;
        });
    }

    template <typename Use>
    bool withSnapshot(Use&& use) {
        UI::AccessibilityState* s = nullptr;
        {
            std::lock_guard lock(mutex);
            s = state;
        }
        if (!s) return false;
        s->read([&](const AccessibilitySnapshot& snapshot) { use(snapshot); });
        return true;
    }

    bool queue(AccessibleAction action) {
        std::lock_guard lock(mutex);
        if (!state) return false;
        state->queue(std::move(action));
        return true;
    }

    NodeElement* element(const std::shared_ptr<Core>& self, uint32_t id);
};

/// AddRef and Release for an object that implements several interfaces.
class RefCounted {
public:
    virtual ~RefCounted() = default;

protected:
    ULONG addRef() { return ++m_refs; }
    ULONG release() {
        const ULONG left = --m_refs;
        if (left == 0) delete this;
        return left;
    }

private:
    std::atomic<ULONG> m_refs{1};
};

#define SHOONYAKASHA_REFCOUNT                                                  \
    ULONG STDMETHODCALLTYPE AddRef() override { return addRef(); }             \
    ULONG STDMETHODCALLTYPE Release() override { return release(); }

// ─── Text ranges ────────────────────────────────────────────────

bool isSpace(char32_t c) {
    return c == U' ' || c == U'\t' || c == U'\n' || c == 0x3000 || c == 0x00A0;
}

/// The positions where units of `unit` start, with the text's end.
std::vector<size_t> boundaries(const std::u32string& text, TextUnit unit) {
    std::vector<size_t> out{0};
    for (size_t i = 1; i < text.size(); ++i) {
        bool starts = false;
        switch (unit) {
        case TextUnit_Character: starts = true; break;
        case TextUnit_Word:      starts = !isSpace(text[i]) && isSpace(text[i - 1]); break;
        case TextUnit_Line:
        case TextUnit_Paragraph: starts = text[i - 1] == U'\n'; break;
        default:                 break;
        }
        if (starts) out.push_back(i);
    }
    if (text.size() > 0) out.push_back(text.size());
    return out;
}

class TextRange final : public RefCounted, public ITextRangeProvider {
public:
    TextRange(std::shared_ptr<Core> core, uint32_t id, size_t start, size_t end)
        : m_core(std::move(core)), m_id(id), m_start(start), m_end(end) {}

    SHOONYAKASHA_REFCOUNT

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override {
        if (!out) return E_INVALIDARG;
        if (riid == __uuidof(IUnknown) || riid == __uuidof(ITextRangeProvider)) {
            *out = static_cast<ITextRangeProvider*>(this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }

    HRESULT STDMETHODCALLTYPE Clone(ITextRangeProvider** out) override {
        if (!out) return E_INVALIDARG;
        *out = new TextRange(m_core, m_id, m_start, m_end);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Compare(ITextRangeProvider* other, BOOL* out) override {
        if (!out) return E_INVALIDARG;
        const auto* range = dynamic_cast<TextRange*>(other);
        *out = range && range->m_id == m_id && range->m_start == m_start && range->m_end == m_end;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE CompareEndpoints(TextPatternRangeEndpoint endpoint, ITextRangeProvider* target,
                                               TextPatternRangeEndpoint targetEndpoint, int* out) override {
        const auto* range = dynamic_cast<TextRange*>(target);
        if (!out || !range || range->m_id != m_id) return E_INVALIDARG;
        const size_t a = endpoint == TextPatternRangeEndpoint_Start ? m_start : m_end;
        const size_t b = targetEndpoint == TextPatternRangeEndpoint_Start ? range->m_start : range->m_end;
        *out = a < b ? -1 : (a > b ? 1 : 0);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE ExpandToEnclosingUnit(TextUnit unit) override {
        const std::u32string text = this->text();
        clamp(text);
        const std::vector<size_t> starts = boundaries(text, unit);
        // The unit the start is in, or the last one at the end.
        size_t begin = 0, end = text.size();
        for (size_t k = 0; k + 1 < starts.size(); ++k) {
            if (starts[k] <= m_start) {
                begin = starts[k];
                end = starts[k + 1];
            }
        }
        m_start = begin;
        m_end = end;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE FindAttribute(TEXTATTRIBUTEID, VARIANT, BOOL, ITextRangeProvider** out) override {
        if (!out) return E_INVALIDARG;
        *out = nullptr;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE FindText(BSTR search, BOOL backward, BOOL ignoreCase, ITextRangeProvider** out) override {
        if (!out) return E_INVALIDARG;
        *out = nullptr;
        const std::u32string text = this->text();
        clamp(text);
        std::u32string needle = UI::decodeUtf8(narrow(search));
        std::u32string hay = text.substr(m_start, m_end - m_start);
        if (needle.empty()) return S_OK;
        if (ignoreCase) {
            auto lower = [](std::u32string s) {
                for (char32_t& c : s) {
                    if (c < 0x10000) c = static_cast<char32_t>(towlower(static_cast<wint_t>(c)));
                }
                return s;
            };
            needle = lower(needle);
            hay = lower(hay);
        }
        const size_t at = backward ? hay.rfind(needle) : hay.find(needle);
        if (at == std::u32string::npos) return S_OK;
        *out = new TextRange(m_core, m_id, m_start + at, m_start + at + needle.size());
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetAttributeValue(TEXTATTRIBUTEID attribute, VARIANT* out) override {
        if (!out) return E_INVALIDARG;
        VariantInit(out);
        if (attribute == UIA_IsReadOnlyAttributeId) {
            bool readOnly = true;
            m_core->withNode(m_id, [&](const AccessibilitySnapshot&, const AccessibleNode& node, int) {
                readOnly = node.readOnly;
            });
            setBool(out, readOnly);
            return S_OK;
        }
        out->vt = VT_UNKNOWN;
        return UiaGetReservedNotSupportedValue(&out->punkVal);
    }

    HRESULT STDMETHODCALLTYPE GetBoundingRectangles(SAFEARRAY** out) override {
        if (!out) return E_INVALIDARG;
        *out = SafeArrayCreateVector(VT_R8, 0, 0);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetEnclosingElement(IRawElementProviderSimple** out) override;

    HRESULT STDMETHODCALLTYPE GetText(int maxLength, BSTR* out) override {
        if (!out) return E_INVALIDARG;
        const std::u32string text = this->text();
        clamp(text);
        std::wstring w = wide(UI::encodeUtf8(text.substr(m_start, m_end - m_start)));
        if (maxLength >= 0 && w.size() > static_cast<size_t>(maxLength)) w.resize(static_cast<size_t>(maxLength));
        *out = SysAllocStringLen(w.data(), static_cast<UINT>(w.size()));
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Move(TextUnit unit, int count, int* moved) override {
        if (!moved) return E_INVALIDARG;
        *moved = 0;
        const std::u32string text = this->text();
        clamp(text);
        const bool degenerate = m_start == m_end;
        const std::vector<size_t> starts = boundaries(text, unit);
        size_t position = m_start;
        if (count > 0) {
            while (*moved < count) {
                const auto next = std::upper_bound(starts.begin(), starts.end(), position);
                if (next == starts.end() || *next >= text.size()) break;
                position = *next;
                ++*moved;
            }
        } else {
            while (*moved > count && position > 0) {
                auto previous = std::lower_bound(starts.begin(), starts.end(), position);
                if (previous == starts.begin()) break;
                position = *--previous;
                --*moved;
            }
        }
        m_start = position;
        if (degenerate) {
            m_end = m_start;
        } else {
            const auto next = std::upper_bound(starts.begin(), starts.end(), m_start);
            m_end = next == starts.end() ? text.size() : *next;
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE MoveEndpointByUnit(TextPatternRangeEndpoint endpoint, TextUnit unit, int count,
                                                 int* moved) override {
        if (!moved) return E_INVALIDARG;
        *moved = 0;
        const std::u32string text = this->text();
        clamp(text);
        const std::vector<size_t> starts = boundaries(text, unit);
        size_t& position = endpoint == TextPatternRangeEndpoint_Start ? m_start : m_end;
        while (count > 0 && *moved < count) {
            const auto next = std::upper_bound(starts.begin(), starts.end(), position);
            if (next == starts.end()) break;
            position = *next;
            ++*moved;
        }
        while (count < 0 && *moved > count) {
            auto previous = std::lower_bound(starts.begin(), starts.end(), position);
            if (previous == starts.begin()) break;
            position = *--previous;
            --*moved;
        }
        keepOrder(endpoint);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE MoveEndpointByRange(TextPatternRangeEndpoint endpoint, ITextRangeProvider* target,
                                                  TextPatternRangeEndpoint targetEndpoint) override {
        const auto* range = dynamic_cast<TextRange*>(target);
        if (!range || range->m_id != m_id) return E_INVALIDARG;
        const size_t to = targetEndpoint == TextPatternRangeEndpoint_Start ? range->m_start : range->m_end;
        (endpoint == TextPatternRangeEndpoint_Start ? m_start : m_end) = to;
        keepOrder(endpoint);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Select() override {
        AccessibleAction action;
        action.kind = AccessibleAction::Kind::Select;
        action.node = m_id;
        action.selectionStart = m_start;
        action.selectionEnd = m_end;
        return m_core->queue(action) ? S_OK : UIA_E_ELEMENTNOTAVAILABLE;
    }

    HRESULT STDMETHODCALLTYPE AddToSelection() override { return UIA_E_INVALIDOPERATION; }
    HRESULT STDMETHODCALLTYPE RemoveFromSelection() override { return UIA_E_INVALIDOPERATION; }
    HRESULT STDMETHODCALLTYPE ScrollIntoView(BOOL) override { return S_OK; }

    HRESULT STDMETHODCALLTYPE GetChildren(SAFEARRAY** out) override {
        if (!out) return E_INVALIDARG;
        *out = SafeArrayCreateVector(VT_UNKNOWN, 0, 0);
        return S_OK;
    }

private:
    std::u32string text() const {
        std::u32string out;
        m_core->withNode(m_id, [&](const AccessibilitySnapshot&, const AccessibleNode& node, int) {
            out = UI::decodeUtf8(node.text);
        });
        return out;
    }

    void clamp(const std::u32string& text) {
        m_start = std::min(m_start, text.size());
        m_end = std::min(std::max(m_end, m_start), text.size());
    }

    /// After `moved` moved: the other endpoint follows it past it.
    void keepOrder(TextPatternRangeEndpoint moved) {
        if (m_start <= m_end) return;
        if (moved == TextPatternRangeEndpoint_Start) m_end = m_start;
        else m_start = m_end;
    }

    std::shared_ptr<Core> m_core;
    uint32_t m_id;
    size_t m_start;
    size_t m_end;
};

// ─── The window's root ──────────────────────────────────────────

class RootElement final : public RefCounted,
                          public IRawElementProviderSimple,
                          public IRawElementProviderFragment,
                          public IRawElementProviderFragmentRoot {
public:
    explicit RootElement(std::weak_ptr<Core> core) : m_core(std::move(core)) {}

    SHOONYAKASHA_REFCOUNT

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override {
        if (!out) return E_INVALIDARG;
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IRawElementProviderSimple)) {
            *out = static_cast<IRawElementProviderSimple*>(this);
        } else if (riid == __uuidof(IRawElementProviderFragment)) {
            *out = static_cast<IRawElementProviderFragment*>(this);
        } else if (riid == __uuidof(IRawElementProviderFragmentRoot)) {
            *out = static_cast<IRawElementProviderFragmentRoot*>(this);
        } else {
            *out = nullptr;
            return E_NOINTERFACE;
        }
        AddRef();
        return S_OK;
    }

    // IRawElementProviderSimple
    HRESULT STDMETHODCALLTYPE get_ProviderOptions(ProviderOptions* out) override {
        if (!out) return E_INVALIDARG;
        *out = static_cast<ProviderOptions>(ProviderOptions_ServerSideProvider | ProviderOptions_UseComThreading);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetPatternProvider(PATTERNID, IUnknown** out) override {
        if (!out) return E_INVALIDARG;
        *out = nullptr;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetPropertyValue(PROPERTYID property, VARIANT* out) override {
        if (!out) return E_INVALIDARG;
        VariantInit(out);
        if (property == UIA_ProviderDescriptionPropertyId) setString(out, "Shoonyakasha canvas UI");
        if (property == UIA_FrameworkIdPropertyId) setString(out, "Shoonyakasha");
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_HostRawElementProvider(IRawElementProviderSimple** out) override {
        if (!out) return E_INVALIDARG;
        *out = nullptr;
        const auto core = m_core.lock();
        return core ? UiaHostProviderFromHwnd(core->hwnd, out) : S_OK;
    }

    // IRawElementProviderFragment
    HRESULT STDMETHODCALLTYPE Navigate(NavigateDirection direction, IRawElementProviderFragment** out) override;
    HRESULT STDMETHODCALLTYPE GetRuntimeId(SAFEARRAY** out) override {
        if (!out) return E_INVALIDARG;
        *out = nullptr;   // the window's own
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_BoundingRectangle(UiaRect* out) override {
        if (!out) return E_INVALIDARG;
        *out = UiaRect{};   // the window's own
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetEmbeddedFragmentRoots(SAFEARRAY** out) override {
        if (!out) return E_INVALIDARG;
        *out = nullptr;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetFocus() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE get_FragmentRoot(IRawElementProviderFragmentRoot** out) override {
        return give<IRawElementProviderFragmentRoot>(this, out);
    }

    // IRawElementProviderFragmentRoot
    HRESULT STDMETHODCALLTYPE ElementProviderFromPoint(double x, double y, IRawElementProviderFragment** out) override;
    HRESULT STDMETHODCALLTYPE GetFocus(IRawElementProviderFragment** out) override;

private:
    std::weak_ptr<Core> m_core;
};

// ─── An element ─────────────────────────────────────────────────

class NodeElement final : public RefCounted,
                          public IRawElementProviderSimple,
                          public IRawElementProviderFragment,
                          public IInvokeProvider,
                          public IToggleProvider,
                          public IRangeValueProvider,
                          public IValueProvider,
                          public IScrollProvider,
                          public ITextProvider {
public:
    NodeElement(std::shared_ptr<Core> core, uint32_t id) : m_core(std::move(core)), m_id(id) {}

    SHOONYAKASHA_REFCOUNT

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override {
        if (!out) return E_INVALIDARG;
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IRawElementProviderSimple)) {
            *out = static_cast<IRawElementProviderSimple*>(this);
        } else if (riid == __uuidof(IRawElementProviderFragment)) {
            *out = static_cast<IRawElementProviderFragment*>(this);
        } else if (riid == __uuidof(IInvokeProvider)) {
            *out = static_cast<IInvokeProvider*>(this);
        } else if (riid == __uuidof(IToggleProvider)) {
            *out = static_cast<IToggleProvider*>(this);
        } else if (riid == __uuidof(IRangeValueProvider)) {
            *out = static_cast<IRangeValueProvider*>(this);
        } else if (riid == __uuidof(IValueProvider)) {
            *out = static_cast<IValueProvider*>(this);
        } else if (riid == __uuidof(IScrollProvider)) {
            *out = static_cast<IScrollProvider*>(this);
        } else if (riid == __uuidof(ITextProvider)) {
            *out = static_cast<ITextProvider*>(this);
        } else {
            *out = nullptr;
            return E_NOINTERFACE;
        }
        AddRef();
        return S_OK;
    }

    uint32_t id() const { return m_id; }

    // IRawElementProviderSimple
    HRESULT STDMETHODCALLTYPE get_ProviderOptions(ProviderOptions* out) override {
        if (!out) return E_INVALIDARG;
        *out = static_cast<ProviderOptions>(ProviderOptions_ServerSideProvider | ProviderOptions_UseComThreading);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetPatternProvider(PATTERNID pattern, IUnknown** out) override {
        if (!out) return E_INVALIDARG;
        *out = nullptr;
        IUnknown* provider = nullptr;
        if (!m_core->withNode(m_id, [&](const AccessibilitySnapshot&, const AccessibleNode& node, int) {
                switch (node.role) {
                case AccessibleRole::Button:
                    if (pattern == UIA_InvokePatternId) provider = static_cast<IInvokeProvider*>(this);
                    break;
                case AccessibleRole::CheckBox:
                    if (pattern == UIA_TogglePatternId) provider = static_cast<IToggleProvider*>(this);
                    break;
                case AccessibleRole::Slider:
                    if (pattern == UIA_RangeValuePatternId) provider = static_cast<IRangeValueProvider*>(this);
                    break;
                case AccessibleRole::Edit:
                    if (pattern == UIA_ValuePatternId) provider = static_cast<IValueProvider*>(this);
                    if (pattern == UIA_TextPatternId) provider = static_cast<ITextProvider*>(this);
                    break;
                case AccessibleRole::Pane:
                    if (pattern == UIA_ScrollPatternId && (node.scrollsHorizontally || node.scrollsVertically)) {
                        provider = static_cast<IScrollProvider*>(this);
                    }
                    break;
                default:
                    break;
                }
            })) {
            return UIA_E_ELEMENTNOTAVAILABLE;
        }
        if (provider) {
            provider->AddRef();
            *out = provider;
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetPropertyValue(PROPERTYID property, VARIANT* out) override {
        if (!out) return E_INVALIDARG;
        VariantInit(out);
        const bool found = m_core->withNode(m_id, [&](const AccessibilitySnapshot&, const AccessibleNode& node, int) {
            switch (property) {
            case UIA_ControlTypePropertyId:         setInt(out, controlType(node.role)); break;
            case UIA_NamePropertyId:                setString(out, node.name); break;
            case UIA_HelpTextPropertyId:            setString(out, node.description); break;
            case UIA_AutomationIdPropertyId:        setString(out, std::to_string(node.id)); break;
            case UIA_FrameworkIdPropertyId:         setString(out, "Shoonyakasha"); break;
            case UIA_IsEnabledPropertyId:           setBool(out, node.enabled); break;
            case UIA_IsKeyboardFocusablePropertyId: setBool(out, node.focusable); break;
            case UIA_HasKeyboardFocusPropertyId:    setBool(out, node.focused); break;
            case UIA_IsOffscreenPropertyId:         setBool(out, node.offscreen); break;
            case UIA_IsPasswordPropertyId:          setBool(out, node.password); break;
            case UIA_IsControlElementPropertyId:
            case UIA_IsContentElementPropertyId:    setBool(out, true); break;
            default: break;
            }
        });
        return found ? S_OK : UIA_E_ELEMENTNOTAVAILABLE;
    }

    HRESULT STDMETHODCALLTYPE get_HostRawElementProvider(IRawElementProviderSimple** out) override {
        if (!out) return E_INVALIDARG;
        *out = nullptr;
        return S_OK;
    }

    // IRawElementProviderFragment
    HRESULT STDMETHODCALLTYPE Navigate(NavigateDirection direction, IRawElementProviderFragment** out) override {
        if (!out) return E_INVALIDARG;
        *out = nullptr;
        // The element to go to, if any, or the root.
        std::optional<uint32_t> target;
        bool toRoot = false;
        const bool found = m_core->withNode(m_id, [&](const AccessibilitySnapshot& snapshot, const AccessibleNode& node,
                                                      int index) {
            auto idAt = [&](int i) { return snapshot.nodes[static_cast<size_t>(i)].id; };
            std::vector<int> siblings;
            if (node.parent >= 0) {
                siblings = snapshot.nodes[static_cast<size_t>(node.parent)].children;
            } else {
                for (size_t i = 0; i < snapshot.nodes.size(); ++i) {
                    if (snapshot.nodes[i].parent < 0) siblings.push_back(static_cast<int>(i));
                }
            }
            const auto at = std::find(siblings.begin(), siblings.end(), index);
            switch (direction) {
            case NavigateDirection_Parent:
                if (node.parent >= 0) target = idAt(node.parent);
                else toRoot = true;
                break;
            case NavigateDirection_NextSibling:
                if (at != siblings.end() && at + 1 != siblings.end()) target = idAt(*(at + 1));
                break;
            case NavigateDirection_PreviousSibling:
                if (at != siblings.end() && at != siblings.begin()) target = idAt(*(at - 1));
                break;
            case NavigateDirection_FirstChild:
                if (!node.children.empty()) target = idAt(node.children.front());
                break;
            case NavigateDirection_LastChild:
                if (!node.children.empty()) target = idAt(node.children.back());
                break;
            }
        });
        if (!found) return UIA_E_ELEMENTNOTAVAILABLE;
        if (toRoot) {
            std::lock_guard lock(m_core->mutex);
            return give<IRawElementProviderFragment>(m_core->root, out);
        }
        if (target) return give<IRawElementProviderFragment>(m_core->element(m_core, *target), out);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetRuntimeId(SAFEARRAY** out) override {
        if (!out) return E_INVALIDARG;
        const int ids[] = {UiaAppendRuntimeId, static_cast<int>(m_id)};
        *out = SafeArrayCreateVector(VT_I4, 0, 2);
        if (!*out) return E_OUTOFMEMORY;
        for (LONG i = 0; i < 2; ++i) SafeArrayPutElement(*out, &i, const_cast<int*>(&ids[i]));
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE get_BoundingRectangle(UiaRect* out) override {
        if (!out) return E_INVALIDARG;
        *out = UiaRect{};
        const bool found = m_core->withNode(m_id, [&](const AccessibilitySnapshot&, const AccessibleNode& node, int) {
            if (node.offscreen) return;
            POINT topLeft{static_cast<LONG>(node.bounds.min.x), static_cast<LONG>(node.bounds.min.y)};
            ClientToScreen(m_core->hwnd, &topLeft);
            const glm::vec2 size = node.bounds.size();
            *out = UiaRect{static_cast<double>(topLeft.x), static_cast<double>(topLeft.y), size.x, size.y};
        });
        return found ? S_OK : UIA_E_ELEMENTNOTAVAILABLE;
    }

    HRESULT STDMETHODCALLTYPE GetEmbeddedFragmentRoots(SAFEARRAY** out) override {
        if (!out) return E_INVALIDARG;
        *out = nullptr;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE SetFocus() override { return act(AccessibleAction::Kind::Focus); }

    HRESULT STDMETHODCALLTYPE get_FragmentRoot(IRawElementProviderFragmentRoot** out) override {
        std::lock_guard lock(m_core->mutex);
        return give<IRawElementProviderFragmentRoot>(m_core->root, out);
    }

    // IInvokeProvider
    HRESULT STDMETHODCALLTYPE Invoke() override { return act(AccessibleAction::Kind::Invoke); }

    // IToggleProvider
    HRESULT STDMETHODCALLTYPE Toggle() override { return act(AccessibleAction::Kind::Toggle); }
    HRESULT STDMETHODCALLTYPE get_ToggleState(ToggleState* out) override {
        return field(out, [](const AccessibleNode& n) { return n.toggled ? ToggleState_On : ToggleState_Off; });
    }

    // IRangeValueProvider
    HRESULT STDMETHODCALLTYPE SetValue(double value) override {
        HRESULT result = S_OK;
        AccessibleAction action;
        action.kind = AccessibleAction::Kind::SetValue;
        action.node = m_id;
        action.value = static_cast<float>(value);
        if (!m_core->withNode(m_id, [&](const AccessibilitySnapshot&, const AccessibleNode& node, int) {
                if (!node.enabled) result = UIA_E_ELEMENTNOTENABLED;
                else if (value < node.minimum || value > node.maximum) result = E_INVALIDARG;
            })) {
            return UIA_E_ELEMENTNOTAVAILABLE;
        }
        if (FAILED(result)) return result;
        return m_core->queue(action) ? S_OK : UIA_E_ELEMENTNOTAVAILABLE;
    }
    HRESULT STDMETHODCALLTYPE get_Value(double* out) override {
        return field(out, [](const AccessibleNode& n) { return static_cast<double>(n.value); });
    }
    HRESULT STDMETHODCALLTYPE get_IsReadOnly(BOOL* out) override {
        return field(out, [](const AccessibleNode& n) {
            return static_cast<BOOL>(!n.enabled || (n.role == AccessibleRole::Edit && n.readOnly));
        });
    }
    HRESULT STDMETHODCALLTYPE get_Maximum(double* out) override {
        return field(out, [](const AccessibleNode& n) { return static_cast<double>(n.maximum); });
    }
    HRESULT STDMETHODCALLTYPE get_Minimum(double* out) override {
        return field(out, [](const AccessibleNode& n) { return static_cast<double>(n.minimum); });
    }
    HRESULT STDMETHODCALLTYPE get_LargeChange(double* out) override {
        return field(out, [](const AccessibleNode& n) {
            return static_cast<double>(std::max(n.smallChange, (n.maximum - n.minimum) / 4.0f));
        });
    }
    HRESULT STDMETHODCALLTYPE get_SmallChange(double* out) override {
        return field(out, [](const AccessibleNode& n) { return static_cast<double>(n.smallChange); });
    }

    // IValueProvider
    HRESULT STDMETHODCALLTYPE SetValue(LPCWSTR value) override {
        HRESULT result = S_OK;
        if (!m_core->withNode(m_id, [&](const AccessibilitySnapshot&, const AccessibleNode& node, int) {
                if (!node.enabled || node.readOnly) result = UIA_E_ELEMENTNOTENABLED;
            })) {
            return UIA_E_ELEMENTNOTAVAILABLE;
        }
        if (FAILED(result)) return result;
        AccessibleAction action;
        action.kind = AccessibleAction::Kind::SetText;
        action.node = m_id;
        action.text = narrow(value);
        return m_core->queue(std::move(action)) ? S_OK : UIA_E_ELEMENTNOTAVAILABLE;
    }
    HRESULT STDMETHODCALLTYPE get_Value(BSTR* out) override {
        if (!out) return E_INVALIDARG;
        std::string text;
        if (!m_core->withNode(m_id, [&](const AccessibilitySnapshot&, const AccessibleNode& n, int) { text = n.text; })) {
            return UIA_E_ELEMENTNOTAVAILABLE;
        }
        const std::wstring w = wide(text);
        *out = SysAllocStringLen(w.data(), static_cast<UINT>(w.size()));
        return S_OK;
    }

    // IScrollProvider
    HRESULT STDMETHODCALLTYPE Scroll(ScrollAmount horizontal, ScrollAmount vertical) override {
        AccessibleAction action;
        action.kind = AccessibleAction::Kind::Scroll;
        action.node = m_id;
        if (!m_core->withNode(m_id, [&](const AccessibilitySnapshot&, const AccessibleNode& n, int) {
                const ScrollAmount amounts[2] = {horizontal, vertical};
                const bool scrolls[2] = {n.scrollsHorizontally, n.scrollsVertically};
                for (int axis = 0; axis < 2; ++axis) {
                    float step = 0.0f;
                    switch (amounts[axis]) {
                    case ScrollAmount_LargeDecrement: step = -n.viewPercent[axis]; break;
                    case ScrollAmount_SmallDecrement: step = -10.0f; break;
                    case ScrollAmount_LargeIncrement: step = n.viewPercent[axis]; break;
                    case ScrollAmount_SmallIncrement: step = 10.0f; break;
                    default: break;
                    }
                    action.scrollPercent[axis] =
                        scrolls[axis] && step != 0.0f ? std::clamp(n.scrollPercent[axis] + step, 0.0f, 100.0f) : -1.0f;
                }
            })) {
            return UIA_E_ELEMENTNOTAVAILABLE;
        }
        return m_core->queue(action) ? S_OK : UIA_E_ELEMENTNOTAVAILABLE;
    }
    HRESULT STDMETHODCALLTYPE SetScrollPercent(double horizontal, double vertical) override {
        AccessibleAction action;
        action.kind = AccessibleAction::Kind::Scroll;
        action.node = m_id;
        action.scrollPercent = {horizontal == UIA_ScrollPatternNoScroll ? -1.0f : static_cast<float>(horizontal),
                                vertical == UIA_ScrollPatternNoScroll ? -1.0f : static_cast<float>(vertical)};
        return m_core->queue(action) ? S_OK : UIA_E_ELEMENTNOTAVAILABLE;
    }
    HRESULT STDMETHODCALLTYPE get_HorizontalScrollPercent(double* out) override {
        return field(out, [](const AccessibleNode& n) {
            return n.scrollsHorizontally ? static_cast<double>(n.scrollPercent.x) : UIA_ScrollPatternNoScroll;
        });
    }
    HRESULT STDMETHODCALLTYPE get_VerticalScrollPercent(double* out) override {
        return field(out, [](const AccessibleNode& n) {
            return n.scrollsVertically ? static_cast<double>(n.scrollPercent.y) : UIA_ScrollPatternNoScroll;
        });
    }
    HRESULT STDMETHODCALLTYPE get_HorizontalViewSize(double* out) override {
        return field(out, [](const AccessibleNode& n) { return static_cast<double>(n.viewPercent.x); });
    }
    HRESULT STDMETHODCALLTYPE get_VerticalViewSize(double* out) override {
        return field(out, [](const AccessibleNode& n) { return static_cast<double>(n.viewPercent.y); });
    }
    HRESULT STDMETHODCALLTYPE get_HorizontallyScrollable(BOOL* out) override {
        return field(out, [](const AccessibleNode& n) { return static_cast<BOOL>(n.scrollsHorizontally); });
    }
    HRESULT STDMETHODCALLTYPE get_VerticallyScrollable(BOOL* out) override {
        return field(out, [](const AccessibleNode& n) { return static_cast<BOOL>(n.scrollsVertically); });
    }

    // ITextProvider
    HRESULT STDMETHODCALLTYPE GetSelection(SAFEARRAY** out) override {
        if (!out) return E_INVALIDARG;
        size_t start = 0, end = 0;
        if (!m_core->withNode(m_id, [&](const AccessibilitySnapshot&, const AccessibleNode& n, int) {
                start = n.selectionStart;
                end = n.selectionEnd;
            })) {
            return UIA_E_ELEMENTNOTAVAILABLE;
        }
        return rangeArray(start, end, out);
    }
    HRESULT STDMETHODCALLTYPE GetVisibleRanges(SAFEARRAY** out) override {
        if (!out) return E_INVALIDARG;
        return rangeArray(0, ~size_t{0}, out);
    }
    HRESULT STDMETHODCALLTYPE RangeFromChild(IRawElementProviderSimple*, ITextRangeProvider** out) override {
        if (!out) return E_INVALIDARG;
        *out = nullptr;
        return E_INVALIDARG;   // the text has no child elements
    }
    HRESULT STDMETHODCALLTYPE RangeFromPoint(UiaPoint, ITextRangeProvider** out) override {
        if (!out) return E_INVALIDARG;
        *out = new TextRange(m_core, m_id, 0, 0);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_DocumentRange(ITextRangeProvider** out) override {
        if (!out) return E_INVALIDARG;
        *out = new TextRange(m_core, m_id, 0, ~size_t{0});
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_SupportedTextSelection(SupportedTextSelection* out) override {
        if (!out) return E_INVALIDARG;
        *out = SupportedTextSelection_Single;
        return S_OK;
    }

private:
    HRESULT act(AccessibleAction::Kind kind) {
        bool enabled = true;
        if (!m_core->withNode(m_id, [&](const AccessibilitySnapshot&, const AccessibleNode& n, int) {
                enabled = n.enabled;
            })) {
            return UIA_E_ELEMENTNOTAVAILABLE;
        }
        if (!enabled) return UIA_E_ELEMENTNOTENABLED;
        AccessibleAction action;
        action.kind = kind;
        action.node = m_id;
        return m_core->queue(action) ? S_OK : UIA_E_ELEMENTNOTAVAILABLE;
    }

    template <typename T, typename Get>
    HRESULT field(T* out, Get&& get) {
        if (!out) return E_INVALIDARG;
        return m_core->withNode(m_id, [&](const AccessibilitySnapshot&, const AccessibleNode& n, int) { *out = get(n); })
            ? S_OK
            : UIA_E_ELEMENTNOTAVAILABLE;
    }

    HRESULT rangeArray(size_t start, size_t end, SAFEARRAY** out) {
        *out = SafeArrayCreateVector(VT_UNKNOWN, 0, 1);
        if (!*out) return E_OUTOFMEMORY;
        auto* range = new TextRange(m_core, m_id, start, end);
        LONG index = 0;
        SafeArrayPutElement(*out, &index, static_cast<IUnknown*>(static_cast<ITextRangeProvider*>(range)));
        range->Release();   // the array holds its own reference
        return S_OK;
    }

    std::shared_ptr<Core> m_core;
    uint32_t m_id;
};

NodeElement* Core::element(const std::shared_ptr<Core>& self, uint32_t id) {
    std::lock_guard lock(mutex);
    if (!state) return nullptr;
    auto it = elements.find(id);
    if (it == elements.end()) it = elements.emplace(id, new NodeElement(self, id)).first;
    return it->second;
}

HRESULT STDMETHODCALLTYPE TextRange::GetEnclosingElement(IRawElementProviderSimple** out) {
    return give<IRawElementProviderSimple>(m_core->element(m_core, m_id), out);
}

HRESULT STDMETHODCALLTYPE RootElement::Navigate(NavigateDirection direction, IRawElementProviderFragment** out) {
    if (!out) return E_INVALIDARG;
    *out = nullptr;
    const auto core = m_core.lock();
    if (!core || (direction != NavigateDirection_FirstChild && direction != NavigateDirection_LastChild)) return S_OK;
    std::optional<uint32_t> target;
    core->withSnapshot([&](const AccessibilitySnapshot& snapshot) {
        for (const AccessibleNode& node : snapshot.nodes) {
            if (node.parent >= 0) continue;
            target = node.id;
            if (direction == NavigateDirection_FirstChild) break;
        }
    });
    return target ? give<IRawElementProviderFragment>(core->element(core, *target), out) : S_OK;
}

HRESULT STDMETHODCALLTYPE RootElement::ElementProviderFromPoint(double x, double y, IRawElementProviderFragment** out) {
    if (!out) return E_INVALIDARG;
    *out = nullptr;
    const auto core = m_core.lock();
    if (!core) return S_OK;
    POINT point{static_cast<LONG>(x), static_cast<LONG>(y)};
    ScreenToClient(core->hwnd, &point);
    const glm::vec2 p(static_cast<float>(point.x), static_cast<float>(point.y));
    // The last node in tree order holding the point: the deepest, the one drawn on top.
    std::optional<uint32_t> target;
    core->withSnapshot([&](const AccessibilitySnapshot& snapshot) {
        for (const AccessibleNode& node : snapshot.nodes) {
            if (!node.offscreen && node.parent >= 0 && node.bounds.contains(p)) target = node.id;
        }
    });
    return target ? give<IRawElementProviderFragment>(core->element(core, *target), out) : S_OK;
}

HRESULT STDMETHODCALLTYPE RootElement::GetFocus(IRawElementProviderFragment** out) {
    if (!out) return E_INVALIDARG;
    *out = nullptr;
    const auto core = m_core.lock();
    if (!core) return S_OK;
    std::optional<uint32_t> target;
    core->withSnapshot([&](const AccessibilitySnapshot& snapshot) {
        if (snapshot.focused >= 0) target = snapshot.nodes[static_cast<size_t>(snapshot.focused)].id;
    });
    return target ? give<IRawElementProviderFragment>(core->element(core, *target), out) : S_OK;
}

// ─── The provider ───────────────────────────────────────────────

/// What a node showed at the last snapshot, for the events of what changed.
struct Seen {
    std::string name;
    bool toggled = false;
    float value = 0.0f;
    std::string text;
    AccessibleRole role = AccessibleRole::Group;
};

class UIAutomationProvider final : public AccessibilityProvider {
public:
    UIAutomationProvider(HWND hwnd, UI::AccessibilityState& state) : m_core(std::make_shared<Core>()) {
        const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        m_uninitialize = SUCCEEDED(com);
        m_core->hwnd = hwnd;
        m_core->state = &state;
        m_core->root = new RootElement(m_core);
    }

    ~UIAutomationProvider() override {
        // Screen readers holding the window's elements are told they are gone.
        UiaReturnRawElementProvider(m_core->hwnd, 0, 0, nullptr);
        RootElement* root = nullptr;
        std::map<uint32_t, NodeElement*> elements;
        {
            std::lock_guard lock(m_core->mutex);
            m_core->state = nullptr;
            root = std::exchange(m_core->root, nullptr);
            elements.swap(m_core->elements);
        }
        for (auto& [id, element] : elements) {
            UiaDisconnectProvider(static_cast<IRawElementProviderSimple*>(element));
            element->Release();
        }
        if (root) {
            UiaDisconnectProvider(static_cast<IRawElementProviderSimple*>(root));
            root->Release();
        }
        if (m_uninitialize) CoUninitialize();
    }

    std::optional<intptr_t> handleMessage(uint32_t message, uintptr_t wParam, intptr_t lParam) override {
        if (message != WM_GETOBJECT || static_cast<long>(lParam) != static_cast<long>(UiaRootObjectId)) {
            return std::nullopt;
        }
        RootElement* root = nullptr;
        {
            std::lock_guard lock(m_core->mutex);
            if (!m_core->state) return std::nullopt;
            m_core->state->enabled = true;   // a screen reader is asking: describe the UI from now on
            root = m_core->root;
        }
        return static_cast<intptr_t>(UiaReturnRawElementProvider(m_core->hwnd, static_cast<WPARAM>(wParam),
                                                                 static_cast<LPARAM>(lParam), root));
    }

    void snapshotPublished() override {
        // What changed since the last snapshot, read under the lock and
        // raised after it, as raising an event may call the provider.
        struct Change {
            uint32_t id;
            PROPERTYID property;
            VARIANT before;
            VARIANT after;
        };
        std::vector<Change> changes;
        std::optional<uint32_t> focus;
        bool structure = false;
        std::map<uint32_t, Seen> seen;
        m_core->withSnapshot([&](const AccessibilitySnapshot& snapshot) {
            for (const AccessibleNode& node : snapshot.nodes) {
                Seen now{node.name, node.toggled, node.value, node.text, node.role};
                const auto before = m_seen.find(node.id);
                if (before == m_seen.end()) {
                    structure = true;
                } else if (before->second.role == node.role) {
                    const Seen& was = before->second;
                    auto change = [&](PROPERTYID property, auto set, auto oldValue, auto newValue) {
                        Change c{node.id, property, {}, {}};
                        VariantInit(&c.before);
                        VariantInit(&c.after);
                        set(&c.before, oldValue);
                        set(&c.after, newValue);
                        changes.push_back(c);
                    };
                    if (was.name != now.name) change(UIA_NamePropertyId, setString, was.name, now.name);
                    if (was.toggled != now.toggled) {
                        change(UIA_ToggleToggleStatePropertyId, setInt, was.toggled ? 1 : 0, now.toggled ? 1 : 0);
                    }
                    if (was.value != now.value) {
                        change(UIA_RangeValueValuePropertyId,
                               [](VARIANT* v, double d) { v->vt = VT_R8; v->dblVal = d; },
                               static_cast<double>(was.value), static_cast<double>(now.value));
                    }
                    if (was.text != now.text) change(UIA_ValueValuePropertyId, setString, was.text, now.text);
                }
                seen.emplace(node.id, std::move(now));
            }
            if (seen.size() != m_seen.size()) structure = true;
            if (snapshot.focused >= 0) focus = snapshot.nodes[static_cast<size_t>(snapshot.focused)].id;
        });
        m_seen.swap(seen);
        const bool focusMoved = focus != m_focus;
        m_focus = focus;

        // Elements of nodes that are gone are let go of; screen readers
        // still holding them find them gone.
        {
            std::lock_guard lock(m_core->mutex);
            for (auto it = m_core->elements.begin(); it != m_core->elements.end();) {
                if (m_seen.count(it->first)) {
                    ++it;
                    continue;
                }
                it->second->Release();
                it = m_core->elements.erase(it);
            }
        }

        if (UiaClientsAreListening()) {
            RootElement* root = nullptr;
            {
                std::lock_guard lock(m_core->mutex);
                root = m_core->root;
                if (root) root->AddRef();
            }
            if (structure && root) {
                UiaRaiseStructureChangedEvent(static_cast<IRawElementProviderSimple*>(root),
                                              StructureChangeType_ChildrenInvalidated, nullptr, 0);
            }
            if (root) root->Release();
            for (Change& c : changes) {
                if (NodeElement* element = m_core->element(m_core, c.id)) {
                    UiaRaiseAutomationPropertyChangedEvent(static_cast<IRawElementProviderSimple*>(element), c.property,
                                                           c.before, c.after);
                    if (c.property == UIA_ValueValuePropertyId) {
                        UiaRaiseAutomationEvent(static_cast<IRawElementProviderSimple*>(element),
                                                UIA_Text_TextChangedEventId);
                    }
                }
            }
            if (focusMoved && focus) {
                if (NodeElement* element = m_core->element(m_core, *focus)) {
                    UiaRaiseAutomationEvent(static_cast<IRawElementProviderSimple*>(element),
                                            UIA_AutomationFocusChangedEventId);
                }
            }
        }
        for (Change& c : changes) {
            VariantClear(&c.before);
            VariantClear(&c.after);
        }
    }

private:
    std::shared_ptr<Core> m_core;
    bool m_uninitialize = false;
    std::map<uint32_t, Seen> m_seen;
    std::optional<uint32_t> m_focus;
};

} // namespace

std::unique_ptr<AccessibilityProvider> AccessibilityProvider::create(void* nativeWindow, UI::AccessibilityState& state) {
    if (!nativeWindow) return nullptr;
    return std::make_unique<UIAutomationProvider>(static_cast<HWND>(nativeWindow), state);
}

} // namespace Platform
} // namespace Shoonyakasha

#endif
