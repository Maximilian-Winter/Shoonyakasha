//
// Sprite2DComponents.h - ECS Components for 2D sprites, UI elements, and text
//
// Design:
//   - Sprite2DComponent: marker for entities rendered by the "sprite_geometry"
//     pass. Shader-visible sprite data (tint, UV rect) lives in the existing
//     MaterialComponentV5 (params + textures maps), exactly like PBR
//     materials do. This component only carries the flags needed to filter
//     and place the entity in C++ - it is never read by a shader.
//   - UIAnchorComponent: screen-space placement relative to a viewport
//     corner/edge/center. Resolved into TransformComponent::position each
//     frame by UILayoutSystem.
//   - Text2DComponent: a text label rendered by a dedicated glyph-batching
//     system (TextRenderSystem) rather than the generic per-entity path,
//     since a single label expands into many glyph quads.
//
// A sprite/UI entity is a normal renderable entity: it also carries
// MeshComponent (a shared unit quad), MaterialComponentV5, TransformComponent
// and RenderableTagComponent, just like 3D mesh entities.
//

#pragma once

#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace Shoonyakasha {

// ============================================================================
// Sprite2DComponent - Marker for sprite_geometry entities
// ============================================================================

struct Sprite2DComponent {
    // World-space sprites are placed/projected with the active 3D camera
    // (entity.transform.worldMatrix * scene.camera.viewProjection).
    // Screen-space sprites are placed in pixel coordinates and projected
    // directly from scene.screen.resolution, ignoring the 3D camera.
    bool screenSpace = false;
};

// ============================================================================
// UIAnchorComponent - Screen-space anchoring for UI sprites/text
// ============================================================================

struct UIAnchorComponent {
    enum class Anchor {
        TopLeft, TopCenter, TopRight,
        MiddleLeft, MiddleCenter, MiddleRight,
        BottomLeft, BottomCenter, BottomRight
    };

    Anchor anchor = Anchor::TopLeft;

    // Pixel offset from the anchor point to the element's CENTER (sprite
    // quads are center-pivoted). For corner/edge anchors this offsets away
    // from the screen edge; for center anchors it offsets from the screen
    // center. E.g. a TopLeft panel of size (200, 60) fully inside the
    // viewport needs offsetPixels = (100, 30).
    glm::vec2 offsetPixels{0.0f};

    // Resolve this anchor + offset into a pixel-space position for the
    // given screen size. Shared by UILayoutSystem and tests.
    glm::vec2 resolve(float screenWidth, float screenHeight) const {
        glm::vec2 base{0.0f};
        switch (anchor) {
            case Anchor::TopLeft:      base = {0.0f,          0.0f};           break;
            case Anchor::TopCenter:    base = {screenWidth * 0.5f, 0.0f};      break;
            case Anchor::TopRight:     base = {screenWidth,   0.0f};           break;
            case Anchor::MiddleLeft:   base = {0.0f,          screenHeight * 0.5f}; break;
            case Anchor::MiddleCenter: base = {screenWidth * 0.5f, screenHeight * 0.5f}; break;
            case Anchor::MiddleRight:  base = {screenWidth,   screenHeight * 0.5f}; break;
            case Anchor::BottomLeft:   base = {0.0f,          screenHeight};   break;
            case Anchor::BottomCenter: base = {screenWidth * 0.5f, screenHeight}; break;
            case Anchor::BottomRight:  base = {screenWidth,   screenHeight};   break;
        }
        return base + offsetPixels;
    }
};

// ============================================================================
// Text2DComponent - A text label rendered via TextRenderSystem
// ============================================================================

// NOTE: destroying the entity that owns a Text2DComponent does not
// currently cascade-delete the glyph sprite entities TextRenderSystem
// generated for it (they aren't linked via HierarchyComponent, since that
// would double-apply the label's transform to each glyph's already-
// absolute screen position). Hide unused labels via
// Text2DComponent::visible instead of destroying the entity.
struct Text2DComponent {
    enum class HAlign { Left, Center, Right };

    std::string text;
    std::string font;          // Font handle name, registered via FontLoader
    float fontSize = 24.0f;
    glm::vec4 color{1.0f};
    HAlign hAlign = HAlign::Left;
    bool visible = true;

    // Draw order among text labels/sprites - lower draws first.
    // Mirrors RenderableTagComponent::sortKey for entities that don't
    // otherwise carry a RenderableTagComponent.
    uint32_t sortKey = 0;
};

// ============================================================================
// Internal text-rendering state (TextRenderSystem only)
// ============================================================================
//
// TextRenderSystem bakes a Text2DComponent into one sprite entity per
// glyph, using the label entity's UIAnchorComponent as the base anchor.
// This tracks what was last baked so unchanged labels are skipped, and
// owns the generated glyph entities so they can be torn down when the
// label's text/font/size/color/alignment changes.
//

struct TextBakedComponent {
    std::string bakedText;
    std::string bakedFont;
    float bakedFontSize = -1.0f;
    glm::vec4 bakedColor{1.0f};
    Text2DComponent::HAlign bakedAlign = Text2DComponent::HAlign::Left;
    bool bakedVisible = true;
    std::vector<entt::entity> glyphEntities;
};

// Marker on entities generated by TextRenderSystem, for identification.
struct TextGlyphOwnerComponent {
    entt::entity owner = entt::null;
};

} // namespace Shoonyakasha
