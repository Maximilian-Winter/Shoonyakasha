// VulkanPipeline.cpp - Implementation of the graphics pipeline system

#include "Vulkan/VulkanPipeline.h"
#include "Vulkan/VulkanDevice.h"
#include "Vulkan/VulkanRenderPass.h"
#include "Vulkan/VertexTypes.h"
#include <fstream>
#include <stdexcept>

namespace Shoonyakasha {

// ═══════════════════════════════════════════════════════════════
// PipelineStateBuilder Implementation
// ═══════════════════════════════════════════════════════════════

PipelineStateBuilder::PipelineStateBuilder() {
    // Initialize with sensible defaults
}

PipelineStateBuilder& PipelineStateBuilder::withShaders(const std::string& vertPath, const std::string& fragPath) {
    m_state.shaderPaths.clear();
    m_state.shaderPaths.emplace_back(VK_SHADER_STAGE_VERTEX_BIT, vertPath);
    m_state.shaderPaths.emplace_back(VK_SHADER_STAGE_FRAGMENT_BIT, fragPath);
    return *this;
}

PipelineStateBuilder& PipelineStateBuilder::withComputeShader(const std::string& computePath) {
    m_state.shaderPaths.clear();
    m_state.shaderPaths.emplace_back(VK_SHADER_STAGE_COMPUTE_BIT, computePath);
    return *this;
}

PipelineStateBuilder& PipelineStateBuilder::withGeometryShader(const std::string& geomPath) {
    m_state.shaderPaths.emplace_back(VK_SHADER_STAGE_GEOMETRY_BIT, geomPath);
    return *this;
}

PipelineStateBuilder& PipelineStateBuilder::withTopology(VkPrimitiveTopology topology) {
    m_state.topology = topology;
    return *this;
}

PipelineStateBuilder& PipelineStateBuilder::withPrimitiveRestart(bool enable) {
    m_state.primitiveRestart = enable;
    return *this;
}

PipelineStateBuilder& PipelineStateBuilder::withCustomVertexInput(
    const std::vector<VkVertexInputBindingDescription>& bindings,
    const std::vector<VkVertexInputAttributeDescription>& attributes) {
    m_state.vertexBindings = bindings;
    m_state.vertexAttributes = attributes;
    return *this;
}

PipelineStateBuilder& PipelineStateBuilder::withWireframe(bool enable) {
    m_state.wireframe = enable;
    return *this;
}

PipelineStateBuilder& PipelineStateBuilder::withCulling(VkCullModeFlags cullMode, VkFrontFace frontFace) {
    m_state.cullMode = cullMode;
    m_state.frontFace = frontFace;
    return *this;
}

PipelineStateBuilder& PipelineStateBuilder::withDepthClamp(bool enable) {
    m_state.depthClamp = enable;
    return *this;
}

PipelineStateBuilder& PipelineStateBuilder::withLineWidth(float width) {
    m_state.lineWidth = width;
    return *this;
}

PipelineStateBuilder& PipelineStateBuilder::withDepthTest(bool enable, VkCompareOp compareOp) {
    m_state.depthTest = enable;
    m_state.depthCompareOp = compareOp;
    return *this;
}

PipelineStateBuilder& PipelineStateBuilder::withDepthWrite(bool enable) {
    m_state.depthWrite = enable;
    return *this;
}

PipelineStateBuilder& PipelineStateBuilder::withDepthBounds(float min, float max) {
    m_state.depthBounds = true;
    m_state.minDepthBounds = min;
    m_state.maxDepthBounds = max;
    return *this;
}

PipelineStateBuilder& PipelineStateBuilder::withStencilTest(bool enable) {
    m_state.stencilTest = enable;
    return *this;
}

PipelineStateBuilder& PipelineStateBuilder::withBlending(bool enable) {
    m_state.blending = enable;
    return *this;
}

PipelineStateBuilder& PipelineStateBuilder::withAlphaBlending() {
    m_state.blending = true;
    m_state.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    m_state.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    m_state.colorBlendOp = VK_BLEND_OP_ADD;
    m_state.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    m_state.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    m_state.alphaBlendOp = VK_BLEND_OP_ADD;
    return *this;
}

PipelineStateBuilder& PipelineStateBuilder::withAdditiveBlending() {
    m_state.blending = true;
    m_state.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    m_state.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
    m_state.colorBlendOp = VK_BLEND_OP_ADD;
    return *this;
}

PipelineStateBuilder& PipelineStateBuilder::withCustomBlending(VkBlendFactor srcColor, VkBlendFactor dstColor, VkBlendOp op) {
    m_state.blending = true;
    m_state.srcColorBlendFactor = srcColor;
    m_state.dstColorBlendFactor = dstColor;
    m_state.colorBlendOp = op;
    return *this;
}

PipelineStateBuilder& PipelineStateBuilder::withColorAttachmentCount(uint32_t count) {
    m_state.colorAttachmentCount = count;
    return *this;
}

PipelineStateBuilder& PipelineStateBuilder::withMultisampling(VkSampleCountFlagBits samples, bool sampleShading) {
    m_state.sampleCount = samples;
    m_state.sampleShading = sampleShading;
    return *this;
}

PipelineStateBuilder& PipelineStateBuilder::withDynamicViewport() {
    m_state.dynamicStates.push_back(VK_DYNAMIC_STATE_VIEWPORT);
    return *this;
}

PipelineStateBuilder& PipelineStateBuilder::withDynamicScissor() {
    m_state.dynamicStates.push_back(VK_DYNAMIC_STATE_SCISSOR);
    return *this;
}

PipelineStateBuilder& PipelineStateBuilder::withDynamicLineWidth() {
    m_state.dynamicStates.push_back(VK_DYNAMIC_STATE_LINE_WIDTH);
    return *this;
}

PipelineStateBuilder& PipelineStateBuilder::withDynamicState(VkDynamicState state) {
    m_state.dynamicStates.push_back(state);
    return *this;
}

PipelineStateBuilder& PipelineStateBuilder::withDescriptorSetLayout(VkDescriptorSetLayout layout) {
    m_state.descriptorSetLayouts.push_back(layout);
    return *this;
}

PipelineStateBuilder& PipelineStateBuilder::withPushConstants(VkShaderStageFlags stages, uint32_t size, uint32_t offset) {
    VkPushConstantRange range{};
    range.stageFlags = stages;
    range.size = size;
    range.offset = offset;
    m_state.pushConstantRanges.push_back(range);
    return *this;
}

std::unique_ptr<VulkanPipeline> PipelineStateBuilder::buildPipeline(VulkanDevice &device, VulkanRenderPass &renderPass,
    VkExtent2D extent) {
    auto state = build();
    return std::make_unique<VulkanPipeline>(device, renderPass, extent, state);
}

PipelineStateBuilder::PipelineState PipelineStateBuilder::build() const {
    return m_state;
}

// Template specialization for Vertex type
template<>
PipelineStateBuilder& PipelineStateBuilder::withVertexType<Vertex>() {
    auto bindingDescription = Vertex::getBindingDescription();
    auto attributeDescriptions = Vertex::getAttributeDescriptions();

    m_state.vertexBindings = {bindingDescription};
    m_state.vertexAttributes.assign(attributeDescriptions.begin(), attributeDescriptions.end());
    return *this;
}

// ═══════════════════════════════════════════════════════════════
// VulkanPipeline Implementation - Flexible rendering pipeline
// ═══════════════════════════════════════════════════════════════

VulkanPipeline::VulkanPipeline(VulkanDevice& device,
                               VulkanRenderPass& renderPass,
                               VkExtent2D extent,
                               const PipelineStateBuilder::PipelineState& state)
    : m_device(device)
    , m_renderPass(renderPass)
    , m_extent(extent)
    , m_state(state) {

    createPipelineLayout();
    loadShaders();
    createPipeline();
}

std::unique_ptr<VulkanPipeline> VulkanPipeline::createDefault(
    VulkanDevice& device,
    VulkanRenderPass& renderPass,
    VkExtent2D extent,
    const std::string& vertShader,
    const std::string& fragShader) {

    auto state = PipelineStateBuilder()
        .withShaders(vertShader, fragShader)
        .withVertexType<Vertex>()
        .withDepthTest(true)
        .withCulling(VK_CULL_MODE_BACK_BIT)
        .build();

    return std::make_unique<VulkanPipeline>(device, renderPass, extent, state);
}

std::unique_ptr<VulkanPipeline> VulkanPipeline::createWireframe(
    VulkanDevice& device,
    VulkanRenderPass& renderPass,
    VkExtent2D extent,
    const std::string& vertShader,
    const std::string& fragShader) {

    auto state = PipelineStateBuilder()
        .withShaders(vertShader, fragShader)
        .withVertexType<Vertex>()
        .withWireframe(true)
        .withCulling(VK_CULL_MODE_NONE)
        .withDepthTest(true)
        .build();

    return std::make_unique<VulkanPipeline>(device, renderPass, extent, state);
}

std::unique_ptr<VulkanPipeline> VulkanPipeline::createTransparent(
    VulkanDevice& device,
    VulkanRenderPass& renderPass,
    VkExtent2D extent,
    const std::string& vertShader,
    const std::string& fragShader) {

    auto state = PipelineStateBuilder()
        .withShaders(vertShader, fragShader)
        .withVertexType<Vertex>()
        .withAlphaBlending()
        .withCulling(VK_CULL_MODE_NONE) // Often want both sides for transparent objects
        .withDepthTest(true)
        .withDepthWrite(false) // Usually don't write depth for transparent objects
        .build();

    return std::make_unique<VulkanPipeline>(device, renderPass, extent, state);
}

VulkanPipeline::~VulkanPipeline() {
    cleanup();
}

void VulkanPipeline::bind(VkCommandBuffer commandBuffer) {
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
}

void VulkanPipeline::recreate(VkExtent2D newExtent) {
    m_extent = newExtent;
    cleanup();
    createPipeline();
}

void VulkanPipeline::reloadShaders() {
    // Hot-reload for development workflow - useful for iteration
    for (auto& [stage, module] : m_shaderModules) {
        vkDestroyShaderModule(m_device.getLogicalDevice(), module, nullptr);
    }
    m_shaderModules.clear();

    loadShaders();
    recreate(m_extent);
}

bool VulkanPipeline::supportsWireframe() const {
    return m_state.wireframe;
}

bool VulkanPipeline::hasTransparency() const {
    return m_state.blending;
}

const std::vector<VkDescriptorSetLayout>& VulkanPipeline::getDescriptorSetLayouts() const {
    return m_state.descriptorSetLayouts;
}

void VulkanPipeline::createPipelineLayout() {
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(m_state.descriptorSetLayouts.size());
    pipelineLayoutInfo.pSetLayouts = m_state.descriptorSetLayouts.data();
    pipelineLayoutInfo.pushConstantRangeCount = static_cast<uint32_t>(m_state.pushConstantRanges.size());
    pipelineLayoutInfo.pPushConstantRanges = m_state.pushConstantRanges.data();

    if (vkCreatePipelineLayout(m_device.getLogicalDevice(), &pipelineLayoutInfo, nullptr, &m_pipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create pipeline layout!");
    }
}

void VulkanPipeline::loadShaders() {
    for (const auto& [stage, path] : m_state.shaderPaths) {
        auto code = readShaderFile(path);
        m_shaderModules[stage] = createShaderModule(code);
    }
}

void VulkanPipeline::createPipeline() {
    // Build shader stage create infos
    std::vector<VkPipelineShaderStageCreateInfo> shaderStages;
    for (const auto& [stage, module] : m_shaderModules) {
        VkPipelineShaderStageCreateInfo stageInfo{};
        stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stageInfo.stage = stage;
        stageInfo.module = module;
        stageInfo.pName = "main";
        shaderStages.push_back(stageInfo);
    }

    // Vertex input state
    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = static_cast<uint32_t>(m_state.vertexBindings.size());
    vertexInputInfo.pVertexBindingDescriptions = m_state.vertexBindings.data();
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(m_state.vertexAttributes.size());
    vertexInputInfo.pVertexAttributeDescriptions = m_state.vertexAttributes.data();

    // Input assembly state
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = m_state.topology;
    inputAssembly.primitiveRestartEnable = m_state.primitiveRestart ? VK_TRUE : VK_FALSE;

    // Viewport state
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(m_extent.width);
    viewport.height = static_cast<float>(m_extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = m_extent;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;

    // Rasterization state
    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = m_state.depthClamp ? VK_TRUE : VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = m_state.wireframe ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = m_state.lineWidth;
    rasterizer.cullMode = m_state.cullMode;
    rasterizer.frontFace = m_state.frontFace;
    rasterizer.depthBiasEnable = VK_FALSE;

    // Multisampling state
    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = m_state.sampleShading ? VK_TRUE : VK_FALSE;
    multisampling.rasterizationSamples = m_state.sampleCount;
    multisampling.minSampleShading = m_state.minSampleShading;

    // Depth stencil state
    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = m_state.depthTest ? VK_TRUE : VK_FALSE;
    depthStencil.depthWriteEnable = m_state.depthWrite ? VK_TRUE : VK_FALSE;
    depthStencil.depthCompareOp = m_state.depthCompareOp;
    depthStencil.depthBoundsTestEnable = m_state.depthBounds ? VK_TRUE : VK_FALSE;
    depthStencil.minDepthBounds = m_state.minDepthBounds;
    depthStencil.maxDepthBounds = m_state.maxDepthBounds;
    depthStencil.stencilTestEnable = m_state.stencilTest ? VK_TRUE : VK_FALSE;

    // Color blend state — replicate for all color attachments (MRT support)
    uint32_t colorCount = m_state.colorAttachmentCount > 0 ? m_state.colorAttachmentCount : 1;

    VkPipelineColorBlendAttachmentState colorBlendTemplate{};
    colorBlendTemplate.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendTemplate.blendEnable = m_state.blending ? VK_TRUE : VK_FALSE;
    colorBlendTemplate.srcColorBlendFactor = m_state.srcColorBlendFactor;
    colorBlendTemplate.dstColorBlendFactor = m_state.dstColorBlendFactor;
    colorBlendTemplate.colorBlendOp = m_state.colorBlendOp;
    colorBlendTemplate.srcAlphaBlendFactor = m_state.srcAlphaBlendFactor;
    colorBlendTemplate.dstAlphaBlendFactor = m_state.dstAlphaBlendFactor;
    colorBlendTemplate.alphaBlendOp = m_state.alphaBlendOp;

    std::vector<VkPipelineColorBlendAttachmentState> colorBlendAttachments(colorCount, colorBlendTemplate);

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = colorCount;
    colorBlending.pAttachments = colorBlendAttachments.data();

    // Dynamic state
    VkPipelineDynamicStateCreateInfo dynamicState{};
    if (!m_state.dynamicStates.empty()) {
        dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicState.dynamicStateCount = static_cast<uint32_t>(m_state.dynamicStates.size());
        dynamicState.pDynamicStates = m_state.dynamicStates.data();
    }

    // Create graphics pipeline
    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
    pipelineInfo.pStages = shaderStages.data();
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = m_state.dynamicStates.empty() ? nullptr : &dynamicState;
    pipelineInfo.layout = m_pipelineLayout;
    pipelineInfo.renderPass = m_renderPass.getHandle();
    pipelineInfo.subpass = 0;

    if (vkCreateGraphicsPipelines(m_device.getLogicalDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_pipeline) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create graphics pipeline!");
    }
}

void VulkanPipeline::cleanup() {
    if (m_pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(m_device.getLogicalDevice(), m_pipeline, nullptr);
        m_pipeline = VK_NULL_HANDLE;
    }

    if (m_pipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(m_device.getLogicalDevice(), m_pipelineLayout, nullptr);
        m_pipelineLayout = VK_NULL_HANDLE;
    }

    for (auto& [stage, module] : m_shaderModules) {
        vkDestroyShaderModule(m_device.getLogicalDevice(), module, nullptr);
    }
    m_shaderModules.clear();
}

VkShaderModule VulkanPipeline::createShaderModule(const std::vector<char>& code) {
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

    VkShaderModule shaderModule;
    if (vkCreateShaderModule(m_device.getLogicalDevice(), &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create shader module!");
    }

    return shaderModule;
}

std::vector<char> VulkanPipeline::readShaderFile(const std::string& filename) {
    std::ifstream file(filename, std::ios::ate | std::ios::binary);

    if (!file.is_open()) {
        throw std::runtime_error("Failed to open shader file: " + filename);
    }

    size_t fileSize = static_cast<size_t>(file.tellg());
    std::vector<char> buffer(fileSize);

    file.seekg(0);
    file.read(buffer.data(), fileSize);
    file.close();

    return buffer;
}

} // namespace Shoonyakasha
