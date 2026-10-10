//
// UIContext.cpp - What the canvas UI keeps between frames
//

#include "UI/UIContext.h"
#include "UI/UIRenderer.h"

#include "ECS/Core.h"
#include "ECS/RenderComponents.h"

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
        m_renderer->prepareFrame(cmd, frameIndex, registry, m_drawData, m_glyphs);
    } else {
        m_glyphs.clearPendingUploads();
    }
}

void UIContext::setTextureReleaser(std::function<void(VkImageView)> releaser) {
    if (m_renderer) m_renderer->setTextureReleaser(std::move(releaser));
}

entt::entity UIContext::createWorldCanvas(entt::registry& registry, glm::vec2 pixelSize, glm::vec2 worldSize,
                                          float emission) {
    const entt::entity entity = registry.create();

    UICanvas canvas;
    canvas.mode = UICanvas::Mode::WorldTexture;
    canvas.pixelSize = pixelSize;
    registry.emplace<UICanvas>(entity, canvas);

    auto& transform = registry.emplace<ECS::TransformComponent>(entity);
    transform.scale = glm::vec3(worldSize, 1.0f);
    transform.isDirty = true;

    auto& material = registry.emplace<MaterialComponentV5>(entity);
    material.setParam("baseColorFactor", glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
    material.setParam("emissiveFactor", glm::vec4(glm::vec3(emission), 0.0f));
    material.setParam("metallicFactor", 0.0f);
    material.setParam("roughnessFactor", 1.0f);

    if (m_renderer) {
        registry.emplace<MeshComponent>(entity, m_renderer->worldQuad());
        registry.emplace<RenderableTagComponent>(entity).castShadows = false;
    }
    return entity;
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
