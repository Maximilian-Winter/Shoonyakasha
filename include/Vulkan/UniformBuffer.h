//
// Created by maxim on 05.07.2024.
//

#pragma once

#include "Vulkan/VulkanBuffer.h"
#include <glm/glm.hpp>

namespace Shoonyakasha {

struct UniformBufferObject {
    alignas(16) glm::mat4 model;
    alignas(16) glm::mat4 view;
    alignas(16) glm::mat4 proj;
};
// Template version of UniformBuffer for flexible use
template<typename T>
class TypedUniformBuffer {
public:
    TypedUniformBuffer(VulkanDevice& device, uint32_t count)
            : m_device(device), m_bufferCount(count) {
        m_buffers.resize(count);
        for (uint32_t i = 0; i < count; i++) {
            m_buffers[i] = std::make_unique<VulkanBuffer>(
                    device,
                    sizeof(T),
                    VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
            );
        }
    }

    void update(uint32_t index, const T& data) {
        m_buffers[index]->update(&data, sizeof(T));
    }

    VkBuffer getBuffer(uint32_t index) const {
        return m_buffers[index]->getBuffer();
    }

    // Get all buffers as a vector (for registerUniformBuffer)
    std::vector<VkBuffer> getBuffers() const {
        std::vector<VkBuffer> result;
        result.reserve(m_buffers.size());
        for (const auto& buf : m_buffers) {
            result.push_back(buf->getBuffer());
        }
        return result;
    }

    VkDeviceSize getSize() const {
        return sizeof(T);
    }

private:
    VulkanDevice& m_device;
    std::vector<std::unique_ptr<VulkanBuffer>> m_buffers;
    uint32_t m_bufferCount;
};

using UniformBuffer = TypedUniformBuffer<UniformBufferObject>;

} // namespace Shoonyakasha
using Shoonyakasha::UniformBufferObject;
using Shoonyakasha::TypedUniformBuffer;
using Shoonyakasha::UniformBuffer;