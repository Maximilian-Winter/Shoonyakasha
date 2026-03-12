//
// Shoonyakasha Engine - Frame Graph Pass Declarations
//
// 兵法即碼法  碼法即兵法
// The art of war is the art of code
//

#pragma once

#include "FrameGraphResource.h"
#include <vulkan/vulkan.h>
#include <functional>
#include <vector>
#include <string>
#include <array>
#include <memory>

namespace Shoonyakasha {
class VulkanCommandBuilder;
class VulkanImage;
class VulkanPipeline;
class VulkanComputePipeline;
class VulkanDescriptorSet;
}

namespace Shoonyakasha {
namespace FrameGraph {

// ═══════════════════════════════════════════════════════════════
// Pass Types
// ═══════════════════════════════════════════════════════════════

enum class PassType {
    Graphics,   // Needs a VkRenderPass + framebuffer
    Compute,    // Dispatches only, no render pass
    Transfer    // Copies / blits (future: readback, mipmap gen)
};

// ═══════════════════════════════════════════════════════════════
// Queue Type — which physical queue a pass executes on
// 雙流並行 — Dual streams in parallel
// ═══════════════════════════════════════════════════════════════

enum class QueueType : uint32_t {
    Graphics = 0,
    Compute  = 1
};

// ═══════════════════════════════════════════════════════════════
// Resource Usage — how a pass accesses a resource
// ═══════════════════════════════════════════════════════════════

enum class ResourceUsage {
    ColorAttachmentWrite,       // Render target output
    ColorAttachmentBlend,       // Color attachment with alpha blending (read-modify-write)
                                // 透明混合 — Blending light through transparent surfaces
    DepthStencilWrite,          // Depth buffer output
    DepthStencilReadOnly,       // Depth read (shadow sampling)
    ShaderReadOnly,             // Sampled image / UBO / SSBO read
    ShaderReadWrite,            // Storage image / SSBO read+write
    StorageImageWrite,          // Storage image write-only (compute output)
    InputAttachment,            // Vulkan input attachment (subpass read)
    TransferSrc,                // Copy source
    TransferDst,                // Copy destination
    Present                     // Swapchain image presentation
};

// ═══════════════════════════════════════════════════════════════
// Sampler Description — JSON-declared sampler configuration
// 簡而明 — Simple yet clear
// ═══════════════════════════════════════════════════════════════

struct SamplerDesc {
    std::string name;

    // Filtering
    std::string magFilter = "linear";           // "linear", "nearest"
    std::string minFilter = "linear";
    std::string mipmapMode = "linear";          // "linear", "nearest"

    // Address modes
    std::string addressModeU = "repeat";        // "repeat", "clamp_to_edge", "clamp_to_border", "mirrored_repeat"
    std::string addressModeV = "repeat";
    std::string addressModeW = "repeat";
    std::string addressMode = "";               // Shorthand: sets U, V, W together

    // Border color (when using clamp_to_border)
    std::string borderColor = "float_opaque_black";  // "float_opaque_black", "float_opaque_white", "float_transparent_black"

    // Anisotropy
    bool        anisotropyEnable = false;
    float       maxAnisotropy = 1.0f;

    // Depth comparison (for shadow sampling)
    bool        compareEnable = false;
    std::string compareOp = "less";             // "never", "less", "equal", "less_or_equal", "greater", "not_equal", "greater_or_equal", "always"

    // LOD control
    float       minLod = 0.0f;
    float       maxLod = 0.0f;
    float       mipLodBias = 0.0f;
};

// ═══════════════════════════════════════════════════════════════
// Uniform Buffer Description — JSON-declared UBO structure
// 氣之流轉 — The flow of qi (data)
// ═══════════════════════════════════════════════════════════════

struct UniformFieldDesc {
    std::string name;           // Field name for setUniformField()
    std::string type;           // "float", "int", "vec2", "vec3", "vec4", "mat4"
    uint32_t    offset = 0;     // Byte offset within UBO
    uint32_t    size = 0;       // Auto-calculated from type if not specified
};

struct UniformBufferDesc {
    std::string name;
    uint32_t    size = 0;                       // Total buffer size in bytes
    bool        perFrame = true;                // Create one buffer per frame in flight
    bool        frameworkManaged = false;       // If true, framework creates/owns the buffer
    std::vector<UniformFieldDesc> fields;       // Optional: field layout for setUniformField()
};

// ═══════════════════════════════════════════════════════════════
// Descriptor Set Layout Description — JSON-declared binding layout
// 名正則言順 — When names are correct, bindings flow naturally
// ═══════════════════════════════════════════════════════════════

struct DescriptorBindingDesc {
    uint32_t                    binding = 0;
    std::string                 type;           // "uniform_buffer", "combined_image_sampler", etc.
    std::vector<std::string>    stages;         // "vertex", "fragment", "compute"
    uint32_t                    count = 1;
    std::string                 name;           // Optional binding name for resource lookup

    // Auto-binding: automatically bind resources at compile time
    std::string                 autoBindResource;   // Resource name from graph resources (images)
    std::string                 autoBindSampler;    // Sampler name from graph samplers
    std::string                 autoBindBuffer;     // Buffer name (for external UBOs)
};

struct DescriptorSetLayoutDesc {
    std::string                         name;
    std::vector<DescriptorBindingDesc>  bindings;
};

// ═══════════════════════════════════════════════════════════════
// Resource Access — a pass's reference to a resource + how it uses it
// ═══════════════════════════════════════════════════════════════

struct ResourceAccess {
    ResourceHandle  handle;
    ResourceUsage   usage;

    // Clear value (only meaningful for attachment writes)
    bool            hasClearValue = false;
    VkClearValue    clearValue{};

    // Resolved at compile time:
    VkImageLayout       requiredLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkPipelineStageFlags stageMask  = 0;
    VkAccessFlags        accessMask = 0;
};

// ═══════════════════════════════════════════════════════════════
// Pass Execute Context — injected into pass callbacks
// ═══════════════════════════════════════════════════════════════

// Forward declaration: opaque compiled graph data (defined in FrameGraph.h)
struct CompiledGraphData;

struct PassExecuteContext {
    VulkanCommandBuilder&   cmd;
    uint32_t                frameIndex = 0;         // 0..MAX_FRAMES_IN_FLIGHT-1
    uint32_t                swapchainIndex = 0;     // Which swapchain image
    VkExtent2D              renderExtent = {};      // Pass render area

    // Opaque pointer to compiled physical resources (implementation accesses internals)
    const void* physicalResourcesPtr = nullptr;
    uint32_t    physicalResourceCount = 0;

    // Auto-created pipeline and layout (null if manually managed)
    VulkanPipeline*         pipeline = nullptr;
    VulkanComputePipeline*  computePipeline = nullptr;
    VkPipelineLayout        pipelineLayout = VK_NULL_HANDLE;

    // Auto-created descriptor sets for this pass (ordered by descriptorSetRefs)
    const std::vector<std::shared_ptr<VulkanDescriptorSet>>* descriptorSets = nullptr;

    explicit PassExecuteContext(VulkanCommandBuilder& cmdBuilder) : cmd(cmdBuilder) {}

    // Physical resource accessors
    VkImageView getImageView(ResourceHandle h) const;
    VkImage     getImage(ResourceHandle h) const;
    VkBuffer    getBuffer(ResourceHandle h) const;

    // Descriptor set accessors — returns the auto-created set at given index
    std::shared_ptr<VulkanDescriptorSet> getDescriptorSet(uint32_t setIndex) const;
};

// ═══════════════════════════════════════════════════════════════
// Pass Execute Callback
// ═══════════════════════════════════════════════════════════════

using PassExecuteFn = std::function<void(const PassExecuteContext&)>;

// ═══════════════════════════════════════════════════════════════
// Pipeline Description — JSON-serializable pipeline state
// ═══════════════════════════════════════════════════════════════

struct PipelineDesc {
    std::string vertexShader;
    std::string fragmentShader;
    std::string computeShader;

    bool depthTest  = true;
    bool depthWrite = true;
    std::string cullMode      = "back";           // "none", "front", "back", "front_and_back"
    std::string blending      = "none";           // "none", "alpha", "additive"
    std::string topology      = "triangle_list";  // "triangle_list", "triangle_strip", "line_list", "point_list"
    std::string vertexInput   = "default";        // "default" (Vertex type), "none" (fullscreen)
    bool wireframe = false;
};

// ═══════════════════════════════════════════════════════════════
// Push Constant Description
// ═══════════════════════════════════════════════════════════════

struct PushConstantBindingDesc {
    std::string name;           // Parameter name to bind (e.g., "elapsedTime")
    uint32_t    offset = 0;     // Offset in push constant range
    std::string type;           // "float", "int", "vec2", "vec3", "vec4", "mat4"
};

struct PushConstantDesc {
    std::vector<std::string>            stages;     // "vertex", "fragment", "compute"
    uint32_t                            size   = 0;
    uint32_t                            offset = 0;
    std::vector<PushConstantBindingDesc> bindings;  // Named parameter bindings
};

// ═══════════════════════════════════════════════════════════════
// Dispatch Dimension — how to calculate compute dispatch size
// ═══════════════════════════════════════════════════════════════

struct DispatchDimension {
    // Option 1: Fixed value
    uint32_t    value = 0;

    // Option 2: From resource dimensions
    std::string resource;           // Resource name
    std::string dimension;          // "width", "height", "depth"
    uint32_t    divisor = 1;        // Divide by workgroup size

    // Option 3: From named parameter
    std::string parameter;          // Parameter name (for dynamic dispatch)

    bool isFixed() const { return value > 0; }
    bool isFromResource() const { return !resource.empty(); }
    bool isFromParameter() const { return !parameter.empty(); }
};

// ═══════════════════════════════════════════════════════════════
// Execution Description — how a pass executes (auto-callback config)
// 位先於動 — Position before action
// ═══════════════════════════════════════════════════════════════

struct ExecutionDesc {
    // Execution type presets
    // Supported types:
    //   "none"               - No auto-execution (use manual callback)
    //   "fullscreen"         - Draw 3 vertices for fullscreen triangle
    //   "draw"               - Draw with specified vertex count
    //   "compute_dispatch"   - Compute shader dispatch
    //   "compute_image"      - Compute dispatch based on image dimensions
    //   "scene_geometry"     - Hybrid: framework binds, callback draws
    //   "opaque_geometry"    - Built-in: render opaque entities
    //   "transparent_geometry" - Built-in: render transparent entities (back-to-front)
    //   "shadow_casters"     - Built-in: render shadow-casting entities
    //   "skinned_geometry"   - Built-in: render skinned (animated) entities
    //   "skinned_transparent" - Built-in: render skinned transparent entities
    std::string type = "none";

    // For "draw" type
    DispatchDimension vertexCount;
    uint32_t    instanceCount = 1;
    uint32_t    firstVertex = 0;
    uint32_t    firstInstance = 0;

    // For "compute_dispatch" type
    std::array<uint32_t, 3>     workgroupSize = {16, 16, 1};
    std::array<DispatchDimension, 3> dispatch;      // x, y, z group counts

    // For built-in geometry types (opaque_geometry, transparent_geometry, shadow_casters)
    std::string sortMode = "none";          // "none", "front_to_back", "back_to_front"
    std::string entityDataBinding;          // Reference to entityDataBindings config (e.g., "pbrOpaque")
    uint32_t    renderLayerMask = 0xFFFFFFFF;  // Bitmask for render layer filtering
    int32_t     lightIndex = -1;            // For shadow_casters: which light's VP to use

    // Common options
    bool        bindDescriptorSets = true;
    bool        bindPipeline = true;
};

// ═══════════════════════════════════════════════════════════════
// Pass Declaration — a named pass in the graph
// ═══════════════════════════════════════════════════════════════

struct PassDeclaration {
    std::string                 name;
    PassType                    type = PassType::Graphics;

    std::vector<ResourceAccess> inputs;
    std::vector<ResourceAccess> outputs;

    // Pipeline configuration (from JSON or programmatic)
    PipelineDesc                pipelineDesc{};

    // Descriptor set layout references (names into a layout registry)
    std::vector<std::string>    descriptorSetRefs;

    // Push constant descriptions (with named parameter bindings)
    std::vector<PushConstantDesc> pushConstants;

    // Execution configuration — auto-callback settings from JSON
    ExecutionDesc               execution{};

    // Execute callback — manual override (takes precedence over execution config)
    PassExecuteFn               executeFn;

    // Scene renderer callback — for "scene_geometry" execution type
    PassExecuteFn               sceneRendererFn;

    // Queue affinity: which queue this pass should execute on
    QueueType                   queueType = QueueType::Graphics;

    // Side effects flag: if true, pass won't be culled even without tracked outputs
    // Use for passes that write to external resources (SSBOs via descriptors)
    bool                        hasSideEffects = false;

    // Whether this pass is enabled (disabled passes are skipped during execution)
    bool                        enabled = true;
};

} // namespace FrameGraph
} // namespace Shoonyakasha

// Backward compatibility alias
namespace FrameGraph = Shoonyakasha::FrameGraph;
