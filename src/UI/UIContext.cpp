//
// UIContext.cpp - What the canvas UI keeps between frames
//

#include "UI/UIContext.h"
#include "UI/UIRenderer.h"

namespace Shoonyakasha {
namespace UI {

UIContext::UIContext() = default;

UIContext::UIContext(VulkanDevice& device, const std::string& shaderDirectory, uint32_t framesInFlight,
                     Logger* logger)
    : m_renderer(std::make_unique<UIRenderer>(device, shaderDirectory, framesInFlight, m_glyphs.size(), logger)) {}

UIContext::~UIContext() = default;

void UIContext::prepareFrame(entt::registry& registry, VkCommandBuffer cmd, uint32_t frameIndex) {
    m_overlaysDrawn = false;
    buildDrawData(registry, m_fonts, m_glyphs, m_drawData);
    if (m_renderer) {
        m_renderer->prepareFrame(cmd, frameIndex, m_drawData, m_glyphs);
    } else {
        m_glyphs.clearPendingUploads();
    }
}

void UIContext::drawOverlays(const FrameGraph::PassExecuteContext& ctx) {
    m_overlaysDrawn = true;
    if (m_renderer) m_renderer->drawOverlays(ctx, m_drawData);
}

bool UIContext::overlaysUndrawn() const {
    if (m_overlaysDrawn) return false;
    for (const auto& canvas : m_drawData.canvases) {
        if (canvas.mode == UICanvas::Mode::ScreenOverlay && canvas.batchCount > 0) return true;
    }
    return false;
}

} // namespace UI
} // namespace Shoonyakasha
