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
    /// renderer, records the glyph uploads into `cmd` and writes the vertices
    /// for frame `frameIndex`; without one, drops the queued glyph uploads.
    void prepareFrame(entt::registry& registry, VkCommandBuffer cmd, uint32_t frameIndex);

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
