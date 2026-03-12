//
// Created by maxim on 11.08.2025.
//

//
// VulkanDescriptorSystem.h - Resource binding and descriptor management
//

#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <unordered_map>
#include <string>
#include <memory>
#include <variant>
#include <functional>

namespace Shoonyakasha {

// Forward declarations
class VulkanDevice;
class VulkanBuffer;
class VulkanImage;

// ═══════════════════════════════════════════════════════════════
// Resource Descriptors - Describing the nature of bindings
// ═══════════════════════════════════════════════════════════════

struct BufferBinding {
    std::string name;
    VkDescriptorType type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    VkShaderStageFlags stages = VK_SHADER_STAGE_VERTEX_BIT;
    uint32_t binding = 0;
    uint32_t count = 1;

    // Factory methods for common patterns
    static BufferBinding createUniform(const std::string& name, uint32_t binding,
                                     VkShaderStageFlags stages = VK_SHADER_STAGE_VERTEX_BIT);
    static BufferBinding createStorage(const std::string& name, uint32_t binding,
                                     VkShaderStageFlags stages = VK_SHADER_STAGE_COMPUTE_BIT);
    static BufferBinding createStorageArray(const std::string& name, uint32_t binding,
                                          uint32_t count, VkShaderStageFlags stages);
};

struct ImageBinding {
    std::string name;
    VkDescriptorType type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    VkShaderStageFlags stages = VK_SHADER_STAGE_FRAGMENT_BIT;
    uint32_t binding = 0;
    uint32_t count = 1;

    // Factory methods for common patterns
    static ImageBinding createTexture(const std::string& name, uint32_t binding,
                                    VkShaderStageFlags stages = VK_SHADER_STAGE_FRAGMENT_BIT);
    static ImageBinding createStorageImage(const std::string& name, uint32_t binding,
                                         VkShaderStageFlags stages = VK_SHADER_STAGE_COMPUTE_BIT);
    static ImageBinding createTextureArray(const std::string& name, uint32_t binding,
                                         uint32_t count, VkShaderStageFlags stages);
    static ImageBinding createSeparateTexture(const std::string& name, uint32_t binding);
    static ImageBinding createSeparateSampler(const std::string& name, uint32_t binding);
};

// Resource variant - what can be bound
using ResourceBinding = std::variant<BufferBinding, ImageBinding>;

// ═══════════════════════════════════════════════════════════════
// Resource References - Actual resources to bind
// ═══════════════════════════════════════════════════════════════

struct BufferResource {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceSize offset = 0;
    VkDeviceSize range = VK_WHOLE_SIZE;

    BufferResource() = default;
    BufferResource(VkBuffer buf, VkDeviceSize off = 0, VkDeviceSize rng = VK_WHOLE_SIZE)
        : buffer(buf), offset(off), range(rng) {}
};

struct ImageResource {
    VkImageView imageView = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
    VkImageLayout layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    ImageResource() = default;
    ImageResource(VkImageView view, VkSampler samp = VK_NULL_HANDLE,
                  VkImageLayout lay = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
        : imageView(view), sampler(samp), layout(lay) {}
};

using Resource = std::variant<BufferResource, ImageResource>;

// ═══════════════════════════════════════════════════════════════
// Descriptor Layout Builder - Composing resource patterns
// ═══════════════════════════════════════════════════════════════

class DescriptorLayoutBuilder {
public:
    DescriptorLayoutBuilder();

    // Fluent API for descriptor layout composition
    DescriptorLayoutBuilder& addBuffer(const BufferBinding& binding);
    DescriptorLayoutBuilder& addImage(const ImageBinding& binding);
    DescriptorLayoutBuilder& addBinding(const ResourceBinding& binding);

    // Convenience methods for common patterns
    DescriptorLayoutBuilder& addUniformBuffer(const std::string& name, uint32_t binding,
                                            VkShaderStageFlags stages = VK_SHADER_STAGE_VERTEX_BIT);
    DescriptorLayoutBuilder& addTexture(const std::string& name, uint32_t binding,
                                      VkShaderStageFlags stages = VK_SHADER_STAGE_FRAGMENT_BIT);
    DescriptorLayoutBuilder& addStorageBuffer(const std::string& name, uint32_t binding,
                                            VkShaderStageFlags stages = VK_SHADER_STAGE_COMPUTE_BIT);
    DescriptorLayoutBuilder& addStorageImage(const std::string& name, uint32_t binding,
                                           VkShaderStageFlags stages = VK_SHADER_STAGE_COMPUTE_BIT);

    // Common pattern presets
    DescriptorLayoutBuilder& configureBasicMaterial(); // UBO + texture
    DescriptorLayoutBuilder& configurePBRMaterial();   // UBO + multiple textures
    DescriptorLayoutBuilder& configureCompute();       // Storage buffers + images
    DescriptorLayoutBuilder& configurePostProcess();   // Input textures + output

    struct LayoutConfiguration {
        std::vector<ResourceBinding> bindings;
        std::unordered_map<std::string, uint32_t> bindingIndices;
        std::unordered_map<uint32_t, std::string> indexToName;
    };

    LayoutConfiguration build();

private:
    LayoutConfiguration m_config;
    uint32_t m_nextBinding = 0;

    void validateAndAddBinding(const ResourceBinding& binding);
};

// ═══════════════════════════════════════════════════════════════
// Modern Descriptor Set - Flexible like bamboo, strong like oak
// ═══════════════════════════════════════════════════════════════

class VulkanDescriptorSet {
public:
    VulkanDescriptorSet(VulkanDevice& device,
                       const DescriptorLayoutBuilder::LayoutConfiguration& config,
                       uint32_t maxSets = 1);

    // Named constructors for common patterns
    static std::unique_ptr<VulkanDescriptorSet> createBasicMaterial(
        VulkanDevice& device, uint32_t maxSets = 1);

    static std::unique_ptr<VulkanDescriptorSet> createPBRMaterial(
        VulkanDevice& device, uint32_t maxSets = 1);

    static std::unique_ptr<VulkanDescriptorSet> createCompute(
        VulkanDevice& device, uint32_t maxSets = 1);

    static std::unique_ptr<VulkanDescriptorSet> createFromBuilder(
        VulkanDevice& device, const DescriptorLayoutBuilder& builder, uint32_t maxSets = 1);

    ~VulkanDescriptorSet();

    // Resource binding - flexible descriptor management
    void bindBuffer(const std::string& name, uint32_t set, const BufferResource& resource);
    void bindImage(const std::string& name, uint32_t set, const ImageResource& resource);
    void bindResource(const std::string& name, uint32_t set, const Resource& resource);

    // Bulk operations for efficiency
    void bindResources(uint32_t set, const std::unordered_map<std::string, Resource>& resources);
    void updateSet(uint32_t set); // Apply all pending bindings

    // Pipeline integration
    void bind(VkCommandBuffer commandBuffer, VkPipelineLayout pipelineLayout,
              uint32_t set = 0, uint32_t firstSet = 0);

    // Hot-reloading support
    void recreate(); // Recreate with same configuration

    // Introspection
    bool hasBinding(const std::string& name) const;
    VkDescriptorType getBindingType(const std::string& name) const;
    const std::vector<std::string>& getBindingNames() const;

    // Accessors
    VkDescriptorSetLayout getLayout() const { return m_layout; }
    VkDescriptorPool getPool() const { return m_pool; }
    const std::vector<VkDescriptorSet>& getSets() const { return m_sets; }
    const DescriptorLayoutBuilder::LayoutConfiguration& getConfiguration() const { return m_config; }

private:
    VulkanDevice& m_device;
    DescriptorLayoutBuilder::LayoutConfiguration m_config;
    uint32_t m_maxSets;

    VkDescriptorSetLayout m_layout = VK_NULL_HANDLE;
    VkDescriptorPool m_pool = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> m_sets;

    // Cached binding names for introspection
    std::vector<std::string> m_bindingNames;

    // Pending updates per set
    std::vector<std::vector<VkWriteDescriptorSet>> m_pendingWrites;
    std::vector<std::vector<VkDescriptorBufferInfo>> m_bufferInfos;
    std::vector<std::vector<VkDescriptorImageInfo>> m_imageInfos;

    void createLayout();
    void createPool();
    void allocateSets();
    void cleanup();

    uint32_t getBindingIndex(const std::string& name) const;
    void addPendingWrite(uint32_t set, const std::string& name, const Resource& resource);
};

// ═══════════════════════════════════════════════════════════════
// Descriptor Manager - Coordinating the flow of resources
// ═══════════════════════════════════════════════════════════════

class DescriptorManager {
public:
    DescriptorManager(VulkanDevice& device);
    ~DescriptorManager();

    // Template creation from configurations
    std::shared_ptr<VulkanDescriptorSet> createDescriptorSet(
        const std::string& name,
        const DescriptorLayoutBuilder::LayoutConfiguration& config,
        uint32_t maxSets = 1);

    std::shared_ptr<VulkanDescriptorSet> createDescriptorSet(
        const std::string& name,
        const DescriptorLayoutBuilder& builder,
        uint32_t maxSets = 1);

    // Get existing descriptor set
    std::shared_ptr<VulkanDescriptorSet> getDescriptorSet(const std::string& name);

    // Resource binding shortcuts
    void bindGlobalResources(const std::unordered_map<std::string, Resource>& resources);
    void updateAllSets();

    // Cleanup and recreation
    void recreateAll(); // Useful for device loss scenarios

private:
    VulkanDevice& m_device;
    std::unordered_map<std::string, std::shared_ptr<VulkanDescriptorSet>> m_descriptorSets;
    std::unordered_map<std::string, Resource> m_globalResources;
};

} // namespace Shoonyakasha
using Shoonyakasha::BufferBinding;
using Shoonyakasha::ImageBinding;
using Shoonyakasha::ResourceBinding;
using Shoonyakasha::BufferResource;
using Shoonyakasha::ImageResource;
using Shoonyakasha::Resource;
using Shoonyakasha::DescriptorLayoutBuilder;
using Shoonyakasha::VulkanDescriptorSet;
using Shoonyakasha::DescriptorManager;

// ═══════════════════════════════════════════════════════════════
// Usage Examples - Poetry in motion
// ═══════════════════════════════════════════════════════════════

/*

// Creating a flexible material descriptor set:
auto materialSet = DescriptorLayoutBuilder()
    .addUniformBuffer("materialUBO", 0, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
    .addTexture("diffuseTexture", 1)
    .addTexture("normalTexture", 2)
    .addTexture("metallicRoughnessTexture", 3)
    .build();

auto descriptorSet = VulkanDescriptorSet::createFromBuilder(device, materialSet, 2);

// Binding resources:
descriptorSet->bindBuffer("materialUBO", 0, BufferResource{uboBuffer});
descriptorSet->bindImage("diffuseTexture", 0, ImageResource{diffuseView, sampler});
descriptorSet->bindImage("normalTexture", 0, ImageResource{normalView, sampler});
descriptorSet->updateSet(0);

// Or bind all at once:
std::unordered_map<std::string, Resource> resources = {
    {"materialUBO", BufferResource{uboBuffer}},
    {"diffuseTexture", ImageResource{diffuseView, sampler}},
    {"normalTexture", ImageResource{normalView, sampler}}
};
descriptorSet->bindResources(0, resources);

// Pipeline integration:
descriptorSet->bind(commandBuffer, pipeline.getLayout(), 0);

// Common patterns made simple:
auto basicMaterial = VulkanDescriptorSet::createBasicMaterial(device, maxFramesInFlight);
auto pbrMaterial = VulkanDescriptorSet::createPBRMaterial(device, maxFramesInFlight);
auto computeSet = VulkanDescriptorSet::createCompute(device, 1);

*/
