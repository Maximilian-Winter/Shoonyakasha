//
// RenderTargetSaver — Save render target images to disk
//
// Screenshots of any named render target: PNG, HDR, JPG, BMP, TGA
// Synchronous one-shot capture via staging buffer readback
//

#pragma once

#include <vulkan/vulkan.h>
#include <string>
#include <vector>
#include <cstdint>

namespace Shoonyakasha {
class VulkanDevice;
class Logger;
}

namespace Shoonyakasha {
namespace FrameGraph {

class RenderTargetSaver {
public:
    explicit RenderTargetSaver(VulkanDevice& device);

    /// Save a GPU image to disk (synchronous, blocks until complete)
    /// Format is auto-detected from file extension:
    ///   .png  - 8-bit RGBA
    ///   .jpg  - 8-bit RGB (quality 90)
    ///   .bmp  - 8-bit RGBA
    ///   .tga  - 8-bit RGBA
    ///   .hdr  - 32-bit float RGBA
    bool save(VkImage image, VkFormat format, VkExtent2D extent,
              VkImageLayout currentLayout, const std::string& path,
              Logger* logger = nullptr);

private:
    VulkanDevice& m_device;

    // Convert GPU pixel data to 8-bit RGBA for PNG/JPG/BMP/TGA output
    std::vector<uint8_t> convertToRGBA8(const void* data, VkFormat srcFormat,
                                         uint32_t width, uint32_t height);

    // Convert GPU pixel data to float RGBA for HDR output
    std::vector<float> convertToRGBAFloat(const void* data, VkFormat srcFormat,
                                           uint32_t width, uint32_t height);

    // Get bytes per pixel for a given VkFormat
    uint32_t getBytesPerPixel(VkFormat format);

    // Determine if format is a depth format
    bool isDepthFormat(VkFormat format);

    // Get file extension (lowercase)
    static std::string getExtension(const std::string& path);
};

} // namespace FrameGraph
} // namespace Shoonyakasha

// Backward compatibility alias
namespace FrameGraph = Shoonyakasha::FrameGraph;
