//
// GPUResourceFactory.cpp - Implementation of GPU resource creation utilities
//

#include "GPU/GPUResourceFactory.h"
#include <stdexcept>
#include <algorithm>
#include <cmath>

namespace Shoonyakasha {

// ============================================================================
// Buffer Creation
// ============================================================================

GPUBuffer GPUResourceFactory::createBuffer(
    VmaAllocator allocator,
    VkDeviceSize size,
    VkBufferUsageFlags usage,
    VmaMemoryUsage memoryUsage
) {
    GPUBuffer buffer;
    buffer.size = size;

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = memoryUsage;

    if (vmaCreateBuffer(allocator, &bufferInfo, &allocInfo,
                        &buffer.buffer, &buffer.allocation, nullptr) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create GPU buffer");
    }

    return buffer;
}

GPUBuffer GPUResourceFactory::createVertexBuffer(
    VmaAllocator allocator,
    const void* data,
    VkDeviceSize size,
    VkCommandBuffer cmdBuffer,
    VmaAllocator stagingAllocator
) {
    VkBufferUsageFlags usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    if (data != nullptr) {
        usage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    }

    return createBuffer(allocator, size, usage, VMA_MEMORY_USAGE_GPU_ONLY);
}

GPUBuffer GPUResourceFactory::createIndexBuffer(
    VmaAllocator allocator,
    const void* data,
    VkDeviceSize size,
    IndexType indexType,
    VkCommandBuffer cmdBuffer
) {
    VkBufferUsageFlags usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    if (data != nullptr) {
        usage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    }

    return createBuffer(allocator, size, usage, VMA_MEMORY_USAGE_GPU_ONLY);
}

GPUBuffer GPUResourceFactory::createUniformBuffer(
    VmaAllocator allocator,
    VkDeviceSize size
) {
    return createBuffer(
        allocator,
        size,
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VMA_MEMORY_USAGE_CPU_TO_GPU  // Host visible for easy updates
    );
}

GPUBuffer GPUResourceFactory::createStorageBuffer(
    VmaAllocator allocator,
    VkDeviceSize size,
    bool hostVisible
) {
    return createBuffer(
        allocator,
        size,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        hostVisible ? VMA_MEMORY_USAGE_CPU_TO_GPU : VMA_MEMORY_USAGE_GPU_ONLY
    );
}

// ============================================================================
// Buffer Operations
// ============================================================================

void GPUResourceFactory::uploadBuffer(
    VmaAllocator allocator,
    VkDevice device,
    VkQueue queue,
    VkCommandPool cmdPool,
    GPUBuffer& buffer,
    const void* data,
    VkDeviceSize size,
    VkDeviceSize offset
) {
    // Create staging buffer
    GPUBuffer staging = createBuffer(
        allocator,
        size,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VMA_MEMORY_USAGE_CPU_ONLY
    );

    // Copy data to staging buffer
    void* mapped = mapBuffer(allocator, staging);
    std::memcpy(mapped, data, size);
    unmapBuffer(allocator, staging);

    // Create command buffer for transfer
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = cmdPool;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer cmdBuffer;
    vkAllocateCommandBuffers(device, &allocInfo, &cmdBuffer);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(cmdBuffer, &beginInfo);

    VkBufferCopy copyRegion{};
    copyRegion.srcOffset = 0;
    copyRegion.dstOffset = offset;
    copyRegion.size = size;
    vkCmdCopyBuffer(cmdBuffer, staging.buffer, buffer.buffer, 1, &copyRegion);

    vkEndCommandBuffer(cmdBuffer);

    // Submit and wait
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmdBuffer;

    vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(queue);

    vkFreeCommandBuffers(device, cmdPool, 1, &cmdBuffer);

    // Clean up staging buffer
    destroyBuffer(allocator, staging);
}

void* GPUResourceFactory::mapBuffer(VmaAllocator allocator, GPUBuffer& buffer) {
    void* data;
    if (vmaMapMemory(allocator, buffer.allocation, &data) != VK_SUCCESS) {
        throw std::runtime_error("Failed to map buffer memory");
    }
    return data;
}

void GPUResourceFactory::unmapBuffer(VmaAllocator allocator, GPUBuffer& buffer) {
    vmaUnmapMemory(allocator, buffer.allocation);
}

void GPUResourceFactory::destroyBuffer(VmaAllocator allocator, GPUBuffer& buffer) {
    if (buffer.buffer != VK_NULL_HANDLE) {
        vmaDestroyBuffer(allocator, buffer.buffer, buffer.allocation);
        buffer.reset();
    }
}

// ============================================================================
// Texture Creation
// ============================================================================

GPUTexture GPUResourceFactory::createTexture2D(
    VmaAllocator allocator,
    VkDevice device,
    uint32_t width,
    uint32_t height,
    VkFormat format,
    VkImageUsageFlags usage,
    uint32_t mipLevels,
    VkSampleCountFlagBits samples
) {
    GPUTexture texture;
    texture.width = width;
    texture.height = height;
    texture.format = format;
    texture.mipLevels = mipLevels;
    texture.exists = true;

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = mipLevels;
    imageInfo.arrayLayers = 1;
    imageInfo.format = format;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = usage;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.samples = samples;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    if (vmaCreateImage(allocator, &imageInfo, &allocInfo,
                       &texture.image, &texture.allocation, nullptr) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create texture image");
    }

    // Create image view
    VkImageAspectFlags aspectFlags = VK_IMAGE_ASPECT_COLOR_BIT;
    texture.view = createImageView(device, texture.image, format, aspectFlags,
                                   VK_IMAGE_VIEW_TYPE_2D, mipLevels);

    return texture;
}

GPUTexture GPUResourceFactory::createTexture2DWithData(
    VmaAllocator allocator,
    VkDevice device,
    VkQueue queue,
    VkCommandPool cmdPool,
    uint32_t width,
    uint32_t height,
    VkFormat format,
    const void* pixels,
    VkDeviceSize pixelDataSize,
    bool generateMips
) {
    uint32_t mipLevels = generateMips ? calculateMipLevels(width, height) : 1;

    VkImageUsageFlags usage = VK_IMAGE_USAGE_SAMPLED_BIT |
                              VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (generateMips) {
        usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    }

    GPUTexture texture = createTexture2D(allocator, device, width, height,
                                          format, usage, mipLevels);

    // Create staging buffer
    GPUBuffer staging = createBuffer(
        allocator,
        pixelDataSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VMA_MEMORY_USAGE_CPU_ONLY
    );

    void* mapped = mapBuffer(allocator, staging);
    std::memcpy(mapped, pixels, pixelDataSize);
    unmapBuffer(allocator, staging);

    // Create command buffer
    VkCommandBufferAllocateInfo cmdAllocInfo{};
    cmdAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdAllocInfo.commandPool = cmdPool;
    cmdAllocInfo.commandBufferCount = 1;

    VkCommandBuffer cmdBuffer;
    vkAllocateCommandBuffers(device, &cmdAllocInfo, &cmdBuffer);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(cmdBuffer, &beginInfo);

    // Transition to transfer destination
    transitionImageLayout(cmdBuffer, texture.image, format,
                          VK_IMAGE_LAYOUT_UNDEFINED,
                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                          mipLevels);

    // Copy buffer to image
    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {width, height, 1};

    vkCmdCopyBufferToImage(cmdBuffer, staging.buffer, texture.image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    if (generateMips) {
        generateMipmaps(cmdBuffer, texture.image, format, width, height, mipLevels);
    } else {
        transitionImageLayout(cmdBuffer, texture.image, format,
                              VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }

    vkEndCommandBuffer(cmdBuffer);

    // Submit and wait
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmdBuffer;

    vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(queue);

    vkFreeCommandBuffers(device, cmdPool, 1, &cmdBuffer);
    destroyBuffer(allocator, staging);

    return texture;
}

GPUTexture GPUResourceFactory::createTextureCube(
    VmaAllocator allocator,
    VkDevice device,
    uint32_t size,
    VkFormat format,
    VkImageUsageFlags usage,
    uint32_t mipLevels
) {
    GPUTexture texture;
    texture.width = size;
    texture.height = size;
    texture.format = format;
    texture.mipLevels = mipLevels;
    texture.exists = true;

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = size;
    imageInfo.extent.height = size;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = mipLevels;
    imageInfo.arrayLayers = 6;  // Cubemap has 6 faces
    imageInfo.format = format;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = usage;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    if (vmaCreateImage(allocator, &imageInfo, &allocInfo,
                       &texture.image, &texture.allocation, nullptr) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create cubemap image");
    }

    texture.view = createImageView(device, texture.image, format,
                                   VK_IMAGE_ASPECT_COLOR_BIT,
                                   VK_IMAGE_VIEW_TYPE_CUBE,
                                   mipLevels, 6);

    return texture;
}

GPUTexture GPUResourceFactory::createDepthTexture(
    VmaAllocator allocator,
    VkDevice device,
    uint32_t width,
    uint32_t height,
    VkFormat format
) {
    GPUTexture texture;
    texture.width = width;
    texture.height = height;
    texture.format = format;
    texture.mipLevels = 1;
    texture.exists = true;

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = format;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    if (vmaCreateImage(allocator, &imageInfo, &allocInfo,
                       &texture.image, &texture.allocation, nullptr) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create depth image");
    }

    texture.view = createImageView(device, texture.image, format,
                                   VK_IMAGE_ASPECT_DEPTH_BIT,
                                   VK_IMAGE_VIEW_TYPE_2D);

    return texture;
}

VkImageView GPUResourceFactory::createImageView(
    VkDevice device,
    VkImage image,
    VkFormat format,
    VkImageAspectFlags aspectFlags,
    VkImageViewType viewType,
    uint32_t mipLevels,
    uint32_t layerCount
) {
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image;
    viewInfo.viewType = viewType;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = aspectFlags;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = mipLevels;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = layerCount;

    VkImageView view;
    if (vkCreateImageView(device, &viewInfo, nullptr, &view) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create image view");
    }

    return view;
}

VkSampler GPUResourceFactory::createSampler(
    VkDevice device,
    VkFilter magFilter,
    VkFilter minFilter,
    VkSamplerAddressMode addressMode,
    float maxAnisotropy,
    VkSamplerMipmapMode mipmapMode,
    float maxLod
) {
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = magFilter;
    samplerInfo.minFilter = minFilter;
    samplerInfo.addressModeU = addressMode;
    samplerInfo.addressModeV = addressMode;
    samplerInfo.addressModeW = addressMode;
    samplerInfo.anisotropyEnable = maxAnisotropy > 1.0f ? VK_TRUE : VK_FALSE;
    samplerInfo.maxAnisotropy = maxAnisotropy;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    samplerInfo.mipmapMode = mipmapMode;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = maxLod;

    VkSampler sampler;
    if (vkCreateSampler(device, &samplerInfo, nullptr, &sampler) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create texture sampler");
    }

    return sampler;
}

// ============================================================================
// Texture Operations
// ============================================================================

void GPUResourceFactory::transitionImageLayout(
    VkCommandBuffer cmdBuffer,
    VkImage image,
    VkFormat format,
    VkImageLayout oldLayout,
    VkImageLayout newLayout,
    uint32_t mipLevels,
    uint32_t layerCount
) {
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = mipLevels;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = layerCount;

    // Determine aspect mask
    if (newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL ||
        format == VK_FORMAT_D32_SFLOAT ||
        format == VK_FORMAT_D32_SFLOAT_S8_UINT ||
        format == VK_FORMAT_D24_UNORM_S8_UINT) {
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        if (format == VK_FORMAT_D32_SFLOAT_S8_UINT ||
            format == VK_FORMAT_D24_UNORM_S8_UINT) {
            barrier.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
        }
    } else {
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    }

    VkPipelineStageFlags sourceStage;
    VkPipelineStageFlags destinationStage;

    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
        newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
               newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
               newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                               VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    } else {
        // Generic fallback
        barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
        sourceStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        destinationStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    }

    vkCmdPipelineBarrier(
        cmdBuffer,
        sourceStage, destinationStage,
        0,
        0, nullptr,
        0, nullptr,
        1, &barrier
    );
}

void GPUResourceFactory::generateMipmaps(
    VkCommandBuffer cmdBuffer,
    VkImage image,
    VkFormat format,
    uint32_t width,
    uint32_t height,
    uint32_t mipLevels
) {
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.image = image;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.subresourceRange.levelCount = 1;

    int32_t mipWidth = static_cast<int32_t>(width);
    int32_t mipHeight = static_cast<int32_t>(height);

    for (uint32_t i = 1; i < mipLevels; i++) {
        barrier.subresourceRange.baseMipLevel = i - 1;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

        vkCmdPipelineBarrier(cmdBuffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
            0, nullptr,
            0, nullptr,
            1, &barrier);

        VkImageBlit blit{};
        blit.srcOffsets[0] = {0, 0, 0};
        blit.srcOffsets[1] = {mipWidth, mipHeight, 1};
        blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.srcSubresource.mipLevel = i - 1;
        blit.srcSubresource.baseArrayLayer = 0;
        blit.srcSubresource.layerCount = 1;
        blit.dstOffsets[0] = {0, 0, 0};
        blit.dstOffsets[1] = {
            mipWidth > 1 ? mipWidth / 2 : 1,
            mipHeight > 1 ? mipHeight / 2 : 1,
            1
        };
        blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.dstSubresource.mipLevel = i;
        blit.dstSubresource.baseArrayLayer = 0;
        blit.dstSubresource.layerCount = 1;

        vkCmdBlitImage(cmdBuffer,
            image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1, &blit,
            VK_FILTER_LINEAR);

        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(cmdBuffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
            0, nullptr,
            0, nullptr,
            1, &barrier);

        if (mipWidth > 1) mipWidth /= 2;
        if (mipHeight > 1) mipHeight /= 2;
    }

    // Transition last mip level
    barrier.subresourceRange.baseMipLevel = mipLevels - 1;
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(cmdBuffer,
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
        0, nullptr,
        0, nullptr,
        1, &barrier);
}

void GPUResourceFactory::destroyTexture(VmaAllocator allocator, VkDevice device, GPUTexture& texture) {
    if (texture.sampler != VK_NULL_HANDLE) {
        vkDestroySampler(device, texture.sampler, nullptr);
    }
    if (texture.view != VK_NULL_HANDLE) {
        vkDestroyImageView(device, texture.view, nullptr);
    }
    if (texture.image != VK_NULL_HANDLE && texture.allocation != nullptr) {
        vmaDestroyImage(allocator, texture.image, texture.allocation);
    }
    texture.reset();
}

// ============================================================================
// Default Textures
// ============================================================================

GPUTexture GPUResourceFactory::createSolidColorTexture(
    VmaAllocator allocator,
    VkDevice device,
    VkQueue queue,
    VkCommandPool cmdPool,
    uint8_t r, uint8_t g, uint8_t b, uint8_t a
) {
    uint8_t pixels[4] = {r, g, b, a};

    GPUTexture texture = createTexture2DWithData(
        allocator, device, queue, cmdPool,
        1, 1, VK_FORMAT_R8G8B8A8_UNORM,
        pixels, sizeof(pixels), false
    );

    texture.sampler = createSampler(device,
        VK_FILTER_NEAREST, VK_FILTER_NEAREST,
        VK_SAMPLER_ADDRESS_MODE_REPEAT);

    return texture;
}

GPUResourceFactory::DefaultTextures GPUResourceFactory::createDefaultTextures(
    VmaAllocator allocator,
    VkDevice device,
    VkQueue queue,
    VkCommandPool cmdPool
) {
    DefaultTextures defaults;

    // White (default albedo)
    defaults.white = createSolidColorTexture(allocator, device, queue, cmdPool,
                                              255, 255, 255, 255);
    defaults.white.exists = false;  // It's a fallback

    // Black
    defaults.black = createSolidColorTexture(allocator, device, queue, cmdPool,
                                              0, 0, 0, 255);
    defaults.black.exists = false;

    // Flat normal (pointing up in tangent space)
    defaults.normal = createSolidColorTexture(allocator, device, queue, cmdPool,
                                               128, 128, 255, 255);
    defaults.normal.exists = false;

    // Default metallic-roughness (non-metallic, medium roughness)
    // R = occlusion (1.0), G = roughness (0.5), B = metallic (0.0)
    defaults.metallicRoughness = createSolidColorTexture(allocator, device, queue, cmdPool,
                                                          255, 128, 0, 255);
    defaults.metallicRoughness.exists = false;

    return defaults;
}

void GPUResourceFactory::destroyDefaultTextures(
    VmaAllocator allocator,
    VkDevice device,
    DefaultTextures& defaults
) {
    destroyTexture(allocator, device, defaults.white);
    destroyTexture(allocator, device, defaults.black);
    destroyTexture(allocator, device, defaults.normal);
    destroyTexture(allocator, device, defaults.metallicRoughness);
}

// ============================================================================
// Utility
// ============================================================================

VkDeviceSize GPUResourceFactory::getFormatSize(VkFormat format) {
    switch (format) {
        case VK_FORMAT_R8_UNORM:
        case VK_FORMAT_R8_SNORM:
        case VK_FORMAT_R8_UINT:
        case VK_FORMAT_R8_SINT:
            return 1;
        case VK_FORMAT_R8G8_UNORM:
        case VK_FORMAT_R8G8_SNORM:
        case VK_FORMAT_R8G8_UINT:
        case VK_FORMAT_R8G8_SINT:
        case VK_FORMAT_R16_SFLOAT:
        case VK_FORMAT_R16_UNORM:
            return 2;
        case VK_FORMAT_R8G8B8_UNORM:
        case VK_FORMAT_R8G8B8_SNORM:
        case VK_FORMAT_B8G8R8_UNORM:
            return 3;
        case VK_FORMAT_R8G8B8A8_UNORM:
        case VK_FORMAT_R8G8B8A8_SNORM:
        case VK_FORMAT_R8G8B8A8_SRGB:
        case VK_FORMAT_B8G8R8A8_UNORM:
        case VK_FORMAT_B8G8R8A8_SRGB:
        case VK_FORMAT_R16G16_SFLOAT:
        case VK_FORMAT_R32_SFLOAT:
        case VK_FORMAT_D32_SFLOAT:
            return 4;
        case VK_FORMAT_R16G16B16_SFLOAT:
            return 6;
        case VK_FORMAT_R16G16B16A16_SFLOAT:
        case VK_FORMAT_R32G32_SFLOAT:
            return 8;
        case VK_FORMAT_R32G32B32_SFLOAT:
            return 12;
        case VK_FORMAT_R32G32B32A32_SFLOAT:
            return 16;
        default:
            return 4;  // Default fallback
    }
}

} // namespace Shoonyakasha
