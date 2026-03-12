//
// EntityRenderExecutor.h - Entity Render Execution Engine
//
// Bridges the dot-path resolver and ECS components with the FrameGraph.
// This module provides:
//   - Automatic entity iteration for "draw_entities" passes
//   - Automatic push constant filling via dot-path resolution
//   - Automatic descriptor set binding for entity textures
//
// 道之框架 — The Way of the Framework
//

#pragma once

#include "FrameGraph/DotPathResolver.h"
#include "FrameGraph/BufferLayoutCompiler.h"
#include "ECS/RenderComponents.h"
#include "ECS/SkeletonComponents.h"
#include "GPU/GPUTypes.h"
#include "GPU/GPUResourceFactory.h"

#include <vulkan/vulkan.h>
#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <functional>
#include <memory>
#include <unordered_map>

// Forward declarations
namespace Shoonyakasha {
class VulkanDevice;
}

namespace Shoonyakasha {

// ============================================================================
// Entity Filter - For "draw_entities" execution type
// ============================================================================

enum class EntityFilter {
    All,              // All renderable entities
    Opaque,           // AlphaMode::Opaque or AlphaMode::Mask (excludes skinned)
    Transparent,      // AlphaMode::Blend (excludes skinned)
    ShadowCasters,    // Entities with castShadows = true
    Skinned,          // Skinned entities (has SkeletonComponent) - opaque
    SkinnedTransparent // Skinned transparent entities
};

enum class EntitySortMode {
    None,           // No sorting
    FrontToBack,    // Closest first (for depth prepass, opaque)
    BackToFront     // Farthest first (for transparency)
};

// ============================================================================
// Renderable Entity - A filtered, sorted entity ready for rendering
// ============================================================================

struct RenderableEntity {
    entt::entity entity;
    float distanceToCamera = 0.0f;

    // Component pointers (cached for fast access during draw)
    const MeshComponent* mesh = nullptr;
    const MaterialComponentV5* material = nullptr;
    const ECS::TransformComponent* transform = nullptr;
};

// ============================================================================
// Entity Draw Context - Passed to custom draw callbacks
// ============================================================================

struct EntityDrawContext {
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;

    const SceneContext* scene = nullptr;
    entt::registry* registry = nullptr;

    uint32_t frameIndex = 0;
    uint32_t swapchainIndex = 0;

    // Compiled layouts for push constants
    const CompiledBufferLayout* pushConstantLayout = nullptr;

    // Resolver for manual path resolution
    const DotPathResolver* pathResolver = nullptr;
};

// ============================================================================
// Custom draw callback - Called for each entity
// ============================================================================

using EntityDrawFn = std::function<void(const EntityDrawContext& ctx, const RenderableEntity& entity)>;

// ============================================================================
// EntityRenderExecutor - The entity render execution engine
// ============================================================================

class EntityRenderExecutor {
public:
    explicit EntityRenderExecutor(VulkanDevice& device);
    ~EntityRenderExecutor();

    // ─── Setup ──────────────────────────────────────────────────

    // Set the ECS registry to query entities from
    void setRegistry(entt::registry* registry) { m_registry = registry; }

    // Set the scene environment (IBL textures)
    void setEnvironment(const SceneEnvironment* env) { m_environment = env; }

    // Compile buffer layouts from JSON
    void compileBufferLayouts(const nlohmann::json& bufferLayoutsJson);

    // Get a compiled layout by name
    const CompiledBufferLayout* getBufferLayout(const std::string& name) const;

    // ─── Per-Frame Update ───────────────────────────────────────

    // Update the scene context (call once per frame, before any passes)
    void updateSceneContext(float deltaTime, VkExtent2D screenExtent);

    // Get current scene context (for custom draw callbacks)
    const SceneContext& getSceneContext() const { return m_sceneContext; }
    SceneContext& getSceneContext() { return m_sceneContext; }

    // ─── Entity Queries ─────────────────────────────────────────

    // Query and filter renderable entities
    std::vector<RenderableEntity> queryEntities(
        EntityFilter filter,
        EntitySortMode sortMode = EntitySortMode::None
    ) const;

    // ─── Automatic Draw Loop ────────────────────────────────────

    // Execute automatic draw loop for "draw_entities" pass
    // This is the main entry point for declarative entity rendering
    void executeDrawEntities(
        VkCommandBuffer commandBuffer,
        VkPipelineLayout pipelineLayout,
        const std::string& pushConstantLayoutName,
        EntityFilter filter,
        EntitySortMode sortMode,
        uint32_t frameIndex,
        uint32_t materialDescriptorSetIndex = 1  // Which descriptor set to bind textures to
    );

    // Execute with custom draw callback (for special rendering needs)
    void executeDrawEntitiesCustom(
        VkCommandBuffer commandBuffer,
        VkPipelineLayout pipelineLayout,
        const std::string& pushConstantLayoutName,
        EntityFilter filter,
        EntitySortMode sortMode,
        uint32_t frameIndex,
        EntityDrawFn customDrawFn
    );

    // ─── Push Constant Helpers ──────────────────────────────────

    // Fill a push constant buffer for an entity
    void fillPushConstants(
        void* buffer,
        const CompiledBufferLayout& layout,
        entt::entity entity
    ) const;

    // Push constants directly to command buffer
    void pushConstants(
        VkCommandBuffer commandBuffer,
        VkPipelineLayout pipelineLayout,
        const CompiledBufferLayout& layout,
        entt::entity entity,
        VkShaderStageFlags stages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT
    ) const;

    // ─── Descriptor Set Helpers ─────────────────────────────────

    // Get or create a descriptor set for an entity's textures
    // Uses a cache to avoid recreating sets every frame
    VkDescriptorSet getEntityTextureDescriptorSet(
        entt::entity entity,
        VkDescriptorSetLayout layout,
        uint32_t frameIndex
    );

    // Bind vertex and index buffers for an entity
    void bindEntityMesh(
        VkCommandBuffer commandBuffer,
        const MeshComponent& mesh
    ) const;

    // Draw an entity (issues the draw call)
    void drawEntity(
        VkCommandBuffer commandBuffer,
        const MeshComponent& mesh
    ) const;

    // ─── Default Textures ───────────────────────────────────────

    // Get default/fallback textures (created on first access)
    const GPUResourceFactory::DefaultTextures& getDefaultTextures();

    // ─── Path Resolution ────────────────────────────────────────

    const DotPathResolver& getPathResolver() const { return m_pathResolver; }
    const BufferLayoutResolver& getLayoutResolver() const { return m_layoutResolver; }

private:
    VulkanDevice& m_device;
    entt::registry* m_registry = nullptr;
    const SceneEnvironment* m_environment = nullptr;

    // Core resolvers
    DotPathResolver m_pathResolver;
    BufferLayoutResolver m_layoutResolver;
    BufferLayoutCompiler m_layoutCompiler;

    // Compiled layouts
    std::unordered_map<std::string, CompiledBufferLayout> m_bufferLayouts;

    // Per-frame scene context
    SceneContext m_sceneContext;
    uint32_t m_frameCounter = 0;

    // Default textures (lazy-initialized)
    std::unique_ptr<GPUResourceFactory::DefaultTextures> m_defaultTextures;

    // Descriptor set cache for entity textures
    // Key: (entity ID << 32) | frameIndex
    std::unordered_map<uint64_t, VkDescriptorSet> m_textureSetCache;

    // Descriptor pool for entity texture sets
    VkDescriptorPool m_textureDescriptorPool = VK_NULL_HANDLE;

    // Push constant buffer (reused across entities)
    std::vector<uint8_t> m_pushConstantBuffer;

    // ─── Internal Helpers ───────────────────────────────────────

    void createDescriptorPool();
    void destroyDescriptorPool();

    // Filter and sort entities
    bool passesFilter(const MaterialComponentV5& material,
                      const RenderableTagComponent* tag,
                      EntityFilter filter) const;

    static float computeDistanceToCamera(const glm::vec3& entityPos,
                                          const glm::vec3& cameraPos);
};

// ============================================================================
// Inline helper - Parse filter from JSON string
// ============================================================================

inline EntityFilter parseEntityFilter(const std::string& str) {
    if (str == "all") return EntityFilter::All;
    if (str == "opaque") return EntityFilter::Opaque;
    if (str == "transparent") return EntityFilter::Transparent;
    if (str == "shadow_casters") return EntityFilter::ShadowCasters;
    if (str == "skinned") return EntityFilter::Skinned;
    if (str == "skinned_transparent") return EntityFilter::SkinnedTransparent;
    return EntityFilter::All;
}

inline EntitySortMode parseEntitySortMode(const std::string& str) {
    if (str == "none") return EntitySortMode::None;
    if (str == "front_to_back") return EntitySortMode::FrontToBack;
    if (str == "back_to_front") return EntitySortMode::BackToFront;
    return EntitySortMode::None;
}

} // namespace Shoonyakasha
