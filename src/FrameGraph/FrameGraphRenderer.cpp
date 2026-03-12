//
// FrameGraphRenderer.cpp - Automatic Entity Rendering from Compiled Frame Graph
//
// 虚空之舞 — The Dance of Emptiness
//

#include "FrameGraph/FrameGraphRenderer.h"
#include "FrameGraph/DotPathResolver.h"  // For SceneContext
#include "Vulkan/FrameGraph/FrameGraph.h"  // For RenderGraph, CompiledPass (full definitions)
#include "Vulkan/FrameGraph/FrameGraphPass.h"  // For PassDeclaration
#include "GPU/GPUTypes.h"

#include <algorithm>

namespace Shoonyakasha {

// ============================================================================
// getCameraPosition - Get camera position from override or scene context
// ============================================================================

glm::vec3 FrameGraphRenderer::getCameraPosition() const {
    if (m_hasCameraPosition) {
        return m_cameraPosition;
    }
    // Get from scene context (needs full SceneContext definition)
    return m_renderGraph.getSceneContext().cameraPosition;
}

// ============================================================================
// queryEntities - Query and optionally sort renderable entities
// ============================================================================

std::vector<RenderableEntity> FrameGraphRenderer::queryEntities(
    EntityFilter filter,
    EntitySortMode sortMode) const
{
    std::vector<RenderableEntity> result;

    if (!m_registry) {
        m_lastQueryCount = 0;
        return result;
    }

    // Query entities with required render components
    auto view = m_registry->view<
        MeshComponent,
        MaterialComponentV5,
        RenderableTagComponent,
        ECS::TransformComponent
    >();

    // Reserve space (estimate)
    result.reserve(view.size_hint());

    for (auto entity : view) {
        auto& mesh = view.get<MeshComponent>(entity);
        auto& material = view.get<MaterialComponentV5>(entity);
        auto& tag = view.get<RenderableTagComponent>(entity);
        auto& transform = view.get<ECS::TransformComponent>(entity);

        // Skip invalid meshes
        if (!mesh.isValid()) continue;

        // Check if entity has a skeleton (for skinned vs. static filtering)
        bool hasSkeleton = m_registry->all_of<Shoonyakasha::SkeletonComponent>(entity);

        // Apply filter
        if (!passesFilter(material, tag, filter, hasSkeleton)) continue;

        RenderableEntity re;
        re.entity = entity;
        re.mesh = &mesh;
        re.material = &material;
        re.tag = &tag;
        re.transform = &transform;
        re.distanceToCamera = calculateDistance(transform);

        result.push_back(re);
    }

    // Sort if requested
    switch (sortMode) {
        case EntitySortMode::FrontToBack:
            std::sort(result.begin(), result.end(),
                [](const RenderableEntity& a, const RenderableEntity& b) {
                    return a.distanceToCamera < b.distanceToCamera;
                });
            break;
        case EntitySortMode::BackToFront:
            std::sort(result.begin(), result.end(),
                [](const RenderableEntity& a, const RenderableEntity& b) {
                    return a.distanceToCamera > b.distanceToCamera;
                });
            break;
        case EntitySortMode::None:
        default:
            break;
    }

    m_lastQueryCount = static_cast<uint32_t>(result.size());
    return result;
}

// ============================================================================
// executeGeometryPass - The main rendering method (reads from CompiledPass!)
// ============================================================================

uint32_t FrameGraphRenderer::executeGeometryPass(
    const FrameGraph::CompiledPass& pass,
    const FrameGraph::PassDeclaration& passDecl,
    VkCommandBuffer cmd,
    uint32_t frameIndex)
{
    if (!m_registry) {
        m_lastDrawCount = 0;
        return 0;
    }

    // Check if pass has entity data binding
    if (!pass.hasEntityDataBinding) {
        // No binding config - can't render geometry automatically
        // This might be intentional for passes that use manual callbacks
        m_lastDrawCount = 0;
        return 0;
    }

    // Determine filter and sort mode from execution config
    EntityFilter filter = executionTypeToFilter(passDecl.execution.type);
    EntitySortMode sortMode = sortModeStringToEnum(passDecl.execution.sortMode);

    // Query entities
    auto entities = queryEntities(filter, sortMode);

    // Render each entity
    uint32_t drawCount = 0;
    for (const auto& re : entities) {
        bindAndDrawEntity(re.entity, *re.mesh, pass, cmd, frameIndex);
        drawCount++;
    }

    m_lastDrawCount = drawCount;

    // Debug logging (once per session)
    static bool loggedOnce = false;
    if (!loggedOnce && drawCount > 0) {
        printf("[FrameGraphRenderer] executeGeometryPass: type='%s', sortMode='%s', draws=%u\n",
            passDecl.execution.type.c_str(),
            passDecl.execution.sortMode.c_str(),
            drawCount);
        printf("  entityDataBinding: perDraw='%s', material='%s'\n",
            pass.entityDataBinding.perDraw.layoutRef.c_str(),
            pass.entityDataBinding.material.layoutRef.c_str());
        loggedOnce = true;
    }

    return drawCount;
}

// ============================================================================
// bindAndDrawEntity - Bind push constants, textures, and issue draw call
// ============================================================================

void FrameGraphRenderer::bindAndDrawEntity(
    entt::entity entity,
    const MeshComponent& mesh,
    const FrameGraph::CompiledPass& pass,
    VkCommandBuffer cmd,
    uint32_t frameIndex)
{
    // Bind push constants via dot-path resolution
    // The layoutRef comes from the COMPILED PASS - no hardcoded strings!
    if (!pass.entityDataBinding.perDraw.layoutRef.empty()) {
        m_renderGraph.bindEntityData(
            entity,
            *m_registry,
            pass.entityDataBinding.perDraw.layoutRef,
            cmd,
            pass.pipelineLayout
        );
    }

    // Bind material textures
    // The layoutRef comes from the COMPILED PASS - no hardcoded strings!
    if (!pass.entityDataBinding.material.layoutRef.empty()) {
        m_renderGraph.bindMaterialTextures(
            entity,
            *m_registry,
            pass.entityDataBinding.material.layoutRef,
            cmd,
            pass.pipelineLayout,
            frameIndex
        );
    }

    // Bind skeleton SSBO (per-entity bone matrices for skinned geometry)
    // 骨之繫 — The binding of bones
    if (!pass.entityDataBinding.skeleton.layoutRef.empty()) {
        m_renderGraph.bindSkeletonSSBO(
            entity,
            *m_registry,
            pass.entityDataBinding.skeleton.layoutRef,
            cmd,
            pass.pipelineLayout,
            frameIndex
        );
    }

    // Bind vertex buffer
    VkBuffer vertexBuffer = mesh.vertexBuffer.buffer;
    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &vertexBuffer, &offset);

    // Bind index buffer and draw
    if (mesh.hasIndices()) {
        VkIndexType indexType = toVkIndexType(mesh.indexType);
        vkCmdBindIndexBuffer(cmd, mesh.indexBuffer.buffer, 0, indexType);
        vkCmdDrawIndexed(cmd, mesh.indexCount, 1, 0, 0, 0);
    } else {
        vkCmdDraw(cmd, mesh.vertexCount, 1, 0, 0);
    }
}

} // namespace Shoonyakasha
