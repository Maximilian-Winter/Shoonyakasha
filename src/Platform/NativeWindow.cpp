//
// NativeWindow.cpp - Reading IMM32's composition, and the platforms without
// a native window implementation
//

#include "Platform/NativeWindow.h"

#include <algorithm>
#include <map>
#include <vector>

namespace Shoonyakasha {
namespace Platform {

TextComposition imeComposition(std::u16string_view text, std::span<const uint8_t> attributes,
                               std::span<const uint32_t> clauseOffsets, size_t cursor) {
    // Decode, keeping the code-point index each UTF-16 offset starts.
    TextComposition composition;
    std::vector<size_t> pointAt(text.size() + 1, 0);
    for (size_t i = 0; i < text.size();) {
        const size_t start = i;
        char32_t c = text[i++];
        if (c >= 0xD800 && c <= 0xDBFF && i < text.size() && text[i] >= 0xDC00 && text[i] <= 0xDFFF) {
            c = 0x10000 + ((c - 0xD800) << 10) + (text[i++] - 0xDC00);
        } else if (c >= 0xD800 && c <= 0xDFFF) {
            c = 0xFFFD;   // an unpaired surrogate
        }
        for (size_t j = start; j < i; ++j) pointAt[j] = composition.text.size();
        composition.text.push_back(c);
    }
    pointAt[text.size()] = composition.text.size();
    composition.caret = pointAt[std::min(cursor, text.size())];

    // Offsets that start at 0, rise and end at the text's length.
    bool valid = clauseOffsets.size() >= 2 && clauseOffsets.front() == 0 && clauseOffsets.back() == text.size();
    for (size_t i = 1; valid && i < clauseOffsets.size(); ++i) valid = clauseOffsets[i] > clauseOffsets[i - 1];
    if (!valid) return composition;

    for (size_t i = 0; i + 1 < clauseOffsets.size(); ++i) {
        const uint32_t begin = clauseOffsets[i];
        const auto attribute = begin < attributes.size() ? static_cast<ImeAttribute>(attributes[begin])
                                                         : ImeAttribute::Input;
        composition.clauses.push_back({pointAt[begin], pointAt[clauseOffsets[i + 1]],
                                       attribute == ImeAttribute::TargetConverted ||
                                           attribute == ImeAttribute::TargetNotConverted});
    }
    return composition;
}

#ifndef _WIN32

namespace {

/// Hooks kept but never called, and no IME.
class NullNativeWindow final : public NativeWindow {
public:
    void setTextInputActive(bool active) override { m_active = active; }
    bool isTextInputActive() const override { return m_active; }
    void setTextInputRect(glm::vec2, glm::vec2) override {}
    int addMessageHook(MessageHook hook) override {
        m_hooks.emplace(m_nextId, std::move(hook));
        return m_nextId++;
    }
    void removeMessageHook(int id) override { m_hooks.erase(id); }

private:
    bool m_active = false;
    std::map<int, MessageHook> m_hooks;
    int m_nextId = 1;
};

} // namespace

std::unique_ptr<NativeWindow> NativeWindow::create(GLFWwindow*, EventDispatcher*) {
    return std::make_unique<NullNativeWindow>();
}

#endif

} // namespace Platform
} // namespace Shoonyakasha
