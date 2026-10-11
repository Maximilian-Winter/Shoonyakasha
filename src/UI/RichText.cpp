//
// RichText.cpp - Reading markup, and resolving it against the fonts
//

#include "UI/RichText.h"
#include "UI/FontLibrary.h"
#include "UI/Utf8.h"

#include <algorithm>
#include <cstdlib>
#include <optional>

namespace Shoonyakasha {
namespace UI {

namespace {

std::u32string_view kTags[] = {U"color", U"b", U"i", U"size", U"font", U"link", U"u", U"s"};

bool needsValue(std::u32string_view name) {
    return name == U"color" || name == U"size" || name == U"font" || name == U"link";
}

std::optional<int> hexDigit(char32_t c) {
    if (c >= U'0' && c <= U'9') return static_cast<int>(c - U'0');
    if (c >= U'a' && c <= U'f') return static_cast<int>(c - U'a' + 10);
    if (c >= U'A' && c <= U'F') return static_cast<int>(c - U'A' + 10);
    return std::nullopt;
}

/// #RRGGBB or #RRGGBBAA.
std::optional<glm::vec4> parseColor(std::u32string_view value) {
    if ((value.size() != 7 && value.size() != 9) || value[0] != U'#') return std::nullopt;
    glm::vec4 color(1.0f);
    for (size_t c = 0; c * 2 + 1 < value.size(); ++c) {
        const auto high = hexDigit(value[c * 2 + 1]);
        const auto low = hexDigit(value[c * 2 + 2]);
        if (!high || !low) return std::nullopt;
        color[static_cast<int>(c)] = static_cast<float>(*high * 16 + *low) / 255.0f;
    }
    return color;
}

/// A positive number.
std::optional<float> parseSize(std::u32string_view value) {
    const std::string text = encodeUtf8(std::u32string(value));
    char* end = nullptr;
    const float size = std::strtof(text.c_str(), &end);
    if (text.empty() || end != text.c_str() + text.size() || !(size > 0.0f)) return std::nullopt;
    return size;
}

struct OpenTag {
    std::u32string name;
    std::u32string value;
    int link = -1;
};

/// The style the open tags set, applied in the order they opened.
RichTextStyle styleOf(const std::vector<OpenTag>& open) {
    RichTextStyle style;
    for (const OpenTag& tag : open) {
        if (tag.name == U"color") {
            style.hasColor = true;
            style.color = *parseColor(tag.value);
        } else if (tag.name == U"b") {
            style.bold = true;
        } else if (tag.name == U"i") {
            style.italic = true;
        } else if (tag.name == U"u") {
            style.underline = true;
        } else if (tag.name == U"s") {
            style.strikethrough = true;
        } else if (tag.name == U"size") {
            style.size = *parseSize(tag.value);
        } else if (tag.name == U"font") {
            style.font = encodeUtf8(tag.value);
        } else if (tag.name == U"link") {
            style.link = tag.link;
        }
    }
    return style;
}

} // namespace

RichText parseRichText(std::u32string_view markup) {
    RichText out;
    std::vector<OpenTag> open;
    RichTextStyle style;

    auto emit = [&](char32_t c) {
        if (out.spans.empty() || !(out.spans.back().style == style)) {
            out.spans.push_back({out.text.size(), out.text.size(), style});
        }
        out.text.push_back(c);
        out.spans.back().end = out.text.size();
    };

    for (size_t i = 0; i < markup.size(); ++i) {
        const char32_t c = markup[i];
        if (c == U'&') {
            if (markup.substr(i, 4) == U"&lt;") { emit(U'<'); i += 3; continue; }
            if (markup.substr(i, 4) == U"&gt;") { emit(U'>'); i += 3; continue; }
            emit(c);
            continue;
        }
        if (c != U'<') {
            emit(c);
            continue;
        }

        // A tag runs to the next '>', with no '<' or line break in it.
        const size_t close = markup.find_first_of(U"<>\n", i + 1);
        if (close == std::u32string_view::npos || markup[close] != U'>') {
            emit(c);
            continue;
        }
        std::u32string_view body = markup.substr(i + 1, close - i - 1);
        const bool closing = !body.empty() && body[0] == U'/';
        if (closing) body.remove_prefix(1);
        const size_t equals = body.find(U'=');
        const std::u32string_view name = body.substr(0, equals);
        std::u32string_view value = equals == std::u32string_view::npos ? std::u32string_view{} : body.substr(equals + 1);
        if (value.size() >= 2 && value.front() == U'"' && value.back() == U'"') value = value.substr(1, value.size() - 2);

        const bool known = std::find(std::begin(kTags), std::end(kTags), name) != std::end(kTags);
        bool valid = known;
        if (valid && closing) {
            valid = equals == std::u32string_view::npos;
        } else if (valid) {
            if (!needsValue(name)) valid = equals == std::u32string_view::npos;
            else if (name == U"color") valid = parseColor(value).has_value();
            else if (name == U"size") valid = parseSize(value).has_value();
            else valid = !value.empty();
        }
        if (valid && closing) {
            const auto last = std::find_if(open.rbegin(), open.rend(), [&](const OpenTag& t) { return t.name == name; });
            valid = last != open.rend();
            if (valid) open.erase(std::next(last).base());
        } else if (valid) {
            OpenTag tag{std::u32string(name), std::u32string(value)};
            if (name == U"link") {
                tag.link = static_cast<int>(out.links.size());
                out.links.push_back(encodeUtf8(tag.value));
            }
            open.push_back(std::move(tag));
        }
        if (!valid) {
            emit(c);   // the '<' is text, and so is what follows it
            continue;
        }
        style = styleOf(open);
        i = close;
    }
    return out;
}

PreparedText prepareText(std::string_view text, bool rich, uint32_t font, float fontSize, const FontLibrary& fonts) {
    PreparedText out;
    if (!rich) {
        out.text = decodeUtf8(text);
        out.fonts = fonts.chain(font);
        return out;
    }

    RichText parsed = parseRichText(decodeUtf8(text));
    out.text = std::move(parsed.text);
    out.links = std::move(parsed.links);
    out.fonts = fonts.chain(font);
    if (out.fonts.empty()) return out;

    // Each span's font chain, added to the fonts the first time it is used.
    auto indicesOf = [&](const std::vector<uint32_t>& chain) {
        std::vector<uint32_t> indices;
        for (const uint32_t id : chain) {
            auto it = std::find(out.fonts.begin(), out.fonts.end(), id);
            if (it == out.fonts.end()) it = out.fonts.insert(out.fonts.end(), id);
            indices.push_back(static_cast<uint32_t>(it - out.fonts.begin()));
        }
        return indices;
    };
    const std::vector<uint32_t> baseChain = out.fonts;
    for (const RichTextSpan& span : parsed.spans) {
        const uint32_t named = span.style.font.empty() ? 0 : fonts.find(span.style.font);
        const FontLibrary::Styled styled = fonts.styled(named ? named : font, span.style.bold, span.style.italic);
        const std::vector<uint32_t> chain = fonts.chain(styled.font);

        TextRun run;
        run.begin = span.begin;
        run.end = span.end;
        run.style = span.style;
        run.fontSize = span.style.size > 0.0f ? span.style.size : fontSize;
        run.fauxBold = styled.fauxBold;
        run.fauxItalic = styled.fauxItalic;
        out.runs.push_back(run);
        out.spans.push_back({span.begin, span.end, indicesOf(chain.empty() ? baseChain : chain), run.fontSize});
    }
    return out;
}

const TextRun* runAt(const std::vector<TextRun>& runs, size_t index) {
    const auto it = std::upper_bound(runs.begin(), runs.end(), index,
                                     [](size_t i, const TextRun& run) { return i < run.end; });
    return it != runs.end() && index >= it->begin ? &*it : nullptr;
}

int linkAt(const TextLayoutResult& layout, const std::vector<TextRun>& runs, glm::vec2 point) {
    for (const TextCluster& cluster : layout.clusters) {
        const TextLine& line = layout.lines[cluster.line];
        if (point.y < line.top || point.y >= line.bottom) continue;
        if (point.x < cluster.x || point.x >= cluster.x + cluster.width) continue;
        const TextRun* run = runAt(runs, cluster.firstCodepoint);
        return run ? run->style.link : -1;
    }
    return -1;
}

} // namespace UI
} // namespace Shoonyakasha
