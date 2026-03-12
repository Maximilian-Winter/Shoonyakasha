//
// StagingBufferManager — Ring-buffered CPU↔GPU transfer management
//
// 輪廻之流 — The wheel of data flows between worlds
//
// Each SSBO that needs CPU involvement gets a ring of staging buffers
// (default = maxFramesInFlight). Staging buffers are persistently mapped
// for zero-overhead CPU access. Fence synchronization piggybacks on
// the application's existing per-frame fences.
//

#pragma once

#include <vulkan/vulkan.h>
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <functional>

// Forward declarations
namespace Shoonyakasha {
class VulkanDevice;
class VulkanBuffer;
class Logger;

namespace FrameGraph {
struct ReadbackResult;
struct MemoryPolicy;
struct ReadbackPolicy;
enum class BufferUpdateFrequency;
using ReadbackCallbackFn = std::function<void(const ReadbackResult&)>;
}
}

namespace Shoonyakasha {
namespace FrameGraph {

class StagingBufferManager {
public:
    StagingBufferManager(VulkanDevice& device, Logger* logger = nullptr);
    ~StagingBufferManager();

    // ── Configuration ──

    /// Per-buffer staging configuration
    struct BufferConfig {
        std::string name;               // Buffer name (matches SSBO name)
        VkBuffer gpuBuffer;             // Device-local GPU buffer handle
        VkDeviceSize size;              // Buffer size in bytes
        uint32_t elementCount;          // Number of struct elements
        uint32_t elementStride;         // Bytes per element

        // Upload (CPU→GPU) configuration
        bool hasUpload = false;
        BufferUpdateFrequency uploadFrequency;
        uint32_t uploadN = 1;           // For EveryNFrames

        // Readback (GPU→CPU) configuration
        bool hasReadback = false;
        BufferUpdateFrequency readbackFrequency;
        uint32_t readbackN = 1;         // For EveryNFrames

        uint32_t ringDepthOverride = 0; // 0 = auto (maxFramesInFlight)
    };

    /// Create staging buffers for all configured SSBOs
    void create(const std::vector<BufferConfig>& configs, uint32_t maxFramesInFlight);

    /// Destroy all staging buffers and free resources
    void destroy();

    // ── Readback Callbacks ──

    /// Register a callback invoked when readback completes for a buffer
    void registerReadbackCallback(const std::string& name, ReadbackCallbackFn cb);

    /// Trigger a one-shot readback for a buffer (Phase 4: for manual save triggers)
    void triggerReadback(const std::string& name);

    // ── Upload API (CPU→GPU) ──

    /// Write data into the upload staging buffer for the current frame
    /// Data will be copied to GPU when recordUploadCommands is called
    void writeUploadData(const std::string& name, uint32_t frameIndex, const void* data, VkDeviceSize size);

    /// Mark a buffer's upload staging as dirty (will be copied to GPU on next recordUploadCommands)
    void markUploadDirty(const std::string& name);

    // ── Command Recording ──

    /// Record upload copy commands (CPU→GPU) — call BEFORE pass execution
    void recordUploadCommands(VkCommandBuffer cmd, uint32_t frameIndex, uint64_t frameNumber);

    /// Record readback copy commands (GPU→CPU) — call AFTER pass execution
    void recordReadbackCommands(VkCommandBuffer cmd, uint32_t frameIndex, uint64_t frameNumber);

    // ── Completion Processing ──

    /// Process completed readbacks and invoke callbacks
    /// Call AFTER vkWaitForFences for the current frame (fence guarantees copy completion)
    void processCompletedReadbacks(uint32_t frameIndex, uint64_t frameNumber);

    /// Poll for completed readback results without callbacks
    std::vector<ReadbackResult> pollReadbacks();

    // ── Image Readback (render target data flow) ──

    /// Per-image staging configuration
    struct ImageConfig {
        std::string name;               // Resource name (matches resource declaration)
        VkImage gpuImage;               // GPU image handle
        VkFormat format;                // Image format
        VkExtent2D extent;              // Image dimensions
        VkDeviceSize bufferSize;        // width * height * bytesPerPixel
        VkImageAspectFlags aspectMask;  // Color or depth

        bool hasReadback = false;
        BufferUpdateFrequency readbackFrequency;
        uint32_t readbackN = 1;
        uint32_t ringDepthOverride = 0;
    };

    /// Create staging buffers for image readback
    void createImages(const std::vector<ImageConfig>& configs, uint32_t maxFramesInFlight);

    /// Update the current layout of an image (call before recordImageReadbackCommands)
    void updateImageLayout(const std::string& name, VkImageLayout layout);

    /// Record image readback commands (image → staging buffer) — call AFTER pass execution
    void recordImageReadbackCommands(VkCommandBuffer cmd, uint32_t frameIndex, uint64_t frameNumber);

    /// Process completed image readbacks and invoke callbacks
    void processCompletedImageReadbacks(uint32_t frameIndex, uint64_t frameNumber);

    /// Register callback for image readback completion
    void registerImageReadbackCallback(const std::string& name, ReadbackCallbackFn cb);

    /// Trigger a one-shot image readback
    void triggerImageReadback(const std::string& name);

private:
    VulkanDevice& m_device;
    Logger* m_logger = nullptr;

    /// Internal per-buffer staging state
    struct StagingEntry {
        std::string name;
        VkBuffer gpuBuffer = VK_NULL_HANDLE;
        VkDeviceSize size = 0;
        uint32_t elementCount = 0;
        uint32_t elementStride = 0;
        uint32_t ringDepth = 0;

        // Upload ring (CPU→GPU)
        bool hasUpload = false;
        BufferUpdateFrequency uploadFrequency;
        uint32_t uploadN = 1;
        std::vector<std::unique_ptr<VulkanBuffer>> uploadStaging;
        bool uploadDirty = false;

        // Readback ring (GPU→CPU)
        bool hasReadback = false;
        BufferUpdateFrequency readbackFrequency;
        uint32_t readbackN = 1;
        std::vector<std::unique_ptr<VulkanBuffer>> readbackStaging;
        std::vector<std::vector<uint8_t>> readbackCpuBuffers;     // CPU-side copy per slot
        std::vector<uint64_t> readbackPendingFrame;               // 0 = no pending copy
        ReadbackCallbackFn callback;
        bool readbackTriggered = false;  // Phase 4: One-shot override for manual readback
    };

    std::unordered_map<std::string, StagingEntry> m_entries;
    std::vector<ReadbackResult> m_pendingResults;  // For pollReadbacks()

    /// Check if a readback should happen this frame based on frequency policy
    bool shouldReadback(const StagingEntry& entry, uint64_t frameNumber) const;

    /// Check if an upload should happen this frame based on frequency policy
    bool shouldUpload(const StagingEntry& entry, uint64_t frameNumber) const;

    // ── Image staging internals ──

    struct ImageStagingEntry {
        std::string name;
        VkImage gpuImage = VK_NULL_HANDLE;
        VkFormat format = VK_FORMAT_UNDEFINED;
        VkExtent2D extent = {};
        VkDeviceSize bufferSize = 0;
        VkImageAspectFlags aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        VkImageLayout currentLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        uint32_t ringDepth = 0;

        bool hasReadback = false;
        BufferUpdateFrequency readbackFrequency;
        uint32_t readbackN = 1;

        std::vector<std::unique_ptr<VulkanBuffer>> readbackStaging;
        std::vector<std::vector<uint8_t>> readbackCpuBuffers;
        std::vector<uint64_t> readbackPendingFrame;
        ReadbackCallbackFn callback;
        bool readbackTriggered = false;
    };

    std::unordered_map<std::string, ImageStagingEntry> m_imageEntries;

    bool shouldImageReadback(const ImageStagingEntry& entry, uint64_t frameNumber) const;
};

} // namespace FrameGraph
} // namespace Shoonyakasha

// Backward compatibility alias
namespace FrameGraph = Shoonyakasha::FrameGraph;
