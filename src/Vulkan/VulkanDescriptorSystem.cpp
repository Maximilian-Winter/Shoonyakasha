//
// Created by maxim on 11.08.2025.
//
//
// VulkanDescriptorSystem.cpp - Implementation of the descriptor system
//

#include "Vulkan/VulkanDescriptorSystem.h"
#include "Vulkan/VulkanDevice.h"
#include <stdexcept>
#include <algorithm>
#include <set>

namespace Shoonyakasha {

// ═══════════════════════════════════════════════════════════════
// BufferBinding Factory Methods
// ═══════════════════════════════════════════════════════════════

BufferBinding BufferBinding::createUniform(const std::string& name, uint32_t binding, VkShaderStageFlags stages) {
    BufferBinding result;
    result.name = name;
    result.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    result.stages = stages;
    result.binding = binding;
    result.count = 1;
    return result;
}

BufferBinding BufferBinding::createStorage(const std::string& name, uint32_t binding, VkShaderStageFlags stages) {
    BufferBinding result;
    result.name = name;
    result.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    result.stages = stages;
    result.binding = binding;
    result.count = 1;
    return result;
}

BufferBinding BufferBinding::createStorageArray(const std::string& name, uint32_t binding, uint32_t count, VkShaderStageFlags stages) {
    BufferBinding result;
    result.name = name;
    result.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    result.stages = stages;
    result.binding = binding;
    result.count = count;
    return result;
}

// ═══════════════════════════════════════════════════════════════
// ImageBinding Factory Methods
// ═══════════════════════════════════════════════════════════════

ImageBinding ImageBinding::createTexture(const std::string& name, uint32_t binding, VkShaderStageFlags stages) {
    ImageBinding result;
    result.name = name;
    result.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    result.stages = stages;
    result.binding = binding;
    result.count = 1;
    return result;
}

ImageBinding ImageBinding::createStorageImage(const std::string& name, uint32_t binding, VkShaderStageFlags stages) {
    ImageBinding result;
    result.name = name;
    result.type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    result.stages = stages;
    result.binding = binding;
    result.count = 1;
    return result;
}

ImageBinding ImageBinding::createTextureArray(const std::string& name, uint32_t binding, uint32_t count, VkShaderStageFlags stages) {
    ImageBinding result;
    result.name = name;
    result.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    result.stages = stages;
    result.binding = binding;
    result.count = count;
    return result;
}

ImageBinding ImageBinding::createSeparateTexture(const std::string& name, uint32_t binding) {
    ImageBinding result;
    result.name = name;
    result.type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    result.stages = VK_SHADER_STAGE_FRAGMENT_BIT;
    result.binding = binding;
    result.count = 1;
    return result;
}

ImageBinding ImageBinding::createSeparateSampler(const std::string& name, uint32_t binding) {
    ImageBinding result;
    result.name = name;
    result.type = VK_DESCRIPTOR_TYPE_SAMPLER;
    result.stages = VK_SHADER_STAGE_FRAGMENT_BIT;
    result.binding = binding;
    result.count = 1;
    return result;
}

// ═══════════════════════════════════════════════════════════════
// DescriptorLayoutBuilder Implementation
// ═══════════════════════════════════════════════════════════════

DescriptorLayoutBuilder::DescriptorLayoutBuilder() {
    // Initialize with clean slate, ready for composition
}

DescriptorLayoutBuilder& DescriptorLayoutBuilder::addBuffer(const BufferBinding& binding) {
    validateAndAddBinding(binding);
    return *this;
}

DescriptorLayoutBuilder& DescriptorLayoutBuilder::addImage(const ImageBinding& binding) {
    validateAndAddBinding(binding);
    return *this;
}

DescriptorLayoutBuilder& DescriptorLayoutBuilder::addBinding(const ResourceBinding& binding) {
    validateAndAddBinding(binding);
    return *this;
}

DescriptorLayoutBuilder& DescriptorLayoutBuilder::addUniformBuffer(const std::string& name, uint32_t binding, VkShaderStageFlags stages) {
    return addBuffer(BufferBinding::createUniform(name, binding, stages));
}

DescriptorLayoutBuilder& DescriptorLayoutBuilder::addTexture(const std::string& name, uint32_t binding, VkShaderStageFlags stages) {
    return addImage(ImageBinding::createTexture(name, binding, stages));
}

DescriptorLayoutBuilder& DescriptorLayoutBuilder::addStorageBuffer(const std::string& name, uint32_t binding, VkShaderStageFlags stages) {
    return addBuffer(BufferBinding::createStorage(name, binding, stages));
}

DescriptorLayoutBuilder& DescriptorLayoutBuilder::addStorageImage(const std::string& name, uint32_t binding, VkShaderStageFlags stages) {
    return addImage(ImageBinding::createStorageImage(name, binding, stages));
}

DescriptorLayoutBuilder& DescriptorLayoutBuilder::configureBasicMaterial() {
    addUniformBuffer("materialData", 0, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);
    addTexture("mainTexture", 1, VK_SHADER_STAGE_FRAGMENT_BIT);
    return *this;
}

DescriptorLayoutBuilder& DescriptorLayoutBuilder::configurePBRMaterial() {
    addUniformBuffer("materialData", 0, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);
    addTexture("baseColorTexture", 1);
    addTexture("normalTexture", 2);
    addTexture("metallicRoughnessTexture", 3);
    addTexture("occlusionTexture", 4);
    addTexture("emissiveTexture", 5);
    return *this;
}

DescriptorLayoutBuilder& DescriptorLayoutBuilder::configureCompute() {
    addStorageBuffer("inputBuffer", 0, VK_SHADER_STAGE_COMPUTE_BIT);
    addStorageBuffer("outputBuffer", 1, VK_SHADER_STAGE_COMPUTE_BIT);
    addStorageImage("resultImage", 2, VK_SHADER_STAGE_COMPUTE_BIT);
    return *this;
}

DescriptorLayoutBuilder& DescriptorLayoutBuilder::configurePostProcess() {
    addTexture("inputTexture", 0, VK_SHADER_STAGE_FRAGMENT_BIT);
    addUniformBuffer("postProcessParams", 1, VK_SHADER_STAGE_FRAGMENT_BIT);
    return *this;
}

DescriptorLayoutBuilder::LayoutConfiguration DescriptorLayoutBuilder::build() {
    // Build indices for fast lookup
    for (size_t i = 0; i < m_config.bindings.size(); ++i) {
        std::visit([&](const auto& binding) {
            m_config.bindingIndices[binding.name] = binding.binding;
            m_config.indexToName[binding.binding] = binding.name;
        }, m_config.bindings[i]);
    }

    return m_config;
}

void DescriptorLayoutBuilder::validateAndAddBinding(const ResourceBinding& binding) {
    uint32_t bindingIndex = std::visit([](const auto& b) { return b.binding; }, binding);
    std::string bindingName = std::visit([](const auto& b) { return b.name; }, binding);

    // Check for duplicate binding indices
    for (const auto& existingBinding : m_config.bindings) {
        uint32_t existingIndex = std::visit([](const auto& b) { return b.binding; }, existingBinding);
        if (existingIndex == bindingIndex) {
            throw std::runtime_error("Duplicate binding index: " + std::to_string(bindingIndex));
        }
    }

    // Check for duplicate names
    for (const auto& existingBinding : m_config.bindings) {
        std::string existingName = std::visit([](const auto& b) { return b.name; }, existingBinding);
        if (existingName == bindingName) {
            throw std::runtime_error("Duplicate binding name: " + bindingName);
        }
    }

    m_config.bindings.push_back(binding);
}

// ═══════════════════════════════════════════════════════════════
// VulkanDescriptorSet Implementation
// ═══════════════════════════════════════════════════════════════

VulkanDescriptorSet::VulkanDescriptorSet(VulkanDevice& device,
                                        const DescriptorLayoutBuilder::LayoutConfiguration& config,
                                        uint32_t maxSets)
    : m_device(device), m_config(config), m_maxSets(maxSets) {

    // Extract binding names for introspection
    for (const auto& binding : m_config.bindings) {
        std::visit([&](const auto& b) {
            m_bindingNames.push_back(b.name);
        }, binding);
    }

    // Initialize pending update structures
    m_pendingWrites.resize(maxSets);
    m_bufferInfos.resize(maxSets);
    m_imageInfos.resize(maxSets);

    createLayout();
    createPool();
    allocateSets();
}

std::unique_ptr<VulkanDescriptorSet> VulkanDescriptorSet::createBasicMaterial(VulkanDevice& device, uint32_t maxSets) {
    auto config = DescriptorLayoutBuilder()
        .configureBasicMaterial()
        .build();
    return std::make_unique<VulkanDescriptorSet>(device, config, maxSets);
}

std::unique_ptr<VulkanDescriptorSet> VulkanDescriptorSet::createPBRMaterial(VulkanDevice& device, uint32_t maxSets) {
    auto config = DescriptorLayoutBuilder()
        .configurePBRMaterial()
        .build();
    return std::make_unique<VulkanDescriptorSet>(device, config, maxSets);
}

std::unique_ptr<VulkanDescriptorSet> VulkanDescriptorSet::createCompute(VulkanDevice& device, uint32_t maxSets) {
    auto config = DescriptorLayoutBuilder()
        .configureCompute()
        .build();
    return std::make_unique<VulkanDescriptorSet>(device, config, maxSets);
}

std::unique_ptr<VulkanDescriptorSet> VulkanDescriptorSet::createFromBuilder(
    VulkanDevice& device, const DescriptorLayoutBuilder& builder, uint32_t maxSets) {

    // Create a copy of the builder and build the configuration
    auto builderCopy = builder;
    auto config = builderCopy.build();
    return std::make_unique<VulkanDescriptorSet>(device, config, maxSets);
}

VulkanDescriptorSet::~VulkanDescriptorSet() {
    cleanup();
}

void VulkanDescriptorSet::bindBuffer(const std::string& name, uint32_t set, const BufferResource& resource) {
    bindResource(name, set, resource);
}

void VulkanDescriptorSet::bindImage(const std::string& name, uint32_t set, const ImageResource& resource) {
    bindResource(name, set, resource);
}

void VulkanDescriptorSet::bindResource(const std::string& name, uint32_t set, const Resource& resource) {
    if (set >= m_maxSets) {
        throw std::runtime_error("Set index out of range: " + std::to_string(set));
    }

    addPendingWrite(set, name, resource);
}

void VulkanDescriptorSet::bindResources(uint32_t set, const std::unordered_map<std::string, Resource>& resources) {
    for (const auto& [name, resource] : resources) {
        bindResource(name, set, resource);
    }
}

void VulkanDescriptorSet::updateSet(uint32_t set) {
    if (set >= m_maxSets) {
        throw std::runtime_error("Set index out of range: " + std::to_string(set));
    }

    if (!m_pendingWrites[set].empty()) {
        // Fix up pointers that may have been invalidated by vector reallocation.
        // The bufferInfos and imageInfos vectors are now stable, so we can rebuild
        // the pointers by iterating through writes and matching them to their info indices.
        size_t bufferIdx = 0;
        size_t imageIdx = 0;
        for (auto& write : m_pendingWrites[set]) {
            if (write.pBufferInfo != nullptr) {
                write.pBufferInfo = &m_bufferInfos[set][bufferIdx++];
            }
            if (write.pImageInfo != nullptr) {
                write.pImageInfo = &m_imageInfos[set][imageIdx++];
            }
        }

        vkUpdateDescriptorSets(m_device.getLogicalDevice(),
                              static_cast<uint32_t>(m_pendingWrites[set].size()),
                              m_pendingWrites[set].data(), 0, nullptr);

        // Clear pending writes
        m_pendingWrites[set].clear();
        m_bufferInfos[set].clear();
        m_imageInfos[set].clear();
    }
}

void VulkanDescriptorSet::bind(VkCommandBuffer commandBuffer, VkPipelineLayout pipelineLayout,
                              uint32_t set, uint32_t firstSet) {
    if (set >= m_sets.size()) {
        throw std::runtime_error("Set index out of range: " + std::to_string(set));
    }

    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                           pipelineLayout, firstSet, 1, &m_sets[set], 0, nullptr);
}

void VulkanDescriptorSet::recreate() {
    cleanup();
    createLayout();
    createPool();
    allocateSets();
}

bool VulkanDescriptorSet::hasBinding(const std::string& name) const {
    return m_config.bindingIndices.find(name) != m_config.bindingIndices.end();
}

VkDescriptorType VulkanDescriptorSet::getBindingType(const std::string& name) const {
    auto it = m_config.bindingIndices.find(name);
    if (it == m_config.bindingIndices.end()) {
        throw std::runtime_error("Unknown binding: " + name);
    }

    uint32_t bindingIndex = it->second;
    for (const auto& binding : m_config.bindings) {
        uint32_t currentIndex = std::visit([](const auto& b) { return b.binding; }, binding);
        if (currentIndex == bindingIndex) {
            return std::visit([](const auto& b) { return b.type; }, binding);
        }
    }

    throw std::runtime_error("Binding type not found for: " + name);
}

const std::vector<std::string>& VulkanDescriptorSet::getBindingNames() const {
    return m_bindingNames;
}

void VulkanDescriptorSet::createLayout() {
    std::vector<VkDescriptorSetLayoutBinding> layoutBindings;
    layoutBindings.reserve(m_config.bindings.size());

    for (const auto& binding : m_config.bindings) {
        VkDescriptorSetLayoutBinding layoutBinding{};

        std::visit([&](const auto& b) {
            layoutBinding.binding = b.binding;
            layoutBinding.descriptorType = b.type;
            layoutBinding.descriptorCount = b.count;
            layoutBinding.stageFlags = b.stages;
            layoutBinding.pImmutableSamplers = nullptr;
        }, binding);

        layoutBindings.push_back(layoutBinding);
    }

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(layoutBindings.size());
    layoutInfo.pBindings = layoutBindings.data();

    if (vkCreateDescriptorSetLayout(m_device.getLogicalDevice(), &layoutInfo, nullptr, &m_layout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create descriptor set layout!");
    }
}

void VulkanDescriptorSet::createPool() {
    // Count descriptor types
    std::unordered_map<VkDescriptorType, uint32_t> typeCounts;

    for (const auto& binding : m_config.bindings) {
        std::visit([&](const auto& b) {
            typeCounts[b.type] += b.count * m_maxSets;
        }, binding);
    }

    std::vector<VkDescriptorPoolSize> poolSizes;
    poolSizes.reserve(typeCounts.size());

    for (const auto& [type, count] : typeCounts) {
        VkDescriptorPoolSize poolSize{};
        poolSize.type = type;
        poolSize.descriptorCount = count;
        poolSizes.push_back(poolSize);
    }

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = m_maxSets;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT; // Allow individual set freeing

    if (vkCreateDescriptorPool(m_device.getLogicalDevice(), &poolInfo, nullptr, &m_pool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create descriptor pool!");
    }
}

void VulkanDescriptorSet::allocateSets() {
    std::vector<VkDescriptorSetLayout> layouts(m_maxSets, m_layout);

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_pool;
    allocInfo.descriptorSetCount = m_maxSets;
    allocInfo.pSetLayouts = layouts.data();

    m_sets.resize(m_maxSets);
    if (vkAllocateDescriptorSets(m_device.getLogicalDevice(), &allocInfo, m_sets.data()) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate descriptor sets!");
    }
}

void VulkanDescriptorSet::cleanup() {
    if (m_pool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(m_device.getLogicalDevice(), m_pool, nullptr);
        m_pool = VK_NULL_HANDLE;
    }

    if (m_layout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(m_device.getLogicalDevice(), m_layout, nullptr);
        m_layout = VK_NULL_HANDLE;
    }

    m_sets.clear();
}

uint32_t VulkanDescriptorSet::getBindingIndex(const std::string& name) const {
    auto it = m_config.bindingIndices.find(name);
    if (it == m_config.bindingIndices.end()) {
        throw std::runtime_error("Unknown binding: " + name);
    }
    return it->second;
}

void VulkanDescriptorSet::addPendingWrite(uint32_t set, const std::string& name, const Resource& resource) {
    uint32_t bindingIndex = getBindingIndex(name);

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = m_sets[set];
    write.dstBinding = bindingIndex;
    write.dstArrayElement = 0;
    write.descriptorCount = 1;

    std::visit([&](const auto& res) {
        using T = std::decay_t<decltype(res)>;

        if constexpr (std::is_same_v<T, BufferResource>) {
            VkDescriptorBufferInfo bufferInfo{};
            bufferInfo.buffer = res.buffer;
            bufferInfo.offset = res.offset;
            bufferInfo.range = res.range;

            m_bufferInfos[set].push_back(bufferInfo);
            write.pBufferInfo = &m_bufferInfos[set].back();
            write.descriptorType = getBindingType(name);
        }
        else if constexpr (std::is_same_v<T, ImageResource>) {
            VkDescriptorImageInfo imageInfo{};
            imageInfo.imageLayout = res.layout;
            imageInfo.imageView = res.imageView;
            imageInfo.sampler = res.sampler;

            m_imageInfos[set].push_back(imageInfo);
            write.pImageInfo = &m_imageInfos[set].back();
            write.descriptorType = getBindingType(name);
        }
    }, resource);

    m_pendingWrites[set].push_back(write);
}

// ═══════════════════════════════════════════════════════════════
// DescriptorManager Implementation - The wise coordinator
// ═══════════════════════════════════════════════════════════════

DescriptorManager::DescriptorManager(VulkanDevice& device) : m_device(device) {
    // Ready to coordinate the flow of descriptors
}

DescriptorManager::~DescriptorManager() {
    m_descriptorSets.clear();
}

std::shared_ptr<VulkanDescriptorSet> DescriptorManager::createDescriptorSet(
    const std::string& name,
    const DescriptorLayoutBuilder::LayoutConfiguration& config,
    uint32_t maxSets) {

    auto descriptorSet = std::make_shared<VulkanDescriptorSet>(m_device, config, maxSets);
    m_descriptorSets[name] = descriptorSet;
    return descriptorSet;
}

std::shared_ptr<VulkanDescriptorSet> DescriptorManager::createDescriptorSet(
    const std::string& name,
    const DescriptorLayoutBuilder& builder,
    uint32_t maxSets) {

    auto builderCopy = builder;
    auto config = builderCopy.build();
    return createDescriptorSet(name, config, maxSets);
}

std::shared_ptr<VulkanDescriptorSet> DescriptorManager::getDescriptorSet(const std::string& name) {
    auto it = m_descriptorSets.find(name);
    if (it == m_descriptorSets.end()) {
        throw std::runtime_error("Descriptor set not found: " + name);
    }
    return it->second;
}

void DescriptorManager::bindGlobalResources(const std::unordered_map<std::string, Resource>& resources) {
    for (const auto& [name, resource] : resources) {
        m_globalResources[name] = resource;
    }
}

void DescriptorManager::updateAllSets() {
    for (auto& [name, descriptorSet] : m_descriptorSets) {
        // Apply global resources to all sets that have matching bindings
        for (const auto& [resourceName, resource] : m_globalResources) {
            if (descriptorSet->hasBinding(resourceName)) {
                for (uint32_t i = 0; i < descriptorSet->getSets().size(); ++i) {
                    descriptorSet->bindResource(resourceName, i, resource);
                }
            }
        }

        // Update all sets
        for (uint32_t i = 0; i < descriptorSet->getSets().size(); ++i) {
            descriptorSet->updateSet(i);
        }
    }
}

void DescriptorManager::recreateAll() {
    for (auto& [name, descriptorSet] : m_descriptorSets) {
        descriptorSet->recreate();
    }
}

} // namespace Shoonyakasha
