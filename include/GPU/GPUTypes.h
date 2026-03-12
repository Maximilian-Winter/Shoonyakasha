//
// GPUTypes.h - Thin GPU resource types
//
// Simple data structures that hold GPU handles. No builders, no binding info,
// no complex APIs. Just data.
//

#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <array>
#include <string>
#include <cstring>
#include <stdexcept>

namespace Shoonyakasha {

// ============================================================================
// AlphaMode - How transparency is handled
// ============================================================================

enum class AlphaMode : uint8_t {
    Opaque = 0,   // Fully opaque, no alpha testing
    Mask   = 1,   // Alpha cutoff testing (discard if below threshold)
    Blend  = 2    // Alpha blending (sorted back-to-front)
};

// ============================================================================
// IndexType - For index buffers
// ============================================================================

enum class IndexType : uint8_t {
    UInt16 = 0,
    UInt32 = 1
};

inline VkIndexType toVkIndexType(IndexType type) {
    return type == IndexType::UInt16 ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32;
}

// ============================================================================
// GPUBuffer - Thin wrapper around VkBuffer + VMA allocation
// ============================================================================
//
// This is a SIMPLE DATA STRUCT. It holds a buffer handle and its allocation.
// It does NOT manage lifetime automatically - that's the responsibility of
// whoever creates it (GltfLoader, ResourceManager, etc.)
//
// For automatic cleanup, wrap in std::unique_ptr with custom deleter,
// or use the GPUBufferOwned variant.
//

struct GPUBuffer {
    VkBuffer      buffer     = VK_NULL_HANDLE;
    VmaAllocation allocation = nullptr;
    VkDeviceSize  size       = 0;

    // Check if this buffer is valid (has been allocated)
    bool isValid() const { return buffer != VK_NULL_HANDLE; }

    // Reset to empty state (does NOT free GPU memory!)
    void reset() {
        buffer = VK_NULL_HANDLE;
        allocation = nullptr;
        size = 0;
    }
};

// ============================================================================
// GPUTexture - Image + View + Sampler bundle
// ============================================================================
//
// A complete texture ready for shader binding. Includes the image, view,
// and sampler. The 'exists' flag indicates whether this is a real texture
// or a fallback/default - used by shaders to branch behavior.
//

struct GPUTexture {
    VkImage       image   = VK_NULL_HANDLE;
    VkImageView   view    = VK_NULL_HANDLE;
    VkSampler     sampler = VK_NULL_HANDLE;
    VmaAllocation allocation = nullptr;

    VkFormat      format  = VK_FORMAT_UNDEFINED;
    uint32_t      width   = 0;
    uint32_t      height  = 0;
    uint32_t      mipLevels = 1;

    // True if this texture was loaded from actual data.
    // False if this is a default/fallback texture.
    // Shaders can use this to branch (e.g., skip normal mapping if no normal map)
    bool exists = false;

    // Check all three handles for combined_image_sampler binding
    bool isValid() const { return image != VK_NULL_HANDLE && view != VK_NULL_HANDLE && sampler != VK_NULL_HANDLE; }

    void reset() {
        image = VK_NULL_HANDLE;
        view = VK_NULL_HANDLE;
        sampler = VK_NULL_HANDLE;
        allocation = nullptr;
        format = VK_FORMAT_UNDEFINED;
        width = 0;
        height = 0;
        mipLevels = 1;
        exists = false;
    }
};

// ============================================================================
// MaterialParam - Type-safe parameter storage for material values
// ============================================================================
//
// Stores scalar/vector/matrix values with type information.
// Used in MaterialComponent::params map for generic material properties.
//
// Example:
//   material.params["baseColorFactor"] = MaterialParam::from(glm::vec4(1,0,0,1));
//   glm::vec4 color = material.params["baseColorFactor"].as<glm::vec4>();
//

struct MaterialParam {
    enum class Type : uint8_t {
        Float = 0,
        Vec2  = 1,
        Vec3  = 2,
        Vec4  = 3,
        Mat3  = 4,
        Mat4  = 5,
        Int   = 6,
        UInt  = 7
    };

    Type type = Type::Float;
    std::array<uint8_t, 64> data = {};  // Large enough for mat4 (64 bytes)

    // ─── Constructors ───────────────────────────────────────────

    MaterialParam() = default;

    // ─── Type-safe getters ──────────────────────────────────────

    template<typename T>
    T as() const {
        static_assert(sizeof(T) <= 64, "Type too large for MaterialParam");
        T result;
        std::memcpy(&result, data.data(), sizeof(T));
        return result;
    }

    // ─── Type-safe factory methods ──────────────────────────────

    static MaterialParam from(float v) {
        MaterialParam p;
        p.type = Type::Float;
        std::memcpy(p.data.data(), &v, sizeof(float));
        return p;
    }

    static MaterialParam from(const glm::vec2& v) {
        MaterialParam p;
        p.type = Type::Vec2;
        std::memcpy(p.data.data(), &v, sizeof(glm::vec2));
        return p;
    }

    static MaterialParam from(const glm::vec3& v) {
        MaterialParam p;
        p.type = Type::Vec3;
        std::memcpy(p.data.data(), &v, sizeof(glm::vec3));
        return p;
    }

    static MaterialParam from(const glm::vec4& v) {
        MaterialParam p;
        p.type = Type::Vec4;
        std::memcpy(p.data.data(), &v, sizeof(glm::vec4));
        return p;
    }

    static MaterialParam from(const glm::mat3& m) {
        MaterialParam p;
        p.type = Type::Mat3;
        std::memcpy(p.data.data(), &m, sizeof(glm::mat3));
        return p;
    }

    static MaterialParam from(const glm::mat4& m) {
        MaterialParam p;
        p.type = Type::Mat4;
        std::memcpy(p.data.data(), &m, sizeof(glm::mat4));
        return p;
    }

    static MaterialParam from(int32_t v) {
        MaterialParam p;
        p.type = Type::Int;
        std::memcpy(p.data.data(), &v, sizeof(int32_t));
        return p;
    }

    static MaterialParam from(uint32_t v) {
        MaterialParam p;
        p.type = Type::UInt;
        std::memcpy(p.data.data(), &v, sizeof(uint32_t));
        return p;
    }

    // ─── Size helpers ───────────────────────────────────────────

    size_t byteSize() const {
        switch (type) {
            case Type::Float: return sizeof(float);
            case Type::Vec2:  return sizeof(glm::vec2);
            case Type::Vec3:  return sizeof(glm::vec3);
            case Type::Vec4:  return sizeof(glm::vec4);
            case Type::Mat3:  return sizeof(glm::mat3);
            case Type::Mat4:  return sizeof(glm::mat4);
            case Type::Int:   return sizeof(int32_t);
            case Type::UInt:  return sizeof(uint32_t);
            default:          return 0;
        }
    }

    // Raw pointer to the data bytes
    const void* rawData() const { return data.data(); }
    void* rawData() { return data.data(); }
};

// ============================================================================
// Type name helpers (for debugging/logging)
// ============================================================================

inline const char* toString(AlphaMode mode) {
    switch (mode) {
        case AlphaMode::Opaque: return "Opaque";
        case AlphaMode::Mask:   return "Mask";
        case AlphaMode::Blend:  return "Blend";
        default:                return "Unknown";
    }
}

inline const char* toString(MaterialParam::Type type) {
    switch (type) {
        case MaterialParam::Type::Float: return "float";
        case MaterialParam::Type::Vec2:  return "vec2";
        case MaterialParam::Type::Vec3:  return "vec3";
        case MaterialParam::Type::Vec4:  return "vec4";
        case MaterialParam::Type::Mat3:  return "mat3";
        case MaterialParam::Type::Mat4:  return "mat4";
        case MaterialParam::Type::Int:   return "int";
        case MaterialParam::Type::UInt:  return "uint";
        default:                         return "unknown";
    }
}

} // namespace Shoonyakasha
