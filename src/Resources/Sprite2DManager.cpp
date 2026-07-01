//
// Sprite2DManager.cpp
//

#include "Resources/Sprite2DManager.h"
#include "Vulkan/VulkanDevice.h"
#include "GPU/GPUResourceFactory.h"

// stb_image implementation in src/ThirdParty/stb_impl.cpp
#include <stb_image.h>

#include <glm/glm.hpp>
#include <iostream>
#include <vector>

namespace Shoonyakasha {

namespace {
struct SpriteVertex {
    glm::vec3 position;
    glm::vec2 uv;
};
}

Sprite2DManager::Sprite2DManager(VulkanDevice& device)
    : m_device(device)
{
}

Sprite2DManager::~Sprite2DManager() {
    if (m_quadCreated) {
        GPUResourceFactory::destroyBuffer(m_device.getAllocator().getHandle(), m_quadMesh.vertexBuffer);
        GPUResourceFactory::destroyBuffer(m_device.getAllocator().getHandle(), m_quadMesh.indexBuffer);
    }
    for (auto& [path, texture] : m_textureCache) {
        GPUResourceFactory::destroyTexture(m_device.getAllocator().getHandle(), m_device.getLogicalDevice(), texture);
    }
}

void Sprite2DManager::createQuadMesh() {
    // Unit quad centered at origin, XY in [-0.5, 0.5], UV top-left origin.
    const std::vector<SpriteVertex> vertices = {
        {{-0.5f, -0.5f, 0.0f}, {0.0f, 0.0f}},
        {{ 0.5f, -0.5f, 0.0f}, {1.0f, 0.0f}},
        {{ 0.5f,  0.5f, 0.0f}, {1.0f, 1.0f}},
        {{-0.5f,  0.5f, 0.0f}, {0.0f, 1.0f}},
    };
    const std::vector<uint16_t> indices = {0, 1, 2, 2, 3, 0};

    VkDeviceSize vbSize = vertices.size() * sizeof(SpriteVertex);
    m_quadMesh.vertexBuffer = GPUResourceFactory::createBuffer(
        m_device.getAllocator().getHandle(), vbSize,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY);
    GPUResourceFactory::uploadBuffer(
        m_device.getAllocator().getHandle(), m_device.getLogicalDevice(),
        m_device.getGraphicsQueue(), m_device.getCommandPool(),
        m_quadMesh.vertexBuffer, vertices.data(), vbSize);

    VkDeviceSize ibSize = indices.size() * sizeof(uint16_t);
    m_quadMesh.indexBuffer = GPUResourceFactory::createBuffer(
        m_device.getAllocator().getHandle(), ibSize,
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY);
    GPUResourceFactory::uploadBuffer(
        m_device.getAllocator().getHandle(), m_device.getLogicalDevice(),
        m_device.getGraphicsQueue(), m_device.getCommandPool(),
        m_quadMesh.indexBuffer, indices.data(), ibSize);

    m_quadMesh.indexType = IndexType::UInt16;
    m_quadMesh.vertexCount = static_cast<uint32_t>(vertices.size());
    m_quadMesh.indexCount = static_cast<uint32_t>(indices.size());
    m_quadMesh.vertexStride = sizeof(SpriteVertex);

    m_quadCreated = true;
}

const MeshComponent& Sprite2DManager::getQuadMesh() {
    if (!m_quadCreated) {
        createQuadMesh();
    }
    return m_quadMesh;
}

GPUTexture Sprite2DManager::loadTexture(const std::string& path, bool srgb) {
    std::string cacheKey = path + (srgb ? "_srgb" : "_linear");
    auto it = m_textureCache.find(cacheKey);
    if (it != m_textureCache.end()) {
        return it->second;
    }

    int width, height, channels;
    stbi_uc* pixels = stbi_load(path.c_str(), &width, &height, &channels, STBI_rgb_alpha);
    if (!pixels) {
        std::cerr << "[Sprite2DManager] Failed to load texture: " << path << std::endl;
        return GPUTexture{};
    }

    VkFormat format = srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
    VkDeviceSize imageSize = static_cast<VkDeviceSize>(width) * height * 4;

    GPUTexture texture = GPUResourceFactory::createTexture2DWithData(
        m_device.getAllocator().getHandle(),
        m_device.getLogicalDevice(),
        m_device.getGraphicsQueue(),
        m_device.getCommandPool(),
        static_cast<uint32_t>(width),
        static_cast<uint32_t>(height),
        format,
        pixels,
        imageSize,
        false);

    stbi_image_free(pixels);

    texture.sampler = GPUResourceFactory::createSampler(
        m_device.getLogicalDevice(),
        VK_FILTER_LINEAR, VK_FILTER_LINEAR,
        VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        1.0f,
        VK_SAMPLER_MIPMAP_MODE_LINEAR,
        static_cast<float>(texture.mipLevels));

    texture.exists = true;
    m_textureCache[cacheKey] = texture;
    return texture;
}

} // namespace Shoonyakasha
