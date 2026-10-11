//
// WordDictionary.cpp - Finding words in scripts written without spaces
//

#include "UI/WordDictionary.h"
#include "UI/Utf8.h"

#include "Core/AssetPaths.h"

#include <stb_image.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <limits>

namespace Shoonyakasha {
namespace UI {

std::optional<DictionaryScript> dictionaryScriptOf(char32_t c) {
    if (c >= 0x0E00 && c <= 0x0E7F) return DictionaryScript::Thai;
    if (c >= 0x0E80 && c <= 0x0EFF) return DictionaryScript::Lao;
    if ((c >= 0x1780 && c <= 0x17FF) || (c >= 0x19E0 && c <= 0x19FF)) return DictionaryScript::Khmer;
    if ((c >= 0x1000 && c <= 0x109F) || (c >= 0xA9E0 && c <= 0xA9FF) || (c >= 0xAA60 && c <= 0xAA7F)) {
        return DictionaryScript::Myanmar;
    }
    return std::nullopt;
}

bool WordDictionary::loadFromFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    const std::string compressed((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (compressed.empty() || compressed.size() > static_cast<size_t>(std::numeric_limits<int>::max())) return false;

    int length = 0;
    char* data = stbi_zlib_decode_malloc(compressed.data(), static_cast<int>(compressed.size()), &length);
    if (!data) return false;
    const std::string_view text(data, static_cast<size_t>(length));
    const size_t before = m_words.size();
    for (size_t begin = 0; begin < text.size();) {
        size_t end = text.find('\n', begin);
        if (end == std::string_view::npos) end = text.size();
        std::string_view line = text.substr(begin, end - begin);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (!line.empty()) add(decodeUtf8(std::string(line)));
        begin = end + 1;
    }
    std::free(data);
    return m_words.size() > before;
}

void WordDictionary::add(std::u32string_view word) {
    if (word.empty()) return;
    m_words.emplace(word);
    m_maxLength = std::max(m_maxLength, word.size());
}

std::vector<size_t> WordDictionary::split(std::u32string_view run, const std::vector<bool>* unitEnds) const {
    const size_t n = run.size();
    std::vector<size_t> starts;
    if (n < 2 || m_words.empty()) return starts;
    if (unitEnds && unitEnds->size() < n) unitEnds = nullptr;
    auto endsUnit = [&](size_t i) { return !unitEnds || (*unitEnds)[i] || i + 1 == n; };   // after run[i]

    // The best split of run[0, i): fewest characters outside known words,
    // then fewest pieces; each piece a known word or one unit outside them.
    struct Best {
        size_t unknown = std::numeric_limits<size_t>::max();
        size_t pieces = 0;
        size_t from = 0;      // where the last piece starts
        bool known = false;   // the last piece is a known word
    };
    std::vector<Best> best(n + 1);
    best[0].unknown = 0;
    auto relax = [&](size_t from, size_t to, size_t unknown, bool known) {
        const size_t pieces = best[from].pieces + 1;
        Best& b = best[to];
        if (unknown < b.unknown || (unknown == b.unknown && pieces < b.pieces)) b = {unknown, pieces, from, known};
    };
    for (size_t i = 0; i < n; ++i) {
        if (best[i].unknown == std::numeric_limits<size_t>::max()) continue;
        const size_t longest = std::min(m_maxLength, n - i);
        for (size_t length = 1; length <= longest; ++length) {
            if (endsUnit(i + length - 1) && contains(run.substr(i, length))) relax(i, i + length, best[i].unknown, true);
        }
        size_t j = i + 1;
        while (j < n && !endsUnit(j - 1)) ++j;
        relax(i, j, best[i].unknown + (j - i), false);
    }

    // Back from the end; neighbouring pieces outside known words stay together.
    std::vector<std::pair<size_t, bool>> pieces;   // start, known
    for (size_t i = n; i > 0; i = best[i].from) pieces.emplace_back(best[i].from, best[i].known);
    bool previousUnknown = false;
    for (auto it = pieces.rbegin(); it != pieces.rend(); ++it) {
        const bool unknown = !it->second;
        if (it->first > 0 && !(unknown && previousUnknown)) starts.push_back(it->first);
        previousUnknown = unknown;
    }
    return starts;
}

// ─── The shipped lists ──────────────────────────────────────────

namespace {

struct Slot {
    std::shared_ptr<const WordDictionary> dictionary;
    bool tried = false;   // the shipped list was looked for
};

std::array<Slot, 4>& slots() {
    static std::array<Slot, 4> s;
    return s;
}

const char* fileOf(DictionaryScript script) {
    switch (script) {
    case DictionaryScript::Thai:    return "text/thai.words.zlib";
    case DictionaryScript::Lao:     return "text/lao.words.zlib";
    case DictionaryScript::Khmer:   return "text/khmer.words.zlib";
    default:                        return "text/myanmar.words.zlib";
    }
}

} // namespace

const WordDictionary* wordDictionary(DictionaryScript script) {
    Slot& slot = slots()[static_cast<size_t>(script)];
    if (!slot.dictionary && !slot.tried) {
        slot.tried = true;
        auto dictionary = std::make_shared<WordDictionary>();
        if (dictionary->loadFromFile(AssetPaths::locate(fileOf(script)).string())) slot.dictionary = std::move(dictionary);
    }
    return slot.dictionary.get();
}

void setWordDictionary(DictionaryScript script, std::shared_ptr<const WordDictionary> dictionary) {
    Slot& slot = slots()[static_cast<size_t>(script)];
    slot.dictionary = std::move(dictionary);
    slot.tried = slot.dictionary != nullptr;
}

} // namespace UI
} // namespace Shoonyakasha
