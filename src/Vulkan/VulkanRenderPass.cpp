// VulkanRenderPass.cpp - Implementation of the render pass system

#include "../../include/Vulkan/VulkanRenderPass.h"
#include "../../include/Vulkan/VulkanDevice.h"
#include <stdexcept>
#include <algorithm>
#include <set>

namespace Shoonyakasha {

// ═══════════════════════════════════════════════════════════════
// AttachmentDescriptor Factory Methods
// ═══════════════════════════════════════════════════════════════

AttachmentDescriptor AttachmentDescriptor::createColorAttachment(
    const std::string& name,
    VkFormat format,
    const std::array<float, 4>& clearColor) {

    AttachmentDescriptor attachment;
    attachment.name = name;
    attachment.format = format;
    attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    attachment.clearValue.color.float32[0] = clearColor[0];
    attachment.clearValue.color.float32[1] = clearColor[1];
    attachment.clearValue.color.float32[2] = clearColor[2];
    attachment.clearValue.color.float32[3] = clearColor[3];

    return attachment;
}

AttachmentDescriptor AttachmentDescriptor::createDepthAttachment(
    const std::string& name,
    VkFormat format,
    float clearDepth,
    uint32_t clearStencil) {

    AttachmentDescriptor attachment;
    attachment.name = name;
    attachment.format = format; // Will be auto-selected if VK_FORMAT_UNDEFINED
    attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    attachment.clearValue.depthStencil.depth = clearDepth;
    attachment.clearValue.depthStencil.stencil = clearStencil;

    return attachment;
}

AttachmentDescriptor AttachmentDescriptor::createResolveAttachment(
    const std::string& name,
    VkFormat format) {

    AttachmentDescriptor attachment;
    attachment.name = name;
    attachment.format = format;
    attachment.samples = VK_SAMPLE_COUNT_1_BIT; // Resolve targets are always single-sampled
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    return attachment;
}

AttachmentDescriptor AttachmentDescriptor::createInputAttachment(
    const std::string& name,
    VkFormat format) {

    AttachmentDescriptor attachment;
    attachment.name = name;
    attachment.format = format;
    attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD; // Must preserve previous content
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment.initialLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    attachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    return attachment;
}

// ═══════════════════════════════════════════════════════════════
// SubpassDescriptor Factory Methods
// ═══════════════════════════════════════════════════════════════

SubpassDescriptor SubpassDescriptor::createBasicGraphics(
    const std::string& name,
    const std::vector<std::string>& colorTargets,
    const std::string& depthTarget) {

    SubpassDescriptor subpass;
    subpass.name = name;
    subpass.bindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachments = colorTargets;
    subpass.depthStencilAttachment = depthTarget;

    return subpass;
}

SubpassDescriptor SubpassDescriptor::createMultisampled(
    const std::string& name,
    const std::vector<std::string>& colorTargets,
    const std::vector<std::string>& resolveTargets,
    const std::string& depthTarget) {

    SubpassDescriptor subpass;
    subpass.name = name;
    subpass.bindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachments = colorTargets;
    subpass.resolveAttachments = resolveTargets;
    subpass.depthStencilAttachment = depthTarget;

    return subpass;
}

SubpassDescriptor SubpassDescriptor::createDeferred(
    const std::string& name,
    const std::vector<std::string>& gBufferTargets,
    const std::vector<std::string>& inputTargets,
    const std::string& depthTarget) {

    SubpassDescriptor subpass;
    subpass.name = name;
    subpass.bindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachments = gBufferTargets;
    subpass.inputAttachments = inputTargets;
    subpass.depthStencilAttachment = depthTarget;

    return subpass;
}

// ═══════════════════════════════════════════════════════════════
// SubpassDependency Factory Methods
// ═══════════════════════════════════════════════════════════════

SubpassDependency SubpassDependency::createColorDependency(
    const std::string& src, const std::string& dst) {

    SubpassDependency dependency;
    dependency.srcSubpass = src;
    dependency.dstSubpass = dst;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    return dependency;
}

SubpassDependency SubpassDependency::createDepthDependency(
    const std::string& src, const std::string& dst) {

    SubpassDependency dependency;
    dependency.srcSubpass = src;
    dependency.dstSubpass = dst;
    dependency.srcStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dependency.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependency.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    return dependency;
}

SubpassDependency SubpassDependency::createExternalDependency(const std::string& dstSubpass) {
    SubpassDependency dependency;
    dependency.srcSubpass = "VK_SUBPASS_EXTERNAL";
    dependency.dstSubpass = dstSubpass;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    return dependency;
}

// ═══════════════════════════════════════════════════════════════
// RenderPassBuilder Implementation - Composing like poetry
// ═══════════════════════════════════════════════════════════════

RenderPassBuilder::RenderPassBuilder() {
    // Empty - ready to be filled with rendering intent
}

RenderPassBuilder& RenderPassBuilder::addAttachment(const AttachmentDescriptor& attachment) {
    m_config.attachments.push_back(attachment);
    return *this;
}

RenderPassBuilder& RenderPassBuilder::addColorAttachment(const std::string& name, VkFormat format,
                                                         const std::array<float, 4>& clearColor) {
    return addAttachment(AttachmentDescriptor::createColorAttachment(name, format, clearColor));
}

RenderPassBuilder& RenderPassBuilder::addDepthAttachment(const std::string& name, VkFormat format) {
    return addAttachment(AttachmentDescriptor::createDepthAttachment(name, format));
}

RenderPassBuilder& RenderPassBuilder::addSubpass(const SubpassDescriptor& subpass) {
    m_config.subpasses.push_back(subpass);
    return *this;
}

RenderPassBuilder& RenderPassBuilder::addBasicSubpass(const std::string& name,
                                                      const std::vector<std::string>& colorTargets,
                                                      const std::string& depthTarget) {
    return addSubpass(SubpassDescriptor::createBasicGraphics(name, colorTargets, depthTarget));
}

RenderPassBuilder& RenderPassBuilder::addDependency(const SubpassDependency& dependency) {
    m_config.dependencies.push_back(dependency);
    return *this;
}

RenderPassBuilder& RenderPassBuilder::addExternalDependency(const std::string& subpass) {
    return addDependency(SubpassDependency::createExternalDependency(subpass));
}

RenderPassBuilder& RenderPassBuilder::configureForward(VkFormat colorFormat, VkFormat depthFormat) {
    addColorAttachment("color", colorFormat);
    addDepthAttachment("depth", depthFormat);
    addBasicSubpass("main", {"color"}, "depth");
    addExternalDependency("main");
    return *this;
}

RenderPassBuilder& RenderPassBuilder::configureDeferred(VkFormat colorFormat, VkFormat depthFormat,
                                                        const std::vector<VkFormat>& gBufferFormats) {
    // G-Buffer attachments
    for (size_t i = 0; i < gBufferFormats.size(); ++i) {
        addColorAttachment("gbuffer" + std::to_string(i), gBufferFormats[i]);
    }
    addDepthAttachment("depth", depthFormat);

    // G-Buffer generation pass
    std::vector<std::string> gBufferNames;
    for (size_t i = 0; i < gBufferFormats.size(); ++i) {
        gBufferNames.push_back("gbuffer" + std::to_string(i));
    }
    addBasicSubpass("geometry", gBufferNames, "depth");

    // Lighting pass
    addColorAttachment("color", colorFormat);
    addSubpass(SubpassDescriptor::createDeferred("lighting", {"color"}, gBufferNames, ""));

    // Dependencies
    addExternalDependency("geometry");
    addDependency(SubpassDependency::createColorDependency("geometry", "lighting"));

    return *this;
}

RenderPassBuilder& RenderPassBuilder::configureShadowMapping(VkFormat depthFormat) {
    addDepthAttachment("shadow", depthFormat);
    addBasicSubpass("shadowpass", {}, "shadow");
    addExternalDependency("shadowpass");
    return *this;
}

RenderPassBuilder& RenderPassBuilder::configurePostProcess(VkFormat inputFormat, VkFormat outputFormat) {
    addAttachment(AttachmentDescriptor::createInputAttachment("input", inputFormat));
    addColorAttachment("output", outputFormat);
    addSubpass(SubpassDescriptor::createBasicGraphics("postprocess", {"output"}, ""));
    addExternalDependency("postprocess");
    return *this;
}

RenderPassBuilder::RenderPassConfiguration RenderPassBuilder::build() {
    validateConfiguration();
    buildIndices();
    return m_config;
}

void RenderPassBuilder::validateConfiguration() {
    if (m_config.attachments.empty()) {
        throw std::runtime_error("RenderPass must have at least one attachment");
    }

    if (m_config.subpasses.empty()) {
        throw std::runtime_error("RenderPass must have at least one subpass");
    }

    // Validate attachment references in subpasses
    std::set<std::string> attachmentNames;
    for (const auto& attachment : m_config.attachments) {
        attachmentNames.insert(attachment.name);
    }

    for (const auto& subpass : m_config.subpasses) {
        auto validateAttachmentList = [&](const std::vector<std::string>& attachments, const std::string& type) {
            for (const auto& attachmentName : attachments) {
                if (attachmentNames.find(attachmentName) == attachmentNames.end()) {
                    throw std::runtime_error("Subpass '" + subpass.name + "' references unknown " + type + " attachment: " + attachmentName);
                }
            }
        };

        validateAttachmentList(subpass.colorAttachments, "color");
        validateAttachmentList(subpass.inputAttachments, "input");
        validateAttachmentList(subpass.resolveAttachments, "resolve");
        validateAttachmentList(subpass.preserveAttachments, "preserve");

        if (!subpass.depthStencilAttachment.empty() &&
            attachmentNames.find(subpass.depthStencilAttachment) == attachmentNames.end()) {
            throw std::runtime_error("Subpass '" + subpass.name + "' references unknown depth attachment: " + subpass.depthStencilAttachment);
        }
    }
}

void RenderPassBuilder::buildIndices() {
    // Build attachment indices
    for (size_t i = 0; i < m_config.attachments.size(); ++i) {
        m_config.attachmentIndices[m_config.attachments[i].name] = static_cast<uint32_t>(i);
    }

    // Build subpass indices
    for (size_t i = 0; i < m_config.subpasses.size(); ++i) {
        m_config.subpassIndices[m_config.subpasses[i].name] = static_cast<uint32_t>(i);
    }
}

// ═══════════════════════════════════════════════════════════════
// VulkanRenderPass Implementation - Where dreams become reality
// ═══════════════════════════════════════════════════════════════

VulkanRenderPass::VulkanRenderPass(VulkanDevice& device, const RenderPassBuilder::RenderPassConfiguration& config)
    : m_device(device), m_config(config) {

    // Auto-select depth format if needed
    for (auto& attachment : m_config.attachments) {
        if (attachment.format == VK_FORMAT_UNDEFINED && attachment.name.find("depth") != std::string::npos) {
            attachment.format = m_device.findDepthFormat();
        }
    }

    createRenderPass();
}

std::unique_ptr<VulkanRenderPass> VulkanRenderPass::createForward(
    VulkanDevice& device, VkFormat colorFormat, VkFormat depthFormat) {

    auto config = RenderPassBuilder()
        .configureForward(colorFormat, depthFormat)
        .build();

    return std::make_unique<VulkanRenderPass>(device, config);
}

std::unique_ptr<VulkanRenderPass> VulkanRenderPass::createDeferred(
    VulkanDevice& device, VkFormat colorFormat, VkFormat depthFormat,
    const std::vector<VkFormat>& gBufferFormats) {

    auto config = RenderPassBuilder()
        .configureDeferred(colorFormat, depthFormat, gBufferFormats)
        .build();

    return std::make_unique<VulkanRenderPass>(device, config);
}

std::unique_ptr<VulkanRenderPass> VulkanRenderPass::createShadowMap(
    VulkanDevice& device, VkFormat depthFormat) {

    auto config = RenderPassBuilder()
        .configureShadowMapping(depthFormat)
        .build();

    return std::make_unique<VulkanRenderPass>(device, config);
}

std::unique_ptr<VulkanRenderPass> VulkanRenderPass::createPostProcess(
    VulkanDevice& device, VkFormat inputFormat, VkFormat outputFormat) {

    auto config = RenderPassBuilder()
        .configurePostProcess(inputFormat, outputFormat)
        .build();

    return std::make_unique<VulkanRenderPass>(device, config);
}

VulkanRenderPass::~VulkanRenderPass() {
    cleanup();
}

void VulkanRenderPass::begin(VkCommandBuffer commandBuffer, VkFramebuffer framebuffer,
                            VkExtent2D extent, uint32_t subpass) {
    begin(commandBuffer, framebuffer, extent, getDefaultClearValues(), subpass);
}

void VulkanRenderPass::begin(VkCommandBuffer commandBuffer, VkFramebuffer framebuffer,
                            VkExtent2D extent, const std::vector<VkClearValue>& clearValues,
                            uint32_t subpass) {
    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = m_renderPass;
    renderPassInfo.framebuffer = framebuffer;
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = extent;
    renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
    renderPassInfo.pClearValues = clearValues.data();

    vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
}

void VulkanRenderPass::nextSubpass(VkCommandBuffer commandBuffer, VkSubpassContents contents) {
    vkCmdNextSubpass(commandBuffer, contents);
}

void VulkanRenderPass::end(VkCommandBuffer commandBuffer) {
    vkCmdEndRenderPass(commandBuffer);
}

VkFramebuffer VulkanRenderPass::createFramebuffer(VkExtent2D extent, const std::vector<VkImageView>& attachments) {
    VkFramebufferCreateInfo framebufferInfo{};
    framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebufferInfo.renderPass = m_renderPass;
    framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    framebufferInfo.pAttachments = attachments.data();
    framebufferInfo.width = extent.width;
    framebufferInfo.height = extent.height;
    framebufferInfo.layers = 1;

    VkFramebuffer framebuffer;
    if (vkCreateFramebuffer(m_device.getLogicalDevice(), &framebufferInfo, nullptr, &framebuffer) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create framebuffer!");
    }

    return framebuffer;
}

VkFramebuffer VulkanRenderPass::createFramebuffer(VkExtent2D extent,
                                                 const std::unordered_map<std::string, VkImageView>& namedAttachments) {
    std::vector<VkImageView> attachments;
    attachments.reserve(m_config.attachments.size());

    for (const auto& attachment : m_config.attachments) {
        auto it = namedAttachments.find(attachment.name);
        if (it == namedAttachments.end()) {
            throw std::runtime_error("Missing attachment: " + attachment.name);
        }
        attachments.push_back(it->second);
    }

    return createFramebuffer(extent, attachments);
}

uint32_t VulkanRenderPass::getAttachmentIndex(const std::string& name) const {
    auto it = m_config.attachmentIndices.find(name);
    if (it == m_config.attachmentIndices.end()) {
        throw std::runtime_error("Unknown attachment: " + name);
    }
    return it->second;
}

uint32_t VulkanRenderPass::getSubpassIndex(const std::string& name) const {
    auto it = m_config.subpassIndices.find(name);
    if (it == m_config.subpassIndices.end()) {
        throw std::runtime_error("Unknown subpass: " + name);
    }
    return it->second;
}

const AttachmentDescriptor& VulkanRenderPass::getAttachment(const std::string& name) const {
    uint32_t index = getAttachmentIndex(name);
    return m_config.attachments[index];
}

const SubpassDescriptor& VulkanRenderPass::getSubpass(const std::string& name) const {
    uint32_t index = getSubpassIndex(name);
    return m_config.subpasses[index];
}

std::vector<VkClearValue> VulkanRenderPass::getDefaultClearValues() const {
    std::vector<VkClearValue> clearValues;
    clearValues.reserve(m_config.attachments.size());

    for (const auto& attachment : m_config.attachments) {
        clearValues.push_back(attachment.clearValue);
    }

    return clearValues;
}

std::vector<VkFormat> VulkanRenderPass::getAttachmentFormats() const {
    std::vector<VkFormat> formats;
    formats.reserve(m_config.attachments.size());

    for (const auto& attachment : m_config.attachments) {
        formats.push_back(attachment.format);
    }

    return formats;
}

void VulkanRenderPass::updateClearValue(const std::string& attachmentName, const VkClearValue& clearValue) {
    uint32_t index = getAttachmentIndex(attachmentName);
    m_config.attachments[index].clearValue = clearValue;
}

void VulkanRenderPass::createRenderPass() {
    auto attachmentDescriptions = buildAttachmentDescriptions();

    // Storage for attachment references (needed for lifetime)
    std::vector<std::vector<VkAttachmentReference>> colorRefs;
    std::vector<std::vector<VkAttachmentReference>> inputRefs;
    std::vector<std::vector<VkAttachmentReference>> resolveRefs;
    std::vector<VkAttachmentReference> depthRefs;

    auto subpassDescriptions = buildSubpassDescriptions(colorRefs, inputRefs, resolveRefs, depthRefs);
    auto subpassDependencies = buildSubpassDependencies();

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = static_cast<uint32_t>(attachmentDescriptions.size());
    renderPassInfo.pAttachments = attachmentDescriptions.data();
    renderPassInfo.subpassCount = static_cast<uint32_t>(subpassDescriptions.size());
    renderPassInfo.pSubpasses = subpassDescriptions.data();
    renderPassInfo.dependencyCount = static_cast<uint32_t>(subpassDependencies.size());
    renderPassInfo.pDependencies = subpassDependencies.data();

    if (vkCreateRenderPass(m_device.getLogicalDevice(), &renderPassInfo, nullptr, &m_renderPass) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create render pass!");
    }
}

std::vector<VkAttachmentDescription> VulkanRenderPass::buildAttachmentDescriptions() const {
    std::vector<VkAttachmentDescription> descriptions;
    descriptions.reserve(m_config.attachments.size());

    for (const auto& attachment : m_config.attachments) {
        VkAttachmentDescription desc{};
        desc.format = attachment.format;
        desc.samples = attachment.samples;
        desc.loadOp = attachment.loadOp;
        desc.storeOp = attachment.storeOp;
        desc.stencilLoadOp = attachment.stencilLoadOp;
        desc.stencilStoreOp = attachment.stencilStoreOp;
        desc.initialLayout = attachment.initialLayout;
        desc.finalLayout = attachment.finalLayout;
        descriptions.push_back(desc);
    }

    return descriptions;
}

std::vector<VkSubpassDescription> VulkanRenderPass::buildSubpassDescriptions(
    std::vector<std::vector<VkAttachmentReference>>& colorRefs,
    std::vector<std::vector<VkAttachmentReference>>& inputRefs,
    std::vector<std::vector<VkAttachmentReference>>& resolveRefs,
    std::vector<VkAttachmentReference>& depthRefs) const {

    std::vector<VkSubpassDescription> descriptions;
    descriptions.reserve(m_config.subpasses.size());

    colorRefs.resize(m_config.subpasses.size());
    inputRefs.resize(m_config.subpasses.size());
    resolveRefs.resize(m_config.subpasses.size());
    depthRefs.resize(m_config.subpasses.size());

    for (size_t i = 0; i < m_config.subpasses.size(); ++i) {
        const auto& subpass = m_config.subpasses[i];

        // Color attachments
        for (const auto& colorName : subpass.colorAttachments) {
            VkAttachmentReference ref{};
            ref.attachment = getAttachmentIndex(colorName);
            ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            colorRefs[i].push_back(ref);
        }

        // Input attachments
        for (const auto& inputName : subpass.inputAttachments) {
            VkAttachmentReference ref{};
            ref.attachment = getAttachmentIndex(inputName);
            ref.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            inputRefs[i].push_back(ref);
        }

        // Resolve attachments
        for (const auto& resolveName : subpass.resolveAttachments) {
            VkAttachmentReference ref{};
            ref.attachment = getAttachmentIndex(resolveName);
            ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            resolveRefs[i].push_back(ref);
        }

        // Depth attachment
        bool hasDepth = !subpass.depthStencilAttachment.empty();
        if (hasDepth) {
            depthRefs[i].attachment = getAttachmentIndex(subpass.depthStencilAttachment);
            depthRefs[i].layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        }

        VkSubpassDescription desc{};
        desc.pipelineBindPoint = subpass.bindPoint;
        desc.colorAttachmentCount = static_cast<uint32_t>(colorRefs[i].size());
        desc.pColorAttachments = colorRefs[i].empty() ? nullptr : colorRefs[i].data();
        desc.pResolveAttachments = resolveRefs[i].empty() ? nullptr : resolveRefs[i].data();
        desc.inputAttachmentCount = static_cast<uint32_t>(inputRefs[i].size());
        desc.pInputAttachments = inputRefs[i].empty() ? nullptr : inputRefs[i].data();
        desc.pDepthStencilAttachment = hasDepth ? &depthRefs[i] : nullptr;
        descriptions.push_back(desc);
    }

    return descriptions;
}

std::vector<VkSubpassDependency> VulkanRenderPass::buildSubpassDependencies() const {
    std::vector<VkSubpassDependency> dependencies;
    dependencies.reserve(m_config.dependencies.size());

    for (const auto& dep : m_config.dependencies) {
        VkSubpassDependency vkDep{};

        // Handle external subpass
        if (dep.srcSubpass == "VK_SUBPASS_EXTERNAL") {
            vkDep.srcSubpass = VK_SUBPASS_EXTERNAL;
        } else {
            vkDep.srcSubpass = getSubpassIndex(dep.srcSubpass);
        }

        if (dep.dstSubpass == "VK_SUBPASS_EXTERNAL") {
            vkDep.dstSubpass = VK_SUBPASS_EXTERNAL;
        } else {
            vkDep.dstSubpass = getSubpassIndex(dep.dstSubpass);
        }

        vkDep.srcStageMask = dep.srcStageMask;
        vkDep.dstStageMask = dep.dstStageMask;
        vkDep.srcAccessMask = dep.srcAccessMask;
        vkDep.dstAccessMask = dep.dstAccessMask;
        vkDep.dependencyFlags = dep.dependencyFlags;

        dependencies.push_back(vkDep);
    }

    return dependencies;
}

void VulkanRenderPass::cleanup() {
    if (m_renderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(m_device.getLogicalDevice(), m_renderPass, nullptr);
        m_renderPass = VK_NULL_HANDLE;
    }
}

} // namespace Shoonyakasha
