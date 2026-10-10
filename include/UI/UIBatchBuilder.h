//
// UIBatchBuilder.h - Turning laid-out canvases into vertices, indices and batches
//
// All canvases share one vertex and one index array. Each canvas owns a run
// of batches; a batch is a range of indices drawn with one image and one
// scissor rect. Quads are emitted in draw order (UIRect::drawOrder; within an
// element the panel, then the image, then the text), and a quad joins the
// previous batch whenever the image and scissor allow it, so the order of
// overlapping elements is kept.
//

#pragma once

#include "UI/FontLibrary.h"
#include "UI/GlyphCache.h"
#include "UI/UIComponents.h"

#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>

namespace Shoonyakasha {
namespace UI {

/// How the fragment shader colours a quad.
enum class UIDrawMode : uint8_t {
    Image = 0,  // the batch image times the vertex colour
    Text = 1,   // the glyph atlas distance field times the vertex colour
    Solid = 2,  // the vertex colour
};

struct UIVertex {
    glm::vec2 position;  // target pixels, origin top-left, y down
    glm::vec2 uv;
    uint32_t color;      // RGBA8, sRGB-encoded, straight alpha
    uint32_t mode;       // UIDrawMode in the low byte
};
static_assert(sizeof(UIVertex) == 24, "UIVertex is read by ui.vert with a 24-byte stride");

struct UIBatch {
    VkImageView image = VK_NULL_HANDLE;  // null: no Image quads in the batch
    VkSampler sampler = VK_NULL_HANDLE;
    glm::ivec4 scissor{0};               // x, y, width, height in target pixels
    uint32_t firstIndex = 0;
    uint32_t indexCount = 0;
};

struct CanvasDraw {
    entt::entity canvas = entt::null;
    UICanvas::Mode mode = UICanvas::Mode::ScreenOverlay;
    int sortOrder = 0;
    glm::vec2 targetSize{0.0f};
    glm::vec4 clearColor{0.0f};  // WorldTexture: UICanvas::clearColor
    uint32_t firstBatch = 0;
    uint32_t batchCount = 0;
};

struct UIDrawData {
    std::vector<UIVertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<UIBatch> batches;
    std::vector<CanvasDraw> canvases;  // screen overlays in ascending sortOrder, then world canvases

    void clear() {
        vertices.clear();
        indices.clear();
        batches.clear();
        canvases.clear();
    }
};

/// Builds draw data for every laid-out canvas, after CanvasLayoutSystem and
/// CanvasTextSystem have run. Glyphs missing from the atlas are added to
/// `glyphs`. If the atlas fills up, it is reset and the build starts over
/// once; glyphs that still do not fit are left out.
void buildDrawData(entt::registry& registry, const FontLibrary& fonts, GlyphCache& glyphs, UIDrawData& out);

/// RGBA8 packing of a colour with components in [0, 1].
uint32_t packColor(const glm::vec4& color);

} // namespace UI
} // namespace Shoonyakasha
