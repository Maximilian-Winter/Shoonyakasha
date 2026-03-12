//
// Shoonyakasha Engine - Frame Graph Executor
//
// 朱雀司變  熾熱而速達
// The Vermilion Bird governs transformation — blazing heat, swift arrival
//

#include "Vulkan/FrameGraph/FrameGraph.h"
#include "Vulkan/FrameGraph/FrameGraphDebugger.h"
#include "Vulkan/VulkanCommandBuffer.h"
#include "Vulkan/VulkanRenderPass.h"
#include "Vulkan/VulkanPipeline.h"
#include "Vulkan/VulkanComputePipeline.h"
#include "Vulkan/VulkanDescriptorSystem.h"
#include "Vulkan/VulkanImage.h"
#include "Vulkan/VulkanDevice.h"
#include "Core/Logger.h"

#include <stdexcept>
#include <cassert>

#include "Vulkan/FrameGraph/FrameGraphJson.h"

namespace Shoonyakasha {
namespace FrameGraph {

// ═══════════════════════════════════════════════════════════════
// PassExecuteContext — resource accessor implementations
// ═══════════════════════════════════════════════════════════════

// Resource accessor implementations — cast from opaque pointer to typed vector
static const std::vector<PhysicalResource>& getPhysResources(const PassExecuteContext& ctx) {
    return *static_cast<const std::vector<PhysicalResource>*>(ctx.physicalResourcesPtr);
}

VkImageView PassExecuteContext::getImageView(ResourceHandle h) const {
    if (!h.valid() || !physicalResourcesPtr) return VK_NULL_HANDLE;
    const auto& resources = getPhysResources(*this);
    if (h.index >= resources.size()) return VK_NULL_HANDLE;

    if (const auto* img = std::get_if<PhysicalImage>(&resources[h.index])) {
        return img->view;
    }
    return VK_NULL_HANDLE;
}

VkImage PassExecuteContext::getImage(ResourceHandle h) const {
    if (!h.valid() || !physicalResourcesPtr) return VK_NULL_HANDLE;
    const auto& resources = getPhysResources(*this);
    if (h.index >= resources.size()) return VK_NULL_HANDLE;

    if (const auto* img = std::get_if<PhysicalImage>(&resources[h.index])) {
        return img->vkImage;
    }
    return VK_NULL_HANDLE;
}

VkBuffer PassExecuteContext::getBuffer(ResourceHandle h) const {
    if (!h.valid() || !physicalResourcesPtr) return VK_NULL_HANDLE;
    const auto& resources = getPhysResources(*this);
    if (h.index >= resources.size()) return VK_NULL_HANDLE;

    if (const auto* buf = std::get_if<PhysicalBuffer>(&resources[h.index])) {
        return buf->vkBuffer;
    }
    return VK_NULL_HANDLE;
}

std::shared_ptr<VulkanDescriptorSet> PassExecuteContext::getDescriptorSet(uint32_t setIndex) const {
    if (!descriptorSets || setIndex >= descriptorSets->size()) return nullptr;
    return (*descriptorSets)[setIndex];
}

// ═══════════════════════════════════════════════════════════════
// Frame Graph Executor
// ═══════════════════════════════════════════════════════════════

FrameGraphExecutor::FrameGraphExecutor(VulkanDevice& device, VulkanCommandManager& cmdManager)
    : m_device(device)
    , m_cmdManager(cmdManager)
{
    m_logger = new Logger("framegraph_executor.log");
}

FrameGraphExecutor::~FrameGraphExecutor() {
    delete m_logger;
}

void FrameGraphExecutor::execute(
    const FrameGraphCompiler::CompileResult& compiled,
    const FrameGraphBuilder& builder,
    uint32_t frameIndex,
    uint32_t swapchainImageIndex,
    VkCommandBuffer commandBuffer,
    const std::unordered_map<std::string, ParameterValue>* parameters)
{
    if (!compiled.valid) {
        m_logger->log(LogLevel::Error, "Cannot execute invalid frame graph");
        return;
    }

    // Notify debugger of frame begin
    if (m_debugger) {
        m_debugger->onFrameBegin(frameIndex);
    }

    const auto& passes = builder.getPassDeclarations();
    const auto& resources = builder.getResourceDeclarations();
    VulkanCommandBuilder cmd(m_device, commandBuffer);

    // Track previous pass type for compute→graphics buffer barriers
    PassType previousPassType = PassType::Graphics;

    uint32_t execIdx = 0;
    for (uint32_t passIdx : compiled.executionOrder) {
        const auto& compiledPass = compiled.compiledPasses[passIdx];
        const auto& passDecl = passes[compiledPass.declIndex];

        // Skip disabled passes
        if (!passDecl.enabled) {
            execIdx++;
            continue;
        }

        // Log execution order for debugging pass scheduling (throttled to every 5s)
        m_logger->logEvery(5.0f, LogLevel::Info, "  [pass %u/%u] '%s' (type=%s, exec=%s)",
            execIdx++, static_cast<uint32_t>(compiled.executionOrder.size()),
            passDecl.name.c_str(),
            passDecl.type == PassType::Compute ? "compute" : "graphics",
            passDecl.execution.type.c_str());

        // Debug label for RenderDoc / validation layers
        cmd.beginDebugLabel(passDecl.name, {0.3f, 0.7f, 0.9f, 1.0f});

        // Notify debugger of pass begin
        if (m_debugger) {
            m_debugger->onPassBegin(passIdx, passDecl.name, commandBuffer);
        }

        // ── Insert compute→graphics memory barrier for SSBO synchronization ──
        if (passDecl.type != PassType::Compute && previousPassType == PassType::Compute) {
            VkMemoryBarrier memBarrier{};
            memBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
            memBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            memBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;

            vkCmdPipelineBarrier(commandBuffer,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                VK_PIPELINE_STAGE_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
                0,
                1, &memBarrier,
                0, nullptr,
                0, nullptr);

            m_logger->logEvery(5.0f, LogLevel::Info,
                "  Inserted compute->graphics memory barrier before pass '%s'",
                passDecl.name.c_str());
        }

        // ── Insert acquire barriers (queue ownership transfers) ──
        for (const auto& barrier : compiledPass.acquireBarriers) {
            if (!barrier.resource.valid()) continue;
            const auto* physImg = std::get_if<PhysicalImage>(
                &compiled.physicalResources[barrier.resource.index]);
            if (physImg && physImg->vkImage != VK_NULL_HANDLE) {
                VkImageMemoryBarrier imgBarrier{};
                imgBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                imgBarrier.oldLayout = barrier.oldLayout;
                imgBarrier.newLayout = barrier.newLayout;
                imgBarrier.srcQueueFamilyIndex = barrier.srcQueueFamilyIndex;
                imgBarrier.dstQueueFamilyIndex = barrier.dstQueueFamilyIndex;
                imgBarrier.image = physImg->vkImage;
                imgBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                imgBarrier.subresourceRange.baseMipLevel = 0;
                imgBarrier.subresourceRange.levelCount = 1;
                imgBarrier.subresourceRange.baseArrayLayer = 0;
                imgBarrier.subresourceRange.layerCount = 1;
                imgBarrier.srcAccessMask = barrier.srcAccess;
                imgBarrier.dstAccessMask = barrier.dstAccess;

                vkCmdPipelineBarrier(commandBuffer,
                    barrier.srcStage, barrier.dstStage,
                    0, 0, nullptr, 0, nullptr,
                    1, &imgBarrier);
            }
        }

        // ── Insert pre-barriers ──
        for (const auto& barrier : compiledPass.preBarriers) {
            if (!barrier.resource.valid()) continue;

            const auto* physImg = std::get_if<PhysicalImage>(
                &compiled.physicalResources[barrier.resource.index]);

            if (physImg && physImg->vkImage != VK_NULL_HANDLE) {
                // Notify debugger of barrier
                if (m_debugger) {
                    std::string resourceName = (barrier.resource.index < resources.size())
                        ? resources[barrier.resource.index].name : "unknown";
                    bool isQueueTransfer = barrier.srcQueueFamilyIndex != VK_QUEUE_FAMILY_IGNORED;
                    m_debugger->onBarrierInserted(resourceName, barrier.oldLayout,
                                                  barrier.newLayout, isQueueTransfer);
                }

                if (barrier.srcQueueFamilyIndex != VK_QUEUE_FAMILY_IGNORED) {
                    // Queue ownership transfer: use full barrier with queue family indices
                    VkImageMemoryBarrier imgBarrier{};
                    imgBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                    imgBarrier.oldLayout = barrier.oldLayout;
                    imgBarrier.newLayout = barrier.newLayout;
                    imgBarrier.srcQueueFamilyIndex = barrier.srcQueueFamilyIndex;
                    imgBarrier.dstQueueFamilyIndex = barrier.dstQueueFamilyIndex;
                    imgBarrier.image = physImg->vkImage;
                    imgBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                    imgBarrier.subresourceRange.baseMipLevel = 0;
                    imgBarrier.subresourceRange.levelCount = 1;
                    imgBarrier.subresourceRange.baseArrayLayer = 0;
                    imgBarrier.subresourceRange.layerCount = 1;
                    imgBarrier.srcAccessMask = barrier.srcAccess;
                    imgBarrier.dstAccessMask = barrier.dstAccess;

                    vkCmdPipelineBarrier(commandBuffer,
                        barrier.srcStage, barrier.dstStage,
                        0, 0, nullptr, 0, nullptr,
                        1, &imgBarrier);
                } else {
                    cmd.imageBarrier(
                        physImg->vkImage,
                        barrier.oldLayout,
                        barrier.newLayout,
                        barrier.srcStage,
                        barrier.dstStage
                    );
                }
            }
        }

        // ── Build execution context ──
        PassExecuteContext ctx(cmd);
        ctx.frameIndex = frameIndex;
        ctx.swapchainIndex = swapchainImageIndex;
        ctx.renderExtent = compiledPass.extent;
        ctx.physicalResourcesPtr = &compiled.physicalResources;
        ctx.physicalResourceCount = static_cast<uint32_t>(compiled.physicalResources.size());

        // Populate descriptor set and pipeline context
        if (!compiledPass.descriptorSets.empty()) {
            ctx.descriptorSets = &compiledPass.descriptorSets;
        }
        if (compiledPass.pipeline) {
            ctx.pipeline = compiledPass.pipeline.get();
            ctx.pipelineLayout = compiledPass.pipelineLayout;
        }
        if (compiledPass.computePipeline) {
            ctx.computePipeline = compiledPass.computePipeline.get();
            ctx.pipelineLayout = compiledPass.pipelineLayout;
        }

        // ── Begin render pass for graphics passes ──
        if (passDecl.type == PassType::Graphics && compiledPass.renderPass) {
            // Select framebuffer for this swapchain image
            VkFramebuffer fb = VK_NULL_HANDLE;
            if (!compiledPass.framebuffers.empty()) {
                uint32_t fbIdx = (compiledPass.framebuffers.size() > 1)
                    ? swapchainImageIndex
                    : 0;
                fb = compiledPass.framebuffers[fbIdx];
            }

            if (fb != VK_NULL_HANDLE) {
                RenderPassContext rpCtx(compiledPass.renderPass, fb, compiledPass.extent);
                rpCtx.withClearValues(compiledPass.clearValues);
                cmd.beginRenderPass(rpCtx);
            }
        }

        // ── Auto-bind pipeline if available ──
        if (passDecl.type == PassType::Graphics && compiledPass.pipeline) {
            cmd.bindPipeline(compiledPass.pipeline.get())
               .setViewport(ViewportState::fromExtent(compiledPass.extent))
               .setScissor(ScissorState::fromExtent(compiledPass.extent));
        }
        else if (passDecl.type == PassType::Compute && compiledPass.computePipeline) {
            // Auto-bind compute pipeline before callback
            compiledPass.computePipeline->bind(commandBuffer);
        }

        // ── Execute pass (manual callback or auto-execution) ──
        if (passDecl.executeFn) {
            // Manual callback takes highest priority
            passDecl.executeFn(ctx);
        } else if (passDecl.execution.type != "none") {
            // Auto-execution based on execution.type
            executeAutoCallback(ctx, passDecl, compiledPass, compiled, builder, parameters, commandBuffer);
        } else {
            m_logger->log(LogLevel::Warning, "Pass '%s' has no execute callback and execution.type is 'none'",
                          passDecl.name.c_str());
        }

        // ── End render pass ──
        if (passDecl.type == PassType::Graphics && compiledPass.renderPass) {
            if (!compiledPass.framebuffers.empty() &&
                compiledPass.framebuffers[0] != VK_NULL_HANDLE) {
                cmd.endRenderPass();
            }
        }

        // Notify debugger of pass end
        if (m_debugger) {
            m_debugger->onPassEnd(passIdx, passDecl.name, commandBuffer);
        }

        cmd.endDebugLabel();

        previousPassType = passDecl.type;
    }

    // Notify debugger of frame end
    if (m_debugger) {
        m_debugger->onFrameEnd(frameIndex);
    }
}

void FrameGraphExecutor::executePasses(
    const FrameGraphCompiler::CompileResult& compiled,
    const FrameGraphBuilder& builder,
    const std::vector<uint32_t>& passIndices,
    uint32_t frameIndex,
    uint32_t swapchainImageIndex,
    VkCommandBuffer commandBuffer,
    const std::unordered_map<std::string, ParameterValue>* parameters)
{
    if (!compiled.valid) {
        m_logger->log(LogLevel::Error, "Cannot execute invalid frame graph");
        return;
    }

    const auto& passes = builder.getPassDeclarations();
    const auto& resources = builder.getResourceDeclarations();
    VulkanCommandBuilder cmd(m_device, commandBuffer);

    // Track previous pass type for compute→graphics buffer barriers
    PassType previousPassType = PassType::Graphics;

    for (uint32_t passIdx : passIndices) {
        const auto& compiledPass = compiled.compiledPasses[passIdx];
        const auto& passDecl = passes[compiledPass.declIndex];

        // Debug label
        cmd.beginDebugLabel(passDecl.name, {0.3f, 0.7f, 0.9f, 1.0f});

        // Notify debugger of pass begin
        if (m_debugger) {
            m_debugger->onPassBegin(passIdx, passDecl.name, commandBuffer);
        }

        // ── Insert compute→graphics memory barrier for SSBO synchronization ──
        if (passDecl.type != PassType::Compute && previousPassType == PassType::Compute) {
            VkMemoryBarrier memBarrier{};
            memBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
            memBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            memBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;

            vkCmdPipelineBarrier(commandBuffer,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                VK_PIPELINE_STAGE_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
                0,
                1, &memBarrier,
                0, nullptr,
                0, nullptr);

            m_logger->logEvery(5.0f, LogLevel::Info,
                "  Inserted compute->graphics memory barrier before pass '%s'",
                passDecl.name.c_str());
        }

        // ── Insert acquire barriers (queue ownership transfers) ──
        for (const auto& barrier : compiledPass.acquireBarriers) {
            if (!barrier.resource.valid()) continue;
            const auto* physImg = std::get_if<PhysicalImage>(
                &compiled.physicalResources[barrier.resource.index]);
            if (physImg && physImg->vkImage != VK_NULL_HANDLE) {
                VkImageMemoryBarrier imgBarrier{};
                imgBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                imgBarrier.oldLayout = barrier.oldLayout;
                imgBarrier.newLayout = barrier.newLayout;
                imgBarrier.srcQueueFamilyIndex = barrier.srcQueueFamilyIndex;
                imgBarrier.dstQueueFamilyIndex = barrier.dstQueueFamilyIndex;
                imgBarrier.image = physImg->vkImage;
                imgBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                imgBarrier.subresourceRange.baseMipLevel = 0;
                imgBarrier.subresourceRange.levelCount = 1;
                imgBarrier.subresourceRange.baseArrayLayer = 0;
                imgBarrier.subresourceRange.layerCount = 1;
                imgBarrier.srcAccessMask = barrier.srcAccess;
                imgBarrier.dstAccessMask = barrier.dstAccess;

                vkCmdPipelineBarrier(commandBuffer,
                    barrier.srcStage, barrier.dstStage,
                    0, 0, nullptr, 0, nullptr,
                    1, &imgBarrier);
            }
        }

        // ── Insert pre-barriers ──
        for (const auto& barrier : compiledPass.preBarriers) {
            if (!barrier.resource.valid()) continue;
            const auto* physImg = std::get_if<PhysicalImage>(
                &compiled.physicalResources[barrier.resource.index]);
            if (physImg && physImg->vkImage != VK_NULL_HANDLE) {
                // Notify debugger of barrier
                if (m_debugger) {
                    std::string resourceName = (barrier.resource.index < resources.size())
                        ? resources[barrier.resource.index].name : "unknown";
                    bool isQueueTransfer = barrier.srcQueueFamilyIndex != VK_QUEUE_FAMILY_IGNORED;
                    m_debugger->onBarrierInserted(resourceName, barrier.oldLayout,
                                                  barrier.newLayout, isQueueTransfer);
                }

                if (barrier.srcQueueFamilyIndex != VK_QUEUE_FAMILY_IGNORED) {
                    VkImageMemoryBarrier imgBarrier{};
                    imgBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                    imgBarrier.oldLayout = barrier.oldLayout;
                    imgBarrier.newLayout = barrier.newLayout;
                    imgBarrier.srcQueueFamilyIndex = barrier.srcQueueFamilyIndex;
                    imgBarrier.dstQueueFamilyIndex = barrier.dstQueueFamilyIndex;
                    imgBarrier.image = physImg->vkImage;
                    imgBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                    imgBarrier.subresourceRange.baseMipLevel = 0;
                    imgBarrier.subresourceRange.levelCount = 1;
                    imgBarrier.subresourceRange.baseArrayLayer = 0;
                    imgBarrier.subresourceRange.layerCount = 1;
                    imgBarrier.srcAccessMask = barrier.srcAccess;
                    imgBarrier.dstAccessMask = barrier.dstAccess;

                    vkCmdPipelineBarrier(commandBuffer,
                        barrier.srcStage, barrier.dstStage,
                        0, 0, nullptr, 0, nullptr,
                        1, &imgBarrier);
                } else {
                    cmd.imageBarrier(
                        physImg->vkImage,
                        barrier.oldLayout,
                        barrier.newLayout,
                        barrier.srcStage,
                        barrier.dstStage
                    );
                }
            }
        }

        // ── Build execution context ──
        PassExecuteContext ctx(cmd);
        ctx.frameIndex = frameIndex;
        ctx.swapchainIndex = swapchainImageIndex;
        ctx.renderExtent = compiledPass.extent;
        ctx.physicalResourcesPtr = &compiled.physicalResources;
        ctx.physicalResourceCount = static_cast<uint32_t>(compiled.physicalResources.size());

        if (!compiledPass.descriptorSets.empty()) {
            ctx.descriptorSets = &compiledPass.descriptorSets;
        }
        if (compiledPass.pipeline) {
            ctx.pipeline = compiledPass.pipeline.get();
            ctx.pipelineLayout = compiledPass.pipelineLayout;
        }
        if (compiledPass.computePipeline) {
            ctx.computePipeline = compiledPass.computePipeline.get();
            ctx.pipelineLayout = compiledPass.pipelineLayout;
        }

        // ── Begin render pass for graphics passes ──
        if (passDecl.type == PassType::Graphics && compiledPass.renderPass) {
            VkFramebuffer fb = VK_NULL_HANDLE;
            if (!compiledPass.framebuffers.empty()) {
                uint32_t fbIdx = (compiledPass.framebuffers.size() > 1)
                    ? swapchainImageIndex : 0;
                fb = compiledPass.framebuffers[fbIdx];
            }
            if (fb != VK_NULL_HANDLE) {
                RenderPassContext rpCtx(compiledPass.renderPass, fb, compiledPass.extent);
                rpCtx.withClearValues(compiledPass.clearValues);
                cmd.beginRenderPass(rpCtx);
            }
        }

        // ── Auto-bind pipeline ──
        if (passDecl.type == PassType::Graphics && compiledPass.pipeline) {
            cmd.bindPipeline(compiledPass.pipeline.get())
               .setViewport(ViewportState::fromExtent(compiledPass.extent))
               .setScissor(ScissorState::fromExtent(compiledPass.extent));
        }
        else if (passDecl.type == PassType::Compute && compiledPass.computePipeline) {
            compiledPass.computePipeline->bind(commandBuffer);
        }

        // ── Execute pass (manual callback or auto-execution) ──
        if (passDecl.executeFn) {
            passDecl.executeFn(ctx);
        } else if (passDecl.execution.type != "none") {
            executeAutoCallback(ctx, passDecl, compiledPass, compiled, builder, parameters, commandBuffer);
        }

        // ── End render pass ──
        if (passDecl.type == PassType::Graphics && compiledPass.renderPass) {
            if (!compiledPass.framebuffers.empty() &&
                compiledPass.framebuffers[0] != VK_NULL_HANDLE) {
                cmd.endRenderPass();
            }
        }

        // Notify debugger of pass end
        if (m_debugger) {
            m_debugger->onPassEnd(passIdx, passDecl.name, commandBuffer);
        }

        cmd.endDebugLabel();

        previousPassType = passDecl.type;
    }
}

// ═══════════════════════════════════════════════════════════════
// Auto-Execution Implementation
// 位先於動 — Position before action
// ═══════════════════════════════════════════════════════════════

void FrameGraphExecutor::executeAutoCallback(
    const PassExecuteContext& ctx,
    const PassDeclaration& passDecl,
    const CompiledPass& compiledPass,
    const FrameGraphCompiler::CompileResult& compiled,
    const FrameGraphBuilder& builder,
    const std::unordered_map<std::string, ParameterValue>* parameters,
    VkCommandBuffer commandBuffer)
{
    const auto& exec = passDecl.execution;

    // ── Auto-bind descriptor sets if enabled ──
    if (exec.bindDescriptorSets && !compiledPass.descriptorSets.empty()) {
        VkPipelineBindPoint bindPoint = (passDecl.type == PassType::Compute)
            ? VK_PIPELINE_BIND_POINT_COMPUTE
            : VK_PIPELINE_BIND_POINT_GRAPHICS;

        for (uint32_t i = 0; i < compiledPass.descriptorSets.size(); ++i) {
            auto& set = compiledPass.descriptorSets[i];
            if (set) {
                const auto& sets = set->getSets();
                if (ctx.frameIndex < sets.size()) {
                    VkDescriptorSet ds = sets[ctx.frameIndex];
                    vkCmdBindDescriptorSets(commandBuffer, bindPoint,
                        compiledPass.pipelineLayout, i, 1, &ds, 0, nullptr);
                }
            }
        }
    }

    // ── Auto-push constants from named parameters ──
    if (parameters && !passDecl.pushConstants.empty()) {
        for (const auto& pc : passDecl.pushConstants) {
            // Convert stage strings to VkShaderStageFlags
            VkShaderStageFlags stageFlags = 0;
            for (const auto& stage : pc.stages) {
                stageFlags |= JsonUtils::stringToShaderStage(stage);
            }

            // Push each named binding
            for (const auto& binding : pc.bindings) {
                auto paramIt = parameters->find(binding.name);
                if (paramIt == parameters->end()) continue;

                const ParameterValue& value = paramIt->second;
                uint32_t offset = pc.offset + binding.offset;

                // Push based on type
                std::visit([&](const auto& v) {
                    vkCmdPushConstants(commandBuffer, compiledPass.pipelineLayout,
                        stageFlags, offset, sizeof(v), &v);
                }, value);
            }
        }
    }

    // ── Helper to look up resource dimensions ──
    auto getResourceExtent = [&](const std::string& resourceName) -> VkExtent2D {
        ResourceHandle handle = builder.getResource(resourceName);
        if (handle.valid() && handle.index < compiled.physicalResources.size()) {
            const auto* physImg = std::get_if<PhysicalImage>(&compiled.physicalResources[handle.index]);
            if (physImg) {
                return physImg->extent;
            }
        }
        // Fallback to render extent
        return ctx.renderExtent;
    };

    // ── Helper to look up parameter value as uint32_t ──
    auto getParameterAsUint = [&](const std::string& paramName, uint32_t defaultValue) -> uint32_t {
        if (!parameters || paramName.empty()) return defaultValue;
        auto it = parameters->find(paramName);
        if (it == parameters->end()) return defaultValue;

        // Try to extract uint32_t from the variant
        if (auto* v = std::get_if<uint32_t>(&it->second)) return *v;
        if (auto* v = std::get_if<int32_t>(&it->second)) return static_cast<uint32_t>(*v);
        if (auto* v = std::get_if<float>(&it->second)) return static_cast<uint32_t>(*v);
        return defaultValue;
    };

    // ── Execute based on type ──
    if (exec.type == "fullscreen") {
        // Fullscreen triangle draw
        vkCmdDraw(commandBuffer, 3, 1, 0, 0);
    }
    else if (exec.type == "draw") {
        // Custom draw with specified counts
        uint32_t vertexCount = exec.vertexCount.value;

        // Support parameter-based vertex count
        if (exec.vertexCount.isFromParameter()) {
            vertexCount = getParameterAsUint(exec.vertexCount.parameter, vertexCount);
            // Apply divisor if specified (e.g., for instanced rendering)
            if (exec.vertexCount.divisor > 1) {
                vertexCount = (vertexCount + exec.vertexCount.divisor - 1) / exec.vertexCount.divisor;
            }
        }

        m_logger->logEvery(5.0f, LogLevel::Info,
            "  [draw] pass='%s' vertexCount=%u instanceCount=%u",
            passDecl.name.c_str(), vertexCount, exec.instanceCount);

        vkCmdDraw(commandBuffer, vertexCount, exec.instanceCount,
                  exec.firstVertex, exec.firstInstance);
    }
    else if (exec.type == "compute_dispatch" || exec.type == "compute_image") {
        // Calculate dispatch dimensions
        uint32_t groupX = 1, groupY = 1, groupZ = 1;

        if (exec.type == "compute_image") {
            // Dispatch based on output image dimensions
            groupX = (ctx.renderExtent.width + exec.workgroupSize[0] - 1) / exec.workgroupSize[0];
            groupY = (ctx.renderExtent.height + exec.workgroupSize[1] - 1) / exec.workgroupSize[1];
            groupZ = 1;
        } else {
            // Use specified dispatch dimensions with proper resource lookup
            auto calcDim = [&](const DispatchDimension& dim) -> uint32_t {
                if (dim.isFixed()) {
                    return dim.value;
                } else if (dim.isFromParameter()) {
                    // Look up parameter value and apply divisor
                    uint32_t paramValue = getParameterAsUint(dim.parameter, 1);
                    return (paramValue + dim.divisor - 1) / dim.divisor;
                } else if (dim.isFromResource()) {
                    // Look up actual resource dimensions
                    VkExtent2D resExtent = getResourceExtent(dim.resource);
                    uint32_t size = (dim.dimension == "width") ? resExtent.width :
                                    (dim.dimension == "height") ? resExtent.height : 1;
                    return (size + dim.divisor - 1) / dim.divisor;
                } else {
                    return 1;
                }
            };

            groupX = calcDim(exec.dispatch[0]);
            groupY = calcDim(exec.dispatch[1]);
            groupZ = calcDim(exec.dispatch[2]);
        }

        m_logger->logEvery(5.0f, LogLevel::Info,
            "  [compute] pass='%s' dispatch=(%u, %u, %u)",
            passDecl.name.c_str(), groupX, groupY, groupZ);

        vkCmdDispatch(commandBuffer, groupX, groupY, groupZ);
    }
    else if (exec.type == "scene_geometry" ||
             exec.type == "opaque_geometry" ||
             exec.type == "transparent_geometry" ||
             exec.type == "shadow_casters" ||
             exec.type == "skinned_geometry" ||
             exec.type == "skinned_transparent") {
        // Call scene renderer callback if registered
        // For built-in geometry types, RenderGraph auto-registers the callback
        if (passDecl.sceneRendererFn) {
            passDecl.sceneRendererFn(ctx);
        } else {
            m_logger->log(LogLevel::Warning,
                "Pass '%s' has execution.type='%s' but no scene renderer registered",
                passDecl.name.c_str(), exec.type.c_str());
        }
    }
}

} // namespace FrameGraph
} // namespace Shoonyakasha
