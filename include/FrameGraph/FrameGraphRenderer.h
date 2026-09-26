//
// FrameGraphRenderer.h - Automatic Entity Rendering from Compiled Frame Graph
//
// 虚空之舞 — The Dance of Emptiness
//
// The renderer reads everything from CompiledPass. The pass's entityDataBinding
// references binding configurations resolved at compile time from the JSON pipeline.
//

#pragma once

#include "Vulkan/FrameGraph/FrameGraph.h"
#include "ECS/RenderComponents.h"
#include "ECS/SkeletonComponents.h"
#include "ECS/Sprite2DComponents.h"
#include "ECS/Core.h"
#include "FrameGraph/ViewCulling.h"

#include <vulkan/vulkan.h>
#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <unordered_map>

// Forward declarations for FrameGraph types (now under Shoonyakasha::FrameGraph)
namespace Shoonyakasha { namespace FrameGraph {
    class RenderGraph;
    struct CompiledPass;
    struct PassDeclaration;
} }

namespace Shoonyakasha {

// ============================================================================
// Entity Filter - What entities to render
// ============================================================================

enum class EntityFilter {
    All,              // All renderable entities
    Opaque,           // AlphaMode::Opaque or AlphaMode::Mask (excludes skinned)
    Transparent,      // AlphaMode::Blend (excludes skinned)
    ShadowCasters,    // castShadows = true, opaque or masked (excludes skinned)
    Skinned,          // Skinned entities (has SkeletonComponent) - opaque
    SkinnedTransparent, // Skinned transparent entities
    SkinnedShadowCasters, // Skinned, castShadows = true, opaque or masked
    Sprite2D          // Entities with Sprite2DComponent (sprites/UI panels)
};

// ============================================================================
// Entity Sort Mode - How to order the draw calls
// ============================================================================

enum class EntitySortMode {
    None,           // No sorting (render in iteration order)
    FrontToBack,    // Closest to camera first (depth prepass, opaque)
    BackToFront,    // Farthest from camera first (transparency)
    SortKey         // Ascending RenderableTagComponent::sortKey (2D layering)
};

// ============================================================================
// Renderable Entity - Query result with pointers to components
// ============================================================================

struct RenderableEntity {
    entt::entity entity = entt::null;
    float distanceToCamera = 0.0f;

    // Component pointers (valid during the frame)
    const MeshComponent* mesh = nullptr;
    const MaterialComponentV5* material = nullptr;
    const ECS::TransformComponent* transform = nullptr;
    const RenderableTagComponent* tag = nullptr;
};

// ============================================================================
// FrameGraphRenderer - Executes Geometry Passes from Compiled Frame Graph
// ============================================================================
//
// This class takes a compiled RenderGraph and executes geometry passes.
// It reads ALL binding information from the CompiledPass - no strings needed!
//
// The execution type in the pass determines what to render:
//   "opaque_geometry"      -> render opaque entities, sort front-to-back
//   "transparent_geometry" -> render transparent entities, sort back-to-front
//   "shadow_casters"       -> render static shadow-casting entities
//   "skinned_shadow_casters" -> render skinned shadow-casting entities
//
// The entityDataBinding in the pass determines how to bind:
//   pass.entityDataBinding.perDraw.layoutRef   -> push constant layout
//   pass.entityDataBinding.material.layoutRef  -> material texture layout
//

class FrameGraphRenderer {
public:
    // ── Construction ────────────────────────────────────────────────

    explicit FrameGraphRenderer(FrameGraph::RenderGraph& renderGraph);
    ~FrameGraphRenderer() = default;

    // Non-copyable, movable
    FrameGraphRenderer(const FrameGraphRenderer&) = delete;
    FrameGraphRenderer& operator=(const FrameGraphRenderer&) = delete;
    FrameGraphRenderer(FrameGraphRenderer&&) = default;
    FrameGraphRenderer& operator=(FrameGraphRenderer&&) = default;

    // ── Configuration ───────────────────────────────────────────────

    /// Set the ECS registry to query entities from
    void setRegistry(entt::registry* registry) { m_registry = registry; }

    /// Set camera position for distance calculations (optional - uses scene context if not set)
    void setCameraPosition(const glm::vec3& pos) { m_cameraPosition = pos; m_hasCameraPosition = true; }
    void clearCameraPosition() { m_hasCameraPosition = false; }

    // ── The Main Event - Execute from CompiledPass ──────────────────
    //
    // This is the KEY method - it reads EVERYTHING from the compiled pass!
    // No strings, no hardcoded JSON knowledge!
    //

    /// Execute a geometry pass using data from CompiledPass
    ///
    /// @param pass The compiled pass (contains entityDataBinding, pipelineLayout, etc.)
    /// @param passDecl The pass declaration (contains execution.type, execution.sortMode)
    /// @param cmd Command buffer to record into
    /// @param frameIndex Current frame index for descriptor sets
    /// @return Number of draw calls issued
    uint32_t executeGeometryPass(
        const FrameGraph::CompiledPass& pass,
        const FrameGraph::PassDeclaration& passDecl,
        VkCommandBuffer cmd,
        uint32_t frameIndex
    );

    // ── Views ───────────────────────────────────────────────────────

    /// What a pass culls and sorts against: a frustum, and where distances
    /// for sorting are measured from.
    struct ViewCull {
        bool      cull = false;           // test bounds against `frustum`
        Frustum   frustum;
        bool      alongDirection = false; // sort by depth along `direction`, not distance
        glm::vec3 origin{0.0f};           // for distance sorting
        glm::vec3 direction{0.0f};        // for depth sorting (a sun's travel direction)
    };

    /// Resolve execution.view for this frame. A view that cannot be resolved
    /// (no main camera, no casting sun) culls nothing.
    ViewCull resolveView(const FrameGraph::PassDeclaration& passDecl) const;

    /// Whether an entity with these bounds and world matrix is inside the
    /// view. Entities without bounds are always inside.
    static bool isVisible(const ViewCull& view, const MeshComponent& mesh, const glm::mat4& world);

    // ── Query API ───────────────────────────────────────────────────

    /// Query entities matching the filter, optionally sorted, and
    /// restricted to entities whose RenderableTagComponent::renderLayerMask
    /// intersects renderLayerMask (bitwise AND != 0). Default matches
    /// every layer, so passes that don't set "renderLayerMask" in JSON see
    /// no behavior change. Useful for custom rendering or debugging.
    /// alphaFilter further narrows by material alpha mode (JSON
    /// execution "alphaFilter").
    /// With `view`, entities outside it are left out, except skinned ones,
    /// whose animated extent their bind-pose bounds do not cover, and
    /// distances are measured in that view.
    std::vector<RenderableEntity> queryEntities(
        EntityFilter filter,
        EntitySortMode sortMode,
        uint32_t renderLayerMask = 0xFFFFFFFFu,
        FrameGraph::AlphaFilter alphaFilter = FrameGraph::AlphaFilter::Any,
        const ViewCull* view = nullptr
    ) const;

    // ── Filtering ───────────────────────────────────────────────────

    /// Whether an entity is drawn by a pass using `filter`.
    static bool passesFilter(const MaterialComponentV5& material,
                             const RenderableTagComponent& tag,
                             EntityFilter filter,
                             bool hasSkeleton,
                             bool isSprite2D);

    /// Whether a material's alpha mode is accepted by `alphaFilter`.
    static bool passesAlphaFilter(const MaterialComponentV5& material,
                                  FrameGraph::AlphaFilter alphaFilter);

    // ── Statistics ──────────────────────────────────────────────────

    uint32_t getLastDrawCount() const { return m_lastDrawCount; }
    uint32_t getLastQueryCount() const { return m_lastQueryCount; }
    uint32_t getLastCulledCount() const { return m_lastCulledCount; }

    /// Entities drawn and culled by a pass the last time it ran.
    struct PassDrawStats {
        uint32_t drawn = 0;
        uint32_t culled = 0;
    };
    const PassDrawStats* getPassDrawStats(const std::string& passName) const {
        auto it = m_passStats.find(passName);
        return it == m_passStats.end() ? nullptr : &it->second;
    }

private:
    FrameGraph::RenderGraph& m_renderGraph;
    entt::registry* m_registry = nullptr;

    // Camera position (optional override)
    glm::vec3 m_cameraPosition{0.0f};
    bool m_hasCameraPosition = false;

    // Statistics
    mutable uint32_t m_lastDrawCount = 0;
    mutable uint32_t m_lastQueryCount = 0;
    mutable uint32_t m_lastCulledCount = 0;
    std::unordered_map<std::string, PassDrawStats> m_passStats;

    // ── Internal Helpers ────────────────────────────────────────────

    /// Convert execution.type string to EntityFilter
    EntityFilter executionTypeToFilter(const std::string& type) const;

    /// Convert execution.sortMode string to EntitySortMode
    EntitySortMode sortModeStringToEnum(const std::string& mode) const;

    /// Get camera position (from override or scene context)
    glm::vec3 getCameraPosition() const;

    /// Calculate distance from entity to camera, or within `view` if given
    float calculateDistance(const ECS::TransformComponent& transform, const ViewCull* view = nullptr) const;

    /// Bind and draw a single entity using CompiledPass data
    void bindAndDrawEntity(
        entt::entity entity,
        const MeshComponent& mesh,
        const FrameGraph::CompiledPass& pass,
        VkCommandBuffer cmd,
        uint32_t frameIndex
    );
};

// ============================================================================
// Inline Implementations
// ============================================================================

inline FrameGraphRenderer::FrameGraphRenderer(FrameGraph::RenderGraph& renderGraph)
    : m_renderGraph(renderGraph)
{
}

inline EntityFilter FrameGraphRenderer::executionTypeToFilter(const std::string& type) const {
    if (type == "opaque_geometry") return EntityFilter::Opaque;
    if (type == "transparent_geometry") return EntityFilter::Transparent;
    if (type == "shadow_casters") return EntityFilter::ShadowCasters;
    if (type == "skinned_geometry") return EntityFilter::Skinned;
    if (type == "skinned_transparent") return EntityFilter::SkinnedTransparent;
    if (type == "skinned_shadow_casters") return EntityFilter::SkinnedShadowCasters;
    if (type == "sprite_geometry") return EntityFilter::Sprite2D;
    return EntityFilter::All;
}

inline EntitySortMode FrameGraphRenderer::sortModeStringToEnum(const std::string& mode) const {
    if (mode == "front_to_back") return EntitySortMode::FrontToBack;
    if (mode == "back_to_front") return EntitySortMode::BackToFront;
    if (mode == "sort_key") return EntitySortMode::SortKey;
    return EntitySortMode::None;
}

inline bool FrameGraphRenderer::passesFilter(
    const MaterialComponentV5& material,
    const RenderableTagComponent& tag,
    EntityFilter filter,
    bool hasSkeleton,
    bool isSprite2D)
{
    if (!tag.shouldRender()) return false;

    switch (filter) {
        case EntityFilter::All:
            return true;
        case EntityFilter::Opaque:
            return !hasSkeleton && !isSprite2D && material.isOpaqueOrMasked();
        case EntityFilter::Transparent:
            return !hasSkeleton && !isSprite2D && material.isTransparent();
        case EntityFilter::ShadowCasters:
            // Skinned casters need the skinned vertex shader, so they have
            // their own type rather than casting bind-pose shadows here.
            return !hasSkeleton && !isSprite2D && tag.castShadows && material.isOpaqueOrMasked();
        case EntityFilter::Skinned:
            return hasSkeleton && material.isOpaqueOrMasked();
        case EntityFilter::SkinnedTransparent:
            return hasSkeleton && material.isTransparent();
        case EntityFilter::SkinnedShadowCasters:
            return hasSkeleton && tag.castShadows && material.isOpaqueOrMasked();
        case EntityFilter::Sprite2D:
            return isSprite2D;
    }
    return false;
}

inline bool FrameGraphRenderer::passesAlphaFilter(
    const MaterialComponentV5& material,
    FrameGraph::AlphaFilter alphaFilter)
{
    switch (alphaFilter) {
        case FrameGraph::AlphaFilter::Any:    return true;
        case FrameGraph::AlphaFilter::Opaque: return material.isOpaque();
        case FrameGraph::AlphaFilter::Mask:   return material.isMasked();
    }
    return false;
}

inline float FrameGraphRenderer::calculateDistance(const ECS::TransformComponent& transform,
                                                  const ViewCull* view) const {
    glm::vec3 entityPos = glm::vec3(transform.worldMatrix[3]);
    if (view && view->alongDirection) {
        return glm::dot(entityPos, view->direction);  // depth along the light
    }
    if (view) {
        glm::vec3 diff = entityPos - view->origin;
        return glm::dot(diff, diff);
    }
    glm::vec3 camPos = getCameraPosition();
    glm::vec3 diff = entityPos - camPos;
    return glm::dot(diff, diff);  // Squared distance (faster, same ordering)
}

} // namespace Shoonyakasha
