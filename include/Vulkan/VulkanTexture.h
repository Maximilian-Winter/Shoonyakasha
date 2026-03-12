//
// Created by maxim on 05.07.2024.
//

#pragma once

#include <vulkan/vulkan.h>
#include "Core/Logger.h"
#include "Core/EventSystem.h"
#include <string>

namespace Shoonyakasha {

class VulkanDevice;
class VulkanImage;
class VulkanBuffer;

class VulkanTexture {
public:
    // Standard 8-bit texture constructor
    VulkanTexture(VulkanDevice& device, const uint8_t* pixels, uint32_t width, uint32_t height,
                  uint32_t channels = 4, VkFormat format = VK_FORMAT_R8G8B8A8_SRGB);

    // HDR float texture constructor (for .hdr files)
    VulkanTexture(VulkanDevice& device, const float* pixels, uint32_t width, uint32_t height,
                  uint32_t channels = 4, VkFormat format = VK_FORMAT_R32G32B32A32_SFLOAT);

    ~VulkanTexture();

    VkImageView getImageView() const { return m_textureImageView; }
    VkSampler getSampler() const { return m_textureSampler; }
    uint32_t getWidth() const { return m_width; }
    uint32_t getHeight() const { return m_height; }
    uint32_t getChannels() const { return m_channels; }

private:
    VulkanDevice& m_device;
    VulkanImage* m_textureImage;
    VkImageView m_textureImageView;
    VkSampler m_textureSampler;
    uint32_t m_width;
    uint32_t m_height;
    uint32_t m_channels;

    Logger* m_logger;
    EventDispatcher* m_eventDispatcher;

    void createTextureImage(const uint8_t* pixels, uint32_t width, uint32_t height, VkFormat format);
    void createTextureImageView();
    void createTextureSampler();
    void transitionImageLayout(VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout);
    void copyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height);
};


// Events
struct TextureImageCreatedEvent : public Event {
    VkImage image;
    int width;
    int height;
    TextureImageCreatedEvent(VkImage i, int w, int h) : image(i), width(w), height(h) {}
};

struct TextureImageViewCreatedEvent : public Event {
    VkImageView imageView;
    explicit TextureImageViewCreatedEvent(VkImageView iv) : imageView(iv) {}
};

struct TextureSamplerCreatedEvent : public Event {
    VkSampler sampler;
    explicit TextureSamplerCreatedEvent(VkSampler s) : sampler(s) {}
};

} // namespace Shoonyakasha
using Shoonyakasha::VulkanTexture;
using Shoonyakasha::TextureImageCreatedEvent;
using Shoonyakasha::TextureImageViewCreatedEvent;
using Shoonyakasha::TextureSamplerCreatedEvent;
