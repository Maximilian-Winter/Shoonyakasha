//
// UIBatchBuilder.cpp - Turning laid-out canvases into vertices, indices and batches
//

#include "UI/UIBatchBuilder.h"
#include "UI/CanvasLayout.h"

#include <glm/gtc/packing.hpp>

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <utility>

namespace Shoonyakasha {
namespace UI {

uint32_t packColor(const glm::vec4& color) {
    return glm::packUnorm4x8(glm::clamp(color, glm::vec4(0.0f), glm::vec4(1.0f)));
}

namespace {

glm::ivec4 toScissor(const Rect& clip, float scale, const glm::vec2& targetSize) {
    const glm::vec2 min = glm::clamp(glm::round(clip.min * scale), glm::vec2(0.0f), targetSize);
    const glm::vec2 max = glm::clamp(glm::round(clip.max * scale), min, targetSize);
    return glm::ivec4(glm::ivec2(min), glm::ivec2(max - min));
}

bool hasView(const GPUTexture& texture) {
    return texture.view != VK_NULL_HANDLE;
}

struct CanvasBuilder {
    const FontLibrary& fonts;
    GlyphCache& glyphs;
    UIDrawData& out;
    uint32_t firstBatch = 0;
    float scale = 1.0f;
    glm::ivec4 scissor{0};
    bool atlasFull = false;

    /// A quad from `min` to `max`, in canvas units; with a `slant`, its top
    /// moved right and its bottom left by that much per unit of height
    /// above and below `baseline`. A `turned` quad shows its texture turned
    /// 90 degrees clockwise.
    void quad(glm::vec2 min, glm::vec2 max, glm::vec2 uvMin, glm::vec2 uvMax,
              uint32_t color, UIDrawMode mode, const GPUTexture* image = nullptr, uint32_t page = 0,
              uint32_t weight = 0, float slant = 0.0f, float baseline = 0.0f, bool turned = false) {
        const float topShift = slant * (baseline - min.y) * scale;
        const float bottomShift = slant * (baseline - max.y) * scale;
        min *= scale;
        max *= scale;
        if (max.x <= min.x || max.y <= min.y) return;

        const VkImageView view = image ? image->view : VK_NULL_HANDLE;
        UIBatch* batch = out.batches.size() > firstBatch ? &out.batches.back() : nullptr;
        const bool joins = batch && batch->scissor == scissor &&
            (view == VK_NULL_HANDLE || batch->image == VK_NULL_HANDLE || batch->image == view);
        if (!joins) {
            UIBatch fresh;
            fresh.scissor = scissor;
            fresh.firstIndex = static_cast<uint32_t>(out.indices.size());
            out.batches.push_back(fresh);
            batch = &out.batches.back();
        }
        if (view != VK_NULL_HANDLE && batch->image == VK_NULL_HANDLE) {
            batch->image = view;
            batch->sampler = image->sampler;
        }

        const auto base = static_cast<uint32_t>(out.vertices.size());
        const uint32_t m = static_cast<uint32_t>(mode) | ((page & 0xFFu) << 8) | ((weight & 0xFFu) << 16);
        // Texture corners for the quad's top-left, top-right, bottom-right
        // and bottom-left; turned, the texture's bottom-left is at the top-left.
        glm::vec2 uv[4] = {uvMin, {uvMax.x, uvMin.y}, uvMax, {uvMin.x, uvMax.y}};
        if (turned) {
            uv[0] = {uvMin.x, uvMax.y};
            uv[1] = uvMin;
            uv[2] = {uvMax.x, uvMin.y};
            uv[3] = uvMax;
        }
        out.vertices.push_back({{min.x + topShift, min.y}, uv[0], color, m});
        out.vertices.push_back({{max.x + topShift, min.y}, uv[1], color, m});
        out.vertices.push_back({{max.x + bottomShift, max.y}, uv[2], color, m});
        out.vertices.push_back({{min.x + bottomShift, max.y}, uv[3], color, m});
        for (const uint32_t i : {0u, 1u, 2u, 0u, 2u, 3u}) out.indices.push_back(base + i);
        batch->indexCount += 6;
    }

    void image(const Rect& rect, const UIImage& img, const glm::vec4& tint, bool mirrored = false) {
        const uint32_t color = packColor(img.color * tint);
        if (!hasView(img.texture)) {
            quad(rect.min, rect.max, {0, 0}, {1, 1}, color, UIDrawMode::Solid);
            return;
        }
        glm::vec2 uvMin(img.uvRect.x, img.uvRect.y);
        glm::vec2 uvMax = uvMin + glm::vec2(img.uvRect.z, img.uvRect.w);
        if (mirrored) std::swap(uvMin.x, uvMax.x);
        quad(rect.min, rect.max, uvMin, uvMax, color, UIDrawMode::Image, &img.texture);
    }

    void panel(const Rect& rect, const UIPanel& p, const glm::vec4& tint) {
        const uint32_t color = packColor(p.color * tint);
        const glm::vec2 textureSize(static_cast<float>(p.texture.width), static_cast<float>(p.texture.height));
        const bool textured = hasView(p.texture) && textureSize.x > 0.0f && textureSize.y > 0.0f;

        if (!textured) {
            if (p.fillCenter) quad(rect.min, rect.max, {0, 0}, {1, 1}, color, UIDrawMode::Solid);
            return;
        }

        // Border widths in canvas units, shrunk together when they would
        // overlap.
        glm::vec2 lead(p.border.x, p.border.y);   // left, top
        glm::vec2 trail(p.border.z, p.border.w);  // right, bottom
        glm::vec2 startBorder = lead * p.borderScale;
        glm::vec2 endBorder = trail * p.borderScale;
        const glm::vec2 size = rect.size();
        for (int axis = 0; axis < 2; ++axis) {
            const float total = startBorder[axis] + endBorder[axis];
            if (total > size[axis] && total > 0.0f) {
                const float k = std::max(size[axis], 0.0f) / total;
                startBorder[axis] *= k;
                endBorder[axis] *= k;
            }
        }

        const float xs[4] = {rect.min.x, rect.min.x + startBorder.x, rect.max.x - endBorder.x, rect.max.x};
        const float ys[4] = {rect.min.y, rect.min.y + startBorder.y, rect.max.y - endBorder.y, rect.max.y};
        const float us[4] = {0.0f, lead.x / textureSize.x, 1.0f - trail.x / textureSize.x, 1.0f};
        const float vs[4] = {0.0f, lead.y / textureSize.y, 1.0f - trail.y / textureSize.y, 1.0f};

        for (int row = 0; row < 3; ++row) {
            for (int column = 0; column < 3; ++column) {
                if (row == 1 && column == 1 && !p.fillCenter) continue;
                quad({xs[column], ys[row]}, {xs[column + 1], ys[row + 1]},
                     {us[column], vs[row]}, {us[column + 1], vs[row + 1]},
                     color, UIDrawMode::Image, &p.texture);
            }
        }
    }

    void text(const Rect& rect, const UIText& t, const UITextCache& cache, const glm::vec4& tint) {
        // Rich text takes its colour, weight and slant from the run each
        // glyph is in, and a link the pointer is over the link colour.
        auto colorOf = [&](const TextRun* run) {
            glm::vec4 c = run && run->style.hasColor ? run->style.color : t.color;
            if (run && run->style.link >= 0 && static_cast<size_t>(run->style.link) < cache.links.size() &&
                !t.hoveredLink.empty() && cache.links[static_cast<size_t>(run->style.link)] == t.hoveredLink) {
                c = t.linkColor;
            }
            return packColor(c * tint);
        };
        const uint32_t plainColor = colorOf(nullptr);

        for (const auto& glyph : cache.layout.glyphs) {
            if (glyph.font >= cache.fonts.size()) continue;
            const uint32_t fontId = cache.fonts[glyph.font];
            const SdfFont* font = fonts.get(fontId);
            if (!font) continue;
            const auto entry = glyphs.findOrAdd(fontId, glyph.glyph, *font);
            if (!entry) {
                atlasFull = true;
                continue;
            }
            const TextRun* run = cache.runs.empty() ? nullptr : runAt(cache.runs, glyph.codepoint);
            const float baseline = glyph.line < cache.layout.lines.size() ? cache.layout.lines[glyph.line].baseline : 0.0f;
            // Vertical text is not slanted.
            const bool slanted = run && run->fauxItalic && !cache.layout.vertical;
            quad(rect.min + glyph.min, rect.min + glyph.max, entry->uvMin, entry->uvMax,
                 run ? colorOf(run) : plainColor, UIDrawMode::Text, nullptr, entry->page,
                 run && run->fauxBold ? kFauxBoldWeight : 0u, slanted ? kFauxItalicSlant : 0.0f,
                 rect.min.y + baseline, glyph.sideways);
        }
        if (!cache.runs.empty()) decorations(rect, cache, colorOf);
    }

    /// Rich text's underlines and strikethroughs, from its first font's
    /// metrics at each run's size: one quad for each stretch of clusters
    /// side by side on a line with the same line and colour.
    template <typename ColorOf>
    void decorations(const Rect& rect, const UITextCache& cache, const ColorOf& colorOf) {
        const SdfFont* first = cache.fonts.empty() ? nullptr : fonts.get(cache.fonts[0]);
        if (!first) return;
        struct Pending {
            Rect rect;
            uint32_t color = 0;
            bool open = false;
        };
        Pending pending[2];   // underline, strikethrough
        auto flush = [&](Pending& p) {
            if (p.open) quad(p.rect.min, p.rect.max, {0, 0}, {1, 1}, p.color, UIDrawMode::Solid);
            p.open = false;
        };
        for (const TextCluster& cluster : cache.layout.clusters) {
            const TextRun* run = runAt(cache.runs, cluster.firstCodepoint);
            const float s = run ? run->fontSize / SdfFont::kBakeSize : 0.0f;
            const float baseline = rect.min.y + cache.layout.lines[cluster.line].baseline;
            const bool wanted[2] = {run && run->style.underline, run && run->style.strikethrough};
            const SdfFont::LineMetrics metrics[2] = {first->underline(), first->strikeout()};
            for (int d = 0; d < 2; ++d) {
                Pending& p = pending[d];
                if (!wanted[d]) {
                    flush(p);
                    continue;
                }
                const Rect r{{rect.min.x + cluster.x, baseline + metrics[d].offset * s},
                             {rect.min.x + cluster.x + cluster.width, baseline + (metrics[d].offset + metrics[d].thickness) * s}};
                const uint32_t color = colorOf(run);
                if (p.open && p.color == color && std::abs(p.rect.max.x - r.min.x) < 1e-3f && p.rect.min.y == r.min.y &&
                    p.rect.max.y == r.max.y) {
                    p.rect.max.x = r.max.x;
                    continue;
                }
                flush(p);
                p = {r, color, true};
            }
        }
        flush(pending[0]);
        flush(pending[1]);
    }
};

/// Returns false when the glyph atlas filled up during the build.
bool build(entt::registry& registry, const FontLibrary& fonts, GlyphCache& glyphs, UIDrawData& out) {
    out.clear();

    std::vector<std::pair<entt::entity, const UICanvas*>> canvases;
    for (auto [entity, canvas] : registry.view<UICanvas>().each()) {
        canvases.emplace_back(entity, &canvas);
    }
    std::stable_sort(canvases.begin(), canvases.end(), [](const auto& a, const auto& b) {
        const bool aOverlay = a.second->mode == UICanvas::Mode::ScreenOverlay;
        const bool bOverlay = b.second->mode == UICanvas::Mode::ScreenOverlay;
        if (aOverlay != bOverlay) return aOverlay;
        return aOverlay && a.second->sortOrder < b.second->sortOrder;
    });

    // The state colours each tint target takes from the interactables naming it.
    std::unordered_map<entt::entity, glm::vec4> targetTints;
    for (auto [entity, interactable] : registry.view<UIInteractable>().each()) {
        for (const entt::entity target : interactable.tintTargets) {
            auto [it, inserted] = targetTints.try_emplace(target, glm::vec4(1.0f));
            it->second *= interactable.stateColor();
        }
    }

    // Drawable elements per canvas, in draw order.
    std::unordered_map<entt::entity, std::vector<std::pair<uint32_t, entt::entity>>> elements;
    for (auto [entity, element] : registry.view<UIRect>().each()) {
        if (!element.visibleInHierarchy || element.canvas == entt::null) continue;
        if (intersect(element.rect, element.clip).empty()) continue;
        elements[element.canvas].emplace_back(element.drawOrder, entity);
    }

    bool atlasFull = false;
    for (const auto& [canvasEntity, canvas] : canvases) {
        CanvasDraw draw;
        draw.canvas = canvasEntity;
        draw.mode = canvas->mode;
        draw.sortOrder = canvas->sortOrder;
        draw.targetSize = canvas->targetSize;
        draw.clearColor = canvas->clearColor;
        draw.firstBatch = static_cast<uint32_t>(out.batches.size());

        CanvasBuilder builder{fonts, glyphs, out, draw.firstBatch, canvas->scale};

        auto it = elements.find(canvasEntity);
        if (it != elements.end()) {
            auto& list = it->second;
            std::sort(list.begin(), list.end());
            for (const auto& [order, entity] : list) {
                const auto& element = registry.get<UIRect>(entity);
                builder.scissor = toScissor(element.clip, canvas->scale, canvas->targetSize);

                // An interactable's own panel and image take its state colour;
                // a tint target's panel, image and text take the state colours
                // of the interactables naming it.
                const auto* interactable = registry.try_get<UIInteractable>(entity);
                const auto target = targetTints.find(entity);
                const glm::vec4 textTint = target != targetTints.end() ? target->second : glm::vec4(1.0f);
                const glm::vec4 tint = interactable ? interactable->stateColor() * textTint : textTint;
                if (const auto* p = registry.try_get<UIPanel>(entity)) builder.panel(element.rect, *p, tint);
                if (const auto* img = registry.try_get<UIImage>(entity)) {
                    if (const auto* rects = registry.try_get<UIImageRects>(entity)) {
                        for (const Rect& r : rects->rects) builder.image(r, *img, tint);
                    } else {
                        builder.image(element.rect, *img, tint, img->mirrorInRightToLeft && element.rightToLeft);
                    }
                }
                const auto* t = registry.try_get<UIText>(entity);
                const auto* cache = registry.try_get<UITextCache>(entity);
                if (t && cache) builder.text(element.rect, *t, *cache, textTint);
            }
        }

        atlasFull = atlasFull || builder.atlasFull;
        draw.batchCount = static_cast<uint32_t>(out.batches.size()) - draw.firstBatch;
        out.canvases.push_back(draw);
    }
    return !atlasFull;
}

} // namespace

void buildDrawData(entt::registry& registry, const FontLibrary& fonts, GlyphCache& glyphs, UIDrawData& out) {
    glyphs.beginFrame();
    if (build(registry, fonts, glyphs, out)) return;
    glyphs.reset();
    build(registry, fonts, glyphs, out);
}

} // namespace UI
} // namespace Shoonyakasha
