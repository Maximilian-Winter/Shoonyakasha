//
// Shoonyakasha Engine - Frame Graph JSON Serialization
//
// 名正則言順  言順則事成
// When names are correct, language is in accordance
// When language is in accordance, affairs are completed
//

#include "Vulkan/FrameGraph/FrameGraphJson.h"
#include "Vulkan/FrameGraph/FrameGraph.h"

#include <fstream>
#include <stdexcept>
#include <cstring>  // For strerror
#include <filesystem>
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
    {"R8_SINT",                 VK_FORMAT_R8_SINT},
    {"R16_SINT",                VK_FORMAT_R16_SINT},
    {"R32_SINT",                VK_FORMAT_R32_SINT},

    // Unsigned normalized 16-bit
    {"R16_UNORM",               VK_FORMAT_R16_UNORM},
    {"R16G16_UNORM",            VK_FORMAT_R16G16_UNORM},
    {"R16G16B16A16_UNORM",      VK_FORMAT_R16G16B16A16_UNORM},

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

} // namespace JsonUtils

// ═══════════════════════════════════════════════════════════════
// JSON → ResourceAccess parsing helpers
// ═══════════════════════════════════════════════════════════════

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

    return access;
}

// ═══════════════════════════════════════════════════════════════
// JSON Loading
// ═══════════════════════════════════════════════════════════════

void loadGraphFromJson(FrameGraphBuilder& builder, const nlohmann::json& json) {
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
            }

            // Parse packing rule
            std::string packingStr = lj.value("packing", std::string{"std140"});
            if (packingStr == "std140") {
                layout.packing = BufferPackingRule::Std140;
            } else if (packingStr == "std430") {
                layout.packing = BufferPackingRule::Std430;
            } else if (packingStr == "scalar") {
                layout.packing = BufferPackingRule::Scalar;
            } else if (packingStr == "push_constant") {
                layout.packing = BufferPackingRule::PushConstant;
            }

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
                    field.offset = fieldJson.value("offset", 0u);

                    // ─── Parse source for dot-path resolution ─────
                    // JSON: "source": "entity.material.params.baseColorFactor"
                    field.source = fieldJson.value("source", std::string{});

                    // Parse field type
                    std::string typeStr = fieldJson.value("type", std::string{"float"});
                    if (typeStr == "float") field.type = BufferFieldType::Float;
                    else if (typeStr == "double") field.type = BufferFieldType::Double;
                    else if (typeStr == "int") field.type = BufferFieldType::Int;
                    else if (typeStr == "uint") field.type = BufferFieldType::UInt;
                    else if (typeStr == "bool") field.type = BufferFieldType::Bool;
                    else if (typeStr == "vec2") field.type = BufferFieldType::Vec2;
                    else if (typeStr == "vec3") field.type = BufferFieldType::Vec3;
                    else if (typeStr == "vec4") field.type = BufferFieldType::Vec4;
                    else if (typeStr == "ivec2") field.type = BufferFieldType::IVec2;
                    else if (typeStr == "ivec3") field.type = BufferFieldType::IVec3;
                    else if (typeStr == "ivec4") field.type = BufferFieldType::IVec4;
                    else if (typeStr == "uvec2") field.type = BufferFieldType::UVec2;
                    else if (typeStr == "uvec3") field.type = BufferFieldType::UVec3;
                    else if (typeStr == "uvec4") field.type = BufferFieldType::UVec4;
                    else if (typeStr == "mat2") field.type = BufferFieldType::Mat2;
                    else if (typeStr == "mat3") field.type = BufferFieldType::Mat3;
                    else if (typeStr == "mat4") field.type = BufferFieldType::Mat4;

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
        const auto& layoutsJson = json["descriptorSetLayouts"];
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
                        desc.mipLevels   = imgJson.value("mipLevels", 1u);
                        desc.arrayLayers = imgJson.value("arrayLayers", 1u);
                        desc.transient   = imgJson.value("transient", false);

                        if (imgJson.contains("format")) {
                            desc.format = JsonUtils::stringToFormat(imgJson["format"].get<std::string>());
                        }
                        if (imgJson.contains("samples")) {
                            desc.samples = static_cast<VkSampleCountFlagBits>(imgJson["samples"].get<int>());
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
            }
        }
    }

    // ── Parse passes ──
    if (json.contains("passes")) {
        for (const auto& passJson : json["passes"]) {
            PassDeclaration pass;
            pass.name = passJson.at("name").get<std::string>();
            pass.type = JsonUtils::stringToPassType(passJson.at("type").get<std::string>());

            // Parse queue type (default: "graphics" for backward compatibility)
            if (passJson.contains("queue")) {
                pass.queueType = JsonUtils::stringToQueueType(passJson["queue"].get<std::string>());
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
                }
            }

            // Parse pipeline description
            if (passJson.contains("pipeline")) {
                const auto& pipeJson = passJson["pipeline"];
                pass.pipelineDesc.vertexShader   = pipeJson.value("vertexShader", std::string{});
                pass.pipelineDesc.fragmentShader = pipeJson.value("fragmentShader", std::string{});
                pass.pipelineDesc.computeShader  = pipeJson.value("computeShader", std::string{});
                pass.pipelineDesc.depthTest      = pipeJson.value("depthTest", true);
                pass.pipelineDesc.depthWrite     = pipeJson.value("depthWrite", true);
                pass.pipelineDesc.cullMode        = pipeJson.value("cullMode", std::string{"back"});
                pass.pipelineDesc.blending        = pipeJson.value("blending", std::string{"none"});
                pass.pipelineDesc.topology        = pipeJson.value("topology", std::string{"triangle_list"});
                pass.pipelineDesc.vertexInput     = pipeJson.value("vertexInput", std::string{"default"});
                pass.pipelineDesc.wireframe       = pipeJson.value("wireframe", false);

                // Only meaningful when blending == "custom" - see PipelineDesc
                // for the accepted VkBlendFactor/VkBlendOp string names.
                pass.pipelineDesc.srcColorBlendFactor = pipeJson.value("srcColorFactor", std::string{"src_alpha"});
                pass.pipelineDesc.dstColorBlendFactor = pipeJson.value("dstColorFactor", std::string{"one_minus_src_alpha"});
                pass.pipelineDesc.colorBlendOp        = pipeJson.value("colorBlendOp", std::string{"add"});
                pass.pipelineDesc.srcAlphaBlendFactor = pipeJson.value("srcAlphaFactor", std::string{"one"});
                pass.pipelineDesc.dstAlphaBlendFactor = pipeJson.value("dstAlphaFactor", std::string{"zero"});
                pass.pipelineDesc.alphaBlendOp        = pipeJson.value("alphaBlendOp", std::string{"add"});
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

                // Parse vertexCount (can be fixed or from parameter)
                if (execJson.contains("vertexCount")) {
                    const auto& vcJson = execJson["vertexCount"];
                    if (vcJson.is_number()) {
                        pass.execution.vertexCount.value = vcJson.get<uint32_t>();
                    } else if (vcJson.is_object()) {
                        pass.execution.vertexCount.parameter = vcJson.value("parameter", std::string{});
                        pass.execution.vertexCount.resource  = vcJson.value("resource", std::string{});
                        pass.execution.vertexCount.dimension = vcJson.value("dimension", std::string{});
                        pass.execution.vertexCount.divisor   = vcJson.value("divisor", 1u);
                    }
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
                    auto parseDispatchDim = [](const nlohmann::json& dj) -> DispatchDimension {
                        DispatchDimension dim;
                        if (dj.is_number()) {
                            dim.value = dj.get<uint32_t>();
                        } else if (dj.is_object()) {
                            dim.value     = dj.value("value", 0u);
                            dim.resource  = dj.value("resource", std::string{});
                            dim.dimension = dj.value("dimension", std::string{});
                            dim.divisor   = dj.value("divisor", 1u);
                            dim.parameter = dj.value("parameter", std::string{});
                        }
                        return dim;
                    };

                    if (dispJson.contains("x")) pass.execution.dispatch[0] = parseDispatchDim(dispJson["x"]);
                    if (dispJson.contains("y")) pass.execution.dispatch[1] = parseDispatchDim(dispJson["y"]);
                    if (dispJson.contains("z")) pass.execution.dispatch[2] = parseDispatchDim(dispJson["z"]);
                }

                // For built-in geometry types (opaque_geometry, transparent_geometry, shadow_casters)
                pass.execution.sortMode = execJson.value("sortMode", std::string{"none"});
                pass.execution.entityDataBinding = execJson.value("entityDataBinding", std::string{});
                pass.execution.renderLayerMask = execJson.value("renderLayerMask", 0xFFFFFFFFu);
                pass.execution.lightIndex = execJson.value("lightIndex", -1);
            }

            // Parse hasSideEffects flag (prevents pass culling for compute-only passes)
            pass.hasSideEffects = passJson.value("hasSideEffects", false);

            // Parse enabled flag (default: true — passes execute unless explicitly disabled)
            pass.enabled = passJson.value("enabled", true);

            builder.addPass(std::move(pass));
        }
    }
}

void loadGraphFromFile(FrameGraphBuilder& builder, const std::string& filePath) {
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
                    errorDetail = strerror(savedErrno);
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
    loadGraphFromJson(builder, json);
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
            if (desc.mipLevels != 1)   imgJson["mipLevels"] = desc.mipLevels;
            if (desc.arrayLayers != 1) imgJson["arrayLayers"] = desc.arrayLayers;
            if (desc.transient)        imgJson["transient"] = true;

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
                passJson["outputs"].push_back(serializeResourceAccess(output, builder));
            }
        }

        // Pipeline
        const auto& pd = pass.pipelineDesc;
        if (!pd.vertexShader.empty() || !pd.fragmentShader.empty() || !pd.computeShader.empty()) {
            nlohmann::json pipeJson;
            if (!pd.vertexShader.empty())   pipeJson["vertexShader"] = pd.vertexShader;
            if (!pd.fragmentShader.empty()) pipeJson["fragmentShader"] = pd.fragmentShader;
            if (!pd.computeShader.empty())  pipeJson["computeShader"] = pd.computeShader;
            pipeJson["depthTest"]  = pd.depthTest;
            pipeJson["depthWrite"] = pd.depthWrite;
            pipeJson["cullMode"]   = pd.cullMode;
            pipeJson["blending"]   = pd.blending;
            pipeJson["topology"]   = pd.topology;
            if (pd.vertexInput != "default") pipeJson["vertexInput"] = pd.vertexInput;
            if (pd.wireframe) pipeJson["wireframe"] = true;
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
