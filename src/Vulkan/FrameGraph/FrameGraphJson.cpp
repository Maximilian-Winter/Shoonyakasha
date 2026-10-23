//
// Shoonyakasha Engine - Frame Graph JSON Serialization
//
// 名正則言順  言順則事成
// When names are correct, language is in accordance
// When language is in accordance, affairs are completed
//

#include "Vulkan/FrameGraph/FrameGraphJson.h"
#include <system_error>
#include "Vulkan/FrameGraph/FrameGraph.h"
#include "GPU/VkFormatUtils.h"
#include "FrameGraph/ShadowCascades.h"
#include "FrameGraph/LocalShadows.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <stdexcept>
#include <cstring>  // For strerror
#include <filesystem>
#include <optional>
#include <cerrno>

#ifdef _WIN32
    #define NOMINMAX
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
#endif

namespace Shoonyakasha {
namespace FrameGraph {

// ═══════════════════════════════════════════════════════════════
// VkFormat String Tables
// ═══════════════════════════════════════════════════════════════

namespace JsonUtils {

static const std::unordered_map<std::string, VkFormat> s_formatMap = {
    // Unsigned normalized
    {"R8_UNORM",                VK_FORMAT_R8_UNORM},
    {"R8G8_UNORM",              VK_FORMAT_R8G8_UNORM},
    {"R8G8B8_UNORM",            VK_FORMAT_R8G8B8_UNORM},
    {"R8G8B8A8_UNORM",          VK_FORMAT_R8G8B8A8_UNORM},
    {"B8G8R8A8_UNORM",          VK_FORMAT_B8G8R8A8_UNORM},

    // SRGB
    {"R8G8B8A8_SRGB",           VK_FORMAT_R8G8B8A8_SRGB},
    {"B8G8R8A8_SRGB",           VK_FORMAT_B8G8R8A8_SRGB},

    // Signed normalized
    {"R8_SNORM",                VK_FORMAT_R8_SNORM},
    {"R8G8_SNORM",              VK_FORMAT_R8G8_SNORM},
    {"R8G8B8A8_SNORM",          VK_FORMAT_R8G8B8A8_SNORM},

    // Float 16
    {"R16_SFLOAT",              VK_FORMAT_R16_SFLOAT},
    {"R16G16_SFLOAT",           VK_FORMAT_R16G16_SFLOAT},
    {"R16G16B16_SFLOAT",        VK_FORMAT_R16G16B16_SFLOAT},
    {"R16G16B16A16_SFLOAT",     VK_FORMAT_R16G16B16A16_SFLOAT},

    // Float 32
    {"R32_SFLOAT",              VK_FORMAT_R32_SFLOAT},
    {"R32G32_SFLOAT",           VK_FORMAT_R32G32_SFLOAT},
    {"R32G32B32_SFLOAT",        VK_FORMAT_R32G32B32_SFLOAT},
    {"R32G32B32A32_SFLOAT",     VK_FORMAT_R32G32B32A32_SFLOAT},

    // Integer
    {"R8_UINT",                 VK_FORMAT_R8_UINT},
    {"R16_UINT",                VK_FORMAT_R16_UINT},
    {"R32_UINT",                VK_FORMAT_R32_UINT},
    {"R32G32_UINT",             VK_FORMAT_R32G32_UINT},
    {"R32G32B32A32_UINT",       VK_FORMAT_R32G32B32A32_UINT},
    {"R8_SINT",                 VK_FORMAT_R8_SINT},
    {"R16_SINT",                VK_FORMAT_R16_SINT},
    {"R32_SINT",                VK_FORMAT_R32_SINT},

    // Unsigned normalized 16-bit
    {"R16_UNORM",               VK_FORMAT_R16_UNORM},
    {"R16G16_UNORM",            VK_FORMAT_R16G16_UNORM},
    {"R16G16B16A16_UNORM",      VK_FORMAT_R16G16B16A16_UNORM},

    // Packed, 4 bytes a texel
    {"B10G11R11_UFLOAT_PACK32", VK_FORMAT_B10G11R11_UFLOAT_PACK32},
    {"A2B10G10R10_UNORM_PACK32", VK_FORMAT_A2B10G10R10_UNORM_PACK32},

    // Depth formats
    {"D16_UNORM",               VK_FORMAT_D16_UNORM},
    {"D32_SFLOAT",              VK_FORMAT_D32_SFLOAT},
    {"D16_UNORM_S8_UINT",       VK_FORMAT_D16_UNORM_S8_UINT},
    {"D24_UNORM_S8_UINT",       VK_FORMAT_D24_UNORM_S8_UINT},
    {"D32_SFLOAT_S8_UINT",      VK_FORMAT_D32_SFLOAT_S8_UINT},

    // Special
    {"UNDEFINED",               VK_FORMAT_UNDEFINED},
};

static const std::unordered_map<VkFormat, std::string> s_formatReverseMap = [] {
    std::unordered_map<VkFormat, std::string> map;
    for (const auto& [name, fmt] : s_formatMap) {
        map[fmt] = name;
    }
    return map;
}();

VkFormat stringToFormat(const std::string& str) {
    auto it = s_formatMap.find(str);
    if (it != s_formatMap.end()) return it->second;
    throw std::runtime_error("Unknown VkFormat string: '" + str + "'");
}

std::string formatToString(VkFormat format) {
    auto it = s_formatReverseMap.find(format);
    if (it != s_formatReverseMap.end()) return it->second;
    return "UNDEFINED";
}

// ═══════════════════════════════════════════════════════════════
// ResourceUsage String Tables
// ═══════════════════════════════════════════════════════════════

ResourceUsage stringToResourceUsage(const std::string& str) {
    static const std::unordered_map<std::string, ResourceUsage> map = {
        {"color_write",           ResourceUsage::ColorAttachmentWrite},
        {"color_attachment_write", ResourceUsage::ColorAttachmentWrite},
        {"color_blend",           ResourceUsage::ColorAttachmentBlend},  // Alpha blending (read-modify-write)
        {"color_attachment_blend", ResourceUsage::ColorAttachmentBlend},
        {"depth_write",           ResourceUsage::DepthStencilWrite},
        {"depth_stencil_write",   ResourceUsage::DepthStencilWrite},
        {"depth_read",            ResourceUsage::DepthStencilReadOnly},
        {"shader_read",           ResourceUsage::ShaderReadOnly},
        {"shader_read_write",     ResourceUsage::ShaderReadWrite},
        {"storage_image_write",   ResourceUsage::StorageImageWrite},
        {"input_attachment",      ResourceUsage::InputAttachment},
        {"transfer_src",          ResourceUsage::TransferSrc},
        {"transfer_dst",          ResourceUsage::TransferDst},
        {"present",               ResourceUsage::Present},
    };
    auto it = map.find(str);
    if (it != map.end()) return it->second;
    throw std::runtime_error("Unknown ResourceUsage: '" + str + "'");
}

std::string resourceUsageToString(ResourceUsage usage) {
    switch (usage) {
        case ResourceUsage::ColorAttachmentWrite:   return "color_write";
        case ResourceUsage::ColorAttachmentBlend:   return "color_blend";
        case ResourceUsage::DepthStencilWrite:      return "depth_write";
        case ResourceUsage::DepthStencilReadOnly:   return "depth_read";
        case ResourceUsage::ShaderReadOnly:         return "shader_read";
        case ResourceUsage::ShaderReadWrite:        return "shader_read_write";
        case ResourceUsage::StorageImageWrite:      return "storage_image_write";
        case ResourceUsage::InputAttachment:        return "input_attachment";
        case ResourceUsage::TransferSrc:            return "transfer_src";
        case ResourceUsage::TransferDst:            return "transfer_dst";
        case ResourceUsage::Present:                return "present";
        case ResourceUsage::ColorResolve:           return "color_resolve";
        default:                                    return "unknown";
    }
}

// ═══════════════════════════════════════════════════════════════
// PassType String Tables
// ═══════════════════════════════════════════════════════════════

PassType stringToPassType(const std::string& str) {
    static const std::unordered_map<std::string, PassType> map = {
        {"graphics",    PassType::Graphics},
        {"compute",     PassType::Compute},
        {"transfer",    PassType::Transfer},
        {"ray_tracing", PassType::RayTracing},
    };
    auto it = map.find(str);
    if (it != map.end()) return it->second;
    throw std::runtime_error("Unknown PassType: '" + str + "'");
}

std::string passTypeToString(PassType type) {
    switch (type) {
        case PassType::Graphics:    return "graphics";
        case PassType::Compute:     return "compute";
        case PassType::Transfer:    return "transfer";
        case PassType::RayTracing:  return "ray_tracing";
        default:                    return "unknown";
    }
}

// ═══════════════════════════════════════════════════════════════
// ResourceKind String Tables
// ═══════════════════════════════════════════════════════════════

ResourceKind stringToResourceKind(const std::string& str) {
    if (str == "image")  return ResourceKind::Image;
    if (str == "buffer") return ResourceKind::Buffer;
    throw std::runtime_error("Unknown ResourceKind: '" + str + "'");
}

std::string resourceKindToString(ResourceKind kind) {
    switch (kind) {
        case ResourceKind::Image:   return "image";
        case ResourceKind::Buffer:  return "buffer";
        default:                    return "unknown";
    }
}

// ═══════════════════════════════════════════════════════════════
// ShaderStage String Tables
// ═══════════════════════════════════════════════════════════════

VkShaderStageFlags stringToShaderStage(const std::string& str) {
    static const std::unordered_map<std::string, VkShaderStageFlags> map = {
        {"vertex",      VK_SHADER_STAGE_VERTEX_BIT},
        {"fragment",    VK_SHADER_STAGE_FRAGMENT_BIT},
        {"compute",     VK_SHADER_STAGE_COMPUTE_BIT},
        {"geometry",    VK_SHADER_STAGE_GEOMETRY_BIT},
        {"tess_control",    VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT},
        {"tess_eval",       VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT},
        {"task",        VK_SHADER_STAGE_TASK_BIT_EXT},
        {"mesh",        VK_SHADER_STAGE_MESH_BIT_EXT},
        {"raygen",      VK_SHADER_STAGE_RAYGEN_BIT_KHR},
        {"miss",        VK_SHADER_STAGE_MISS_BIT_KHR},
        {"closest_hit", VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR},
        {"any_hit",     VK_SHADER_STAGE_ANY_HIT_BIT_KHR},
        {"intersection", VK_SHADER_STAGE_INTERSECTION_BIT_KHR},
        {"all",         VK_SHADER_STAGE_ALL},
    };
    auto it = map.find(str);
    if (it != map.end()) return it->second;
    throw std::runtime_error("Unknown shader stage: '" + str + "'");
}

VkShaderStageFlags stringsToShaderStages(const std::vector<std::string>& strs) {
    VkShaderStageFlags flags = 0;
    for (const auto& s : strs) {
        flags |= stringToShaderStage(s);
    }
    return flags;
}

// ═══════════════════════════════════════════════════════════════
// VkDescriptorType String Tables
// ═══════════════════════════════════════════════════════════════

VkDescriptorType stringToDescriptorType(const std::string& str) {
    static const std::unordered_map<std::string, VkDescriptorType> map = {
        {"uniform_buffer",              VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER},
        {"uniform_buffer_dynamic",      VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC},
        {"storage_buffer",              VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
        {"storage_buffer_dynamic",      VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC},
        {"combined_image_sampler",      VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER},
        {"sampled_image",               VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE},
        {"storage_image",               VK_DESCRIPTOR_TYPE_STORAGE_IMAGE},
        {"input_attachment",            VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT},
        {"sampler",                     VK_DESCRIPTOR_TYPE_SAMPLER},
        {"acceleration_structure",      VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR},
        {"uniform_texel_buffer",        VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER},
        {"storage_texel_buffer",        VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER},
    };
    auto it = map.find(str);
    if (it != map.end()) return it->second;
    throw std::runtime_error("Unknown VkDescriptorType: '" + str + "'");
}

std::string descriptorTypeToString(VkDescriptorType type) {
    switch (type) {
        case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:          return "uniform_buffer";
        case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:  return "uniform_buffer_dynamic";
        case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:          return "storage_buffer";
        case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:  return "storage_buffer_dynamic";
        case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:  return "combined_image_sampler";
        case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:           return "sampled_image";
        case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:           return "storage_image";
        case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:        return "input_attachment";
        case VK_DESCRIPTOR_TYPE_SAMPLER:                 return "sampler";
        case VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR: return "acceleration_structure";
        case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:    return "uniform_texel_buffer";
        case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:    return "storage_texel_buffer";
        default:                                         return "unknown";
    }
}

// ═══════════════════════════════════════════════════════════════
// QueueType String Tables
// ═══════════════════════════════════════════════════════════════

QueueType stringToQueueType(const std::string& str) {
    if (str == "compute") return QueueType::Compute;
    return QueueType::Graphics;  // default
}

std::string queueTypeToString(QueueType type) {
    switch (type) {
        case QueueType::Compute:    return "compute";
        case QueueType::Graphics:
        default:                    return "graphics";
    }
}

// ═══════════════════════════════════════════════════════════════
// Sampler Property String Tables
// ═══════════════════════════════════════════════════════════════

VkFilter stringToFilter(const std::string& str) {
    if (str == "linear")  return VK_FILTER_LINEAR;
    if (str == "nearest") return VK_FILTER_NEAREST;
    throw std::runtime_error("Unknown VkFilter: '" + str + "'");
}

VkSamplerMipmapMode stringToMipmapMode(const std::string& str) {
    if (str == "linear")  return VK_SAMPLER_MIPMAP_MODE_LINEAR;
    if (str == "nearest") return VK_SAMPLER_MIPMAP_MODE_NEAREST;
    throw std::runtime_error("Unknown VkSamplerMipmapMode: '" + str + "'");
}

VkSamplerAddressMode stringToAddressMode(const std::string& str) {
    static const std::unordered_map<std::string, VkSamplerAddressMode> map = {
        {"repeat",          VK_SAMPLER_ADDRESS_MODE_REPEAT},
        {"mirrored_repeat", VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT},
        {"clamp_to_edge",   VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE},
        {"clamp_to_border", VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER},
        {"mirror_clamp_to_edge", VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE},
    };
    auto it = map.find(str);
    if (it != map.end()) return it->second;
    throw std::runtime_error("Unknown VkSamplerAddressMode: '" + str + "'");
}

VkBorderColor stringToBorderColor(const std::string& str) {
    static const std::unordered_map<std::string, VkBorderColor> map = {
        {"float_transparent_black", VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK},
        {"int_transparent_black",   VK_BORDER_COLOR_INT_TRANSPARENT_BLACK},
        {"float_opaque_black",      VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK},
        {"int_opaque_black",        VK_BORDER_COLOR_INT_OPAQUE_BLACK},
        {"float_opaque_white",      VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE},
        {"int_opaque_white",        VK_BORDER_COLOR_INT_OPAQUE_WHITE},
        // Shorthand aliases
        {"transparent_black",       VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK},
        {"opaque_black",            VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK},
        {"opaque_white",            VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE},
        {"white",                   VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE},
        {"black",                   VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK},
    };
    auto it = map.find(str);
    if (it != map.end()) return it->second;
    throw std::runtime_error("Unknown VkBorderColor: '" + str + "'");
}

VkCompareOp stringToCompareOp(const std::string& str) {
    static const std::unordered_map<std::string, VkCompareOp> map = {
        {"never",           VK_COMPARE_OP_NEVER},
        {"less",            VK_COMPARE_OP_LESS},
        {"equal",           VK_COMPARE_OP_EQUAL},
        {"less_or_equal",   VK_COMPARE_OP_LESS_OR_EQUAL},
        {"greater",         VK_COMPARE_OP_GREATER},
        {"not_equal",       VK_COMPARE_OP_NOT_EQUAL},
        {"greater_or_equal", VK_COMPARE_OP_GREATER_OR_EQUAL},
        {"always",          VK_COMPARE_OP_ALWAYS},
    };
    auto it = map.find(str);
    if (it != map.end()) return it->second;
    throw std::runtime_error("Unknown VkCompareOp: '" + str + "'");
}

VkCullModeFlags stringToCullMode(const std::string& str) {
    if (str == "none")           return VK_CULL_MODE_NONE;
    if (str == "front")          return VK_CULL_MODE_FRONT_BIT;
    if (str == "back")           return VK_CULL_MODE_BACK_BIT;
    if (str == "front_and_back") return VK_CULL_MODE_FRONT_AND_BACK;
    throw std::runtime_error("Unknown cullMode: '" + str + "' (expected none, front, back or front_and_back)");
}

VkPrimitiveTopology stringToTopology(const std::string& str) {
    if (str == "triangle_list")  return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    if (str == "triangle_strip") return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    if (str == "line_list")      return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
    if (str == "line_strip")     return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
    if (str == "point_list")     return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
    if (str == "patch_list")     return VK_PRIMITIVE_TOPOLOGY_PATCH_LIST;
    throw std::runtime_error("Unknown topology: '" + str + "' (expected triangle_list, triangle_strip, "
                             "line_list, line_strip, point_list or patch_list)");
}

VkBlendFactor stringToBlendFactor(const std::string& str) {
    static const std::unordered_map<std::string, VkBlendFactor> map = {
        {"zero",                     VK_BLEND_FACTOR_ZERO},
        {"one",                      VK_BLEND_FACTOR_ONE},
        {"src_color",                VK_BLEND_FACTOR_SRC_COLOR},
        {"one_minus_src_color",      VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR},
        {"dst_color",                VK_BLEND_FACTOR_DST_COLOR},
        {"one_minus_dst_color",      VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR},
        {"src_alpha",                VK_BLEND_FACTOR_SRC_ALPHA},
        {"one_minus_src_alpha",      VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA},
        {"dst_alpha",                VK_BLEND_FACTOR_DST_ALPHA},
        {"one_minus_dst_alpha",      VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA},
        {"constant_color",           VK_BLEND_FACTOR_CONSTANT_COLOR},
        {"one_minus_constant_color", VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR},
        {"constant_alpha",           VK_BLEND_FACTOR_CONSTANT_ALPHA},
        {"one_minus_constant_alpha", VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA},
        {"src_alpha_saturate",       VK_BLEND_FACTOR_SRC_ALPHA_SATURATE},
    };
    auto it = map.find(str);
    if (it != map.end()) return it->second;
    throw std::runtime_error("Unknown blend factor: '" + str + "'");
}

VkBlendOp stringToBlendOp(const std::string& str) {
    if (str == "add")              return VK_BLEND_OP_ADD;
    if (str == "subtract")         return VK_BLEND_OP_SUBTRACT;
    if (str == "reverse_subtract") return VK_BLEND_OP_REVERSE_SUBTRACT;
    if (str == "min")              return VK_BLEND_OP_MIN;
    if (str == "max")              return VK_BLEND_OP_MAX;
    throw std::runtime_error("Unknown blend op: '" + str + "' (expected add, subtract, reverse_subtract, min or max)");
}

VkStencilOp stringToStencilOp(const std::string& str) {
    static const std::unordered_map<std::string, VkStencilOp> map = {
        {"keep",                VK_STENCIL_OP_KEEP},
        {"zero",                VK_STENCIL_OP_ZERO},
        {"replace",             VK_STENCIL_OP_REPLACE},
        {"increment_and_clamp", VK_STENCIL_OP_INCREMENT_AND_CLAMP},
        {"decrement_and_clamp", VK_STENCIL_OP_DECREMENT_AND_CLAMP},
        {"invert",              VK_STENCIL_OP_INVERT},
        {"increment_and_wrap",  VK_STENCIL_OP_INCREMENT_AND_WRAP},
        {"decrement_and_wrap",  VK_STENCIL_OP_DECREMENT_AND_WRAP},
    };
    auto it = map.find(str);
    if (it != map.end()) return it->second;
    throw std::runtime_error("Unknown VkStencilOp: '" + str + "'");
}

std::string stencilOpToString(VkStencilOp op) {
    switch (op) {
        case VK_STENCIL_OP_KEEP:                return "keep";
        case VK_STENCIL_OP_ZERO:                return "zero";
        case VK_STENCIL_OP_REPLACE:             return "replace";
        case VK_STENCIL_OP_INCREMENT_AND_CLAMP: return "increment_and_clamp";
        case VK_STENCIL_OP_DECREMENT_AND_CLAMP: return "decrement_and_clamp";
        case VK_STENCIL_OP_INVERT:              return "invert";
        case VK_STENCIL_OP_INCREMENT_AND_WRAP:  return "increment_and_wrap";
        case VK_STENCIL_OP_DECREMENT_AND_WRAP:  return "decrement_and_wrap";
        default:                                return "keep";
    }
}

} // namespace JsonUtils

// ═══════════════════════════════════════════════════════════════
// JSON → ResourceAccess parsing helpers
// ═══════════════════════════════════════════════════════════════

// ═══════════════════════════════════════════════════════════════
// Subresources and view types
// ═══════════════════════════════════════════════════════════════

/// Read "mip"/"mips" and "layer"/"layers" from an access or binding object.
/// "mip": n is one level; "mips": [first, count] a block. Same for layers.
static SubresourceRange parseSubresource(const nlohmann::json& j, const std::string& where) {
    SubresourceRange range;

    auto readOne = [&](const char* single, const char* block,
                       uint32_t& base, uint32_t& count) {
        if (j.contains(single) && j.contains(block)) {
            throw std::runtime_error(where + ": give either \"" + single + "\" or \"" +
                                     block + "\", not both");
        }
        if (j.contains(single)) {
            base = j[single].get<uint32_t>();
            count = 1;
        } else if (j.contains(block)) {
            const auto& b = j[block];
            if (!b.is_array() || b.size() != 2) {
                throw std::runtime_error(where + ": \"" + block + "\" must be [first, count]");
            }
            base = b[0].get<uint32_t>();
            count = b[1].get<uint32_t>();
            if (count == 0) {
                throw std::runtime_error(where + ": \"" + block + "\" has a count of 0");
            }
        }
    };
    readOne("mip", "mips", range.baseMip, range.mipCount);
    readOne("layer", "layers", range.baseLayer, range.layerCount);
    return range;
}

/// Inverse of parseSubresource; writes nothing for the whole image.
static void serializeSubresource(nlohmann::json& j, const SubresourceRange& range) {
    auto writeOne = [&](const char* single, const char* block, uint32_t base, uint32_t count) {
        if (count == 1) {
            j[single] = base;
        } else if (base != 0 || count != SubresourceRange::kAll) {
            // A block running to the end keeps its open count, spelled as the
            // largest uint32 so it reloads unchanged.
            j[block] = {base, count};
        }
    };
    writeOne("mip", "mips", range.baseMip, range.mipCount);
    writeOne("layer", "layers", range.baseLayer, range.layerCount);
}

static ImageViewKind stringToImageViewKind(const std::string& str) {
    if (str == "auto")       return ImageViewKind::Auto;
    if (str == "2d")         return ImageViewKind::Tex2D;
    if (str == "2d_array")   return ImageViewKind::Tex2DArray;
    if (str == "cube")       return ImageViewKind::Cube;
    if (str == "cube_array") return ImageViewKind::CubeArray;
    if (str == "3d")         return ImageViewKind::Tex3D;
    throw std::runtime_error("Unknown image viewType: '" + str +
                             "' (expected auto, 2d, 2d_array, cube, cube_array or 3d)");
}

static const char* imageViewKindToString(ImageViewKind kind) {
    switch (kind) {
        case ImageViewKind::Auto:       return "auto";
        case ImageViewKind::Tex2D:      return "2d";
        case ImageViewKind::Tex2DArray: return "2d_array";
        case ImageViewKind::Cube:       return "cube";
        case ImageViewKind::CubeArray:  return "cube_array";
        case ImageViewKind::Tex3D:      return "3d";
    }
    return "auto";
}

/// "writeMask": the channels written, a string of "r", "g", "b" and "a".
static VkColorComponentFlags parseWriteMask(const nlohmann::json& j, const std::string& where) {
    if (!j.is_string()) throw std::runtime_error(where + ": writeMask must be a string such as \"rgb\"");
    VkColorComponentFlags mask = 0;
    for (char c : j.get<std::string>()) {
        const VkColorComponentFlags bit = c == 'r' ? VK_COLOR_COMPONENT_R_BIT
                                        : c == 'g' ? VK_COLOR_COMPONENT_G_BIT
                                        : c == 'b' ? VK_COLOR_COMPONENT_B_BIT
                                        : c == 'a' ? VK_COLOR_COMPONENT_A_BIT : 0;
        if (bit == 0 || (mask & bit)) {
            throw std::runtime_error(where + ": writeMask names each of r, g, b and a at most once");
        }
        mask |= bit;
    }
    return mask;
}

static std::string writeMaskToString(VkColorComponentFlags mask) {
    std::string out;
    if (mask & VK_COLOR_COMPONENT_R_BIT) out += 'r';
    if (mask & VK_COLOR_COMPONENT_G_BIT) out += 'g';
    if (mask & VK_COLOR_COMPONENT_B_BIT) out += 'b';
    if (mask & VK_COLOR_COMPONENT_A_BIT) out += 'a';
    return out;
}

/// "blending" and, for "custom", its factor and operation keys, checked.
static BlendDesc parseBlend(const nlohmann::json& j, const std::string& where) {
    BlendDesc blend;
    blend.blending       = j.value("blending", std::string{"none"});
    blend.srcColorFactor = j.value("srcColorFactor", blend.srcColorFactor);
    blend.dstColorFactor = j.value("dstColorFactor", blend.dstColorFactor);
    blend.colorBlendOp   = j.value("colorBlendOp", blend.colorBlendOp);
    blend.srcAlphaFactor = j.value("srcAlphaFactor", blend.srcAlphaFactor);
    blend.dstAlphaFactor = j.value("dstAlphaFactor", blend.dstAlphaFactor);
    blend.alphaBlendOp   = j.value("alphaBlendOp", blend.alphaBlendOp);
    try {
        if (blend.blending != "none" && blend.blending != "alpha" && blend.blending != "additive" &&
            blend.blending != "custom") {
            throw std::runtime_error("Unknown blending: '" + blend.blending +
                                     "' (expected none, alpha, additive or custom)");
        }
        for (const auto* factor : {&blend.srcColorFactor, &blend.dstColorFactor,
                                   &blend.srcAlphaFactor, &blend.dstAlphaFactor}) {
            JsonUtils::stringToBlendFactor(*factor);
        }
        JsonUtils::stringToBlendOp(blend.colorBlendOp);
        JsonUtils::stringToBlendOp(blend.alphaBlendOp);
    } catch (const std::runtime_error& e) {
        throw std::runtime_error(where + ": " + e.what());
    }
    return blend;
}

static ResourceAccess parseResourceAccess(
    const nlohmann::json& j,
    const FrameGraphBuilder& builder)
{
    ResourceAccess access;

    std::string resourceName = j.at("resource").get<std::string>();
    access.handle = builder.getResource(resourceName);
    if (!access.handle.valid()) {
        throw std::runtime_error("FrameGraph JSON: Unknown resource '" + resourceName + "'");
    }

    access.usage = JsonUtils::stringToResourceUsage(j.at("usage").get<std::string>());
    access.subresource = parseSubresource(j, "Access to '" + resourceName + "'");

    // "usage": "present" is the older spelling of a colour write that also
    // presents. Normalise it here so the rest of the compiler sees one shape, and
    // so `"usage": "color_blend", "present": true` is expressible at all.
    if (access.usage == ResourceUsage::Present) {
        access.usage = ResourceUsage::ColorAttachmentWrite;
        access.present = true;
    }

    // Presentation is a post-condition and composes with any colour usage.
    if (j.contains("present")) {
        access.present = j["present"].get<bool>();
    }

    // Parse clear value if present
    if (j.contains("clear")) {
        access.hasClearValue = true;
        const auto& clear = j["clear"];

        if (clear.is_array() && clear.size() == 4) {
            // Color clear: [r, g, b, a]
            access.clearValue.color.float32[0] = clear[0].get<float>();
            access.clearValue.color.float32[1] = clear[1].get<float>();
            access.clearValue.color.float32[2] = clear[2].get<float>();
            access.clearValue.color.float32[3] = clear[3].get<float>();
        } else if (clear.is_object()) {
            // Depth/stencil clear: {"depth": 1.0, "stencil": 0}
            access.clearValue.depthStencil.depth = clear.value("depth", 1.0f);
            access.clearValue.depthStencil.stencil = clear.value("stencil", 0u);
        }
    }

    // "blending" and "writeMask": this colour attachment's own.
    const bool colour = access.usage == ResourceUsage::ColorAttachmentWrite ||
                        access.usage == ResourceUsage::ColorAttachmentBlend;
    const std::string where = "Access to '" + resourceName + "'";
    for (const char* key : {"blending", "writeMask", "srcColorFactor", "dstColorFactor", "colorBlendOp",
                            "srcAlphaFactor", "dstAlphaFactor", "alphaBlendOp"}) {
        if (j.contains(key) && !colour) {
            throw std::runtime_error(where + ": \"" + key + "\" applies to colour attachments");
        }
    }
    if (j.contains("blending")) {
        access.blend = parseBlend(j, where);
    } else {
        for (const char* key : {"srcColorFactor", "dstColorFactor", "colorBlendOp",
                                "srcAlphaFactor", "dstAlphaFactor", "alphaBlendOp"}) {
            if (j.contains(key)) {
                throw std::runtime_error(where + ": \"" + key + "\" needs \"blending\": \"custom\" beside it");
            }
        }
    }
    if (j.contains("writeMask")) access.writeMask = parseWriteMask(j["writeMask"], where);

    // "resolve": the image a multisampled colour attachment resolves into.
    if (j.contains("resolve")) {
        const std::string target = j["resolve"].get<std::string>();
        if (access.usage != ResourceUsage::ColorAttachmentWrite &&
            access.usage != ResourceUsage::ColorAttachmentBlend) {
            throw std::runtime_error("Access to '" + resourceName + "': \"resolve\" applies to colour "
                                     "attachments (color_write, color_blend)");
        }
        access.resolveTarget = builder.getResource(target);
        if (!access.resolveTarget.valid()) {
            throw std::runtime_error("Access to '" + resourceName + "': unknown resolve target '" + target + "'");
        }
    }

    return access;
}

// ═══════════════════════════════════════════════════════════════
// "repeat": one declaration, several passes or layouts
// ═══════════════════════════════════════════════════════════════

namespace {

struct RepeatSpec {
    std::string index = "i";  // placeholder name, written {i}
    uint32_t    first = 0;    // value of the first instance
    uint32_t    count = 1;
    int64_t     step  = 1;    // added per instance; -1 counts down

    uint32_t valueOf(uint32_t k) const { return static_cast<uint32_t>(first + step * static_cast<int64_t>(k)); }
};

RepeatSpec parseRepeat(const nlohmann::json& r, const std::string& where) {
    if (!r.is_object()) {
        throw std::runtime_error(where + ": \"repeat\" must be an object like {\"count\": 4, \"index\": \"i\"}");
    }
    RepeatSpec spec;
    spec.index = r.value("index", std::string{"i"});
    // Parsed JSON stores positive integers as unsigned, JSON built in C++
    // from an int as signed; either is fine if it is in range.
    auto readCount = [&](const char* key, int64_t minimum, int64_t fallback) -> uint32_t {
        if (!r.contains(key)) return static_cast<uint32_t>(fallback);
        const auto& v = r[key];
        if (!v.is_number_integer() || v.get<int64_t>() < minimum ||
            v.get<int64_t>() > static_cast<int64_t>(UINT32_MAX)) {
            throw std::runtime_error(where + ": repeat \"" + key + "\" must be an integer of at least " +
                                     std::to_string(minimum));
        }
        return static_cast<uint32_t>(v.get<int64_t>());
    };
    if (!r.contains("count")) {
        throw std::runtime_error(where + ": \"repeat\" needs a \"count\" of at least 1");
    }
    spec.count = readCount("count", 1, 1);
    spec.first = readCount("first", 0, 0);
    if (r.contains("step")) {
        const auto& s = r["step"];
        if (!s.is_number_integer() || s.get<int64_t>() == 0) {
            throw std::runtime_error(where + ": repeat \"step\" must be a non-zero integer");
        }
        spec.step = s.get<int64_t>();
    }
    const int64_t last = static_cast<int64_t>(spec.first) + spec.step * (static_cast<int64_t>(spec.count) - 1);
    if (last < 0 || last > static_cast<int64_t>(UINT32_MAX)) {
        throw std::runtime_error(where + ": repeat counts to " + std::to_string(last) +
                                 "; every value must be at least 0");
    }
    if (spec.index.empty() ||
        !std::all_of(spec.index.begin(), spec.index.end(),
                     [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; })) {
        throw std::runtime_error(where + ": repeat \"index\" must be a plain name, e.g. \"cascade\"");
    }
    return spec;
}

/// N from a view named `prefix` + "N]", or nothing when `view` is not one.
std::optional<uint32_t> indexedView(const std::string& view, const std::string& prefix) {
    if (!view.starts_with(prefix) || !view.ends_with("]")) return std::nullopt;
    const std::string digits = view.substr(prefix.size(), view.size() - prefix.size() - 1);
    if (digits.empty() || digits.size() > 3 ||
        !std::all_of(digits.begin(), digits.end(), [](char c) { return c >= '0' && c <= '9'; })) {
        return std::nullopt;
    }
    return static_cast<uint32_t>(std::stoul(digits));
}

/// "copy" and "blit": a transfer pass with one transfer_src input and one
/// transfer_dst output, and for a blit its "filter".
void parseTransferExecution(PassDeclaration& pass, const nlohmann::json& execJson) {
    const std::string& type = pass.execution.type;
    const bool copy = type == "copy";
    const bool blit = type == "blit";
    const std::string where = "Pass '" + pass.name + "'";
    if (execJson.contains("filter") && !blit) {
        throw std::runtime_error(where + ": \"filter\" applies only to blit, not '" + type + "'");
    }
    if (!copy && !blit) return;

    if (pass.type != PassType::Transfer) {
        throw std::runtime_error(where + ": " + type + " needs a transfer pass (\"type\": \"transfer\")");
    }
    const bool shape = pass.inputs.size() == 1 && pass.outputs.size() == 1 &&
                       pass.inputs[0].usage == ResourceUsage::TransferSrc &&
                       pass.outputs[0].usage == ResourceUsage::TransferDst;
    if (!shape) {
        throw std::runtime_error(where + ": " + type + " takes one input with usage transfer_src and "
                                 "one output with usage transfer_dst");
    }
    if (blit) {
        pass.execution.filter = execJson.value("filter", std::string{"linear"});
        if (pass.execution.filter != "linear" && pass.execution.filter != "nearest") {
            throw std::runtime_error(where + ": unknown filter '" + pass.execution.filter +
                                     "' (expected linear or nearest)");
        }
    }
}

/// "arguments" and "count" of dispatch_indirect and draw_indirect: where in
/// which buffer the GPU reads them. Checks what can be checked before the
/// buffers exist; the compiler checks the buffers.
void parseIndirectExecution(PassDeclaration& pass, const nlohmann::json& execJson,
                            const PipelineCapabilities& capabilities) {
    const std::string& type = pass.execution.type;
    const bool dispatch = type == "dispatch_indirect";
    const bool indexed = type == "draw_indexed" || type == "draw_indexed_indirect";
    const bool draw = type == "draw_indirect" || type == "draw_indexed_indirect";
    const bool anyDraw = draw || indexed || type == "draw";
    const std::string where = "Pass '" + pass.name + "'";
    if (anyDraw && pass.type != PassType::Graphics) {
        throw std::runtime_error(where + ": " + type + " needs a graphics pass");
    }
    if (dispatch && pass.type != PassType::Compute) {
        throw std::runtime_error(where + ": dispatch_indirect needs a compute pass");
    }

    auto readUint = [&](const nlohmann::json& j, const char* key, uint32_t fallback, const std::string& in) {
        if (!j.contains(key)) return fallback;
        const auto& v = j[key];
        if (!v.is_number_integer() || v.get<int64_t>() < 0 || v.get<int64_t>() > static_cast<int64_t>(UINT32_MAX)) {
            throw std::runtime_error(where + ": " + in + " \"" + key + "\" must be a non-negative integer");
        }
        return static_cast<uint32_t>(v.get<int64_t>());
    };
    auto readBuffer = [&](const char* key, std::string& buffer, uint32_t& offset) -> const nlohmann::json* {
        if (!execJson.contains(key)) return nullptr;
        const auto& j = execJson[key];
        if (!j.is_object() || !j.contains("buffer") || !j["buffer"].is_string() ||
            j["buffer"].get<std::string>().empty()) {
            throw std::runtime_error(where + ": \"" + key + "\" must be an object naming a \"buffer\"");
        }
        buffer = j["buffer"].get<std::string>();
        offset = readUint(j, "offset", 0, std::string("\"") + key + "\"");
        if (offset % 4 != 0) {
            throw std::runtime_error(where + ": \"" + key + "\" \"offset\" must be a multiple of 4");
        }
        return &j;
    };

    // The vertex and index buffers of the draw types.
    if (execJson.contains("vertexBuffer") && !anyDraw) {
        throw std::runtime_error(where + ": \"vertexBuffer\" applies to draw types, not '" + type + "'");
    }
    if (execJson.contains("indexBuffer") != indexed) {
        throw std::runtime_error(where + ": \"indexBuffer\" is required by draw_indexed and "
                                 "draw_indexed_indirect, and applies to nothing else");
    }
    readBuffer("vertexBuffer", pass.execution.vertexBuffer, pass.execution.vertexBufferOffset);
    if (const nlohmann::json* index = readBuffer("indexBuffer", pass.execution.indexBuffer,
                                                 pass.execution.indexBufferOffset)) {
        const std::string indexType = index->value("type", std::string{"uint32"});
        if (indexType != "uint32" && indexType != "uint16") {
            throw std::runtime_error(where + ": \"indexBuffer\" \"type\" must be uint32 or uint16");
        }
        pass.execution.indexType = indexType == "uint16" ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32;
    }
    if (type == "draw_indexed") {
        pass.execution.firstIndex = readUint(execJson, "firstIndex", 0, "execution");
        if (execJson.contains("vertexOffset")) {
            if (!execJson["vertexOffset"].is_number_integer()) {
                throw std::runtime_error(where + ": \"vertexOffset\" must be an integer");
            }
            pass.execution.vertexOffset = execJson["vertexOffset"].get<int32_t>();
        }
    }

    if (!dispatch && !draw) {
        for (const char* key : {"arguments", "count"}) {
            if (execJson.contains(key)) {
                throw std::runtime_error(where + ": \"" + key + "\" applies only to dispatch_indirect, "
                                         "draw_indirect and draw_indexed_indirect, not '" + type + "'");
            }
        }
        return;
    }

    const nlohmann::json* arguments = readBuffer("arguments", pass.execution.argumentBuffer,
                                                 pass.execution.argumentOffset);
    if (!arguments) {
        throw std::runtime_error(where + ": " + type + " needs \"arguments\": {\"buffer\": ..., \"offset\": ...}");
    }
    if (dispatch) {
        if (execJson.contains("count") || arguments->contains("drawCount") || arguments->contains("stride")) {
            throw std::runtime_error(where + ": dispatch_indirect takes one set of arguments: no \"count\", "
                                     "\"drawCount\" or \"stride\"");
        }
        return;
    }

    const uint32_t commandSize = indexed ? 20u : 16u;  // VkDraw(Indexed)IndirectCommand
    pass.execution.drawCount = readUint(*arguments, "drawCount", 1, "\"arguments\"");
    pass.execution.drawStride = readUint(*arguments, "stride", commandSize, "\"arguments\"");
    if (pass.execution.drawStride % 4 != 0 || pass.execution.drawStride < commandSize) {
        throw std::runtime_error(where + ": \"arguments\" \"stride\" must be a multiple of 4 and at least " +
                                 std::to_string(commandSize) + ", the size of " +
                                 (indexed ? "VkDrawIndexedIndirectCommand" : "VkDrawIndirectCommand"));
    }
    uint32_t most = pass.execution.drawCount;
    if (const nlohmann::json* count = readBuffer("count", pass.execution.countBuffer, pass.execution.countOffset)) {
        if (arguments->contains("drawCount")) {
            throw std::runtime_error(where + ": with \"count\", the number of draws comes from the count "
                                     "buffer; give \"maxDrawCount\" there instead of \"drawCount\"");
        }
        pass.execution.maxDrawCount = readUint(*count, "maxDrawCount", 0, "\"count\"");
        if (pass.execution.maxDrawCount == 0) {
            throw std::runtime_error(where + ": \"count\" needs a \"maxDrawCount\" of at least 1");
        }
        if (!capabilities.drawIndirectCount) {
            throw std::runtime_error(where + ": \"count\" needs the device feature drawIndirectCount, which "
                                     "this device lacks; declare \"requires\": [\"drawIndirectCount\"] to leave "
                                     "the pass out instead");
        }
        most = pass.execution.maxDrawCount;
    } else if (pass.execution.drawCount == 0) {
        throw std::runtime_error(where + ": \"arguments\" \"drawCount\" must be at least 1");
    }
    if (most > 1 && !capabilities.multiDrawIndirect) {
        throw std::runtime_error(where + ": more than one draw per " + type + " needs the device feature "
                                 "multiDrawIndirect, which this device lacks");
    }
}

/// Replace every {index}, {index+N} and {index-N} in `text` with the
/// instance's value. A string that is nothing but one such placeholder
/// becomes a number, so "layer": "{cascade}" reads as an integer. Braces
/// around anything else are left alone.
nlohmann::json substitute(const std::string& text, const RepeatSpec& spec, int64_t value,
                          const std::string& where) {
    std::string out;
    bool wholeIsPlaceholder = false;
    int64_t wholeValue = 0;

    size_t pos = 0;
    while (pos < text.size()) {
        if (text[pos] != '{') {
            out += text[pos++];
            continue;
        }
        const size_t close = text.find('}', pos);
        if (close == std::string::npos) {
            out += text.substr(pos);
            break;
        }
        const std::string inner = text.substr(pos + 1, close - pos - 1);
        if (!inner.starts_with(spec.index)) {
            out += text.substr(pos, close - pos + 1);
            pos = close + 1;
            continue;
        }
        const std::string rest = inner.substr(spec.index.size());
        int64_t offset = 0;
        if (!rest.empty()) {
            const bool validOffset = rest.size() >= 2 && (rest[0] == '+' || rest[0] == '-') &&
                std::all_of(rest.begin() + 1, rest.end(), [](char c) { return c >= '0' && c <= '9'; });
            if (!validOffset) {
                // {indexSomethingElse}: another name that merely starts with ours
                out += text.substr(pos, close - pos + 1);
                pos = close + 1;
                continue;
            }
            offset = std::stoll(rest.substr(1));
            if (rest[0] == '-') offset = -offset;
        }
        const int64_t result = value + offset;
        if (result < 0) {
            throw std::runtime_error(where + ": \"{" + inner + "}\" is negative for " + spec.index +
                                     " = " + std::to_string(value));
        }
        wholeIsPlaceholder = pos == 0 && close + 1 == text.size();
        wholeValue = result;
        out += std::to_string(result);
        pos = close + 1;
    }

    if (wholeIsPlaceholder) return nlohmann::json(wholeValue);
    return nlohmann::json(out);
}

nlohmann::json substituteAll(const nlohmann::json& j, const RepeatSpec& spec, int64_t value,
                             const std::string& where) {
    if (j.is_string()) return substitute(j.get<std::string>(), spec, value, where);
    if (j.is_array()) {
        nlohmann::json out = nlohmann::json::array();
        for (const auto& e : j) out.push_back(substituteAll(e, spec, value, where));
        return out;
    }
    if (j.is_object()) {
        nlohmann::json out = nlohmann::json::object();
        for (auto it = j.begin(); it != j.end(); ++it) {
            out[it.key()] = substituteAll(it.value(), spec, value, where);
        }
        return out;
    }
    return j;
}

struct ExpandedPass {
    nlohmann::json json;
    std::string    group;       // declared name of a repeated pass, else empty
    uint32_t       index = 0;
    uint32_t       count = 1;
};

/// Passes with "repeat" become one pass per index value. An instance is named
/// by substituting into the declared name, or, if the name has no
/// placeholder, by appending [value].
std::vector<ExpandedPass> expandRepeatedPasses(const nlohmann::json& passes) {
    std::vector<ExpandedPass> out;
    for (const auto& passJson : passes) {
        if (!passJson.is_object() || !passJson.contains("repeat")) {
            out.push_back({passJson, {}, 0, 1});
            continue;
        }
        const std::string name = passJson.value("name", std::string{});
        const std::string where = "Pass '" + name + "'";
        const RepeatSpec spec = parseRepeat(passJson["repeat"], where);

        nlohmann::json base = passJson;
        base.erase("repeat");
        for (uint32_t k = 0; k < spec.count; ++k) {
            const uint32_t value = spec.valueOf(k);
            ExpandedPass instance{substituteAll(base, spec, value, where), name, value, spec.count};
            auto& instanceName = instance.json["name"];
            if (instanceName.is_string() && instanceName.get<std::string>() == name) {
                instanceName = name + "[" + std::to_string(value) + "]";
            }
            out.push_back(std::move(instance));
        }
    }
    return out;
}

/// Descriptor set layouts with "repeat" become one layout per index value;
/// their names must contain the placeholder, e.g. "downsample{m}".
nlohmann::json expandRepeatedLayouts(const nlohmann::json& layouts) {
    nlohmann::json out = nlohmann::json::object();
    for (auto it = layouts.begin(); it != layouts.end(); ++it) {
        if (!it.value().is_object() || !it.value().contains("repeat")) {
            out[it.key()] = it.value();
            continue;
        }
        const std::string where = "Descriptor set layout '" + it.key() + "'";
        const RepeatSpec spec = parseRepeat(it.value()["repeat"], where);
        nlohmann::json base = it.value();
        base.erase("repeat");
        for (uint32_t k = 0; k < spec.count; ++k) {
            const uint32_t value = spec.valueOf(k);
            const auto key = substitute(it.key(), spec, value, where);
            const std::string name = key.is_string() ? key.get<std::string>() : key.dump();
            if (name == it.key()) {
                throw std::runtime_error(where + " repeats but its name has no {" + spec.index +
                                         "}, so every copy would have the same name");
            }
            if (out.contains(name)) {
                throw std::runtime_error(where + ": repeated name '" + name + "' is already declared");
            }
            out[name] = substituteAll(base, spec, value, where);
        }
    }
    return out;
}

} // namespace

// ═══════════════════════════════════════════════════════════════
// JSON Loading
// ═══════════════════════════════════════════════════════════════

void loadGraphFromJson(FrameGraphBuilder& builder, const nlohmann::json& json,
                       const PipelineCapabilities& capabilities) {
    builder.clear();

    // ── Parse samplers (before descriptor sets, as they're referenced by name) ──
    if (json.contains("samplers")) {
        const auto& samplersJson = json["samplers"];
        for (auto it = samplersJson.begin(); it != samplersJson.end(); ++it) {
            SamplerDesc sampler;
            sampler.name = it.key();
            const auto& sj = it.value();

            sampler.magFilter   = sj.value("magFilter", std::string{"linear"});
            sampler.minFilter   = sj.value("minFilter", std::string{"linear"});
            sampler.mipmapMode  = sj.value("mipmapMode", std::string{"linear"});

            // Address mode shorthand or individual axes
            sampler.addressMode = sj.value("addressMode", std::string{""});
            if (sampler.addressMode.empty()) {
                sampler.addressModeU = sj.value("addressModeU", std::string{"repeat"});
                sampler.addressModeV = sj.value("addressModeV", std::string{"repeat"});
                sampler.addressModeW = sj.value("addressModeW", std::string{"repeat"});
            }

            sampler.borderColor = sj.value("borderColor", std::string{"float_opaque_black"});

            sampler.anisotropyEnable = sj.value("anisotropyEnable", false);
            sampler.anisotropyEnable = sj.value("anisotropy", sampler.anisotropyEnable); // Alias
            sampler.maxAnisotropy    = sj.value("maxAnisotropy", 1.0f);

            sampler.compareEnable = sj.value("compareEnable", false);
            sampler.compareOp     = sj.value("compareOp", std::string{"less"});

            sampler.minLod     = sj.value("minLod", 0.0f);
            sampler.maxLod     = sj.value("maxLod", 0.0f);
            sampler.mipLodBias = sj.value("mipLodBias", 0.0f);

            builder.addSampler(std::move(sampler));
        }
    }

    // ── Parse vertex formats (declarative vertex input layouts) ──
    // 頂點之構 — The structure of vertices arises from declaration
    if (json.contains("vertexFormats")) {
        const auto& formatsJson = json["vertexFormats"];
        for (auto it = formatsJson.begin(); it != formatsJson.end(); ++it) {
            FrameGraph::VertexFormatDeclaration format;
            format.name = it.key();

            if (it.value().contains("attributes")) {
                for (const auto& attrJson : it.value()["attributes"]) {
                    FrameGraph::VertexAttributeDeclaration attr;
                    attr.name     = attrJson.at("name").get<std::string>();
                    attr.type     = attrJson.at("type").get<std::string>();
                    attr.location = attrJson.at("location").get<uint32_t>();
                    format.attributes.push_back(std::move(attr));
                }
            }

            builder.getVertexFormatRegistry().registerFormat(format);
        }
    }

    // ── Parse uniform buffers (for framework-managed UBOs) ──
    if (json.contains("uniformBuffers")) {
        const auto& ubosJson = json["uniformBuffers"];
        for (auto it = ubosJson.begin(); it != ubosJson.end(); ++it) {
            UniformBufferDesc ubo;
            ubo.name = it.key();
            const auto& uj = it.value();

            ubo.size     = uj.value("size", 0u);
            ubo.perFrame = uj.value("perFrame", true);
            ubo.frameworkManaged = uj.value("frameworkManaged", true);  // Default to true for JSON-declared UBOs

            // Parse fields for setUniformField() support
            if (uj.contains("fields")) {
                for (const auto& fieldJson : uj["fields"]) {
                    UniformFieldDesc field;
                    field.name   = fieldJson.at("name").get<std::string>();
                    field.type   = fieldJson.value("type", std::string{"float"});
                    field.offset = fieldJson.value("offset", 0u);

                    // Auto-calculate size from type if not specified
                    if (fieldJson.contains("size")) {
                        field.size = fieldJson["size"].get<uint32_t>();
                    } else {
                        // Default sizes based on type
                        if (field.type == "float" || field.type == "int" || field.type == "uint") {
                            field.size = 4;
                        } else if (field.type == "vec2") {
                            field.size = 8;
                        } else if (field.type == "vec3") {
                            field.size = 12;
                        } else if (field.type == "vec4") {
                            field.size = 16;
                        } else if (field.type == "mat4") {
                            field.size = 64;
                        } else {
                            field.size = 4;  // Default
                        }
                    }

                    ubo.fields.push_back(std::move(field));
                }
            }

            builder.addUniformBuffer(std::move(ubo));
        }
    }

    // standardBuffers section removed — replaced by bufferLayouts with updateFrequency

    // ── Parse entity data bindings (v4 declarative) ──
    if (json.contains("entityDataBindings")) {
        const auto& bindingsJson = json["entityDataBindings"];
        for (auto it = bindingsJson.begin(); it != bindingsJson.end(); ++it) {
            EntityDataBindingConfig config;
            config.name = it.key();
            const auto& bj = it.value();

            // Parse perDraw configuration
            if (bj.contains("perDraw")) {
                const auto& pd = bj["perDraw"];
                config.perDraw.method = pd.value("method", std::string{"push_constant"});
                config.perDraw.offset = pd.value("offset", 0u);
                config.perDraw.size = pd.value("size", 64u);  // Default: mat4

                if (pd.contains("stages")) {
                    config.perDraw.stages.clear();
                    for (const auto& stage : pd["stages"]) {
                        config.perDraw.stages.push_back(stage.get<std::string>());
                    }
                }

                // For descriptor set method
                config.perDraw.setIndex = pd.value("set", 0u);
                config.perDraw.bindingIndex = pd.value("binding", 0u);

                // Reference to a bufferLayout by name
                // Support both "layoutRef" (preferred) and "layout" (legacy)
                if (pd.contains("layoutRef")) {
                    config.perDraw.layoutRef = pd["layoutRef"].get<std::string>();
                } else {
                    config.perDraw.layoutRef = pd.value("layout", std::string{});
                }
            }

            // Parse material configuration
            if (bj.contains("material")) {
                const auto& mat = bj["material"];
                config.material.method = mat.value("method", std::string{"descriptor_set"});
                config.material.setIndex = mat.value("set", 1u);

                // Parse texture bindings map (generic - any texture name)
                if (mat.contains("bindings")) {
                    const auto& texBindings = mat["bindings"];
                    for (auto tb = texBindings.begin(); tb != texBindings.end(); ++tb) {
                        config.material.textureBindings[tb.key()] = tb.value().get<uint32_t>();
                    }
                }

                // Reference to a bufferLayout by name for declarative texture binding
                // 緩衝之構 — The structure of buffers guides all bindings
                config.material.layoutRef = mat.value("layoutRef", std::string{});
            }

            // Parse skeleton SSBO configuration (optional - for skinned geometry)
            // 骨之繫 — The binding of bones
            if (bj.contains("skeleton")) {
                const auto& skel = bj["skeleton"];
                config.skeleton.layoutRef = skel.value("layoutRef", std::string{});
            }

            builder.addEntityDataBinding(std::move(config));
        }
    }

    // ── Parse buffer layouts (generic buffer/texture definitions) ──
    // 緩衝之構 — The structure of buffers
    if (json.contains("bufferLayouts")) {
        const auto& layoutsJson = json["bufferLayouts"];
        for (auto it = layoutsJson.begin(); it != layoutsJson.end(); ++it) {
            BufferLayoutDesc layout;
            layout.name = it.key();
            const auto& lj = it.value();

            // Parse usage type
            std::string usageStr = lj.value("usage", std::string{"uniform_buffer"});
            if (usageStr == "push_constant") {
                layout.usage = BufferUsageType::PushConstant;
            } else if (usageStr == "uniform_buffer") {
                layout.usage = BufferUsageType::UniformBuffer;
            } else if (usageStr == "storage_buffer") {
                layout.usage = BufferUsageType::StorageBuffer;
            } else if (usageStr == "descriptor_set") {
                layout.usage = BufferUsageType::DescriptorSet;
            } else {
                throw std::runtime_error("Buffer layout '" + layout.name + "': unknown usage '" + usageStr +
                                         "'; one of uniform_buffer, storage_buffer, push_constant, descriptor_set");
            }

            // A missing key defaults to std140. An unrecognised value throws.
            layout.packing = parsePackingRule(lj.value("packing", std::string{"std140"}));

            // Parse update frequency (for dot-path-driven auto-fill)
            std::string freqStr = lj.value("updateFrequency", std::string{"manual"});
            if (freqStr == "per_frame") {
                layout.updateFrequency = BufferUpdateFrequency::PerFrame;
            } else if (freqStr == "every_n_frames") {
                layout.updateFrequency = BufferUpdateFrequency::EveryNFrames;
                layout.updateFrequencyN = lj.value("updateFrequencyN", 1u);
            } else if (freqStr == "on_change") {
                layout.updateFrequency = BufferUpdateFrequency::OnChange;
            } else if (freqStr == "once") {
                layout.updateFrequency = BufferUpdateFrequency::Once;
            } else {
                layout.updateFrequency = BufferUpdateFrequency::Manual;
            }

            // Parse binding configuration
            if (lj.contains("binding")) {
                const auto& bj = lj["binding"];
                layout.binding.set = bj.value("set", 0u);
                layout.binding.binding = bj.value("binding", 0u);
                layout.binding.offset = bj.value("offset", 0u);

                if (bj.contains("stages")) {
                    layout.binding.stages.clear();
                    for (const auto& stage : bj["stages"]) {
                        layout.binding.stages.push_back(stage.get<std::string>());
                    }
                }
            }

            // Parse fields (for buffer types)
            if (lj.contains("fields")) {
                for (const auto& fieldJson : lj["fields"]) {
                    BufferFieldDesc field;
                    field.name = fieldJson.at("name").get<std::string>();
                    field.arrayCount = fieldJson.value("arrayCount", 1u);
                    // An explicitly declared offset of 0 has to be distinguishable
                    // from "not declared", so record presence rather than relying
                    // on the value.
                    field.hasExplicitOffset = fieldJson.contains("offset");
                    field.offset = fieldJson.value("offset", 0u);

                    // ─── Parse source for dot-path resolution ─────
                    // JSON: "source": "entity.material.params.baseColorFactor"
                    field.source = fieldJson.value("source", std::string{});
                    if (fieldJson.contains("default")) {
                        const auto& dj = fieldJson["default"];
                        if (dj.is_number()) {
                            field.defaultValue = {dj.get<float>()};
                        } else if (dj.is_array() && std::all_of(dj.begin(), dj.end(),
                                       [](const nlohmann::json& v) { return v.is_number(); })) {
                            for (const auto& v : dj) field.defaultValue.push_back(v.get<float>());
                        } else {
                            throw std::runtime_error("Buffer layout field '" + field.name +
                                                     "': \"default\" must be a number or an array of numbers");
                        }
                    }

                    // Throws on an unknown type string. Treating one as float
                    // would shift every offset from that field onward.
                    field.type = parseFieldType(fieldJson.value("type", std::string{"float"}));

                    layout.fields.push_back(std::move(field));
                }
            }

            // Parse textures (for descriptor_set type)
            if (lj.contains("textures")) {
                for (const auto& texJson : lj["textures"]) {
                    TextureBindingDesc tex;
                    tex.name = texJson.at("name").get<std::string>();
                    tex.binding = texJson.value("binding", 0u);

                    if (texJson.contains("stages")) {
                        tex.stages.clear();
                        for (const auto& stage : texJson["stages"]) {
                            tex.stages.push_back(stage.get<std::string>());
                        }
                    }

                    layout.textures.push_back(std::move(tex));
                }
            }

            // ─── Parse elementCount for SSBO arrays ───────────────────
            if (lj.contains("elementCount")) {
                layout.elementCount = lj["elementCount"].get<uint32_t>();
            }

            // ─── Parse source initialization config ───────────────────
            if (lj.contains("source")) {
                const auto& srcJson = lj["source"];
                layout.initConfig.type = srcJson.value("type", std::string{"initializer"});
                layout.initConfig.seed = srcJson.value("seed", 42u);

                // Phase 2: buffer_ref source — reference another graph's target SSBO
                if (layout.initConfig.type == "buffer_ref") {
                    layout.initConfig.bufferRef = srcJson.value("ref", std::string{});
                    layout.initConfig.bufferRefFrequency = srcJson.value("frequency", std::string{"per_frame"});
                }

                // Phase 4: file source — load binary data from disk
                if (layout.initConfig.type == "file") {
                    layout.initConfig.filePath = srcJson.value("path", std::string{});
                }

                // Filled from the scene every frame.
                if (layout.initConfig.type == "scene") {
                    layout.initConfig.sceneData = srcJson.value("data", std::string{});
                    if (layout.initConfig.sceneData != "lights" && layout.initConfig.sceneData != "rayInstances") {
                        throw std::runtime_error("Buffer layout '" + layout.name + "': a \"scene\" source needs "
                                                 "\"data\": \"lights\" or \"rayInstances\"");
                    }
                    if (layout.usage != BufferUsageType::StorageBuffer || layout.elementCount == 0) {
                        throw std::runtime_error("Buffer layout '" + layout.name + "': a \"scene\" source fills "
                                                 "a storage_buffer with an \"elementCount\", its capacity");
                    }
                }

                if (srcJson.contains("fields")) {
                    const auto& fieldsInitJson = srcJson["fields"];
                    for (auto fit = fieldsInitJson.begin(); fit != fieldsInitJson.end(); ++fit) {
                        SSBOFieldInit fi;
                        fi.fieldName = fit.key();
                        const auto& fiJson = fit.value();

                        if (fiJson.contains("randomRange")) {
                            fi.type = SSBOFieldInitType::RandomRange;
                            const auto& rr = fiJson["randomRange"];
                            if (rr.contains("min")) {
                                for (size_t i = 0; i < std::min(rr["min"].size(), size_t(4)); ++i)
                                    fi.min[i] = rr["min"][i].get<float>();
                            }
                            if (rr.contains("max")) {
                                for (size_t i = 0; i < std::min(rr["max"].size(), size_t(4)); ++i)
                                    fi.max[i] = rr["max"][i].get<float>();
                            }
                        } else if (fiJson.contains("constant")) {
                            fi.type = SSBOFieldInitType::Constant;
                            const auto& c = fiJson["constant"];
                            for (size_t i = 0; i < std::min(c.size(), size_t(4)); ++i)
                                fi.value[i] = c[i].get<float>();

                        // Phase 5: GaussianRange — normal distribution per component
                        } else if (fiJson.contains("gaussian")) {
                            fi.type = SSBOFieldInitType::GaussianRange;
                            const auto& g = fiJson["gaussian"];
                            if (g.contains("mean")) {
                                for (size_t i = 0; i < std::min(g["mean"].size(), size_t(4)); ++i)
                                    fi.mean[i] = g["mean"][i].get<float>();
                            }
                            if (g.contains("stddev")) {
                                for (size_t i = 0; i < std::min(g["stddev"].size(), size_t(4)); ++i)
                                    fi.stddev[i] = g["stddev"][i].get<float>();
                            }

                        // Phase 5: Grid — 3D lattice placement
                        } else if (fiJson.contains("grid")) {
                            fi.type = SSBOFieldInitType::Grid;
                            const auto& gr = fiJson["grid"];
                            if (gr.contains("dimensions")) {
                                for (size_t i = 0; i < std::min(gr["dimensions"].size(), size_t(3)); ++i)
                                    fi.gridDimensions[i] = gr["dimensions"][i].get<uint32_t>();
                            }
                            if (gr.contains("origin")) {
                                for (size_t i = 0; i < std::min(gr["origin"].size(), size_t(3)); ++i)
                                    fi.gridOrigin[i] = gr["origin"][i].get<float>();
                            }
                            if (gr.contains("spacing")) {
                                for (size_t i = 0; i < std::min(gr["spacing"].size(), size_t(3)); ++i)
                                    fi.gridSpacing[i] = gr["spacing"][i].get<float>();
                            }
                            fi.gridW = gr.value("w", 1.0f);

                        // Phase 5: Sphere — spherical distribution
                        } else if (fiJson.contains("sphere")) {
                            fi.type = SSBOFieldInitType::Sphere;
                            const auto& sp = fiJson["sphere"];
                            if (sp.contains("center")) {
                                for (size_t i = 0; i < std::min(sp["center"].size(), size_t(3)); ++i)
                                    fi.sphereCenter[i] = sp["center"][i].get<float>();
                            }
                            fi.sphereRadius = sp.value("radius", 1.0f);
                            fi.sphereSurface = (sp.value("mode", std::string{"volume"}) == "surface");
                            fi.sphereW = sp.value("w", 1.0f);
                        }

                        layout.initConfig.fieldInits.push_back(std::move(fi));
                    }
                }
            }

            // Phase 2: Parse target (cross-graph output declaration)
            // Phase 3: Extended target with readback configuration
            if (lj.contains("target")) {
                if (lj["target"].is_string()) {
                    // Simple string form: "target": "particles.currentState"
                    layout.target = lj["target"].get<std::string>();
                } else if (lj["target"].is_object()) {
                    const auto& targetJson = lj["target"];
                    layout.target = targetJson.value("name", std::string{});

                    // Parse readback policy
                    if (targetJson.contains("readback")) {
                        const auto& rbJson = targetJson["readback"];
                        layout.readbackPolicy.enabled = true;

                        std::string rbFreq = rbJson.value("frequency", std::string{"manual"});
                        if (rbFreq == "per_frame") {
                            layout.readbackPolicy.frequency = BufferUpdateFrequency::PerFrame;
                        } else if (rbFreq == "every_n_frames") {
                            layout.readbackPolicy.frequency = BufferUpdateFrequency::EveryNFrames;
                            layout.readbackPolicy.n = rbJson.value("n", 1u);
                        } else if (rbFreq == "once") {
                            layout.readbackPolicy.frequency = BufferUpdateFrequency::Once;
                        } else {
                            layout.readbackPolicy.frequency = BufferUpdateFrequency::Manual;
                        }

                        layout.readbackPolicy.callbackEnabled = rbJson.value("callback", false);
                        layout.readbackPolicy.ringDepth = rbJson.value("ringDepth", 0u);
                    }

                    // Phase 4: Parse save policy (GPU→disk persistence)
                    if (targetJson.contains("save")) {
                        const auto& saveJson = targetJson["save"];
                        layout.savePolicy.enabled = true;
                        layout.savePolicy.path = saveJson.value("path", std::string{});
                        layout.savePolicy.trigger = saveJson.value("trigger", std::string{"manual"});
                        layout.savePolicy.n = saveJson.value("n", 1u);
                        layout.savePolicy.autoCreateDirectories = saveJson.value("autoCreateDirectories", true);
                    }

                    // Phase 4: Auto-enable readback when save is enabled
                    if (layout.savePolicy.enabled && !layout.readbackPolicy.enabled) {
                        layout.readbackPolicy.enabled = true;
                        layout.readbackPolicy.callbackEnabled = true;
                        if (layout.savePolicy.trigger == "every_n_frames") {
                            layout.readbackPolicy.frequency = BufferUpdateFrequency::EveryNFrames;
                            layout.readbackPolicy.n = layout.savePolicy.n;
                        } else {
                            layout.readbackPolicy.frequency = BufferUpdateFrequency::Manual;
                        }
                    }
                    // Phase 4: Auto-set transferDirection for readback when save requires it
                    if (layout.savePolicy.enabled &&
                        layout.memoryPolicy.transferDirection == TransferDirection::GpuOnly) {
                        layout.memoryPolicy.transferDirection = TransferDirection::GpuToCpu;
                    }
                }
            }

            // Phase 3: Parse memory policy
            if (lj.contains("memory")) {
                const auto& memJson = lj["memory"];

                // Parse memory location
                std::string locStr = memJson.value("location", std::string{"device_local"});
                if (locStr == "device_local") {
                    layout.memoryPolicy.location = MemoryLocation::DeviceLocal;
                } else if (locStr == "host_visible") {
                    layout.memoryPolicy.location = MemoryLocation::HostVisible;
                } else if (locStr == "host_coherent") {
                    layout.memoryPolicy.location = MemoryLocation::HostCoherent;
                }

                // Parse staging strategy
                std::string stagingStr = memJson.value("staging", std::string{"auto"});
                if (stagingStr == "auto") {
                    layout.memoryPolicy.staging = StagingStrategy::Auto;
                } else if (stagingStr == "persistent") {
                    layout.memoryPolicy.staging = StagingStrategy::Persistent;
                } else if (stagingStr == "none") {
                    layout.memoryPolicy.staging = StagingStrategy::None;
                }

                // Parse transfer direction
                std::string dirStr = memJson.value("transferDirection", std::string{"gpu_only"});
                if (dirStr == "gpu_only") {
                    layout.memoryPolicy.transferDirection = TransferDirection::GpuOnly;
                } else if (dirStr == "cpu_to_gpu") {
                    layout.memoryPolicy.transferDirection = TransferDirection::CpuToGpu;
                } else if (dirStr == "gpu_to_cpu") {
                    layout.memoryPolicy.transferDirection = TransferDirection::GpuToCpu;
                } else if (dirStr == "bidirectional") {
                    layout.memoryPolicy.transferDirection = TransferDirection::Bidirectional;
                }
            }

            builder.addBufferLayout(std::move(layout));
        }
    }

    // ── Parse descriptor set layouts (before resources/passes, as they're referenced by name) ──
    if (json.contains("descriptorSetLayouts")) {
        const auto layoutsJson = expandRepeatedLayouts(json["descriptorSetLayouts"]);
        for (auto it = layoutsJson.begin(); it != layoutsJson.end(); ++it) {
            DescriptorSetLayoutDesc layoutDesc;
            layoutDesc.name = it.key();

            if (it.value().contains("bindings")) {
                for (const auto& bindingJson : it.value()["bindings"]) {
                    DescriptorBindingDesc binding;
                    binding.binding = bindingJson.at("binding").get<uint32_t>();
                    binding.type    = bindingJson.at("type").get<std::string>();
                    binding.count   = bindingJson.value("count", 1u);
                    binding.name    = bindingJson.value("name", "binding_" + std::to_string(binding.binding));

                    if (bindingJson.contains("stages")) {
                        for (const auto& stage : bindingJson["stages"]) {
                            binding.stages.push_back(stage.get<std::string>());
                        }
                    }

                    // Auto-binding fields
                    binding.autoBindResource = bindingJson.value("autoBindResource", std::string{});
                    binding.autoBindSampler  = bindingJson.value("autoBindSampler", std::string{});
                    binding.autoBindBuffer   = bindingJson.value("autoBindBuffer", std::string{});
                    binding.externalImage    = bindingJson.value("externalImage", std::string{});
                    binding.source           = bindingJson.value("source", std::string{});
                    binding.alphaTested      = bindingJson.value("alphaTested", false);
                    if (binding.alphaTested && binding.type != "acceleration_structure") {
                        throw std::runtime_error("Binding '" + binding.name + "' of layout '" + layoutDesc.name +
                                                 "': \"alphaTested\" is for acceleration_structure bindings");
                    }
                    {
                        // A texel buffer: a buffer of bufferLayouts seen as texels of "format".
                        const bool texel = binding.type == "uniform_texel_buffer" ||
                                           binding.type == "storage_texel_buffer";
                        const std::string where = "Binding '" + binding.name + "' of layout '" + layoutDesc.name + "'";
                        if (bindingJson.contains("format") != texel) {
                            throw std::runtime_error(where + ": texel buffers take a \"format\", and only they do");
                        }
                        if (texel) {
                            binding.texelFormat = JsonUtils::stringToFormat(bindingJson["format"].get<std::string>());
                            if (binding.autoBindBuffer.empty() || binding.count != 1) {
                                throw std::runtime_error(where + ": a texel buffer names one buffer with "
                                                         "\"autoBindBuffer\"");
                            }
                        }
                    }
                    if (!binding.source.empty() &&
                        (binding.source != "sceneTextures" || binding.type != "combined_image_sampler")) {
                        throw std::runtime_error("Binding '" + binding.name + "' of layout '" + layoutDesc.name +
                                                 "': \"source\": \"sceneTextures\" is the only binding source, "
                                                 "on a combined_image_sampler");
                    }
                    binding.autoBindSubresource = parseSubresource(
                        bindingJson, "Binding '" + binding.name + "' of layout '" + layoutDesc.name + "'");

                    layoutDesc.bindings.push_back(std::move(binding));
                }
            }

            builder.addDescriptorSetLayout(std::move(layoutDesc));
        }
    }

    // ── Parse resources ──
    if (json.contains("resources")) {
        for (const auto& resJson : json["resources"]) {
            std::string name = resJson.at("name").get<std::string>();
            ResourceKind kind = JsonUtils::stringToResourceKind(resJson.at("kind").get<std::string>());
            bool imported = resJson.value("imported", false);

            if (kind == ResourceKind::Image) {
                if (imported) {
                    VkFormat format = VK_FORMAT_UNDEFINED;
                    if (resJson.contains("image") && resJson["image"].contains("format")) {
                        format = JsonUtils::stringToFormat(resJson["image"]["format"].get<std::string>());
                    }
                    builder.importImage(name, format);
                } else {
                    ImageDesc desc;
                    if (resJson.contains("image")) {
                        const auto& imgJson = resJson["image"];
                        desc.width       = imgJson.value("width", 0u);
                        desc.height      = imgJson.value("height", 0u);
                        desc.widthScale  = imgJson.value("widthScale", 1.0f);
                        desc.heightScale = imgJson.value("heightScale", 1.0f);
                        if (imgJson.contains("mipLevels") && imgJson["mipLevels"].is_string()) {
                            if (imgJson["mipLevels"].get<std::string>() != "full") {
                                throw std::runtime_error("Resource '" + name +
                                    "': mipLevels must be a number or \"full\"");
                            }
                            desc.mipLevels = 0;
                        } else {
                            desc.mipLevels = imgJson.value("mipLevels", 1u);
                            if (desc.mipLevels == 0) {
                                throw std::runtime_error("Resource '" + name +
                                    "': mipLevels must be at least 1 (or \"full\")");
                            }
                        }
                        desc.viewType = stringToImageViewKind(imgJson.value("viewType", std::string{"auto"}));
                        const uint32_t defaultLayers = desc.viewType == ImageViewKind::Cube ? 6u : 1u;
                        desc.arrayLayers = imgJson.value("arrayLayers", defaultLayers);
                        if (desc.arrayLayers == 0) {
                            throw std::runtime_error("Resource '" + name + "': arrayLayers must be at least 1");
                        }
                        if (desc.viewType == ImageViewKind::Cube && desc.arrayLayers != 6) {
                            throw std::runtime_error("Resource '" + name +
                                "': a cube image has exactly 6 layers");
                        }
                        if (desc.viewType == ImageViewKind::CubeArray && desc.arrayLayers % 6 != 0) {
                            throw std::runtime_error("Resource '" + name +
                                "': a cube_array image has a multiple of 6 layers");
                        }
                        if (desc.viewType == ImageViewKind::Tex2D && desc.arrayLayers != 1) {
                            throw std::runtime_error("Resource '" + name +
                                "': a 2d image has one layer; use 2d_array for more");
                        }
                        desc.depth = imgJson.value("depth", 1u);
                        if (desc.depth == 0) {
                            throw std::runtime_error("Resource '" + name + "': depth must be at least 1");
                        }
                        const bool volume = desc.viewType == ImageViewKind::Tex3D;
                        if (!volume && desc.depth != 1) {
                            throw std::runtime_error("Resource '" + name +
                                "': depth is the slice count of a 3d image; add \"viewType\": \"3d\"");
                        }
                        if (volume && desc.arrayLayers != 1) {
                            throw std::runtime_error("Resource '" + name + "': a 3d image has one layer");
                        }
                        desc.transient   = imgJson.value("transient", false);
                        desc.persistent  = imgJson.value("persistent", false);
                        if (desc.persistent) {
                            // Cleared once when the graph is compiled.
                            desc.additionalUsage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
                        }

                        if (imgJson.contains("format")) {
                            desc.format = JsonUtils::stringToFormat(imgJson["format"].get<std::string>());
                        }
                        if (imgJson.contains("samples")) {
                            const auto& s = imgJson["samples"];
                            const int64_t n = s.is_number_integer() ? s.get<int64_t>() : 0;
                            if (n < 1 || n > 64 || (n & (n - 1)) != 0) {
                                throw std::runtime_error("Resource '" + name +
                                    "': samples must be 1, 2, 4, 8, 16, 32 or 64");
                            }
                            desc.samples = static_cast<VkSampleCountFlagBits>(n);
                        }
                        if (desc.samples != VK_SAMPLE_COUNT_1_BIT) {
                            if (desc.mipLevels != 1) {
                                throw std::runtime_error("Resource '" + name + "': a multisampled image has one mip level");
                            }
                            if (desc.viewType != ImageViewKind::Auto && desc.viewType != ImageViewKind::Tex2D &&
                                desc.viewType != ImageViewKind::Tex2DArray) {
                                throw std::runtime_error("Resource '" + name +
                                    "': a multisampled image is 2d or 2d_array");
                            }
                        }
                        if (volume && (desc.format == VK_FORMAT_UNDEFINED || isDepthFormat(desc.format))) {
                            throw std::runtime_error("Resource '" + name + "': a 3d image needs a colour format");
                        }
                    }
                    builder.declareImage(name, desc);
                }
            } else {
                // Buffer
                if (imported) {
                    builder.importBuffer(name);
                } else {
                    BufferDesc desc;
                    if (resJson.contains("buffer")) {
                        const auto& bufJson = resJson["buffer"];
                        desc.size = bufJson.value("size", static_cast<VkDeviceSize>(0));
                        desc.persistentlyMapped = bufJson.value("persistentlyMapped", false);
                    }
                    builder.declareBuffer(name, desc);
                }
            }

            // ── Parse target/readback/save on resources (render target data flow) ──
            // Mirrors SSBO target/readback/save from bufferLayouts section.
            auto* resDecl = builder.getMutableResource(name);
            if (resDecl) {
                // Parse target (string or object form)
                if (resJson.contains("target")) {
                    if (resJson["target"].is_string()) {
                        resDecl->target = resJson["target"].get<std::string>();
                    } else if (resJson["target"].is_object()) {
                        const auto& targetJson = resJson["target"];
                        resDecl->target = targetJson.value("name", std::string{});
                    }
                }

                // Parse readback policy — check resource level first, then inside target object
                const nlohmann::json* rbSource = nullptr;
                if (resJson.contains("readback")) {
                    rbSource = &resJson["readback"];
                } else if (resJson.contains("target") && resJson["target"].is_object() && resJson["target"].contains("readback")) {
                    rbSource = &resJson["target"]["readback"];
                }
                if (rbSource) {
                    const auto& rbJson = *rbSource;
                    resDecl->readbackPolicy.enabled = true;
                    resDecl->readbackPolicy.frequency = rbJson.value("frequency", std::string{"manual"});
                    resDecl->readbackPolicy.n = rbJson.value("n", 1u);
                    resDecl->readbackPolicy.callbackEnabled = rbJson.value("callback", false);
                    resDecl->readbackPolicy.ringDepth = rbJson.value("ringDepth", 0u);
                }

                // Parse save policy — check resource level first, then inside target object
                const nlohmann::json* saveSource = nullptr;
                if (resJson.contains("save")) {
                    saveSource = &resJson["save"];
                } else if (resJson.contains("target") && resJson["target"].is_object() && resJson["target"].contains("save")) {
                    saveSource = &resJson["target"]["save"];
                }
                if (saveSource) {
                    const auto& saveJson = *saveSource;
                    resDecl->savePolicy.enabled = true;
                    resDecl->savePolicy.path = saveJson.value("path", std::string{});
                    resDecl->savePolicy.trigger = saveJson.value("trigger", std::string{"manual"});
                    resDecl->savePolicy.n = saveJson.value("n", 1u);
                    resDecl->savePolicy.autoCreateDirectories = saveJson.value("autoCreateDirectories", true);
                }

                // Auto-enable readback when save is enabled
                if (resDecl->savePolicy.enabled && !resDecl->readbackPolicy.enabled) {
                    resDecl->readbackPolicy.enabled = true;
                    resDecl->readbackPolicy.callbackEnabled = true;
                    if (resDecl->savePolicy.trigger == "every_n_frames") {
                        resDecl->readbackPolicy.frequency = "every_n_frames";
                        resDecl->readbackPolicy.n = resDecl->savePolicy.n;
                    } else {
                        resDecl->readbackPolicy.frequency = "manual";
                    }
                }
                if (resDecl->kind == ResourceKind::Image && resDecl->readbackPolicy.enabled &&
                    resDecl->imageDesc.viewType == ImageViewKind::Tex3D) {
                    throw std::runtime_error("Resource '" + name + "': a 3d image has no readback or save");
                }
                if (resDecl->kind == ResourceKind::Image && resDecl->readbackPolicy.enabled &&
                    resDecl->imageDesc.samples != VK_SAMPLE_COUNT_1_BIT) {
                    throw std::runtime_error("Resource '" + name + "': a multisampled image has no readback or "
                                             "save; resolve it into one that does");
                }
            }
        }
    }

    // ── Parse passes ──
    if (json.contains("passes")) {
        for (const auto& expanded : expandRepeatedPasses(json["passes"])) {
            const auto& passJson = expanded.json;

            // "requires": capabilities without which the pass is left out.
            if (passJson.contains("requires")) {
                const auto& req = passJson["requires"];
                const std::string name = passJson.value("name", std::string{});
                if (!req.is_array()) {
                    throw std::runtime_error("Pass '" + name + "': \"requires\" must be an array, e.g. [\"rayQuery\"]");
                }
                bool met = true;
                for (const auto& r : req) {
                    const std::string what = r.is_string() ? r.get<std::string>() : r.dump();
                    if (what == "rayQuery") {
                        met = met && capabilities.rayQuery;
                    } else if (what == "drawIndirectCount") {
                        met = met && capabilities.drawIndirectCount;
                    } else if (what == "geometryShader") {
                        met = met && capabilities.geometryShader;
                    } else if (what == "tessellationShader") {
                        met = met && capabilities.tessellationShader;
                    } else if (what == "meshShader") {
                        met = met && capabilities.meshShader;
                    } else if (what == "taskShader") {
                        met = met && capabilities.taskShader;
                    } else if (what == "rayTracingPipeline") {
                        met = met && capabilities.rayTracingPipeline;
                    } else if (what == "bindlessTextures") {
                        met = met && capabilities.bindlessTextures;
                    } else {
                        throw std::runtime_error("Pass '" + name + "' requires '" + what +
                                                 "', which is not a capability (expected rayQuery, "
                                                 "drawIndirectCount, geometryShader, tessellationShader, "
                                                 "meshShader, taskShader, rayTracingPipeline or "
                                                 "bindlessTextures)");
                    }
                }
                if (!met) {
                    builder.addUnavailablePass(name);
                    if (!expanded.group.empty()) builder.addUnavailablePass(expanded.group);
                    continue;
                }
            }

            PassDeclaration pass;
            pass.name = passJson.at("name").get<std::string>();
            pass.repeatGroup = expanded.group;
            pass.repeatIndex = expanded.index;
            pass.repeatCount = expanded.count;
            pass.type = JsonUtils::stringToPassType(passJson.at("type").get<std::string>());

            // Parse queue type (default: "graphics" for backward compatibility)
            if (passJson.contains("queue")) {
                pass.queueType = JsonUtils::stringToQueueType(passJson["queue"].get<std::string>());
                if (pass.queueType == QueueType::Compute && pass.type == PassType::Graphics) {
                    throw std::runtime_error("Pass '" + pass.name + "': a graphics pass cannot run on the compute "
                                             "queue; \"queue\": \"compute\" is for compute, transfer and "
                                             "ray_tracing passes");
                }
            }

            // Parse inputs
            if (passJson.contains("inputs")) {
                for (const auto& inputJson : passJson["inputs"]) {
                    pass.inputs.push_back(parseResourceAccess(inputJson, builder));
                }
            }

            // Parse outputs
            if (passJson.contains("outputs")) {
                for (const auto& outputJson : passJson["outputs"]) {
                    pass.outputs.push_back(parseResourceAccess(outputJson, builder));
                    // A resolve target is written by the pass too.
                    if (pass.outputs.back().resolveTarget.valid()) {
                        ResourceAccess resolve;
                        resolve.handle = pass.outputs.back().resolveTarget;
                        resolve.usage = ResourceUsage::ColorResolve;
                        pass.outputs.push_back(resolve);
                    }
                }
            }

            // Parse pipeline description
            if (passJson.contains("pipeline")) {
                const auto& pipeJson = passJson["pipeline"];
                pass.pipelineDesc.vertexShader   = pipeJson.value("vertexShader", std::string{});
                pass.pipelineDesc.fragmentShader = pipeJson.value("fragmentShader", std::string{});
                pass.pipelineDesc.computeShader  = pipeJson.value("computeShader", std::string{});
                pass.pipelineDesc.tessControlShader    = pipeJson.value("tessControlShader", std::string{});
                pass.pipelineDesc.tessEvaluationShader = pipeJson.value("tessEvaluationShader", std::string{});
                pass.pipelineDesc.geometryShader       = pipeJson.value("geometryShader", std::string{});
                pass.pipelineDesc.patchControlPoints   = pipeJson.value("patchControlPoints", 0u);
                pass.pipelineDesc.taskShader           = pipeJson.value("taskShader", std::string{});
                pass.pipelineDesc.meshShader           = pipeJson.value("meshShader", std::string{});
                {
                    // A ray tracing pass's shaders, and only its.
                    auto& pd = pass.pipelineDesc;
                    const std::string where = "Pass '" + pass.name + "'";
                    const bool rt = pass.type == PassType::RayTracing;
                    pd.rayGenShader = pipeJson.value("rayGenShader", std::string{});
                    if (pipeJson.contains("missShaders")) {
                        if (!pipeJson["missShaders"].is_array()) {
                            throw std::runtime_error(where + ": missShaders must be an array of paths");
                        }
                        for (const auto& m : pipeJson["missShaders"]) pd.missShaders.push_back(m.get<std::string>());
                    }
                    if (pipeJson.contains("hitGroups")) {
                        if (!pipeJson["hitGroups"].is_array()) {
                            throw std::runtime_error(where + ": hitGroups must be an array of objects");
                        }
                        for (const auto& g : pipeJson["hitGroups"]) {
                            PipelineDesc::HitGroupDesc group;
                            group.closestHit   = g.value("closestHit", std::string{});
                            group.anyHit       = g.value("anyHit", std::string{});
                            group.intersection = g.value("intersection", std::string{});
                            if (group.closestHit.empty() && group.anyHit.empty() && group.intersection.empty()) {
                                throw std::runtime_error(where + ": a hit group names closestHit, anyHit or "
                                                         "intersection");
                            }
                            pd.hitGroups.push_back(group);
                        }
                    }
                    pd.maxRecursionDepth = pipeJson.value("maxRecursionDepth", 1u);
                    const bool rtKeys = !pd.rayGenShader.empty() || !pd.missShaders.empty() || !pd.hitGroups.empty();
                    if (rtKeys && !rt) {
                        throw std::runtime_error(where + ": rayGenShader, missShaders and hitGroups belong to a "
                                                 "ray_tracing pass");
                    }
                    if (rt) {
                        bool other = false;
                        for (const auto& key : kShaderKeys) other = other || !pd.shader(key.stage).empty();
                        if (pd.rayGenShader.empty() || other) {
                            throw std::runtime_error(where + ": a ray_tracing pass takes a rayGenShader, and "
                                                     "missShaders and hitGroups, but no other shaders");
                        }
                        if (pd.maxRecursionDepth < 1) {
                            throw std::runtime_error(where + ": maxRecursionDepth must be at least 1");
                        }
                        if (!capabilities.rayTracingPipeline) {
                            throw std::runtime_error(where + " is a ray_tracing pass, which this device lacks; "
                                                     "declare \"requires\": [\"rayTracingPipeline\"] to leave "
                                                     "the pass out on such devices");
                        }
                    }
                }
                {
                    const auto& pd = pass.pipelineDesc;
                    const std::string where = "Pass '" + pass.name + "'";
                    const bool mesh = !pd.meshShader.empty();
                    if (!pd.taskShader.empty() && !mesh) {
                        throw std::runtime_error(where + ": a taskShader needs a meshShader");
                    }
                    if (mesh && (pass.type != PassType::Graphics || !pd.vertexShader.empty() ||
                                 !pd.tessControlShader.empty() || !pd.tessEvaluationShader.empty() ||
                                 !pd.geometryShader.empty())) {
                        throw std::runtime_error(where + ": a meshShader takes the place of the vertex, "
                                                 "tessellation and geometry shaders of a graphics pass");
                    }
                    if (mesh && !capabilities.meshShader) {
                        throw std::runtime_error(where + " uses a mesh shader, which this device lacks; "
                                                 "declare \"requires\": [\"meshShader\"] to leave the "
                                                 "pass out on such devices");
                    }
                    if (!pd.taskShader.empty() && !capabilities.taskShader) {
                        throw std::runtime_error(where + " uses a task shader, which this device lacks; "
                                                 "declare \"requires\": [\"taskShader\"] to leave the "
                                                 "pass out on such devices");
                    }
                }
                {
                    const auto& pd = pass.pipelineDesc;
                    const std::string where = "Pass '" + pass.name + "'";
                    const bool tess = !pd.tessControlShader.empty() || !pd.tessEvaluationShader.empty();
                    const bool extra = tess || !pd.geometryShader.empty();
                    if (extra && (pass.type != PassType::Graphics || pd.vertexShader.empty())) {
                        throw std::runtime_error(where + ": tessellation and geometry shaders need a graphics "
                                                 "pass with a vertexShader");
                    }
                    if (tess && (pd.tessControlShader.empty() || pd.tessEvaluationShader.empty())) {
                        throw std::runtime_error(where + ": tessellation takes both tessControlShader and "
                                                 "tessEvaluationShader");
                    }
                    const bool patches = pipeJson.value("topology", std::string{}) == "patch_list";
                    if (tess != patches) {
                        throw std::runtime_error(where + ": tessellation shaders and \"topology\": "
                                                 "\"patch_list\" go together");
                    }
                    if (tess && (pd.patchControlPoints < 1 || pd.patchControlPoints > 32)) {
                        throw std::runtime_error(where + ": patchControlPoints must be 1 to 32");
                    }
                    if (!tess && pipeJson.contains("patchControlPoints")) {
                        throw std::runtime_error(where + ": patchControlPoints applies to tessellation");
                    }
                    if (tess && !capabilities.tessellationShader) {
                        throw std::runtime_error(where + " uses tessellation shaders, which this device lacks; "
                                                 "declare \"requires\": [\"tessellationShader\"] to leave the "
                                                 "pass out on such devices");
                    }
                    if (!pd.geometryShader.empty() && !capabilities.geometryShader) {
                        throw std::runtime_error(where + " uses a geometry shader, which this device lacks; "
                                                 "declare \"requires\": [\"geometryShader\"] to leave the "
                                                 "pass out on such devices");
                    }
                }
                pass.pipelineDesc.depthTest      = pipeJson.value("depthTest", true);
                pass.pipelineDesc.depthWrite     = pipeJson.value("depthWrite", true);
                pass.pipelineDesc.depthCompareOp = pipeJson.value("depthCompareOp", std::string{"less"});
                // Throws on an unknown name, so a typo fails at load, not at
                // pipeline creation.
                JsonUtils::stringToCompareOp(pass.pipelineDesc.depthCompareOp);
                pass.pipelineDesc.depthClamp     = pipeJson.value("depthClamp", false);
                pass.pipelineDesc.cullMode        = pipeJson.value("cullMode", std::string{"back"});
                pass.pipelineDesc.blending        = pipeJson.value("blending", std::string{"none"});
                pass.pipelineDesc.topology        = pipeJson.value("topology", std::string{"triangle_list"});
                pass.pipelineDesc.vertexInput     = pipeJson.value("vertexInput", std::string{"default"});
                pass.pipelineDesc.wireframe       = pipeJson.value("wireframe", false);

                if (pipeJson.contains("depthBias")) {
                    const auto& biasJson = pipeJson["depthBias"];
                    pass.pipelineDesc.depthBias         = true;
                    pass.pipelineDesc.depthBiasConstant = biasJson.value("constant", 0.0f);
                    pass.pipelineDesc.depthBiasSlope    = biasJson.value("slope", 0.0f);
                    pass.pipelineDesc.depthBiasClamp    = biasJson.value("clamp", 0.0f);
                }

                const std::string pipeWhere = "Pass '" + pass.name + "'";
                if (pipeJson.contains("writeMask")) {
                    pass.pipelineDesc.writeMask = parseWriteMask(pipeJson["writeMask"], pipeWhere);
                }
                if (pipeJson.contains("blendConstants")) {
                    const auto& bc = pipeJson["blendConstants"];
                    pass.pipelineDesc.hasBlendConstants = true;
                    if (bc.is_string() && !bc.get<std::string>().empty()) {
                        pass.pipelineDesc.blendConstantsPath = bc.get<std::string>();
                    } else if (bc.is_array() && bc.size() == 4 &&
                               std::all_of(bc.begin(), bc.end(), [](const auto& v) { return v.is_number(); })) {
                        for (size_t k = 0; k < 4; ++k) pass.pipelineDesc.blendConstants[k] = bc[k].get<float>();
                    } else {
                        throw std::runtime_error(pipeWhere + ": blendConstants must be [r, g, b, a] or a "
                                                 "dot-path to a vec4");
                    }
                }
                pass.pipelineDesc.frontFace = pipeJson.value("frontFace", std::string{"counter_clockwise"});
                if (pass.pipelineDesc.frontFace != "counter_clockwise" && pass.pipelineDesc.frontFace != "clockwise") {
                    throw std::runtime_error(pipeWhere + ": unknown frontFace '" + pass.pipelineDesc.frontFace +
                                             "' (expected counter_clockwise or clockwise)");
                }
                pass.pipelineDesc.lineWidth = pipeJson.value("lineWidth", 1.0f);
                if (!(pass.pipelineDesc.lineWidth > 0.0f)) {
                    throw std::runtime_error(pipeWhere + ": lineWidth must be above 0");
                }
                pass.pipelineDesc.primitiveRestart = pipeJson.value("primitiveRestart", false);
                if (pipeJson.contains("depthBounds")) {
                    const auto& db = pipeJson["depthBounds"];
                    if (!db.is_array() || db.size() != 2 || !db[0].is_number() || !db[1].is_number() ||
                        db[0].get<float>() > db[1].get<float>()) {
                        throw std::runtime_error(pipeWhere + ": depthBounds must be [min, max] with min <= max");
                    }
                    pass.pipelineDesc.depthBoundsTest = true;
                    pass.pipelineDesc.minDepthBounds = db[0].get<float>();
                    pass.pipelineDesc.maxDepthBounds = db[1].get<float>();
                }

                if (pipeJson.contains("stencil")) {
                    const auto& stencilJson = pipeJson["stencil"];
                    const std::string where = "Pass '" + pass.name + "': stencil";
                    if (!stencilJson.is_object()) throw std::runtime_error(where + " must be an object");
                    auto readFace = [&](const nlohmann::json& j, VkStencilOpState face) {
                        auto op = [&](const char* key, VkStencilOp& out) {
                            if (j.contains(key)) out = JsonUtils::stringToStencilOp(j[key].get<std::string>());
                        };
                        auto byte = [&](const char* key, uint32_t& out) {
                            if (!j.contains(key)) return;
                            const auto& v = j[key];
                            if (!v.is_number_integer() || v.get<int64_t>() < 0 || v.get<int64_t>() > 255) {
                                throw std::runtime_error(where + " \"" + key + "\" must be an integer from 0 to 255");
                            }
                            out = static_cast<uint32_t>(v.get<int64_t>());
                        };
                        op("failOp", face.failOp);
                        op("passOp", face.passOp);
                        op("depthFailOp", face.depthFailOp);
                        if (j.contains("compareOp")) {
                            face.compareOp = JsonUtils::stringToCompareOp(j["compareOp"].get<std::string>());
                        }
                        byte("compareMask", face.compareMask);
                        byte("writeMask", face.writeMask);
                        byte("reference", face.reference);
                        return face;
                    };
                    pass.pipelineDesc.stencilTest  = true;
                    pass.pipelineDesc.stencilFront = readFace(stencilJson, pass.pipelineDesc.stencilFront);
                    pass.pipelineDesc.stencilBack  = stencilJson.contains("back")
                        ? readFace(stencilJson["back"], pass.pipelineDesc.stencilFront)
                        : pass.pipelineDesc.stencilFront;
                }

                // Only meaningful when blending == "custom" - see PipelineDesc
                // for the accepted VkBlendFactor/VkBlendOp string names.
                pass.pipelineDesc.srcColorBlendFactor = pipeJson.value("srcColorFactor", std::string{"src_alpha"});
                pass.pipelineDesc.dstColorBlendFactor = pipeJson.value("dstColorFactor", std::string{"one_minus_src_alpha"});
                pass.pipelineDesc.colorBlendOp        = pipeJson.value("colorBlendOp", std::string{"add"});
                pass.pipelineDesc.srcAlphaBlendFactor = pipeJson.value("srcAlphaFactor", std::string{"one"});
                pass.pipelineDesc.dstAlphaBlendFactor = pipeJson.value("dstAlphaFactor", std::string{"zero"});
                pass.pipelineDesc.alphaBlendOp        = pipeJson.value("alphaBlendOp", std::string{"add"});

                // Unknown names fail here, at load, rather than drawing wrongly.
                const auto& pd = pass.pipelineDesc;
                try {
                    JsonUtils::stringToCullMode(pd.cullMode);
                    JsonUtils::stringToTopology(pd.topology);
                    if (pd.blending != "none" && pd.blending != "alpha" && pd.blending != "additive" &&
                        pd.blending != "custom") {
                        throw std::runtime_error("Unknown blending: '" + pd.blending +
                                                 "' (expected none, alpha, additive or custom)");
                    }
                    for (const auto* factor : {&pd.srcColorBlendFactor, &pd.dstColorBlendFactor,
                                               &pd.srcAlphaBlendFactor, &pd.dstAlphaBlendFactor}) {
                        JsonUtils::stringToBlendFactor(*factor);
                    }
                    JsonUtils::stringToBlendOp(pd.colorBlendOp);
                    JsonUtils::stringToBlendOp(pd.alphaBlendOp);
                } catch (const std::runtime_error& e) {
                    throw std::runtime_error("Pass '" + pass.name + "': " + e.what());
                }
            }

            // Parse descriptor set references
            if (passJson.contains("descriptorSets")) {
                for (const auto& dsRef : passJson["descriptorSets"]) {
                    pass.descriptorSetRefs.push_back(dsRef.get<std::string>());
                }
            }

            // Parse push constants (with named parameter bindings)
            if (passJson.contains("pushConstants")) {
                const auto& pcJson = passJson["pushConstants"];
                // Support both array and object format
                if (pcJson.is_array()) {
                    for (const auto& pcItem : pcJson) {
                        PushConstantDesc pc;
                        pc.size   = pcItem.at("size").get<uint32_t>();
                        pc.offset = pcItem.value("offset", 0u);
                        if (pcItem.contains("stages")) {
                            for (const auto& s : pcItem["stages"]) {
                                pc.stages.push_back(s.get<std::string>());
                            }
                        }
                        // Parse bindings (named parameter references)
                        if (pcItem.contains("bindings")) {
                            for (const auto& bindJson : pcItem["bindings"]) {
                                PushConstantBindingDesc binding;
                                binding.name   = bindJson.at("name").get<std::string>();
                                binding.offset = bindJson.value("offset", 0u);
                                binding.type   = bindJson.value("type", std::string{"float"});
                                pc.bindings.push_back(std::move(binding));
                            }
                        }
                        pass.pushConstants.push_back(std::move(pc));
                    }
                } else {
                    // Single object format
                    PushConstantDesc pc;
                    pc.size   = pcJson.at("size").get<uint32_t>();
                    pc.offset = pcJson.value("offset", 0u);
                    if (pcJson.contains("stages")) {
                        for (const auto& s : pcJson["stages"]) {
                            pc.stages.push_back(s.get<std::string>());
                        }
                    }
                    if (pcJson.contains("bindings")) {
                        for (const auto& bindJson : pcJson["bindings"]) {
                            PushConstantBindingDesc binding;
                            binding.name   = bindJson.at("name").get<std::string>();
                            binding.offset = bindJson.value("offset", 0u);
                            binding.type   = bindJson.value("type", std::string{"float"});
                            pc.bindings.push_back(std::move(binding));
                        }
                    }
                    pass.pushConstants.push_back(std::move(pc));
                }
            }

            // Parse execution configuration (auto-callback settings)
            if (passJson.contains("execution")) {
                const auto& execJson = passJson["execution"];
                pass.execution.type = execJson.value("type", std::string{"none"});
                pass.execution.bindDescriptorSets = execJson.value("bindDescriptorSets", true);
                pass.execution.bindPipeline = execJson.value("bindPipeline", true);

                // For "draw" type
                pass.execution.instanceCount = execJson.value("instanceCount", 1u);
                pass.execution.firstVertex   = execJson.value("firstVertex", 0u);
                pass.execution.firstInstance = execJson.value("firstInstance", 0u);

                // A count: a number, a parameter, or a dimension of an image,
                // each divided (rounding up) by "divisor".
                auto parseDimension = [&](const nlohmann::json& dj, const std::string& key) -> DispatchDimension {
                    const std::string where = "Pass '" + pass.name + "': " + key;
                    DispatchDimension dim;
                    if (dj.is_number()) {
                        dim.value = dj.get<uint32_t>();
                        return dim;
                    }
                    if (!dj.is_object()) throw std::runtime_error(where + " must be a number or an object");
                    dim.value     = dj.value("value", 0u);
                    dim.resource  = dj.value("resource", std::string{});
                    dim.dimension = dj.value("dimension", std::string{});
                    dim.divisor   = dj.value("divisor", 1u);
                    dim.parameter = dj.value("parameter", std::string{});
                    if (dim.divisor == 0) throw std::runtime_error(where + ": divisor must be at least 1");
                    if (!dim.resource.empty()) {
                        if (!builder.getResource(dim.resource).valid()) {
                            throw std::runtime_error(where + " names '" + dim.resource + "', which is not a resource");
                        }
                        if (dim.dimension != "width" && dim.dimension != "height" && dim.dimension != "depth" &&
                            dim.dimension != "layers") {
                            throw std::runtime_error(where + ": dimension must be width, height, depth or layers");
                        }
                    }
                    return dim;
                };

                if (execJson.contains("vertexCount")) {
                    pass.execution.vertexCount = parseDimension(execJson["vertexCount"], "vertexCount");
                }
                if (execJson.contains("indexCount")) {
                    pass.execution.indexCount = parseDimension(execJson["indexCount"], "indexCount");
                }

                // For "compute_dispatch" type
                if (execJson.contains("workgroupSize")) {
                    const auto& wgs = execJson["workgroupSize"];
                    if (wgs.is_array() && wgs.size() >= 3) {
                        pass.execution.workgroupSize[0] = wgs[0].get<uint32_t>();
                        pass.execution.workgroupSize[1] = wgs[1].get<uint32_t>();
                        pass.execution.workgroupSize[2] = wgs[2].get<uint32_t>();
                    }
                }

                // Parse dispatch dimensions
                if (execJson.contains("dispatch")) {
                    const auto& dispJson = execJson["dispatch"];
                    if (dispJson.contains("x")) pass.execution.dispatch[0] = parseDimension(dispJson["x"], "dispatch.x");
                    if (dispJson.contains("y")) pass.execution.dispatch[1] = parseDimension(dispJson["y"], "dispatch.y");
                    if (dispJson.contains("z")) pass.execution.dispatch[2] = parseDimension(dispJson["z"], "dispatch.z");
                }

                parseIndirectExecution(pass, execJson, capabilities);
                parseTransferExecution(pass, execJson);

                // For built-in geometry types (opaque_geometry, transparent_geometry, shadow_casters)
                pass.execution.sortMode = execJson.value("sortMode", std::string{"none"});
                pass.execution.entityDataBinding = execJson.value("entityDataBinding", std::string{});
                pass.execution.renderLayerMask = execJson.value("renderLayerMask", 0xFFFFFFFFu);
                pass.execution.lightIndex = execJson.value("lightIndex", -1);
                pass.execution.instancing = execJson.value("instancing", false);
                if (pass.execution.instancing && !isEntityGeometryExecutionType(pass.execution.type)) {
                    throw std::runtime_error("Pass '" + pass.name + "': \"instancing\" needs an entity geometry "
                                             "execution type, not '" + pass.execution.type + "'");
                }

                if (execJson.contains("view")) {
                    const auto& viewJson = execJson["view"];
                    const std::string view = viewJson.is_string() ? viewJson.get<std::string>() : viewJson.dump();
                    const std::string cascadePrefix = "shadows.sun.cascades[";
                    if (!isEntityGeometryExecutionType(pass.execution.type)) {
                        throw std::runtime_error("Pass '" + pass.name + "': \"view\" applies only to entity "
                                                 "geometry passes, not '" + pass.execution.type + "'");
                    }
                    if (view == "none") {
                        pass.execution.view = CullView::None;
                    } else if (view == "camera") {
                        pass.execution.view = CullView::Camera;
                    } else if (view.starts_with("cameras.") && view.size() > 8) {
                        pass.execution.view = CullView::NamedCamera;
                        pass.execution.viewName = view.substr(8);
                    } else if (view.starts_with(cascadePrefix) && view.ends_with("]")) {
                        const std::string digits = view.substr(cascadePrefix.size(),
                                                               view.size() - cascadePrefix.size() - 1);
                        const bool numeric = !digits.empty() && digits.size() < 4 &&
                            std::all_of(digits.begin(), digits.end(), [](char c) { return c >= '0' && c <= '9'; });
                        if (!numeric || std::stoul(digits) >= MAX_SUN_CASCADES) {
                            throw std::runtime_error("Pass '" + pass.name + "': view '" + view +
                                                     "' names no sun cascade (0 to " +
                                                     std::to_string(MAX_SUN_CASCADES - 1) + ")");
                        }
                        pass.execution.view = CullView::SunCascade;
                        pass.execution.viewIndex = static_cast<uint32_t>(std::stoul(digits));
                    } else if (auto slot = indexedView(view, "shadows.spot[")) {
                        if (*slot >= MAX_SPOT_SHADOWS) {
                            throw std::runtime_error("Pass '" + pass.name + "': view '" + view +
                                                     "' names no spot shadow slot (0 to " +
                                                     std::to_string(MAX_SPOT_SHADOWS - 1) + ")");
                        }
                        pass.execution.view = CullView::SpotShadow;
                        pass.execution.viewIndex = *slot;
                    } else if (auto face = indexedView(view, "shadows.point.faces[")) {
                        if (*face >= MAX_POINT_SHADOWS * 6) {
                            throw std::runtime_error("Pass '" + pass.name + "': view '" + view +
                                                     "' names no point shadow face (0 to " +
                                                     std::to_string(MAX_POINT_SHADOWS * 6 - 1) + ")");
                        }
                        pass.execution.view = CullView::PointShadowFace;
                        pass.execution.viewIndex = *face;
                    } else {
                        throw std::runtime_error("Pass '" + pass.name + "': unknown view '" + view +
                                                 "' (expected camera, cameras.<name>, none, "
                                                 "shadows.sun.cascades[N], shadows.spot[N] or "
                                                 "shadows.point.faces[N])");
                    }
                }

                if (execJson.contains("alphaFilter")) {
                    const auto alpha = execJson["alphaFilter"].get<std::string>();
                    if (alpha == "any")         pass.execution.alphaFilter = AlphaFilter::Any;
                    else if (alpha == "opaque") pass.execution.alphaFilter = AlphaFilter::Opaque;
                    else if (alpha == "mask")   pass.execution.alphaFilter = AlphaFilter::Mask;
                    else {
                        throw std::runtime_error("Pass '" + pass.name + "': unknown alphaFilter '" +
                                                 alpha + "' (expected any, opaque or mask)");
                    }

                    // Blended passes never see opaque or masked materials, so
                    // narrowing them to one would silently draw nothing.
                    const auto& type = pass.execution.type;
                    if (pass.execution.alphaFilter != AlphaFilter::Any &&
                        (!isEntityGeometryExecutionType(type) ||
                         type == "transparent_geometry" || type == "skinned_transparent" ||
                         type == "sprite_geometry")) {
                        throw std::runtime_error("Pass '" + pass.name + "': alphaFilter '" + alpha +
                                                 "' does not apply to execution type '" + type + "'");
                    }
                }
            }

            // An execution type the executor would record nothing for.
            if (executionKindOf(pass.execution.type) == ExecutionKind::Unknown) {
                throw std::runtime_error("Pass '" + pass.name + "': unknown execution type '" +
                                         pass.execution.type + "'");
            }

            // A ray tracing pass traces with trace_rays, and only it does.
            if ((pass.type == PassType::RayTracing) != (pass.execution.type == "trace_rays") ||
                (pass.type == PassType::RayTracing && pass.pipelineDesc.rayGenShader.empty())) {
                throw std::runtime_error("Pass '" + pass.name + "': a ray_tracing pass has execution type "
                                         "\"trace_rays\" and a rayGenShader, and that type needs a ray_tracing pass");
            }

            // A mesh pipeline draws with draw_mesh_tasks, and only it does.
            if (pass.pipelineDesc.meshShader.empty() != (pass.execution.type != "draw_mesh_tasks")) {
                throw std::runtime_error("Pass '" + pass.name + "': a meshShader goes with execution type "
                                         "\"draw_mesh_tasks\", and that type needs a meshShader");
            }

            // Parse hasSideEffects flag (prevents pass culling for compute-only passes)
            pass.hasSideEffects = passJson.value("hasSideEffects", false);

            // Parse enabled flag (default: true — passes execute unless explicitly disabled)
            pass.enabled = passJson.value("enabled", true);

            // "viewport": a dot-path to the rectangle the pass renders into.
            if (passJson.contains("viewport")) {
                const auto& viewport = passJson["viewport"];
                if (!viewport.is_string() || viewport.get<std::string>().empty()) {
                    throw std::runtime_error("Pass '" + pass.name + "': \"viewport\" must be a dot-path "
                                             "to a vec4, e.g. \"scene.shadows.spot[0].rect\"");
                }
                if (pass.type != PassType::Graphics) {
                    throw std::runtime_error("Pass '" + pass.name + "': \"viewport\" applies only to "
                                             "graphics passes");
                }
                pass.viewport = viewport.get<std::string>();
            }

            builder.addPass(std::move(pass));
        }
    }

    // ── Presets (after passes, whose names they are checked against) ──
    if (json.contains("presets")) {
        const auto& presetsJson = json["presets"];
        if (!presetsJson.is_object()) {
            throw std::runtime_error("\"presets\" must be an object of named presets");
        }
        auto passExists = [&](const std::string& name) {
            if (builder.hasPass(name) || builder.isUnavailablePass(name)) return true;
            for (const auto& p : builder.getPassDeclarations()) if (p.repeatGroup == name) return true;
            return false;
        };
        for (auto it = presetsJson.begin(); it != presetsJson.end(); ++it) {
            const std::string where = "Preset '" + it.key() + "'";
            const auto& pj = it.value();
            if (!pj.is_object()) throw std::runtime_error(where + " must be an object");
            PipelinePreset preset;
            preset.name = it.key();
            if (pj.contains("passes")) {
                if (!pj["passes"].is_object()) {
                    throw std::runtime_error(where + ": \"passes\" must map pass names to true or false");
                }
                for (auto p = pj["passes"].begin(); p != pj["passes"].end(); ++p) {
                    if (!p.value().is_boolean()) {
                        throw std::runtime_error(where + ": pass '" + p.key() + "' must be true or false");
                    }
                    if (!passExists(p.key())) {
                        throw std::runtime_error(where + " switches '" + p.key() + "', which is not a pass");
                    }
                    preset.passes.emplace_back(p.key(), p.value().get<bool>());
                }
            }
            if (pj.contains("values")) {
                if (!pj["values"].is_object()) {
                    throw std::runtime_error(where + ": \"values\" must map scene.custom keys to values");
                }
                for (auto v = pj["values"].begin(); v != pj["values"].end(); ++v) {
                    const auto& value = v.value();
                    if (value.is_null()) {
                        preset.values.emplace_back(v.key(), std::nullopt);
                    } else if (value.is_number()) {
                        preset.values.emplace_back(v.key(), std::vector<float>{value.get<float>()});
                    } else if (value.is_array() && !value.empty() &&
                               std::all_of(value.begin(), value.end(),
                                           [](const nlohmann::json& c) { return c.is_number(); })) {
                        std::vector<float> components;
                        for (const auto& c : value) components.push_back(c.get<float>());
                        preset.values.emplace_back(v.key(), std::move(components));
                    } else {
                        throw std::runtime_error(where + ": value '" + v.key() +
                                                 "' must be a number, an array of numbers or null");
                    }
                }
            }
            builder.addPreset(std::move(preset));
        }
    }

    // ── Canvas UI: the depth image world-canvas hits are tested against ──
    if (json.contains("ui")) {
        const auto& ui = json["ui"];
        if (!ui.is_object() || !ui.contains("depth") || !ui["depth"].is_string() ||
            !ui.contains("afterPass") || !ui["afterPass"].is_string()) {
            throw std::runtime_error("\"ui\" must be an object with \"depth\", an image, and \"afterPass\", "
                                     "the pass after which it is read");
        }
        PipelineUISettings settings;
        settings.depth = ui["depth"].get<std::string>();
        settings.afterPass = ui["afterPass"].get<std::string>();
        const ResourceHandle depth = builder.getResource(settings.depth);
        if (!depth.valid() || builder.getResourceDeclarations()[depth.index].kind != ResourceKind::Image) {
            throw std::runtime_error("\"ui\": \"depth\" names '" + settings.depth + "', which is not an image");
        }
        if (builder.getResourceDeclarations()[depth.index].imageDesc.samples != VK_SAMPLE_COUNT_1_BIT) {
            throw std::runtime_error("\"ui\": \"depth\" names '" + settings.depth + "', which is multisampled");
        }
        const PassDeclaration* pass = builder.getPass(settings.afterPass);
        if (!pass) {
            throw std::runtime_error("\"ui\": \"afterPass\" names '" + settings.afterPass + "', which is not a pass");
        }
        const bool writes = std::any_of(pass->outputs.begin(), pass->outputs.end(),
                                        [&](const ResourceAccess& access) { return access.handle == depth; });
        if (!writes) {
            throw std::runtime_error("\"ui\": pass '" + settings.afterPass + "' does not write '" +
                                     settings.depth + "'");
        }
        // The canvas UI copies a texel of it out each frame.
        builder.getMutableResource(settings.depth)->imageDesc.additionalUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        builder.setUISettings(std::move(settings));
    }
}

namespace {

/// A relative path in `value` that names a file in `dir` becomes the path to
/// that file. Others are left as they are.
void resolvePathAgainst(nlohmann::json& value, const std::filesystem::path& dir) {
    if (!value.is_string()) return;
    const std::filesystem::path path = value.get<std::string>();
    if (path.empty() || path.is_absolute()) return;
    std::error_code ec;
    const std::filesystem::path beside = dir / path;
    if (std::filesystem::exists(beside, ec)) {
        value = beside.lexically_normal().generic_string();
    }
}

/// Relative shader paths, and the paths of buffer layouts filled from a file
/// (`"source": {"type": "file"}`), that name a file beside the pipeline JSON
/// become paths to that file, so a pipeline loads with its shaders and data
/// from any working directory. Paths that do not exist there are left as
/// they are and resolve against the working directory, as they always have.
void resolveShaderPathsAgainst(nlohmann::json& json, const std::filesystem::path& dir) {
    if (dir.empty()) return;
    if (json.contains("passes") && json["passes"].is_array()) {
        for (auto& pass : json["passes"]) {
            if (!pass.is_object() || !pass.contains("pipeline") || !pass["pipeline"].is_object()) continue;
            auto& pipe = pass["pipeline"];
            for (const auto& shader : kShaderKeys) {
                if (pipe.contains(shader.key)) resolvePathAgainst(pipe[shader.key], dir);
            }
            if (pipe.contains("rayGenShader")) resolvePathAgainst(pipe["rayGenShader"], dir);
            if (pipe.contains("missShaders") && pipe["missShaders"].is_array()) {
                for (auto& miss : pipe["missShaders"]) resolvePathAgainst(miss, dir);
            }
            if (pipe.contains("hitGroups") && pipe["hitGroups"].is_array()) {
                for (auto& group : pipe["hitGroups"]) {
                    if (!group.is_object()) continue;
                    for (const char* key : {"closestHit", "anyHit", "intersection"}) {
                        if (group.contains(key)) resolvePathAgainst(group[key], dir);
                    }
                }
            }
        }
    }
    if (json.contains("bufferLayouts") && json["bufferLayouts"].is_object()) {
        for (auto& item : json["bufferLayouts"].items()) {
            auto& layout = item.value();
            if (!layout.is_object() || !layout.contains("source") || !layout["source"].is_object()) continue;
            auto& source = layout["source"];
            if (source.value("type", std::string{}) == "file" && source.contains("path")) {
                resolvePathAgainst(source["path"], dir);
            }
        }
    }
}

} // namespace

void loadGraphFromFile(FrameGraphBuilder& builder, const std::string& filePath,
                       const PipelineCapabilities& capabilities) {
    nlohmann::json json;
    {
        // Capture errno before any other operations
        errno = 0;

        std::ifstream file(filePath);
        if (!file.is_open()) {
            // Get detailed error information
            std::string errorDetail;

            // errno is the reliable cross-platform error indicator for file operations
            int savedErrno = errno;

            #ifdef _WIN32
                // GetLastError() may not reflect ifstream errors - errno is more reliable
                // But we capture both for diagnostics
                DWORD winErr = GetLastError();

                if (savedErrno != 0) {
                    // Not strerror: it returns a pointer to a shared static buffer,
                    // the same hazard localtime had. generic_category is portable
                    // and returns a std::string.
                    errorDetail = std::generic_category().message(savedErrno);
                    errorDetail += " (errno: " + std::to_string(savedErrno) + ")";
                } else if (winErr != 0) {
                    char* msg = nullptr;
                    FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
                                  nullptr, winErr, 0, (LPSTR)&msg, 0, nullptr);
                    errorDetail = msg ? msg : "Unknown Windows error";
                    if (msg) LocalFree(msg);
                    errorDetail += " (WinError: " + std::to_string(winErr) + ")";
                } else {
                    errorDetail = "Unknown error (both errno and GetLastError returned 0)";
                }

                // Also check if file exists and report handle count
                errorDetail += "\n         File exists check: ";
                if (std::filesystem::exists(filePath)) {
                    errorDetail += "YES";
                    auto size = std::filesystem::file_size(filePath);
                    errorDetail += " (size: " + std::to_string(size) + " bytes)";
                } else {
                    errorDetail += "NO";
                }
            #else
                errorDetail = strerror(savedErrno);
            #endif

            throw std::runtime_error("Cannot open frame graph file: '" + filePath + "' - " + errorDetail);
        }
        file >> json;
    } // File closed here before modifying builder
    resolveShaderPathsAgainst(json, std::filesystem::absolute(filePath).parent_path());
    loadGraphFromJson(builder, json, capabilities);
}

// ═══════════════════════════════════════════════════════════════
// JSON Saving
// ═══════════════════════════════════════════════════════════════

static nlohmann::json serializeResourceAccess(const ResourceAccess& access,
                                               const FrameGraphBuilder& builder) {
    nlohmann::json j;

    // Find resource name from handle
    const auto& resources = builder.getResourceDeclarations();
    if (access.handle.valid()) {
        if (access.handle.index >= resources.size()) {
            throw std::runtime_error("FrameGraph JSON: Invalid resource handle index: "
                                     + std::to_string(access.handle.index));
        }
        j["resource"] = resources[access.handle.index].name;
    }

    j["usage"] = JsonUtils::resourceUsageToString(access.usage);
    serializeSubresource(j, access.subresource);

    // Written as a flag rather than folded back into "usage", so a round-trip
    // through the serializer does not lose a blend that also presents.
    if (access.present) {
        j["present"] = true;
    }

    if (access.hasClearValue) {
        if (access.usage == ResourceUsage::DepthStencilWrite) {
            j["clear"] = {
                {"depth", access.clearValue.depthStencil.depth},
                {"stencil", access.clearValue.depthStencil.stencil}
            };
        } else {
            j["clear"] = {
                access.clearValue.color.float32[0],
                access.clearValue.color.float32[1],
                access.clearValue.color.float32[2],
                access.clearValue.color.float32[3]
            };
        }
    }

    if (access.resolveTarget.valid() && access.resolveTarget.index < resources.size()) {
        j["resolve"] = resources[access.resolveTarget.index].name;
    }
    if (access.blend) {
        j["blending"] = access.blend->blending;
        if (access.blend->blending == "custom") {
            j["srcColorFactor"] = access.blend->srcColorFactor;
            j["dstColorFactor"] = access.blend->dstColorFactor;
            j["colorBlendOp"]   = access.blend->colorBlendOp;
            j["srcAlphaFactor"] = access.blend->srcAlphaFactor;
            j["dstAlphaFactor"] = access.blend->dstAlphaFactor;
            j["alphaBlendOp"]   = access.blend->alphaBlendOp;
        }
    }
    if (access.writeMask) j["writeMask"] = writeMaskToString(*access.writeMask);

    return j;
}

nlohmann::json saveGraphToJson(const FrameGraphBuilder& builder) {
    nlohmann::json json;
    json["version"] = 1;

    // ── Serialize descriptor set layouts ──
    const auto& layouts = builder.getDescriptorSetLayouts();
    if (!layouts.empty()) {
        nlohmann::json layoutsJson = nlohmann::json::object();
        for (const auto& layout : layouts) {
            nlohmann::json layoutJson;
            layoutJson["bindings"] = nlohmann::json::array();

            for (const auto& binding : layout.bindings) {
                nlohmann::json bindingJson;
                bindingJson["binding"] = binding.binding;
                bindingJson["type"]    = binding.type;
                bindingJson["count"]   = binding.count;
                if (!binding.name.empty() && binding.name != "binding_" + std::to_string(binding.binding)) {
                    bindingJson["name"] = binding.name;
                }
                if (!binding.stages.empty()) {
                    bindingJson["stages"] = binding.stages;
                }
                layoutJson["bindings"].push_back(bindingJson);
            }

            layoutsJson[layout.name] = layoutJson;
        }
        json["descriptorSetLayouts"] = layoutsJson;
    }

    // ── Serialize resources ──
    auto& resourcesJson = json["resources"];
    resourcesJson = nlohmann::json::array();

    for (const auto& decl : builder.getResourceDeclarations()) {
        nlohmann::json resJson;
        resJson["name"] = decl.name;
        resJson["kind"] = JsonUtils::resourceKindToString(decl.kind);

        if (decl.imported) {
            resJson["imported"] = true;

            // Serialize format for imported images (for round-trip fidelity)
            if (decl.kind == ResourceKind::Image && decl.imageDesc.format != VK_FORMAT_UNDEFINED) {
                nlohmann::json imgJson;
                imgJson["format"] = JsonUtils::formatToString(decl.imageDesc.format);
                resJson["image"] = imgJson;
            }
        }

        if (decl.kind == ResourceKind::Image && !decl.imported) {
            nlohmann::json imgJson;
            const auto& desc = decl.imageDesc;
            if (desc.width > 0)  imgJson["width"] = desc.width;
            if (desc.height > 0) imgJson["height"] = desc.height;
            if (desc.widthScale != 1.0f)  imgJson["widthScale"] = desc.widthScale;
            if (desc.heightScale != 1.0f) imgJson["heightScale"] = desc.heightScale;
            if (desc.format != VK_FORMAT_UNDEFINED) {
                imgJson["format"] = JsonUtils::formatToString(desc.format);
            }
            if (desc.samples != VK_SAMPLE_COUNT_1_BIT) {
                imgJson["samples"] = static_cast<int>(desc.samples);
            }
            if (desc.mipLevels == 0)        imgJson["mipLevels"] = "full";
            else if (desc.mipLevels != 1)   imgJson["mipLevels"] = desc.mipLevels;
            if (desc.arrayLayers != 1) imgJson["arrayLayers"] = desc.arrayLayers;
            if (desc.depth != 1)       imgJson["depth"] = desc.depth;
            if (desc.viewType != ImageViewKind::Auto) imgJson["viewType"] = imageViewKindToString(desc.viewType);
            if (desc.transient)        imgJson["transient"] = true;
            if (desc.persistent)       imgJson["persistent"] = true;

            resJson["image"] = imgJson;
        } else if (decl.kind == ResourceKind::Buffer && !decl.imported) {
            nlohmann::json bufJson;
            if (decl.bufferDesc.size > 0) bufJson["size"] = decl.bufferDesc.size;
            if (decl.bufferDesc.persistentlyMapped) bufJson["persistentlyMapped"] = true;
            resJson["buffer"] = bufJson;
        }

        resourcesJson.push_back(resJson);
    }

    // ── Serialize passes ──
    auto& passesJson = json["passes"];
    passesJson = nlohmann::json::array();

    for (const auto& pass : builder.getPassDeclarations()) {
        nlohmann::json passJson;
        passJson["name"] = pass.name;
        passJson["type"] = JsonUtils::passTypeToString(pass.type);

        // Queue type (only serialize if not default "graphics")
        if (pass.queueType != QueueType::Graphics) {
            passJson["queue"] = JsonUtils::queueTypeToString(pass.queueType);
        }

        // Inputs
        if (!pass.inputs.empty()) {
            passJson["inputs"] = nlohmann::json::array();
            for (const auto& input : pass.inputs) {
                passJson["inputs"].push_back(serializeResourceAccess(input, builder));
            }
        }

        // Outputs
        if (!pass.outputs.empty()) {
            passJson["outputs"] = nlohmann::json::array();
            for (const auto& output : pass.outputs) {
                // Written as "resolve" on the attachment it resolves.
                if (output.usage == ResourceUsage::ColorResolve) continue;
                passJson["outputs"].push_back(serializeResourceAccess(output, builder));
            }
        }

        // Pipeline
        const auto& pd = pass.pipelineDesc;
        if (!pd.vertexShader.empty() || !pd.fragmentShader.empty() || !pd.computeShader.empty() ||
            !pd.meshShader.empty() || !pd.rayGenShader.empty()) {
            nlohmann::json pipeJson;
            if (!pd.rayGenShader.empty()) {
                pipeJson["rayGenShader"] = pd.rayGenShader;
                if (!pd.missShaders.empty()) pipeJson["missShaders"] = pd.missShaders;
                for (const auto& group : pd.hitGroups) {
                    nlohmann::json g = nlohmann::json::object();
                    if (!group.closestHit.empty())   g["closestHit"] = group.closestHit;
                    if (!group.anyHit.empty())       g["anyHit"] = group.anyHit;
                    if (!group.intersection.empty()) g["intersection"] = group.intersection;
                    pipeJson["hitGroups"].push_back(g);
                }
                if (pd.maxRecursionDepth != 1) pipeJson["maxRecursionDepth"] = pd.maxRecursionDepth;
            }
            if (!pd.vertexShader.empty())   pipeJson["vertexShader"] = pd.vertexShader;
            if (!pd.fragmentShader.empty()) pipeJson["fragmentShader"] = pd.fragmentShader;
            if (!pd.computeShader.empty())  pipeJson["computeShader"] = pd.computeShader;
            if (!pd.tessControlShader.empty())    pipeJson["tessControlShader"] = pd.tessControlShader;
            if (!pd.tessEvaluationShader.empty()) pipeJson["tessEvaluationShader"] = pd.tessEvaluationShader;
            if (!pd.geometryShader.empty())       pipeJson["geometryShader"] = pd.geometryShader;
            if (pd.patchControlPoints != 0)       pipeJson["patchControlPoints"] = pd.patchControlPoints;
            if (!pd.taskShader.empty())           pipeJson["taskShader"] = pd.taskShader;
            if (!pd.meshShader.empty())           pipeJson["meshShader"] = pd.meshShader;
            pipeJson["depthTest"]  = pd.depthTest;
            pipeJson["depthWrite"] = pd.depthWrite;
            if (pd.depthCompareOp != "less") pipeJson["depthCompareOp"] = pd.depthCompareOp;
            if (pd.depthClamp) pipeJson["depthClamp"] = true;
            pipeJson["cullMode"]   = pd.cullMode;
            pipeJson["blending"]   = pd.blending;
            pipeJson["topology"]   = pd.topology;
            if (pd.vertexInput != "default") pipeJson["vertexInput"] = pd.vertexInput;
            if (pd.wireframe) pipeJson["wireframe"] = true;
            if (pd.depthBias) {
                pipeJson["depthBias"] = {
                    {"constant", pd.depthBiasConstant},
                    {"slope",    pd.depthBiasSlope},
                    {"clamp",    pd.depthBiasClamp}
                };
            }
            constexpr VkColorComponentFlags kAll = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                                   VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
            if (pd.writeMask != kAll) pipeJson["writeMask"] = writeMaskToString(pd.writeMask);
            if (pd.hasBlendConstants) {
                if (!pd.blendConstantsPath.empty()) pipeJson["blendConstants"] = pd.blendConstantsPath;
                else pipeJson["blendConstants"] = pd.blendConstants;
            }
            if (pd.frontFace != "counter_clockwise") pipeJson["frontFace"] = pd.frontFace;
            if (pd.lineWidth != 1.0f) pipeJson["lineWidth"] = pd.lineWidth;
            if (pd.primitiveRestart) pipeJson["primitiveRestart"] = true;
            if (pd.depthBoundsTest) pipeJson["depthBounds"] = {pd.minDepthBounds, pd.maxDepthBounds};
            if (pd.stencilTest) {
                auto compareName = [](VkCompareOp op) {
                    for (const char* name : {"never", "less", "equal", "less_or_equal", "greater",
                                             "not_equal", "greater_or_equal", "always"}) {
                        if (JsonUtils::stringToCompareOp(name) == op) return std::string{name};
                    }
                    return std::string{"always"};
                };
                auto face = [&](const VkStencilOpState& s) {
                    return nlohmann::json{
                        {"failOp", JsonUtils::stencilOpToString(s.failOp)},
                        {"passOp", JsonUtils::stencilOpToString(s.passOp)},
                        {"depthFailOp", JsonUtils::stencilOpToString(s.depthFailOp)},
                        {"compareOp", compareName(s.compareOp)},
                        {"compareMask", s.compareMask},
                        {"writeMask", s.writeMask},
                        {"reference", s.reference}};
                };
                auto stencilJson = face(pd.stencilFront);
                const auto& f = pd.stencilFront;
                const auto& b = pd.stencilBack;
                const bool sameBack = f.failOp == b.failOp && f.passOp == b.passOp && f.depthFailOp == b.depthFailOp &&
                                      f.compareOp == b.compareOp && f.compareMask == b.compareMask &&
                                      f.writeMask == b.writeMask && f.reference == b.reference;
                if (!sameBack) stencilJson["back"] = face(b);
                pipeJson["stencil"] = stencilJson;
            }
            if (pd.blending == "custom") {
                pipeJson["srcColorFactor"] = pd.srcColorBlendFactor;
                pipeJson["dstColorFactor"] = pd.dstColorBlendFactor;
                pipeJson["colorBlendOp"]   = pd.colorBlendOp;
                pipeJson["srcAlphaFactor"] = pd.srcAlphaBlendFactor;
                pipeJson["dstAlphaFactor"] = pd.dstAlphaBlendFactor;
                pipeJson["alphaBlendOp"]   = pd.alphaBlendOp;
            }
            passJson["pipeline"] = pipeJson;
        }

        // Descriptor set refs
        if (!pass.descriptorSetRefs.empty()) {
            passJson["descriptorSets"] = pass.descriptorSetRefs;
        }

        // Push constants
        if (!pass.pushConstants.empty()) {
            passJson["pushConstants"] = nlohmann::json::array();
            for (const auto& pc : pass.pushConstants) {
                nlohmann::json pcJson;
                pcJson["size"] = pc.size;
                if (pc.offset > 0) pcJson["offset"] = pc.offset;
                if (!pc.stages.empty()) pcJson["stages"] = pc.stages;
                passJson["pushConstants"].push_back(pcJson);
            }
        }

        if (!pass.enabled) passJson["enabled"] = false;

        passesJson.push_back(passJson);
    }

    return json;
}

void saveGraphToFile(const FrameGraphBuilder& builder, const std::string& filePath) {
    nlohmann::json json = saveGraphToJson(builder);
    std::ofstream file(filePath);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open file for writing: '" + filePath + "'");
    }
    file << json.dump(2);
}

} // namespace FrameGraph
} // namespace Shoonyakasha
