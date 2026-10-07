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
#include "Vulkan/FrameGraph/RenderStats.h"

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
    EntitySortMode sortMode,
    uint32_t renderLayerMask,
    FrameGraph::AlphaFilter alphaFilter,
    const ViewCull* cullView) const
{
    std::vector<RenderableEntity> result;
    m_lastCulledCount = 0;

    if (!m_registry || (cullView && cullView->skip)) {
        m_lastQueryCount = 0;
        return result;
    }

    gatherRenderables();
    result.reserve(m_renderables.size());

    for (const auto& r : m_renderables) {
        // Apply filter
        if (!passesFilter(*r.material, *r.tag, filter, r.hasSkeleton, r.isSprite2D)) continue;

        if (!passesAlphaFilter(*r.material, alphaFilter)) continue;

        // Apply render layer mask (bitwise intersection with the tag's
        // 8-bit mask; default renderLayerMask matches every layer)
        if ((static_cast<uint32_t>(r.tag->renderLayerMask) & renderLayerMask) == 0) continue;

        // Outside the pass's view. Skinned meshes are kept: their bounds are
        // the bind pose, which an animation can leave.
        if (cullView && !r.hasSkeleton && cullView->cull && r.hasBounds &&
            !boundsInFrustum(cullView->frustum, r.worldMin, r.worldMax)) {
            ++m_lastCulledCount;
            continue;
        }

        RenderableEntity re;
        re.entity = r.entity;
        re.mesh = r.mesh;
        re.material = r.material;
        re.tag = r.tag;
        re.transform = r.transform;
        re.distanceToCamera = calculateDistance(*r.transform, cullView);

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
        case EntitySortMode::SortKey:
            std::sort(result.begin(), result.end(),
                [](const RenderableEntity& a, const RenderableEntity& b) {
                    uint32_t keyA = a.tag ? a.tag->sortKey : 0;
                    uint32_t keyB = b.tag ? b.tag->sortKey : 0;
                    return keyA < keyB;
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
// gatherRenderables - the frame's renderable entities, once per frame
// ============================================================================

void FrameGraphRenderer::gatherRenderables() const {
    const uint64_t frame = m_renderGraph.getFrameNumber();
    if (frame == m_renderablesFrame) return;
    m_renderablesFrame = frame;
    m_renderables.clear();

    // Transforms are final by the time the frame records, so world bounds
    // are computed here once instead of in each pass that culls.
    auto view = m_registry->view<MeshComponent, MaterialComponentV5, RenderableTagComponent, ECS::TransformComponent>();
    m_renderables.reserve(view.size_hint());
    for (auto entity : view) {
        const auto& mesh = view.get<MeshComponent>(entity);
        if (!mesh.isValid()) continue;

        Renderable r;
        r.entity = entity;
        r.mesh = &mesh;
        r.material = &view.get<MaterialComponentV5>(entity);
        r.tag = &view.get<RenderableTagComponent>(entity);
        r.transform = &view.get<ECS::TransformComponent>(entity);
        r.hasSkeleton = m_registry->all_of<Shoonyakasha::SkeletonComponent>(entity);
        r.isSprite2D = m_registry->all_of<Shoonyakasha::Sprite2DComponent>(entity);
        r.hasBounds = mesh.hasBounds;
        if (r.hasBounds) transformBounds(r.transform->worldMatrix, mesh.boundsMin, mesh.boundsMax, r.worldMin, r.worldMax);
        m_renderables.push_back(r);
    }
}

// ============================================================================
// Views - what a pass culls and sorts against
// ============================================================================

FrameGraphRenderer::ViewCull FrameGraphRenderer::resolveView(
    const FrameGraph::PassDeclaration& passDecl) const
{
    using FrameGraph::CullView;
    const auto& scene = m_renderGraph.getSceneContext();

    ViewCull view;
    view.origin = getCameraPosition();

    CullView kind = passDecl.execution.view;
    if (kind == CullView::Default) {
        // Shadow casters outside the camera's view still cast into it.
        const auto filter = executionTypeToFilter(passDecl.execution.type);
        const bool shadow = filter == EntityFilter::ShadowCasters ||
                            filter == EntityFilter::SkinnedShadowCasters;
        const bool sprites = filter == EntityFilter::Sprite2D;
        kind = (shadow || sprites) ? CullView::None : CullView::Camera;
    }

    if (kind == CullView::Camera) {
        // cameraViewProjection is identity until a main camera is found.
        if (scene.cameraViewProjection != glm::mat4(1.0f)) {
            view.cull = true;
            view.frustum = frustumFromViewProj(scene.cameraViewProjection, ClipDepth::MinusOneToOne);
        }
    } else if (kind == CullView::SunCascade) {
        const auto& sun = scene.sunShadow;
        if (sun.cascades.valid) {
            const uint32_t index = std::min(passDecl.execution.viewIndex, sun.cascades.count - 1);
            // No near plane: with depth clamping, casters between the sun and
            // the cascade still write depth.
            view.cull = true;
            view.frustum = frustumFromViewProj(sun.cascades.viewProj[index], ClipDepth::ZeroToOne,
                                               /*withNearPlane=*/false);
            view.alongDirection = true;
            view.direction = glm::vec3(sun.direction);
        }
    } else if (kind == CullView::SpotShadow) {
        const auto& local = scene.localShadow.shadows;
        const uint32_t slot = passDecl.execution.viewIndex;
        if (slot >= local.spotCount) {
            view.skip = true;
        } else {
            view.cull = true;
            view.frustum = frustumFromViewProj(local.spot[slot].viewProj, ClipDepth::ZeroToOne);
            view.origin = glm::vec3(scene.lights[local.spot[slot].lightIndex].positionType);
        }
    } else if (kind == CullView::PointShadowFace) {
        const auto& local = scene.localShadow.shadows;
        const uint32_t slot = passDecl.execution.viewIndex / 6, face = passDecl.execution.viewIndex % 6;
        if (slot >= local.pointCount) {
            view.skip = true;
        } else {
            view.cull = true;
            view.frustum = frustumFromViewProj(local.point[slot].faceViewProj[face], ClipDepth::ZeroToOne);
            view.origin = glm::vec3(local.point[slot].positionFar);
        }
    }
    return view;
}

bool FrameGraphRenderer::isVisible(const ViewCull& view, const MeshComponent& mesh, const glm::mat4& world) {
    if (!view.cull || !mesh.hasBounds) return true;
    glm::vec3 min, max;
    transformBounds(world, mesh.boundsMin, mesh.boundsMax, min, max);
    return boundsInFrustum(view.frustum, min, max);
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

    // Query entities inside the pass's view
    const ViewCull view = resolveView(passDecl);
    auto entities = queryEntities(filter, sortMode, passDecl.execution.renderLayerMask,
                                  passDecl.execution.alphaFilter, &view);

    // Look up what the pass binds once, then bind each entity through it.
    auto plan = m_renderGraph.planEntityBindings(pass);
    BoundBuffers bound;

    uint32_t drawCount = 0;
    for (const auto& re : entities) {
        bindAndDrawEntity(re.entity, *re.mesh, pass, plan, bound, cmd, frameIndex);
        drawCount++;
    }

    m_lastDrawCount = drawCount;
    m_passStats[passDecl.name] = PassDrawStats{drawCount, m_lastCulledCount};

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
    FrameGraph::RenderGraph::EntityBindingPlan& plan,
    BoundBuffers& bound,
    VkCommandBuffer cmd,
    uint32_t frameIndex)
{
    // Push constants, material textures and bone buffer, as the pass's
    // entityDataBinding names them (looked up once, in `plan`).
    m_renderGraph.bindEntityData(entity, *m_registry, plan, cmd, pass.pipelineLayout);
    m_renderGraph.bindMaterialTextures(entity, *m_registry, plan, cmd, pass.pipelineLayout);
    m_renderGraph.bindSkeletonSSBO(entity, *m_registry, plan, cmd, pass.pipelineLayout, frameIndex);

    // Vertex and index buffers, unless the previous draw left them bound:
    // meshes are often suballocated from shared buffers.
    VkBuffer vertexBuffer = mesh.vertexHandle();
    if (vertexBuffer != bound.vertex) {
        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &vertexBuffer, &offset);
        bound.vertex = vertexBuffer;
    }

    if (mesh.hasIndices()) {
        const VkIndexType indexType = toVkIndexType(mesh.indexType);
        if (mesh.indexHandle() != bound.index || indexType != bound.indexType) {
            vkCmdBindIndexBuffer(cmd, mesh.indexHandle(), 0, indexType);
            bound.index = mesh.indexHandle();
            bound.indexType = indexType;
        }
        vkCmdDrawIndexed(cmd, mesh.indexCount, 1, 0, 0, 0);
        FrameGraph::countDraw(mesh.indexCount);
    } else {
        vkCmdDraw(cmd, mesh.vertexCount, 1, 0, 0);
        FrameGraph::countDraw(mesh.vertexCount);
    }
}

} // namespace Shoonyakasha
