//
// UIRenderer.h - Drawing canvas UI batches with Vulkan
//
// Owns the glyph atlas texture, a ring buffer for each frame's vertices,
// indices and atlas uploads, and a pipeline per attachment format. Each frame
// prepareFrame() runs before the render graph records its passes, and a
// "ui_canvas" pass calls drawOverlays() with rendering already begun.
//

#pragma once

#include "GPU/GPUTypes.h"
#include "UI/UIBatchBuilder.h"

#include <vulkan/vulkan.h>

#include <cstdint>
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
    /// `shaderDirectory` holds ui.vert.spv and ui.frag.spv. The atlas is
    /// `atlasSize` texels square, as the GlyphCache that feeds it.
    UIRenderer(VulkanDevice& device, std::string shaderDirectory, uint32_t framesInFlight,
               int atlasSize, Logger* logger = nullptr);
    ~UIRenderer();

    UIRenderer(const UIRenderer&) = delete;
    UIRenderer& operator=(const UIRenderer&) = delete;

    /// Copies the glyphs queued in `glyphs` into the atlas, clears the queue,
    /// and writes the vertices and indices of `drawData`. Records into `cmd`
    /// before any pass that draws UI, after frame `frameIndex`'s fence was
    /// waited on.
    void prepareFrame(VkCommandBuffer cmd, uint32_t frameIndex, const UIDrawData& drawData, GlyphCache& glyphs);

    /// Draws every screen overlay canvas of `drawData` into the pass's colour
    /// attachment. `drawData` is the one given to this frame's prepareFrame.
    void drawOverlays(const FrameGraph::PassExecuteContext& ctx, const UIDrawData& drawData);

private:
    struct PipelineKey {
        VkFormat color;
        VkFormat depth;
        bool operator<(const PipelineKey& o) const { return std::tie(color, depth) < std::tie(o.color, o.depth); }
    };

    VulkanPipeline* pipelineFor(VkFormat color, VkFormat depth);
    VkDescriptorSet descriptorSetFor(VkImageView view, VkSampler sampler);
    void uploadGlyphs(VkCommandBuffer cmd, GlyphCache& glyphs);

    VulkanDevice& m_device;
    std::string m_shaderDirectory;
    Logger* m_logger;

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

    std::map<PipelineKey, std::unique_ptr<VulkanPipeline>> m_pipelines;  // null: creation failed
};

} // namespace UI
} // namespace Shoonyakasha
