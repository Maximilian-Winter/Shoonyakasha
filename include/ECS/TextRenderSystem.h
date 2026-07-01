//
// TextRenderSystem.h - Bakes Text2DComponent labels into glyph sprite entities
//
// Rather than a custom batched glyph renderer, each visible character of a
// label becomes its own screen-space sprite entity (Sprite2DComponent +
// UIAnchorComponent anchored the same as the label, offset to the glyph's
// position), reusing the exact same sprite_geometry render path as
// textured sprites and UI panels. Re-baked only when a label's
// text/font/size/color/alignment actually changes.
//

#pragma once

#include "ECS/Systems.h"
#include "ECS/RenderComponents.h"
#include "ECS/Sprite2DComponents.h"
#include "Resources/FontLoader.h"
#include "Resources/Sprite2DManager.h"

namespace Shoonyakasha {
namespace ECS {

class TextRenderSystem : public ISystem {
public:
    TextRenderSystem(Shoonyakasha::FontLoader& fontLoader, Shoonyakasha::Sprite2DManager& spriteManager)
        : m_fontLoader(fontLoader), m_spriteManager(spriteManager) {}

    void update(entt::registry& registry, float deltaTime) override {
        if (!enabled) return;

        auto view = registry.view<Shoonyakasha::Text2DComponent>();
        for (auto entity : view) {
            auto& text = view.get<Shoonyakasha::Text2DComponent>(entity);
            auto& baked = registry.get_or_emplace<Shoonyakasha::TextBakedComponent>(entity);

            bool changed = baked.bakedText != text.text ||
                           baked.bakedFont != text.font ||
                           baked.bakedFontSize != text.fontSize ||
                           baked.bakedColor != text.color ||
                           baked.bakedAlign != text.hAlign ||
                           baked.bakedVisible != text.visible ||
                           baked.bakedLayerMask != text.renderLayerMask;
            if (!changed) continue;

            rebuild(registry, entity, text, baked);
        }
    }

private:
    Shoonyakasha::FontLoader& m_fontLoader;
    Shoonyakasha::Sprite2DManager& m_spriteManager;

    void rebuild(entt::registry& registry, entt::entity labelEntity,
                 Shoonyakasha::Text2DComponent& text,
                 Shoonyakasha::TextBakedComponent& baked) {
        // Tear down previously baked glyph entities.
        for (auto glyphEntity : baked.glyphEntities) {
            if (registry.valid(glyphEntity)) registry.destroy(glyphEntity);
        }
        baked.glyphEntities.clear();

        baked.bakedText = text.text;
        baked.bakedFont = text.font;
        baked.bakedFontSize = text.fontSize;
        baked.bakedColor = text.color;
        baked.bakedAlign = text.hAlign;
        baked.bakedVisible = text.visible;
        baked.bakedLayerMask = text.renderLayerMask;

        if (text.font.empty() || text.text.empty() || !text.visible) return;

        const Shoonyakasha::BakedFont* font = m_fontLoader.getOrLoadFont(text.font, text.fontSize);
        if (!font) return;

        Shoonyakasha::UIAnchorComponent baseAnchor;
        if (auto* a = registry.try_get<Shoonyakasha::UIAnchorComponent>(labelEntity)) {
            baseAnchor = *a;
        }

        // Measure total width for center/right alignment.
        float totalWidth = 0.0f;
        for (char c : text.text) {
            auto it = font->glyphs.find(c);
            if (it != font->glyphs.end()) totalWidth += it->second.advancePixels;
        }

        float penX = 0.0f;
        if (text.hAlign == Shoonyakasha::Text2DComponent::HAlign::Center) penX = -totalWidth * 0.5f;
        else if (text.hAlign == Shoonyakasha::Text2DComponent::HAlign::Right) penX = -totalWidth;

        for (char c : text.text) {
            auto it = font->glyphs.find(c);
            if (it == font->glyphs.end()) continue;
            const Shoonyakasha::GlyphInfo& glyph = it->second;

            if (glyph.sizePixels.x > 0.0f && glyph.sizePixels.y > 0.0f) {
                entt::entity glyphEntity = registry.create();

                auto& transform = registry.emplace<Shoonyakasha::ECS::TransformComponent>(glyphEntity);
                transform.scale = glm::vec3(glyph.sizePixels, 1.0f);
                transform.isDirty = true;

                registry.emplace<Shoonyakasha::MeshComponent>(glyphEntity, m_spriteManager.getQuadMesh());

                auto& material = registry.emplace<Shoonyakasha::MaterialComponentV5>(glyphEntity);
                material.alphaMode = Shoonyakasha::AlphaMode::Blend;
                material.setParam("tintColor", text.color);
                material.setParam("uvRect", glyph.uvRect);
                material.setParam("screenSpace", 1.0f);
                material.textures["spriteTexture"] = font->atlas;

                auto& glyphTag = registry.emplace<Shoonyakasha::RenderableTagComponent>(glyphEntity);
                glyphTag.sortKey = text.sortKey;
                glyphTag.renderLayerMask = text.renderLayerMask;
                registry.emplace<Shoonyakasha::Sprite2DComponent>(glyphEntity).screenSpace = true;

                auto& glyphAnchor = registry.emplace<Shoonyakasha::UIAnchorComponent>(glyphEntity);
                glyphAnchor.anchor = baseAnchor.anchor;
                glyphAnchor.offsetPixels = baseAnchor.offsetPixels + glm::vec2(
                    penX + glyph.offsetPixels.x + glyph.sizePixels.x * 0.5f,
                    glyph.offsetPixels.y + glyph.sizePixels.y * 0.5f);

                registry.emplace<Shoonyakasha::TextGlyphOwnerComponent>(glyphEntity).owner = labelEntity;

                baked.glyphEntities.push_back(glyphEntity);
            }

            penX += glyph.advancePixels;
        }
    }
};

} // namespace ECS
} // namespace Shoonyakasha
