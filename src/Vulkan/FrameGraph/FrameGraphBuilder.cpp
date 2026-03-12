//
// Shoonyakasha Engine - Frame Graph Builder
//

#include "Vulkan/FrameGraph/FrameGraph.h"
#include <stdexcept>

namespace Shoonyakasha {
namespace FrameGraph {

FrameGraphBuilder::FrameGraphBuilder() = default;

// ── Resource Declaration ──

ResourceHandle FrameGraphBuilder::declareImage(const std::string& name, const ImageDesc& desc) {
    if (m_resourceLookup.contains(name)) {
        throw std::runtime_error("FrameGraph: Duplicate resource name '" + name + "'");
    }

    ResourceHandle handle{static_cast<uint32_t>(m_resources.size())};

    ResourceDeclaration decl;
    decl.name = name;
    decl.kind = ResourceKind::Image;
    decl.imageDesc = desc;
    decl.imported = false;

    m_resources.push_back(std::move(decl));
    m_resourceLookup[name] = handle;

    return handle;
}

ResourceDeclaration* FrameGraphBuilder::getMutableResource(const std::string& name) {
    auto it = m_resourceLookup.find(name);
    if (it == m_resourceLookup.end()) return nullptr;
    return &m_resources[it->second.index];
}

ResourceHandle FrameGraphBuilder::declareBuffer(const std::string& name, const BufferDesc& desc) {
    if (m_resourceLookup.contains(name)) {
        throw std::runtime_error("FrameGraph: Duplicate resource name '" + name + "'");
    }

    ResourceHandle handle{static_cast<uint32_t>(m_resources.size())};

    ResourceDeclaration decl;
    decl.name = name;
    decl.kind = ResourceKind::Buffer;
    decl.bufferDesc = desc;
    decl.imported = false;

    m_resources.push_back(std::move(decl));
    m_resourceLookup[name] = handle;

    return handle;
}

ResourceHandle FrameGraphBuilder::importImage(const std::string& name, VkFormat format) {
    if (m_resourceLookup.contains(name)) {
        throw std::runtime_error("FrameGraph: Duplicate resource name '" + name + "'");
    }

    ResourceHandle handle{static_cast<uint32_t>(m_resources.size())};

    ResourceDeclaration decl;
    decl.name = name;
    decl.kind = ResourceKind::Image;
    decl.imageDesc.format = format;
    decl.imported = true;

    m_resources.push_back(std::move(decl));
    m_resourceLookup[name] = handle;

    return handle;
}

ResourceHandle FrameGraphBuilder::importBuffer(const std::string& name) {
    if (m_resourceLookup.contains(name)) {
        throw std::runtime_error("FrameGraph: Duplicate resource name '" + name + "'");
    }

    ResourceHandle handle{static_cast<uint32_t>(m_resources.size())};

    ResourceDeclaration decl;
    decl.name = name;
    decl.kind = ResourceKind::Buffer;
    decl.imported = true;

    m_resources.push_back(std::move(decl));
    m_resourceLookup[name] = handle;

    return handle;
}

// ── Pass Declaration ──

void FrameGraphBuilder::addPass(const PassDeclaration& pass) {
    if (m_passLookup.contains(pass.name)) {
        throw std::runtime_error("FrameGraph: Duplicate pass name '" + pass.name + "'");
    }

    uint32_t index = static_cast<uint32_t>(m_passes.size());
    m_passes.push_back(pass);
    m_passLookup[pass.name] = index;
}

// ── Sampler Declaration ──

void FrameGraphBuilder::addSampler(const SamplerDesc& sampler) {
    if (m_samplerLookup.contains(sampler.name)) {
        throw std::runtime_error("FrameGraph: Duplicate sampler name '" + sampler.name + "'");
    }

    uint32_t index = static_cast<uint32_t>(m_samplers.size());
    m_samplers.push_back(sampler);
    m_samplerLookup[sampler.name] = index;
}

const SamplerDesc* FrameGraphBuilder::getSampler(const std::string& name) const {
    auto it = m_samplerLookup.find(name);
    if (it == m_samplerLookup.end()) return nullptr;
    return &m_samplers[it->second];
}

bool FrameGraphBuilder::hasSampler(const std::string& name) const {
    return m_samplerLookup.contains(name);
}

// ── Uniform Buffer Declaration ──

void FrameGraphBuilder::addUniformBuffer(const UniformBufferDesc& ubo) {
    if (m_uniformBufferLookup.contains(ubo.name)) {
        throw std::runtime_error("FrameGraph: Duplicate uniform buffer name '" + ubo.name + "'");
    }

    uint32_t index = static_cast<uint32_t>(m_uniformBuffers.size());
    m_uniformBuffers.push_back(ubo);
    m_uniformBufferLookup[ubo.name] = index;
}

const UniformBufferDesc* FrameGraphBuilder::getUniformBuffer(const std::string& name) const {
    auto it = m_uniformBufferLookup.find(name);
    if (it == m_uniformBufferLookup.end()) return nullptr;
    return &m_uniformBuffers[it->second];
}

bool FrameGraphBuilder::hasUniformBuffer(const std::string& name) const {
    return m_uniformBufferLookup.contains(name);
}

// ── Descriptor Set Layout Declaration ──

void FrameGraphBuilder::addDescriptorSetLayout(const DescriptorSetLayoutDesc& layout) {
    if (m_descriptorSetLayoutLookup.contains(layout.name)) {
        throw std::runtime_error("FrameGraph: Duplicate descriptor set layout name '" + layout.name + "'");
    }

    uint32_t index = static_cast<uint32_t>(m_descriptorSetLayouts.size());
    m_descriptorSetLayouts.push_back(layout);
    m_descriptorSetLayoutLookup[layout.name] = index;
}

const DescriptorSetLayoutDesc* FrameGraphBuilder::getDescriptorSetLayout(const std::string& name) const {
    auto it = m_descriptorSetLayoutLookup.find(name);
    if (it == m_descriptorSetLayoutLookup.end()) return nullptr;
    return &m_descriptorSetLayouts[it->second];
}

bool FrameGraphBuilder::hasDescriptorSetLayout(const std::string& name) const {
    return m_descriptorSetLayoutLookup.contains(name);
}

// ── Entity Data Binding Declaration (v4) ──

void FrameGraphBuilder::addEntityDataBinding(const EntityDataBindingConfig& config) {
    if (m_entityDataBindingLookup.contains(config.name)) {
        throw std::runtime_error("FrameGraph: Duplicate entity data binding name '" + config.name + "'");
    }

    uint32_t index = static_cast<uint32_t>(m_entityDataBindings.size());
    m_entityDataBindings.push_back(config);
    m_entityDataBindingLookup[config.name] = index;
}

const EntityDataBindingConfig* FrameGraphBuilder::getEntityDataBinding(const std::string& name) const {
    auto it = m_entityDataBindingLookup.find(name);
    if (it == m_entityDataBindingLookup.end()) return nullptr;
    return &m_entityDataBindings[it->second];
}

bool FrameGraphBuilder::hasEntityDataBinding(const std::string& name) const {
    return m_entityDataBindingLookup.contains(name);
}

// ── Buffer Layout Declaration ──

void FrameGraphBuilder::addBufferLayout(const BufferLayoutDesc& layout) {
    if (m_bufferLayoutLookup.contains(layout.name)) {
        throw std::runtime_error("FrameGraph: Duplicate buffer layout name '" + layout.name + "'");
    }

    uint32_t index = static_cast<uint32_t>(m_bufferLayouts.size());
    m_bufferLayouts.push_back(layout);
    m_bufferLayoutLookup[layout.name] = index;
}

const BufferLayoutDesc* FrameGraphBuilder::getBufferLayout(const std::string& name) const {
    auto it = m_bufferLayoutLookup.find(name);
    if (it == m_bufferLayoutLookup.end()) return nullptr;
    return &m_bufferLayouts[it->second];
}

bool FrameGraphBuilder::hasBufferLayout(const std::string& name) const {
    return m_bufferLayoutLookup.contains(name);
}

// ── Lookups ──

PassDeclaration* FrameGraphBuilder::getPass(const std::string& name) {
    auto it = m_passLookup.find(name);
    if (it == m_passLookup.end()) return nullptr;
    return &m_passes[it->second];
}

const PassDeclaration* FrameGraphBuilder::getPass(const std::string& name) const {
    auto it = m_passLookup.find(name);
    if (it == m_passLookup.end()) return nullptr;
    return &m_passes[it->second];
}

ResourceHandle FrameGraphBuilder::getResource(const std::string& name) const {
    auto it = m_resourceLookup.find(name);
    if (it == m_resourceLookup.end()) return ResourceHandle{};
    return it->second;
}

bool FrameGraphBuilder::hasResource(const std::string& name) const {
    return m_resourceLookup.contains(name);
}

bool FrameGraphBuilder::hasPass(const std::string& name) const {
    return m_passLookup.contains(name);
}

void FrameGraphBuilder::clear() {
    m_resources.clear();
    m_resourceLookup.clear();
    m_passes.clear();
    m_passLookup.clear();
    m_samplers.clear();
    m_samplerLookup.clear();
    m_uniformBuffers.clear();
    m_uniformBufferLookup.clear();
    m_descriptorSetLayouts.clear();
    m_descriptorSetLayoutLookup.clear();
    m_entityDataBindings.clear();
    m_entityDataBindingLookup.clear();
    m_bufferLayouts.clear();
    m_bufferLayoutLookup.clear();
}

} // namespace FrameGraph
} // namespace Shoonyakasha
