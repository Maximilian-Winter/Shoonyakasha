//
// EntityRenderExecutor.cpp - Entity Render Execution Engine Implementation
//

#include "FrameGraph/EntityRenderExecutor.h"
#include "ECS/SkeletonComponents.h"
#include "Vulkan/VulkanDevice.h"
#include "ECS/Core.h"

#include <algorithm>
#include <stdexcept>

namespace Shoonyakasha {

// ============================================================================
// Constructor / Destructor
// ============================================================================

EntityRenderExecutor::EntityRenderExecutor(VulkanDevice& device)
    : m_device(device)
    , m_layoutResolver(m_pathResolver)
{
    m_pushConstantBuffer.resize(256);  // Max push constant size
}

EntityRenderExecutor::~EntityRenderExecutor() {
    destroyDescriptorPool();

    // Clean up default textures
    if (m_defaultTextures) {
        GPUResourceFactory::destroyDefaultTextures(
            m_device.getAllocator().getHandle(),
            m_device.getLogicalDevice(),
            *m_defaultTextures
        );
    }
}

// ============================================================================
// Setup
// ============================================================================

void EntityRenderExecutor::compileBufferLayouts(const nlohmann::json& bufferLayoutsJson) {
    m_bufferLayouts = m_layoutCompiler.compileAll(bufferLayoutsJson);
}

const CompiledBufferLayout* EntityRenderExecutor::getBufferLayout(const std::string& name) const {
    auto it = m_bufferLayouts.find(name);
    return it != m_bufferLayouts.end() ? &it->second : nullptr;
}

// ============================================================================
// Per-Frame Update
// ============================================================================

void EntityRenderExecutor::updateSceneContext(float deltaTime, VkExtent2D screenExtent) {
    // Update time
    m_sceneContext.timeDelta = deltaTime;
    m_sceneContext.timeElapsed += deltaTime;
    m_sceneContext.timeFrame = m_frameCounter++;

    // Update screen dimensions
    m_sceneContext.screenWidth = static_cast<float>(screenExtent.width);
    m_sceneContext.screenHeight = static_cast<float>(screenExtent.height);

    // Update environment reference
    m_sceneContext.environment = m_environment;

    // Update camera from ECS
    if (m_registry) {
        m_sceneContext.updateFromRegistry(*m_registry);
    }
}

// ============================================================================
// Entity Queries
// ============================================================================

std::vector<RenderableEntity> EntityRenderExecutor::queryEntities(
    EntityFilter filter,
    EntitySortMode sortMode
) const {
    std::vector<RenderableEntity> result;

    if (!m_registry) {
        return result;
    }

    // Query all entities with required components
    auto view = m_registry->view<MeshComponent, MaterialComponentV5, ECS::TransformComponent>();

    for (auto entity : view) {
        const auto& mesh = view.get<MeshComponent>(entity);
        const auto& material = view.get<MaterialComponentV5>(entity);
        const auto& transform = view.get<ECS::TransformComponent>(entity);

        // Check for optional tag component
        auto* tag = m_registry->try_get<RenderableTagComponent>(entity);

        // Skip invisible entities
        if (tag && !tag->shouldRender()) {
            continue;
        }

        // Skip invalid meshes
        if (!mesh.isValid()) {
            continue;
        }

        // Check skeleton presence for skinned/non-skinned filtering
        bool hasSkeleton = m_registry->all_of<SkeletonComponent>(entity);

        // For skinned filters, require SkeletonComponent
        if (filter == EntityFilter::Skinned || filter == EntityFilter::SkinnedTransparent) {
            if (!hasSkeleton) continue;
        }
        // For non-skinned opaque/transparent, exclude entities with SkeletonComponent
        else if (filter == EntityFilter::Opaque || filter == EntityFilter::Transparent) {
            if (hasSkeleton) continue;
        }

        // Apply material-based filter
        if (!passesFilter(material, tag, filter)) {
            continue;
        }

        RenderableEntity renderable;
        renderable.entity = entity;
        renderable.mesh = &mesh;
        renderable.material = &material;
        renderable.transform = &transform;

        // Compute distance for sorting
        if (sortMode != EntitySortMode::None) {
            renderable.distanceToCamera = computeDistanceToCamera(
                transform.position, m_sceneContext.cameraPosition
            );
        }

        result.push_back(renderable);
    }

    // Sort if needed
    if (sortMode == EntitySortMode::FrontToBack) {
        std::sort(result.begin(), result.end(),
            [](const RenderableEntity& a, const RenderableEntity& b) {
                return a.distanceToCamera < b.distanceToCamera;
            });
    } else if (sortMode == EntitySortMode::BackToFront) {
        std::sort(result.begin(), result.end(),
            [](const RenderableEntity& a, const RenderableEntity& b) {
                return a.distanceToCamera > b.distanceToCamera;
            });
    }

    return result;
}

bool EntityRenderExecutor::passesFilter(
    const MaterialComponentV5& material,
    const RenderableTagComponent* tag,
    EntityFilter filter
) const {
    switch (filter) {
        case EntityFilter::All:
            return true;

        case EntityFilter::Opaque:
            return material.isOpaqueOrMasked();

        case EntityFilter::Transparent:
            return material.isTransparent();

        case EntityFilter::ShadowCasters:
            return tag ? tag->castShadows : true;

        case EntityFilter::Skinned:
            return material.isOpaqueOrMasked();  // Skeleton check is done in queryEntities

        case EntityFilter::SkinnedTransparent:
            return material.isTransparent();

        default:
            return true;
    }
}

float EntityRenderExecutor::computeDistanceToCamera(
    const glm::vec3& entityPos,
    const glm::vec3& cameraPos
) {
    glm::vec3 diff = entityPos - cameraPos;
    return glm::dot(diff, diff);  // Squared distance for faster sorting
}

// ============================================================================
// Automatic Draw Loop
// ============================================================================

void EntityRenderExecutor::executeDrawEntities(
    VkCommandBuffer commandBuffer,
    VkPipelineLayout pipelineLayout,
    const std::string& pushConstantLayoutName,
    EntityFilter filter,
    EntitySortMode sortMode,
    uint32_t frameIndex,
    uint32_t materialDescriptorSetIndex
) {
    // Get compiled push constant layout
    const CompiledBufferLayout* layout = getBufferLayout(pushConstantLayoutName);
    if (!layout) {
        // No layout found - skip push constants
        layout = nullptr;
    }

    // Query entities
    auto entities = queryEntities(filter, sortMode);

    // Check if this is a skinned geometry pass
    bool isSkinnedPass = (filter == EntityFilter::Skinned || filter == EntityFilter::SkinnedTransparent);

    // Draw each entity
    for (const auto& renderable : entities) {
        // Push constants
        if (layout) {
            pushConstants(commandBuffer, pipelineLayout, *layout, renderable.entity);
        }

        // For skinned entities, bind the bone SSBO descriptor set
        // The descriptor set for bone matrices is managed by the caller (FrameGraphRenderer)
        // via the autoBindBuffer mechanism in the JSON descriptor set layout.

        // TODO: Bind material textures to descriptor set
        // This requires descriptor set caching per entity

        // Bind mesh
        bindEntityMesh(commandBuffer, *renderable.mesh);

        // Draw
        drawEntity(commandBuffer, *renderable.mesh);
    }
}

void EntityRenderExecutor::executeDrawEntitiesCustom(
    VkCommandBuffer commandBuffer,
    VkPipelineLayout pipelineLayout,
    const std::string& pushConstantLayoutName,
    EntityFilter filter,
    EntitySortMode sortMode,
    uint32_t frameIndex,
    EntityDrawFn customDrawFn
) {
    if (!customDrawFn) {
        return;
    }

    const CompiledBufferLayout* layout = getBufferLayout(pushConstantLayoutName);

    // Build draw context
    EntityDrawContext ctx;
    ctx.commandBuffer = commandBuffer;
    ctx.pipelineLayout = pipelineLayout;
    ctx.scene = &m_sceneContext;
    ctx.registry = m_registry;
    ctx.frameIndex = frameIndex;
    ctx.pushConstantLayout = layout;
    ctx.pathResolver = &m_pathResolver;

    // Query entities
    auto entities = queryEntities(filter, sortMode);

    // Call custom draw for each entity
    for (const auto& renderable : entities) {
        customDrawFn(ctx, renderable);
    }
}

// ============================================================================
// Push Constant Helpers
// ============================================================================

void EntityRenderExecutor::fillPushConstants(
    void* buffer,
    const CompiledBufferLayout& layout,
    entt::entity entity
) const {
    if (!m_registry) {
        std::memset(buffer, 0, layout.totalSize);
        return;
    }

    m_layoutResolver.fillEntityBuffer(buffer, layout, m_sceneContext, entity, *m_registry);
}

void EntityRenderExecutor::pushConstants(
    VkCommandBuffer commandBuffer,
    VkPipelineLayout pipelineLayout,
    const CompiledBufferLayout& layout,
    entt::entity entity,
    VkShaderStageFlags stages
) const {
    // Ensure buffer is large enough
    if (m_pushConstantBuffer.size() < layout.totalSize) {
        const_cast<std::vector<uint8_t>&>(m_pushConstantBuffer).resize(layout.totalSize);
    }

    // Fill the buffer
    fillPushConstants(const_cast<uint8_t*>(m_pushConstantBuffer.data()), layout, entity);

    // Push to GPU
    vkCmdPushConstants(
        commandBuffer,
        pipelineLayout,
        stages,
        0,  // offset
        layout.totalSize,
        m_pushConstantBuffer.data()
    );
}

// ============================================================================
// Mesh Binding and Drawing
// ============================================================================

void EntityRenderExecutor::bindEntityMesh(
    VkCommandBuffer commandBuffer,
    const MeshComponent& mesh
) const {
    // Bind vertex buffer
    VkBuffer vertexBuffers[] = { mesh.vertexBuffer.buffer };
    VkDeviceSize offsets[] = { 0 };
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);

    // Bind index buffer if present
    if (mesh.hasIndices()) {
        vkCmdBindIndexBuffer(
            commandBuffer,
            mesh.indexBuffer.buffer,
            0,
            toVkIndexType(mesh.indexType)
        );
    }
}

void EntityRenderExecutor::drawEntity(
    VkCommandBuffer commandBuffer,
    const MeshComponent& mesh
) const {
    if (mesh.hasIndices()) {
        vkCmdDrawIndexed(commandBuffer, mesh.indexCount, 1, 0, 0, 0);
    } else {
        vkCmdDraw(commandBuffer, mesh.vertexCount, 1, 0, 0);
    }
}

// ============================================================================
// Default Textures
// ============================================================================

const GPUResourceFactory::DefaultTextures& EntityRenderExecutor::getDefaultTextures() {
    if (!m_defaultTextures) {
        m_defaultTextures = std::make_unique<GPUResourceFactory::DefaultTextures>(
            GPUResourceFactory::createDefaultTextures(
                m_device.getAllocator().getHandle(),
                m_device.getLogicalDevice(),
                m_device.getGraphicsQueue(),
                m_device.getCommandPool()
            )
        );
    }
    return *m_defaultTextures;
}

// ============================================================================
// Descriptor Set Management
// ============================================================================

void EntityRenderExecutor::createDescriptorPool() {
    if (m_textureDescriptorPool != VK_NULL_HANDLE) {
        return;  // Already created
    }

    // Pool sizes for texture and storage buffer descriptor sets
    VkDescriptorPoolSize poolSizes[] = {
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 500 }  // For bone SSBO per-entity descriptors
    };

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 2;
    poolInfo.pPoolSizes = poolSizes;
    poolInfo.maxSets = 1000;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;

    if (vkCreateDescriptorPool(m_device.getLogicalDevice(), &poolInfo, nullptr, &m_textureDescriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create entity texture descriptor pool");
    }
}

void EntityRenderExecutor::destroyDescriptorPool() {
    if (m_textureDescriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(m_device.getLogicalDevice(), m_textureDescriptorPool, nullptr);
        m_textureDescriptorPool = VK_NULL_HANDLE;
    }
    m_textureSetCache.clear();
}

VkDescriptorSet EntityRenderExecutor::getEntityTextureDescriptorSet(
    entt::entity entity,
    VkDescriptorSetLayout layout,
    uint32_t frameIndex
) {
    // Ensure pool exists
    createDescriptorPool();

    // Cache key: entity ID combined with frame index
    uint64_t key = (static_cast<uint64_t>(entt::to_integral(entity)) << 16) | frameIndex;

    auto it = m_textureSetCache.find(key);
    if (it != m_textureSetCache.end()) {
        return it->second;
    }

    // Allocate new descriptor set
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_textureDescriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &layout;

    VkDescriptorSet descriptorSet;
    if (vkAllocateDescriptorSets(m_device.getLogicalDevice(), &allocInfo, &descriptorSet) != VK_SUCCESS) {
        // Pool might be full - return null for now
        // TODO: Handle pool exhaustion by creating additional pools
        return VK_NULL_HANDLE;
    }

    // Get entity's material
    if (!m_registry) {
        return VK_NULL_HANDLE;
    }

    auto* material = m_registry->try_get<MaterialComponentV5>(entity);
    if (!material) {
        return VK_NULL_HANDLE;
    }

    // Update descriptor set with material textures
    // TODO: This needs to match the descriptor set layout
    // For now, we'll just write what textures exist

    std::vector<VkDescriptorImageInfo> imageInfos;
    std::vector<VkWriteDescriptorSet> writes;

    uint32_t binding = 0;
    const GPUResourceFactory::DefaultTextures& defaults = getDefaultTextures();

    // Common texture slots
    const std::vector<std::pair<std::string, const GPUTexture*>> textureSlots = {
        {"albedoMap", &defaults.white},
        {"normalMap", &defaults.normal},
        {"metallicRoughnessMap", &defaults.metallicRoughness}
    };

    for (const auto& [name, defaultTexture] : textureSlots) {
        const GPUTexture* texture = defaultTexture;

        // Check if material has this texture
        auto it = material->textures.find(name);
        if (it != material->textures.end() && it->second.isValid()) {
            texture = &it->second;
        }

        VkDescriptorImageInfo imageInfo{};
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfo.imageView = texture->view;
        imageInfo.sampler = texture->sampler;
        imageInfos.push_back(imageInfo);

        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = descriptorSet;
        write.dstBinding = binding++;
        write.dstArrayElement = 0;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.descriptorCount = 1;
        write.pImageInfo = &imageInfos.back();
        writes.push_back(write);
    }

    if (!writes.empty()) {
        vkUpdateDescriptorSets(m_device.getLogicalDevice(),
                               static_cast<uint32_t>(writes.size()), writes.data(),
                               0, nullptr);
    }

    m_textureSetCache[key] = descriptorSet;
    return descriptorSet;
}

} // namespace Shoonyakasha
