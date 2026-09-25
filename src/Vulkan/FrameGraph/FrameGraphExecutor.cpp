//
// Shoonyakasha Engine - Frame Graph Executor
//
// 朱雀司變  熾熱而速達
// The Vermilion Bird governs transformation — blazing heat, swift arrival
//

#include "Vulkan/FrameGraph/FrameGraph.h"
#include "GPU/VkFormatUtils.h"
#include "Vulkan/FrameGraph/FrameGraphDebugger.h"
#include "Vulkan/VulkanCommandBuffer.h"
#include "Vulkan/VulkanPipeline.h"
#include "Vulkan/VulkanComputePipeline.h"
#include "Vulkan/VulkanDescriptorSystem.h"
#include "Vulkan/VulkanImage.h"
#include "Vulkan/VulkanDevice.h"
#include "Core/Logger.h"

#include <stdexcept>
#include <optional>
#include <array>
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

namespace {

/// Record image barriers, each over its own subresource range.
void recordImageBarriers(VulkanCommandBuilder& cmd,
                         const std::vector<BarrierInfo>& barriers,
                         const FrameGraphCompiler::CompileResult& compiled,
                         const std::vector<ResourceDeclaration>& resources,
                         FrameGraphDebugger* debugger)
{
    for (const auto& barrier : barriers) {
        if (!barrier.resource.valid()) continue;
        const auto* physImg = std::get_if<PhysicalImage>(
            &compiled.physicalResources[barrier.resource.index]);
        if (!physImg || physImg->vkImage == VK_NULL_HANDLE) continue;

        if (debugger) {
            std::string resourceName = (barrier.resource.index < resources.size())
                ? resources[barrier.resource.index].name : "unknown";
            bool isQueueTransfer = barrier.srcQueueFamilyIndex != VK_QUEUE_FAMILY_IGNORED;
            debugger->onBarrierInserted(resourceName, barrier.oldLayout,
                                        barrier.newLayout, isQueueTransfer);
        }

        // Outside a queue transfer the family indices hold
        // VK_QUEUE_FAMILY_IGNORED, so one call covers both cases.
        cmd.imageBarrier(
            physImg->vkImage,
            barrier.oldLayout, barrier.newLayout,
            barrier.srcStage,  barrier.dstStage,
            barrier.srcAccess, barrier.dstAccess,
            barrier.range,
            barrier.srcQueueFamilyIndex, barrier.dstQueueFamilyIndex);
    }
}

/// Begin dynamic rendering into the pass's attachments. Returns false, having
/// recorded nothing, if the pass has no attachments or one has no view yet
/// (an imported image that was never provided).
bool beginRendering(VkCommandBuffer commandBuffer,
                    const CompiledPass& pass,
                    const FrameGraphCompiler::CompileResult& compiled)
{
    if (!pass.rendersAttachments() || pass.extent.width == 0 || pass.extent.height == 0) {
        return false;
    }

    auto toInfo = [&](const CompiledAttachment& att, VkRenderingAttachmentInfo& info) {
        VkImageView view = att.view;
        if (view == VK_NULL_HANDLE) {
            // Imported: the view for this frame's swapchain image.
            const auto* physImg = std::get_if<PhysicalImage>(
                &compiled.physicalResources[att.resource.index]);
            view = physImg ? physImg->view : VK_NULL_HANDLE;
        }
        info = VkRenderingAttachmentInfo{};
        info.sType       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        info.imageView   = view;
        info.imageLayout = att.layout;
        info.loadOp      = att.loadOp;
        info.storeOp     = att.storeOp;
        info.clearValue  = att.clearValue;
        return view != VK_NULL_HANDLE;
    };

    std::vector<VkRenderingAttachmentInfo> colors(pass.colorAttachments.size());
    for (size_t i = 0; i < colors.size(); ++i) {
        if (!toInfo(pass.colorAttachments[i], colors[i])) return false;
    }
    VkRenderingAttachmentInfo depth{};
    if (pass.hasDepthAttachment && !toInfo(pass.depthAttachment, depth)) return false;

    VkRenderingInfo rendering{};
    rendering.sType                = VK_STRUCTURE_TYPE_RENDERING_INFO;
    rendering.renderArea           = {{0, 0}, pass.extent};
    rendering.layerCount           = pass.layerCount;
    rendering.colorAttachmentCount = static_cast<uint32_t>(colors.size());
    rendering.pColorAttachments    = colors.data();
    rendering.pDepthAttachment     = pass.hasDepthAttachment ? &depth : nullptr;

    vkCmdBeginRendering(commandBuffer, &rendering);
    return true;
}

} // namespace

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
        recordImageBarriers(cmd, compiledPass.acquireBarriers, compiled, resources, nullptr);

        // ── Insert pre-barriers ──
        recordImageBarriers(cmd, compiledPass.preBarriers, compiled, resources, m_debugger);

        // ── Build execution context ──
        PassExecuteContext ctx(cmd);
        ctx.frameIndex = frameIndex;
        ctx.swapchainIndex = swapchainImageIndex;
        ctx.renderExtent = compiledPass.extent;
        ctx.physicalResourcesPtr = &compiled.physicalResources;
        ctx.physicalResourceCount = static_cast<uint32_t>(compiled.physicalResources.size());
        ctx.repeatIndex = passDecl.repeatIndex;
        ctx.repeatCount = passDecl.repeatCount;

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

        // ── Begin rendering for graphics passes ──
        const bool rendering = passDecl.type == PassType::Graphics &&
                               beginRendering(commandBuffer, compiledPass, compiled);

        // A disabled pass binds and records nothing. Its barriers above and
        // its rendering still run, so its attachments receive their clear
        // values and end in the layouts that later passes' barriers expect.
        const bool active = passDecl.enabled;

        // ── Auto-bind pipeline if available ──
        if (active && passDecl.type == PassType::Graphics && compiledPass.pipeline) {
            cmd.bindPipeline(compiledPass.pipeline.get())
               .setViewport(ViewportState::fromExtent(compiledPass.extent))
               .setScissor(ScissorState::fromExtent(compiledPass.extent));
        }
        else if (active && passDecl.type == PassType::Compute && compiledPass.computePipeline) {
            // Auto-bind compute pipeline before callback
            compiledPass.computePipeline->bind(commandBuffer);
        }

        // ── Execute pass (manual callback or auto-execution) ──
        if (!active) {
            // Nothing to record.
        } else if (passDecl.executeFn) {
            // Manual callback takes highest priority
            passDecl.executeFn(ctx);
        } else if (passDecl.execution.type != "none") {
            // Auto-execution based on execution.type
            executeAutoCallback(ctx, passDecl, compiledPass, compiled, builder, parameters, commandBuffer);
        } else {
            m_logger->log(LogLevel::Warning, "Pass '%s' has no execute callback and execution.type is 'none'",
                          passDecl.name.c_str());
        }

        // ── End rendering, then post-barriers (e.g. to PRESENT_SRC_KHR) ──
        if (rendering) {
            vkCmdEndRendering(commandBuffer);
        }
        recordImageBarriers(cmd, compiledPass.postBarriers, compiled, resources, m_debugger);

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
        recordImageBarriers(cmd, compiledPass.acquireBarriers, compiled, resources, nullptr);

        // ── Insert pre-barriers ──
        recordImageBarriers(cmd, compiledPass.preBarriers, compiled, resources, m_debugger);

        // ── Build execution context ──
        PassExecuteContext ctx(cmd);
        ctx.frameIndex = frameIndex;
        ctx.swapchainIndex = swapchainImageIndex;
        ctx.renderExtent = compiledPass.extent;
        ctx.physicalResourcesPtr = &compiled.physicalResources;
        ctx.physicalResourceCount = static_cast<uint32_t>(compiled.physicalResources.size());
        ctx.repeatIndex = passDecl.repeatIndex;
        ctx.repeatCount = passDecl.repeatCount;

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

        // ── Begin rendering for graphics passes ──
        const bool rendering = passDecl.type == PassType::Graphics &&
                               beginRendering(commandBuffer, compiledPass, compiled);

        // A disabled pass keeps its barriers and rendering; see execute().
        const bool active = passDecl.enabled;

        // ── Auto-bind pipeline ──
        if (active && passDecl.type == PassType::Graphics && compiledPass.pipeline) {
            cmd.bindPipeline(compiledPass.pipeline.get())
               .setViewport(ViewportState::fromExtent(compiledPass.extent))
               .setScissor(ScissorState::fromExtent(compiledPass.extent));
        }
        else if (active && passDecl.type == PassType::Compute && compiledPass.computePipeline) {
            compiledPass.computePipeline->bind(commandBuffer);
        }

        // ── Execute pass (manual callback or auto-execution) ──
        if (!active) {
            // Nothing to record.
        } else if (passDecl.executeFn) {
            passDecl.executeFn(ctx);
        } else if (passDecl.execution.type != "none") {
            executeAutoCallback(ctx, passDecl, compiledPass, compiled, builder, parameters, commandBuffer);
        }

        // ── End rendering, then post-barriers (e.g. to PRESENT_SRC_KHR) ──
        if (rendering) {
            vkCmdEndRendering(commandBuffer);
        }
        recordImageBarriers(cmd, compiledPass.postBarriers, compiled, resources, m_debugger);

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
    // A binding named pass.* reads this pass instead of a graph parameter.
    auto passValue = [&](const std::string& name) -> std::optional<ParameterValue> {
        if (name == "pass.repeatIndex") return ParameterValue{passDecl.repeatIndex};
        if (name == "pass.repeatCount") return ParameterValue{passDecl.repeatCount};
        const float w = static_cast<float>(ctx.renderExtent.width);
        const float h = static_cast<float>(ctx.renderExtent.height);
        if (name == "pass.extent") return ParameterValue{std::array<float, 2>{w, h}};
        if (name == "pass.texelSize") {
            return ParameterValue{std::array<float, 2>{w > 0.0f ? 1.0f / w : 0.0f, h > 0.0f ? 1.0f / h : 0.0f}};
        }
        return std::nullopt;
    };

    if (!passDecl.pushConstants.empty()) {
        for (const auto& pc : passDecl.pushConstants) {
            // Convert stage strings to VkShaderStageFlags
            VkShaderStageFlags stageFlags = 0;
            for (const auto& stage : pc.stages) {
                stageFlags |= JsonUtils::stringToShaderStage(stage);
            }

            // Push each named binding
            for (const auto& binding : pc.bindings) {
                std::optional<ParameterValue> found = passValue(binding.name);
                if (!found && parameters) {
                    auto paramIt = parameters->find(binding.name);
                    if (paramIt != parameters->end()) found = paramIt->second;
                }
                if (!found) continue;

                const ParameterValue& value = *found;
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
    else if (exec.type == "scene_geometry" || isEntityGeometryExecutionType(exec.type)) {
        // Call scene renderer callback if registered. For the entity geometry
        // types RenderGraph auto-registers it, using the same
        // isEntityGeometryExecutionType list as this branch.
        if (passDecl.sceneRendererFn) {
            passDecl.sceneRendererFn(ctx);
        } else {
            m_logger->log(LogLevel::Warning,
                "Pass '%s' has execution.type='%s' but no scene renderer registered",
                passDecl.name.c_str(), exec.type.c_str());
        }
    }
    else if (!exec.type.empty() && exec.type != "manual") {
        // Falling off the end of this chain used to be silent: a pass with a
        // typo'd or unsupported execution type simply drew nothing, with no
        // diagnostic anywhere. That is how sprite_geometry went unnoticed.
        m_logger->logEvery(5.0f, LogLevel::Warning,
            "Pass '%s' has unrecognised execution.type='%s' — nothing was recorded for it",
            passDecl.name.c_str(), exec.type.c_str());
    }
}

} // namespace FrameGraph
} // namespace Shoonyakasha
