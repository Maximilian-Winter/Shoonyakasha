//
// SdfFont.cpp - A TrueType font rasterised as signed distance fields
//

#include "UI/SdfFont.h"

#include <hb.h>
#include <hb-ot.h>
#include <stb_truetype.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <fstream>
#include <iterator>

namespace Shoonyakasha {
namespace UI {

struct SdfFont::Impl {
    std::vector<uint8_t> data;  // stbtt_fontinfo and the HarfBuzz blob point into this
    stbtt_fontinfo info{};
    hb_blob_t* blob = nullptr;
    hb_face_t* face = nullptr;
    hb_font_t* font = nullptr;

    Impl() = default;
    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;
    ~Impl() {
        hb_font_destroy(font);
        hb_face_destroy(face);
        hb_blob_destroy(blob);
    }
};

SdfFont::SdfFont() = default;
SdfFont::~SdfFont() = default;
SdfFont::SdfFont(SdfFont&&) noexcept = default;
SdfFont& SdfFont::operator=(SdfFont&&) noexcept = default;

bool SdfFont::loadFromFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    return loadFromMemory(std::move(data));
}

bool SdfFont::loadFromMemory(std::vector<uint8_t> data) {
    m_impl.reset();
    if (data.empty()) return false;

    auto impl = std::make_unique<Impl>();
    impl->data = std::move(data);

    const int offset = stbtt_GetFontOffsetForIndex(impl->data.data(), 0);
    if (offset < 0 || !stbtt_InitFont(&impl->info, impl->data.data(), offset)) return false;

    // The first face of the same bytes, at the default scale of one unit per
    // font unit.
    impl->blob = hb_blob_create(reinterpret_cast<const char*>(impl->data.data()),
                                static_cast<unsigned int>(impl->data.size()), HB_MEMORY_MODE_READONLY,
                                nullptr, nullptr);
    impl->face = hb_face_create(impl->blob, 0);
    impl->font = hb_font_create(impl->face);

    m_scale = stbtt_ScaleForPixelHeight(&impl->info, kBakeSize);
    int ascent = 0, descent = 0, lineGap = 0;
    stbtt_GetFontVMetrics(&impl->info, &ascent, &descent, &lineGap);
    m_ascent = static_cast<float>(ascent) * m_scale;
    m_descent = static_cast<float>(descent) * m_scale;
    m_lineGap = static_cast<float>(lineGap) * m_scale;
    m_heightInEms = static_cast<float>(ascent - descent) * stbtt_ScaleForMappingEmToPixels(&impl->info, 1.0f);

    // The tables give the top of each line above the baseline; laid out, y
    // runs down.
    auto metric = [&](hb_ot_metrics_tag_t tag, float fallback) {
        hb_position_t value = 0;
        return hb_ot_metrics_get_position(impl->font, tag, &value) ? static_cast<float>(value) * m_scale : fallback;
    };
    const float height = m_ascent - m_descent;
    m_underline.thickness = std::max(metric(HB_OT_METRICS_TAG_UNDERLINE_SIZE, height * 0.05f), 1.0f);
    m_underline.offset = -metric(HB_OT_METRICS_TAG_UNDERLINE_OFFSET, -height * 0.1f);
    m_strikeout.thickness = std::max(metric(HB_OT_METRICS_TAG_STRIKEOUT_SIZE, height * 0.05f), 1.0f);
    m_strikeout.offset = -metric(HB_OT_METRICS_TAG_STRIKEOUT_OFFSET, height * 0.25f);

    m_impl = std::move(impl);
    return true;
}

bool SdfFont::valid() const {
    return m_impl != nullptr;
}

void* SdfFont::shapingFont() const {
    return m_impl ? m_impl->font : nullptr;
}

int SdfFont::findGlyph(char32_t codepoint) const {
    if (!m_impl) return 0;
    return stbtt_FindGlyphIndex(&m_impl->info, static_cast<int>(codepoint));
}

int SdfFont::glyphForCodepoint(char32_t codepoint) const {
    for (const char32_t candidate : {codepoint, char32_t{0xFFFD}, char32_t{'?'}}) {
        if (const int glyph = findGlyph(candidate)) return glyph;
    }
    return 0;
}

float SdfFont::advance(int glyph) const {
    if (!m_impl) return 0.0f;
    int advanceWidth = 0, leftSideBearing = 0;
    stbtt_GetGlyphHMetrics(&m_impl->info, glyph, &advanceWidth, &leftSideBearing);
    return static_cast<float>(advanceWidth) * m_scale;
}

float SdfFont::kerning(int leftGlyph, int rightGlyph) const {
    if (!m_impl) return 0.0f;
    return static_cast<float>(stbtt_GetGlyphKernAdvance(&m_impl->info, leftGlyph, rightGlyph)) * m_scale;
}

SdfFont::GlyphBox SdfFont::glyphBox(int glyph) const {
    GlyphBox box;
    if (!m_impl) return box;

    // The same box stbtt_GetGlyphSDF computes, so it matches rasterize().
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    stbtt_GetGlyphBitmapBoxSubpixel(&m_impl->info, glyph, m_scale, m_scale, 0.0f, 0.0f, &x0, &y0, &x1, &y1);
    if (x0 == x1 || y0 == y1) return box;

    box.offset = glm::vec2(static_cast<float>(x0 - kPadding), static_cast<float>(y0 - kPadding));
    box.size = glm::ivec2(x1 - x0 + 2 * kPadding, y1 - y0 + 2 * kPadding);
    return box;
}

namespace {

struct Segment {
    glm::vec2 a;
    glm::vec2 b;
};

/// The glyph's outline as closed line segments in bitmap pixels, y down:
/// quadratic and cubic curves split into pieces no longer than about a
/// pixel and a half.
std::vector<Segment> outlineSegments(const stbtt_vertex* vertices, int count, float scale) {
    std::vector<Segment> segments;
    auto point = [&](float x, float y) { return glm::vec2(x * scale, -y * scale); };
    glm::vec2 start(0.0f), pen(0.0f);
    bool open = false;
    auto close = [&] {
        if (open && pen != start) segments.push_back({pen, start});
        open = false;
    };
    auto pieces = [](float length) { return std::clamp(static_cast<int>(std::ceil(length / 1.5f)), 1, 64); };
    for (int i = 0; i < count; ++i) {
        const stbtt_vertex& v = vertices[i];
        const glm::vec2 to = point(v.x, v.y);
        switch (v.type) {
        case STBTT_vmove:
            close();
            start = pen = to;
            open = true;
            break;
        case STBTT_vline:
            segments.push_back({pen, to});
            pen = to;
            break;
        case STBTT_vcurve: {
            const glm::vec2 c = point(v.cx, v.cy);
            const int n = pieces(glm::length(c - pen) + glm::length(to - c));
            glm::vec2 previous = pen;
            for (int k = 1; k <= n; ++k) {
                const float t = static_cast<float>(k) / static_cast<float>(n);
                const float u = 1.0f - t;
                const glm::vec2 p = u * u * pen + 2.0f * u * t * c + t * t * to;
                segments.push_back({previous, p});
                previous = p;
            }
            pen = to;
            break;
        }
        case STBTT_vcubic: {
            const glm::vec2 c1 = point(v.cx, v.cy);
            const glm::vec2 c2 = point(v.cx1, v.cy1);
            const int n = pieces(glm::length(c1 - pen) + glm::length(c2 - c1) + glm::length(to - c2));
            glm::vec2 previous = pen;
            for (int k = 1; k <= n; ++k) {
                const float t = static_cast<float>(k) / static_cast<float>(n);
                const float u = 1.0f - t;
                const glm::vec2 p = u * u * u * pen + 3.0f * u * u * t * c1 + 3.0f * u * t * t * c2 + t * t * t * to;
                segments.push_back({previous, p});
                previous = p;
            }
            pen = to;
            break;
        }
        default:
            break;
        }
    }
    close();
    return segments;
}

/// The distance field of `segments` over the pixels [x0, x0 + w) by
/// [y0, y0 + h), sampled at pixel centres, as stbtt_GetGlyphSDF makes it:
/// `onEdge` on the outline, rising inside (non-zero winding) and falling
/// outside by `distScale` per pixel.
std::vector<uint8_t> distanceField(const std::vector<Segment>& segments, int x0, int y0, int w, int h,
                                   uint8_t onEdge, float distScale) {
    std::vector<uint8_t> pixels(static_cast<size_t>(w) * static_cast<size_t>(h));
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const glm::vec2 p(static_cast<float>(x0 + x) + 0.5f, static_cast<float>(y0 + y) + 0.5f);
            float nearest = std::numeric_limits<float>::max();
            int winding = 0;
            for (const Segment& s : segments) {
                const glm::vec2 d = s.b - s.a;
                const float lengthSquared = glm::dot(d, d);
                const float t = lengthSquared > 0.0f ? std::clamp(glm::dot(p - s.a, d) / lengthSquared, 0.0f, 1.0f) : 0.0f;
                const glm::vec2 offset = p - (s.a + t * d);
                nearest = std::min(nearest, glm::dot(offset, offset));
                // Crossings of a ray from p towards +x.
                if ((s.a.y <= p.y) != (s.b.y <= p.y)) {
                    const float crossX = s.a.x + (p.y - s.a.y) / (s.b.y - s.a.y) * d.x;
                    if (crossX > p.x) winding += s.b.y > s.a.y ? 1 : -1;
                }
            }
            const float distance = std::sqrt(nearest) * (winding != 0 ? 1.0f : -1.0f);
            const float value = static_cast<float>(onEdge) + distScale * distance;
            pixels[static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)] =
                static_cast<uint8_t>(std::clamp(value, 0.0f, 255.0f));
        }
    }
    return pixels;
}

} // namespace

SdfFont::Bitmap SdfFont::rasterize(int glyph) const {
    Bitmap bitmap;
    if (!m_impl) return bitmap;

    // stbtt_GetGlyphSDF leaves out cubic curves, which fonts with CFF
    // outlines are drawn with; those glyphs take the field from their
    // outline flattened into lines.
    stbtt_vertex* vertices = nullptr;
    const int count = stbtt_GetGlyphShape(&m_impl->info, glyph, &vertices);
    bool cubic = false;
    for (int i = 0; i < count && !cubic; ++i) cubic = vertices[i].type == STBTT_vcubic;
    if (cubic) {
        const GlyphBox box = glyphBox(glyph);
        if (!box.empty()) {
            bitmap.size = box.size;
            bitmap.pixels = distanceField(outlineSegments(vertices, count, m_scale), static_cast<int>(box.offset.x),
                                          static_cast<int>(box.offset.y), box.size.x, box.size.y, kOnEdge,
                                          kPixelDistScale);
        }
        stbtt_FreeShape(&m_impl->info, vertices);
        return bitmap;
    }
    stbtt_FreeShape(&m_impl->info, vertices);

    int width = 0, height = 0, xoff = 0, yoff = 0;
    unsigned char* pixels = stbtt_GetGlyphSDF(&m_impl->info, m_scale, glyph, kPadding, kOnEdge,
                                              kPixelDistScale, &width, &height, &xoff, &yoff);
    if (!pixels) return bitmap;

    bitmap.size = glm::ivec2(width, height);
    bitmap.pixels.assign(pixels, pixels + static_cast<size_t>(width) * static_cast<size_t>(height));
    stbtt_FreeSDF(pixels, nullptr);
    return bitmap;
}

} // namespace UI
} // namespace Shoonyakasha
