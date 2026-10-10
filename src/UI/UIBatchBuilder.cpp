//
// UIBatchBuilder.cpp - Turning laid-out canvases into vertices, indices and batches
//

#include "UI/UIBatchBuilder.h"
#include "UI/CanvasLayout.h"

#include <glm/gtc/packing.hpp>

#include <algorithm>
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

    void quad(glm::vec2 min, glm::vec2 max, glm::vec2 uvMin, glm::vec2 uvMax,
              uint32_t color, UIDrawMode mode, const GPUTexture* image = nullptr) {
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
        const auto m = static_cast<uint32_t>(mode);
        out.vertices.push_back({min, uvMin, color, m});
        out.vertices.push_back({{max.x, min.y}, {uvMax.x, uvMin.y}, color, m});
        out.vertices.push_back({max, uvMax, color, m});
        out.vertices.push_back({{min.x, max.y}, {uvMin.x, uvMax.y}, color, m});
        for (const uint32_t i : {0u, 1u, 2u, 0u, 2u, 3u}) out.indices.push_back(base + i);
        batch->indexCount += 6;
    }

    void image(const Rect& rect, const UIImage& img) {
        const uint32_t color = packColor(img.color);
        if (!hasView(img.texture)) {
            quad(rect.min, rect.max, {0, 0}, {1, 1}, color, UIDrawMode::Solid);
            return;
        }
        const glm::vec2 uvMin(img.uvRect.x, img.uvRect.y);
        const glm::vec2 uvMax = uvMin + glm::vec2(img.uvRect.z, img.uvRect.w);
        quad(rect.min, rect.max, uvMin, uvMax, color, UIDrawMode::Image, &img.texture);
    }

    void panel(const Rect& rect, const UIPanel& p) {
        const uint32_t color = packColor(p.color);
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

    void text(const Rect& rect, const UIText& t, const UITextCache& cache) {
        const SdfFont* font = fonts.get(cache.font);
        if (!font) return;
        const uint32_t color = packColor(t.color);

        for (const auto& glyph : cache.layout.glyphs) {
            const auto entry = glyphs.findOrAdd(cache.font, glyph.glyph, *font);
            if (!entry) {
                atlasFull = true;
                continue;
            }
            quad(rect.min + glyph.min, rect.min + glyph.max, entry->uvMin, entry->uvMax,
                 color, UIDrawMode::Text);
        }
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

                if (const auto* p = registry.try_get<UIPanel>(entity)) builder.panel(element.rect, *p);
                if (const auto* img = registry.try_get<UIImage>(entity)) builder.image(element.rect, *img);
                const auto* t = registry.try_get<UIText>(entity);
                const auto* cache = registry.try_get<UITextCache>(entity);
                if (t && cache) builder.text(element.rect, *t, *cache);
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
    if (build(registry, fonts, glyphs, out)) return;
    glyphs.reset();
    build(registry, fonts, glyphs, out);
}

} // namespace UI
} // namespace Shoonyakasha
