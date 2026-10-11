//
// WordDictionary.h - Finding words in scripts written without spaces
//
// Thai, Lao, Khmer and Myanmar put no spaces between words, so where a line
// may break, or Ctrl+arrow stop, depends on knowing the words. A run of one
// of these scripts is split by maximal matching against a word list: the
// split with the fewest characters outside known words, and of those the
// fewest words. Characters outside known words are kept together.
//
// The lists ship in assets/text/ (see tools/build_word_lists.py) and are
// loaded on first use; without one, a script's runs are not split.
//

#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace Shoonyakasha {
namespace UI {

enum class DictionaryScript { Thai, Lao, Khmer, Myanmar };

/// The script `c` belongs to, if it is one split by a dictionary.
std::optional<DictionaryScript> dictionaryScriptOf(char32_t c);

class WordDictionary {
public:
    /// Reads a zlib-compressed UTF-8 list, one word per line. False when the
    /// file cannot be read or holds no words.
    bool loadFromFile(const std::string& path);

    void add(std::u32string_view word);

    bool contains(std::u32string_view word) const { return m_words.find(word) != m_words.end(); }
    size_t size() const { return m_words.size(); }
    size_t maxLength() const { return m_maxLength; }

    /// Where words start inside `run`, a run of one script: offsets from 1
    /// to run.size() - 1, ascending. `unitEnds[i]`, when given, is true where
    /// a character outside known words may end after run[i] (a grapheme
    /// cluster boundary); without it any character may.
    std::vector<size_t> split(std::u32string_view run, const std::vector<bool>* unitEnds = nullptr) const;

private:
    struct Hash {
        using is_transparent = void;
        size_t operator()(std::u32string_view s) const { return std::hash<std::u32string_view>{}(s); }
    };
    std::unordered_set<std::u32string, Hash, std::equal_to<>> m_words;
    size_t m_maxLength = 0;
};

/// The word list of `script`, loaded from the asset folder on first use, or
/// the one set with setWordDictionary. Null when there is none.
const WordDictionary* wordDictionary(DictionaryScript script);

/// Replaces the word list of `script`; null goes back to the shipped one.
void setWordDictionary(DictionaryScript script, std::shared_ptr<const WordDictionary> dictionary);

} // namespace UI
} // namespace Shoonyakasha
