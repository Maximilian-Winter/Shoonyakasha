//
// VertexTypes.h - Standard vertex format definitions
//
// Extracted from VulkanModel.h during API cleanup.
// The Vertex struct and its hash functions are used by VulkanPipeline
// and FrameGraphCompiler for the legacy "standard" vertex format.
//

#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <array>
#include <functional>

namespace Shoonyakasha {

// Custom hash function for glm::vec3
struct Vec3Hash {
    size_t operator()(const glm::vec3& v) const {
        size_t h1 = std::hash<float>{}(v.x);
        size_t h2 = std::hash<float>{}(v.y);
        size_t h3 = std::hash<float>{}(v.z);
        return h1 ^ (h2 << 1) ^ (h3 << 2);
    }
};

// Custom hash function for glm::vec2
struct Vec2Hash {
    size_t operator()(const glm::vec2& v) const {
        size_t h1 = std::hash<float>{}(v.x);
        size_t h2 = std::hash<float>{}(v.y);
        return h1 ^ (h2 << 1);
    }
};

struct Vertex {
    glm::vec3 pos;
    glm::vec3 color;
    glm::vec2 texCoord;
    glm::vec3 normal;

    static VkVertexInputBindingDescription getBindingDescription();
    static std::array<VkVertexInputAttributeDescription, 4> getAttributeDescriptions();

    bool operator==(const Vertex& other) const {
        return pos == other.pos && color == other.color &&
               texCoord == other.texCoord && normal == other.normal;
    }
};

} // namespace Shoonyakasha
using Shoonyakasha::Vec3Hash;
using Shoonyakasha::Vec2Hash;
using Shoonyakasha::Vertex;

namespace std {
    template<> struct hash<Shoonyakasha::Vertex> {
        size_t operator()(const Shoonyakasha::Vertex& vertex) const {
            Shoonyakasha::Vec3Hash vec3Hasher;
            Shoonyakasha::Vec2Hash vec2Hasher;
            size_t h1 = vec3Hasher(vertex.pos);
            size_t h2 = vec3Hasher(vertex.color);
            size_t h3 = vec2Hasher(vertex.texCoord);
            size_t h4 = vec3Hasher(vertex.normal);
            return h1 ^ (h2 << 1) ^ (h3 << 2) ^ (h4 << 3);
        }
    };
}
