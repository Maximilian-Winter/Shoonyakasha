//
// UIRenderer.h - Drawing canvas UI batches with Vulkan
//
// Owns the glyph atlas texture, a ring buffer for each frame's vertices,
// indices and atlas uploads, a pipeline per attachment format, and a texture
// for each world canvas. Each frame prepareFrame() runs before the render
// graph records its passes: it uploads, then renders the world canvases into
// their textures. A "ui_canvas" pass then calls drawOverlays() with rendering
// already begun.
//

#pragma once

#include "ECS/RenderComponents.h"
#include "GPU/GPUTypes.h"
#include "UI/UIBatchBuilder.h"

#include <entt/entt.hpp>
#include <vulkan/vulkan.h>

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

namespace Shoonyakasha {

class FrameRingBuffer;
class Logger;
class VulkanDevice;
class VulkanPipeline;
namespace FrameGraph { struct PassExecuteContext; }

namespace UI {

class GlyphCache;

class UIRenderer {
public:
    /// Called with a world canvas texture's view before the texture is
    /// destroyed, once no frame in flight samples it.
    using TextureReleaser = std::function<void(VkImageView)>;

    static constexpr VkFormat kWorldFormat = VK_FORMAT_R8G8B8A8_SRGB;

    /// `shaderDirectory` holds ui.vert.spv and ui.frag.spv. The atlas is
    /// `atlasSize` texels square, as the GlyphCache that feeds it.
    UIRenderer(VulkanDevice& device, std::string shaderDirectory, uint32_t framesInFlight,
               int atlasSize, Logger* logger = nullptr);
    ~UIRenderer();

    UIRenderer(const UIRenderer&) = delete;
    UIRenderer& operator=(const UIRenderer&) = delete;

    void setTextureReleaser(TextureReleaser releaser) { m_releaser = std::move(releaser); }

    /// Copies the glyphs queued in `glyphs` into the atlas, clears the queue,
    /// writes the vertices and indices of `drawData`, and renders its world
    /// canvases into their textures, which it creates, resizes and destroys
    /// as the canvases come and go. Each world canvas's UICanvas::target, and
    /// the texture slot of its MaterialComponentV5, name its texture. Records
    /// into `cmd` before any pass that draws or samples UI, after frame
    /// `frameIndex`'s fence was waited on.
    void prepareFrame(VkCommandBuffer cmd, uint32_t frameIndex, entt::registry& registry,
                      const UIDrawData& drawData, GlyphCache& glyphs);

    /// Draws every screen overlay canvas of `drawData` into the pass's colour
    /// attachment. `drawData` is the one given to this frame's prepareFrame.
    void drawOverlays(const FrameGraph::PassExecuteContext& ctx, const UIDrawData& drawData);

    /// A unit quad in the XY plane facing +Z, from -0.5 to 0.5, in the
    /// engine's standard vertex format. UV (0, 0) is its top-left corner
    /// (-0.5, 0.5), so a canvas texture on it reads upright.
    const MeshComponent& worldQuad();

    size_t worldTextureCount() const { return m_targets.size(); }

private:
    struct PipelineKey {
        VkFormat color;
        VkFormat depth;
        bool operator<(const PipelineKey& o) const { return std::tie(color, depth) < std::tie(o.color, o.depth); }
    };
    struct WorldTarget {
        GPUTexture texture;
        std::string slot;  // material slot it was put in
    };
    struct Retired {
        GPUTexture texture;
        uint64_t frame;
    };

    VulkanPipeline* pipelineFor(VkFormat color, VkFormat depth);
    VkDescriptorSet descriptorSetFor(VkImageView view, VkSampler sampler);
    void uploadGlyphs(VkCommandBuffer cmd, GlyphCache& glyphs);
    void syncWorldTargets(entt::registry& registry, const UIDrawData& drawData);
    void retire(entt::registry& registry, entt::entity canvas, WorldTarget& target);
    void destroyRetired();
    void renderWorldCanvases(VkCommandBuffer cmd, const UIDrawData& drawData);
    void drawCanvas(VkCommandBuffer cmd, VkPipelineLayout layout, const CanvasDraw& canvas,
                    const UIDrawData& drawData, VkExtent2D extent);
    void bindGeometry(VkCommandBuffer cmd, VulkanPipeline& pipeline, VkExtent2D extent);

    VulkanDevice& m_device;
    std::string m_shaderDirectory;
    Logger* m_logger;
    uint32_t m_framesInFlight;
    uint32_t m_maxImageSize = 4096;

    GPUTexture m_atlas;
    bool m_atlasReady = false;  // cleared and in SHADER_READ_ONLY_OPTIMAL

    std::unique_ptr<FrameRingBuffer> m_ring;
    VkBuffer m_vertexBuffer = VK_NULL_HANDLE;
    VkDeviceSize m_vertexOffset = 0;
    VkBuffer m_indexBuffer = VK_NULL_HANDLE;
    VkDeviceSize m_indexOffset = 0;
    size_t m_indexCount = 0;

    VkDescriptorSetLayout m_setLayout = VK_NULL_HANDLE;
    struct FrameDescriptors {
        std::vector<VkDescriptorPool> pools;
        std::unordered_map<VkImageView, VkDescriptorSet> sets;
    };
    std::vector<FrameDescriptors> m_descriptors;  // per frame in flight
    uint32_t m_frame = 0;
    uint64_t m_frameNumber = 0;                   // prepareFrame calls so far

    std::map<PipelineKey, std::unique_ptr<VulkanPipeline>> m_pipelines;  // null: creation failed

    std::unordered_map<entt::entity, WorldTarget> m_targets;
    std::vector<Retired> m_retired;
    TextureReleaser m_releaser;

    MeshComponent m_worldQuad;
};

} // namespace UI
} // namespace Shoonyakasha
