//
// UIContext.h - What the canvas UI keeps between frames
//
// The fonts, the glyph atlas, each frame's draw data and, when there is a
// device, the renderer that draws it. ApplicationBase owns one.
//

#pragma once

#include "UI/FontLibrary.h"
#include "UI/GlyphCache.h"
#include "UI/UIBatchBuilder.h"

#include <entt/entt.hpp>
#include <vulkan/vulkan.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace Shoonyakasha {

class Logger;
class VulkanDevice;
namespace FrameGraph { struct PassExecuteContext; }

namespace UI {

class UIRenderer;

class UIContext {
public:
    /// Fonts, layout and batching without drawing.
    UIContext();

    /// With a renderer whose shaders are in `shaderDirectory`. Throws if the
    /// renderer's GPU resources cannot be created.
    UIContext(VulkanDevice& device, const std::string& shaderDirectory, uint32_t framesInFlight,
              Logger* logger = nullptr);

    ~UIContext();

    UIContext(const UIContext&) = delete;
    UIContext& operator=(const UIContext&) = delete;

    FontLibrary& fonts() { return m_fonts; }
    const FontLibrary& fonts() const { return m_fonts; }
    GlyphCache& glyphs() { return m_glyphs; }
    const UIDrawData& drawData() const { return m_drawData; }
    bool hasRenderer() const { return m_renderer != nullptr; }

    /// Builds this frame's draw data from the laid-out canvases. With a
    /// renderer, records the glyph uploads and the world canvases into `cmd`
    /// for frame `frameIndex`; without one, drops the queued glyph uploads.
    void prepareFrame(entt::registry& registry, VkCommandBuffer cmd, uint32_t frameIndex);

    /// Called with a world canvas texture's view before the texture is
    /// destroyed, so materials can let go of it. No effect without a renderer.
    void setTextureReleaser(std::function<void(VkImageView)> releaser);

    /// A world canvas: an entity with a WorldTexture UICanvas of `pixelSize`
    /// pixels, shown on a quad of `worldSize` units centred on its transform
    /// and facing +Z. The quad's material emits the canvas at `emission`
    /// times its colour, over a black base colour, and casts no shadow. For a
    /// lit canvas, set UICanvas::textureSlot to "albedoMap", the material's
    /// baseColorFactor to white and its emissiveFactor to zero. Without a
    /// renderer the entity has no mesh.
    entt::entity createWorldCanvas(entt::registry& registry, glm::vec2 pixelSize, glm::vec2 worldSize,
                                   float emission = 1.0f);

    /// Draws the screen overlay canvases. The renderer of "ui_canvas" passes.
    void drawOverlays(const FrameGraph::PassExecuteContext& ctx);

    /// True when this frame has screen overlay batches but no drawOverlays()
    /// call since prepareFrame(): the pipeline has no "ui_canvas" pass.
    bool overlaysUndrawn() const;

private:
    FontLibrary m_fonts;
    GlyphCache m_glyphs;
    UIDrawData m_drawData;
    std::unique_ptr<UIRenderer> m_renderer;
    bool m_overlaysDrawn = false;
};

} // namespace UI
} // namespace Shoonyakasha
