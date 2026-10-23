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
#include "ECS/MorphComponents.h"
#include "GPU/GPUResourceFactory.h"
#include "Vulkan/VulkanDevice.h"
#include "Vulkan/FrameGraph/RenderStats.h"

#include <algorithm>
#include <cmath>
#include <cstring>

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
    m_lastOccludedCount = 0;

    if (!m_registry || (cullView && cullView->skip)) {
        m_lastQueryCount = 0;
        return result;
    }

    gatherRenderables();
    result.reserve(m_renderables.size());

    for (const auto& r : m_renderables) {
        // Apply filter
        if (!passesFilter(*r.material, *r.tag, filter, r.hasSkeleton, r.isSprite2D, r.transmissive)) continue;

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
        // Hidden behind the occlusion depth. It is a few frames old, so
        // entities that move, and skinned ones, are always drawn.
        if (cullView && cullView->occlusion && !r.hasSkeleton && r.hasBounds && !r.moving &&
            cullView->occlusion->occluded(r.worldMin, r.worldMax)) {
            ++m_lastOccludedCount;
            continue;
        }

        RenderableEntity re;
        re.entity = r.entity;
        re.mesh = r.mesh;
        re.material = r.material;
        re.tag = r.tag;
        re.transform = r.transform;
        re.batchKey = r.batchKey;
        re.materialHash = r.materialHash;
        re.lod = r.lod;
        re.distanceToCamera = calculateDistance(*r.transform, cullView);

        result.push_back(re);
    }

    // Sort if requested
    switch (sortMode) {
        case EntitySortMode::FrontToBack:
            std::stable_sort(result.begin(), result.end(),
                [](const RenderableEntity& a, const RenderableEntity& b) {
                    return a.distanceToCamera < b.distanceToCamera;
                });
            break;
        case EntitySortMode::BackToFront:
            std::stable_sort(result.begin(), result.end(),
                [](const RenderableEntity& a, const RenderableEntity& b) {
                    return a.distanceToCamera > b.distanceToCamera;
                });
            break;
        case EntitySortMode::SortKey:
            std::stable_sort(result.begin(), result.end(),
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
    m_lodCounts.clear();

    // The main camera this frame, for the occlusion images it renders.
    const auto& scene = m_renderGraph.getSceneContext();
    CameraRecord record;
    record.frame = m_renderGraph.getReadbackFrameNumber();
    record.viewProj = scene.cameraViewProjection;
    record.position = scene.cameraPosition;
    record.forward = -glm::normalize(glm::vec3(scene.cameraInvView[2]));
    if (m_cameraRecords.size() >= 8) m_cameraRecords.erase(m_cameraRecords.begin());
    m_cameraRecords.push_back(record);

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
        r.transmissive = r.material->getParam<float>("transmissionFactor", 0.0f) > 0.0f;
        r.hasBounds = mesh.hasBounds;
        r.moving = r.transform->hasPreviousWorld && r.transform->previousWorldMatrix != r.transform->worldMatrix;
        if (r.hasBounds) transformBounds(r.transform->worldMatrix, mesh.boundsMin, mesh.boundsMax, r.worldMin, r.worldMax);
        const bool ownBuffers = r.hasSkeleton || r.isSprite2D || m_registry->all_of<MorphComponent>(entity);
        // Mirroring world matrices draw with the other front face, apart.
        const bool mirrored = glm::determinant(glm::mat3(r.transform->worldMatrix)) < 0.0f;
        r.materialHash = materialHash(*r.material);
        r.batchKey = ownBuffers ? 0 : batchKey(mesh, r.materialHash) ^ (mirrored ? 0x5bd1e9955bd1e995ull : 0);
        if (!ownBuffers && r.batchKey == 0) r.batchKey = 1;
        // The level of detail every pass draws this frame; levels batch apart.
        if (!mesh.lods.empty() && r.hasBounds) {
            r.lod = selectLod(mesh, r.transform->worldMatrix, r.worldMin, r.worldMax);
            const uint32_t level = r.lod ? static_cast<uint32_t>(r.lod - mesh.lods.data()) + 1 : 0;
            if (m_lodCounts.size() <= level) m_lodCounts.resize(level + 1, 0);
            ++m_lodCounts[level];
            if (r.batchKey != 0 && level > 0) {
                r.batchKey ^= 0x9e3779b97f4a7c15ull * level;
                if (r.batchKey == 0) r.batchKey = 1;
            }
        }
        m_renderables.push_back(r);
    }
}

const MeshLod* FrameGraphRenderer::selectLod(const MeshComponent& mesh, const glm::mat4& world,
                                             const glm::vec3& worldMin, const glm::vec3& worldMax) const {
    const auto& scene = m_renderGraph.getSceneContext();
    const float threshold = m_renderGraph.getLodThreshold();
    if (threshold <= 0.0f || !scene.hasCamera || scene.renderHeight <= 0.0f) return nullptr;

    const glm::vec3 eye(scene.cameraInvView[3]);
    const float distance = glm::length(eye - glm::clamp(eye, worldMin, worldMax));
    if (distance <= 0.0f) return nullptr;
    const float scale = std::max({glm::length(glm::vec3(world[0])), glm::length(glm::vec3(world[1])),
                                  glm::length(glm::vec3(world[2]))});
    const float pixelsPerUnit =
        scene.renderHeight / (2.0f * std::tan(glm::radians(scene.cameraFov) * 0.5f) * distance);

    const MeshLod* chosen = nullptr;
    for (const MeshLod& lod : mesh.lods) {
        if (lod.error * scale * pixelsPerUnit > threshold) break;
        chosen = &lod;
    }
    return chosen;
}

// ============================================================================
// occlusionFor - the latest read-back depth of an occlusion image
// ============================================================================

const DepthPyramid* FrameGraphRenderer::occlusionFor(const std::string& name) {
    OcclusionSource& source = m_occlusion[name];
    const auto* readback = m_renderGraph.latestReadback(name);
    if (readback && readback->isImage && readback->data && readback->frameNumber != source.frame &&
        readback->imageFormat == VK_FORMAT_R32_SFLOAT &&
        readback->size >= static_cast<VkDeviceSize>(readback->imageWidth) * readback->imageHeight * sizeof(float)) {
        // The camera of the frame that rendered it.
        const auto camera = std::find_if(m_cameraRecords.begin(), m_cameraRecords.end(),
                                         [&](const CameraRecord& c) { return c.frame == readback->frameNumber; });
        if (camera != m_cameraRecords.end() && camera->viewProj != glm::mat4(1.0f)) {
            source.pyramid.build(static_cast<const float*>(readback->data), readback->imageWidth,
                                 readback->imageHeight, camera->viewProj);
            source.camera = *camera;
            source.frame = readback->frameNumber;
        }
    }
    if (source.pyramid.empty() || m_cameraRecords.empty()) return nullptr;

    // Too old, or the camera cut away since: test nothing.
    const CameraRecord& now = m_cameraRecords.back();
    if (now.frame > source.frame + 8) return nullptr;
    const float farPlane = m_renderGraph.getSceneContext().cameraFarPlane;
    if (glm::dot(now.forward, source.camera.forward) < std::cos(glm::radians(25.0f)) ||
        glm::length(now.position - source.camera.position) > 0.1f * farPlane) {
        return nullptr;
    }
    return &source.pyramid;
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
            view.mainCamera = true;
            view.frustum = frustumFromViewProj(scene.cameraViewProjection, ClipDepth::ZeroToOne);
        }
    } else if (kind == CullView::NamedCamera) {
        const auto camera = scene.cameras.find(passDecl.execution.viewName);
        if (camera == scene.cameras.end()) {
            view.skip = true;
        } else {
            view.cull = true;
            view.frustum = frustumFromViewProj(camera->second.viewProjection, ClipDepth::ZeroToOne);
            view.origin = camera->second.position;
        }
    } else if (kind == CullView::SunCascade) {
        // Cascade slot c is cascade c % MAX_SUN_CASCADES of sun
        // c / MAX_SUN_CASCADES. A sun after the first that no light is draws
        // nothing.
        const uint32_t sunIndex = passDecl.execution.viewIndex / MAX_SUN_CASCADES;
        const auto& sun = scene.suns[sunIndex];
        if (sunIndex > 0 && !sun.cascades.valid) view.skip = true;
        if (sun.cascades.valid) {
            const uint32_t index = std::min(passDecl.execution.viewIndex % MAX_SUN_CASCADES, sun.cascades.count - 1);
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
    ViewCull view = resolveView(passDecl);
    if (!passDecl.execution.occlusion.empty() && view.mainCamera && m_renderGraph.isOcclusionCullingEnabled()) {
        gatherRenderables();
        view.occlusion = occlusionFor(passDecl.execution.occlusion);
    }
    auto entities = queryEntities(filter, sortMode, passDecl.execution.renderLayerMask,
                                  passDecl.execution.alphaFilter, &view);

    // Look up what the pass binds once, then bind each entity through it.
    auto plan = m_renderGraph.planEntityBindings(pass);
    BoundBuffers bound;

    uint32_t drawCount = 0;
    if (passDecl.execution.instancing) {
        drawCount = drawInstanced(entities, sortMode == EntitySortMode::BackToFront, pass, plan, bound, cmd,
                                  frameIndex);
    } else {
        for (const auto& re : entities) {
            bindAndDrawEntity(re.entity, *re.mesh, re.lod, re.materialHash, pass, plan, bound, cmd, frameIndex);
            drawCount++;
        }
    }

    m_lastDrawCount = drawCount;
    m_passStats[passDecl.name] = PassDrawStats{drawCount, m_lastCulledCount, m_lastOccludedCount};

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
    const MeshLod* lod,
    uint64_t materialHash,
    const FrameGraph::CompiledPass& pass,
    FrameGraph::RenderGraph::EntityBindingPlan& plan,
    BoundBuffers& bound,
    VkCommandBuffer cmd,
    uint32_t frameIndex,
    uint32_t instanceCount)
{
    // Push constants, material textures and bone buffer, as the pass's
    // entityDataBinding names them (looked up once, in `plan`).
    m_renderGraph.bindEntityData(entity, *m_registry, plan, cmd, pass.pipelineLayout);
    m_renderGraph.bindMaterialTextures(entity, *m_registry, plan, cmd, pass.pipelineLayout, materialHash);
    m_renderGraph.bindSkeletonSSBO(entity, *m_registry, plan, cmd, pass.pipelineLayout, frameIndex);

    // Single-sided materials lose their back faces; a mirroring world
    // matrix reverses which faces are front ones.
    if (pass.materialCulling) {
        const auto* material = m_registry->try_get<MaterialComponentV5>(entity);
        const auto* transform = m_registry->try_get<ECS::TransformComponent>(entity);
        const VkCullModeFlags cull = material && material->doubleSided ? VK_CULL_MODE_NONE : VK_CULL_MODE_BACK_BIT;
        const bool mirrored = transform && glm::determinant(glm::mat3(transform->worldMatrix)) < 0.0f;
        const VkFrontFace front = mirrored ? VK_FRONT_FACE_CLOCKWISE : VK_FRONT_FACE_COUNTER_CLOCKWISE;
        if (cull != bound.cullMode) {
            vkCmdSetCullMode(cmd, cull);
            bound.cullMode = cull;
        }
        if (front != bound.frontFace) {
            vkCmdSetFrontFace(cmd, front);
            bound.frontFace = front;
        }
    }

    // Vertex and index buffers, unless the previous draw left them bound:
    // meshes are often suballocated from shared buffers. A mesh with morph
    // targets draws this frame's blended vertices.
    VkBuffer vertexBuffer = mesh.vertexHandle();
    if (const auto* morph = m_registry->try_get<MorphComponent>(entity)) {
        if (VkBuffer blended = morph->bufferFor(frameIndex)) vertexBuffer = blended;
    }
    if (vertexBuffer != bound.vertex || mesh.vertexStride != bound.vertexStride) {
        const VkDeviceSize offset = 0;
        const VkDeviceSize stride = mesh.vertexStride;
        vkCmdBindVertexBuffers2(cmd, 0, 1, &vertexBuffer, &offset, nullptr, &stride);
        bound.vertex = vertexBuffer;
        bound.vertexStride = mesh.vertexStride;
    }

    if (mesh.hasIndices()) {
        const VkIndexType indexType = toVkIndexType(mesh.indexType);
        if (mesh.indexHandle() != bound.index || indexType != bound.indexType) {
            vkCmdBindIndexBuffer(cmd, mesh.indexHandle(), 0, indexType);
            bound.index = mesh.indexHandle();
            bound.indexType = indexType;
        }
        const uint32_t indexCount = lod ? lod->indexCount : mesh.indexCount;
        vkCmdDrawIndexed(cmd, indexCount, instanceCount, lod ? lod->firstIndex : 0, 0, 0);
        FrameGraph::countDraw(indexCount * instanceCount);
    } else {
        vkCmdDraw(cmd, mesh.vertexCount, instanceCount, 0, 0);
        FrameGraph::countDraw(mesh.vertexCount * instanceCount);
    }
}

// ============================================================================
// Instanced drawing
// ============================================================================

uint32_t FrameGraphRenderer::drawInstanced(
    const std::vector<RenderableEntity>& entities,
    bool keepOrder,
    const FrameGraph::CompiledPass& pass,
    FrameGraph::RenderGraph::EntityBindingPlan& plan,
    BoundBuffers& bound,
    VkCommandBuffer cmd,
    uint32_t frameIndex)
{
    if (entities.empty()) return 0;

    // Draws share a batch when they bind the same geometry and material
    // (RenderableEntity::batchKey, made once a frame).
    std::vector<uint64_t> keys(entities.size());
    for (size_t i = 0; i < entities.size(); ++i) keys[i] = entities[i].batchKey;

    std::vector<uint32_t> order;
    const std::vector<InstanceBatch> batches = batchDraws(keys, keepOrder, order);
    std::vector<InstanceRow> rows(order.size());
    for (size_t i = 0; i < order.size(); ++i) {
        const ECS::TransformComponent& transform = *entities[order[i]].transform;
        rows[i] = InstanceRow{transform.worldMatrix, transform.previousWorldMatrix};
    }
    const auto [buffer, firstRow] = writeInstanceRows(rows, frameIndex);
    if (buffer == VK_NULL_HANDLE) return 0;

    const VkDeviceSize stride = sizeof(InstanceRow);
    for (const InstanceBatch& batch : batches) {
        const VkDeviceSize offset = (firstRow + batch.first) * stride;
        vkCmdBindVertexBuffers2(cmd, kInstanceBinding, 1, &buffer, &offset, nullptr, &stride);
        const RenderableEntity& first = entities[order[batch.first]];
        bindAndDrawEntity(first.entity, *first.mesh, first.lod, first.materialHash, pass, plan, bound, cmd, frameIndex,
                          batch.count);
    }
    return static_cast<uint32_t>(batches.size());
}

std::pair<VkBuffer, uint32_t> FrameGraphRenderer::writeInstanceRows(const std::vector<InstanceRow>& rows,
                                                                     uint32_t frameIndex) {
    const uint32_t frames = std::max(m_renderGraph.getMaxFramesInFlight(), 1u);
    if (m_instanceFrames.size() != frames) m_instanceFrames.resize(frames);
    InstanceFrame& frame = m_instanceFrames[frameIndex % frames];
    // This frame's fence has passed: its rows from the last time round are done with.
    if (frame.frameNumber != m_renderGraph.getFrameNumber()) {
        frame.frameNumber = m_renderGraph.getFrameNumber();
        for (InstanceChunk& chunk : frame.chunks) chunk.used = 0;
    }

    const uint32_t count = static_cast<uint32_t>(rows.size());
    InstanceChunk* target = nullptr;
    for (InstanceChunk& chunk : frame.chunks) {
        if (chunk.capacity - chunk.used >= count) {
            target = &chunk;
            break;
        }
    }
    VulkanDevice& device = m_renderGraph.getDevice();
    VmaAllocator allocator = device.getAllocator().getHandle();
    if (!target) {
        InstanceChunk chunk;
        chunk.capacity = std::max(count, 8192u);
        chunk.buffer = device.getDeleteQueue().adopt(GPUResourceFactory::createBuffer(
            allocator, VkDeviceSize{chunk.capacity} * sizeof(InstanceRow), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VMA_MEMORY_USAGE_CPU_TO_GPU));
        frame.chunks.push_back(std::move(chunk));
        target = &frame.chunks.back();
    }
    if (!target->buffer || !target->buffer->isValid()) return {VK_NULL_HANDLE, 0};

    void* mapped = GPUResourceFactory::mapBuffer(allocator, *target->buffer);
    if (!mapped) return {VK_NULL_HANDLE, 0};
    const uint32_t first = target->used;
    std::memcpy(static_cast<InstanceRow*>(mapped) + first, rows.data(), rows.size() * sizeof(InstanceRow));
    vmaFlushAllocation(allocator, target->buffer->allocation, VkDeviceSize{first} * sizeof(InstanceRow),
                       VkDeviceSize{count} * sizeof(InstanceRow));
    GPUResourceFactory::unmapBuffer(allocator, *target->buffer);
    target->used += count;
    return {target->buffer->buffer, first};
}

} // namespace Shoonyakasha
