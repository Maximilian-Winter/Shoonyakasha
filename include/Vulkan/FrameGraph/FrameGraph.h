//
// Shoonyakasha Engine - Frame Graph System
//
// 道生一  一生二  二生三  三生萬物
// The Dao generates One, One generates Two, Two generates Three,
// Three generates the ten thousand things
//

#pragma once

#include "FrameGraphPass.h"
#include "FrameGraphResource.h"
#include "FrameGraphSchedule.h"  // BarrierInfo, dependency and barrier planning
#include "PixelReadback.h"  // One texel of an image back to the CPU
#include "RenderStats.h"  // Frame rate, draw counts and pass times
#include "VertexFormatRegistry.h"  // Declarative vertex format definitions
#include "FrameGraph/DotPathResolver.h"  // For Shoonyakasha::CompiledBufferLayout in DotPathUBO
#include "FrameGraph/SharedBufferRegistry.h"  // Phase 2: cross-graph SSBO sharing
#include "GPU/GPUResourceFactory.h"  // Default textures for fallback
#include "FrameGraph/BufferFieldTypes.h"  // Field types + std140/std430/scalar packing
#include "Vulkan/SceneTextureTable.h"  // The scene's textures by slot

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <entt/entt.hpp>
#include <array>
#include <vector>
#include <unordered_map>
#include <memory>
#include <string>
#include <variant>
#include <optional>
#include <unordered_set>
#include <functional>
#include <algorithm>

// Forward declarations
namespace Shoonyakasha {
class VulkanDevice;
class VulkanRenderPass;
class VulkanPipeline;
class VulkanComputePipeline;
class VulkanRayTracingPipeline;
class VulkanCommandBuilder;
class VulkanCommandManager;
class VulkanSwapChain;
class VulkanImage;
class VulkanBuffer;
class VulkanDescriptorSet;
class ResourceManager;
class Logger;
}

namespace Shoonyakasha {
    namespace ECS { class Scene; }
}

// Forward declarations for ECS binding system
namespace Shoonyakasha {
    class DotPathResolver;
    class BufferLayoutResolver;
    class FrameGraphRenderer;  // Auto-executes geometry passes
    struct SceneContext;
    struct MeshComponent;
    struct MaterialComponentV5;  // Material component with generic params/textures
    struct SkeletonComponent;    // Skeleton SSBO for skinned meshes
    class RayTracingScene;       // Acceleration structures for ray queries
}

namespace Shoonyakasha {
namespace FrameGraph {

// ═══════════════════════════════════════════════════════════════
// Buffer Layout Definitions (Declarative)
// 緩衝之構 — The structure of buffers
//
// Generic, pipeline-agnostic buffer and binding definitions.
// Define ANY buffer layout in JSON - no C++ changes needed!
// ═══════════════════════════════════════════════════════════════

// BufferFieldType, BufferPackingRule and the packing arithmetic now live in
// FrameGraph/BufferFieldTypes.h so that both this compiler and the JSON
// front-end share one implementation of the std140/std430/scalar rules.
// Same namespace, so every existing reference is unaffected.

/// Single field in a buffer layout
struct BufferFieldDesc {
    std::string name;                       // Field name (e.g., "model", "baseColorFactor")
    BufferFieldType type = BufferFieldType::Float;
    uint32_t arrayCount = 1;                // Array size (1 = not an array)
    uint32_t offset = 0;                    // Byte offset — computed by the packer unless
                                            // hasExplicitOffset is set
    bool hasExplicitOffset = false;         // JSON declared "offset". Needed because an
                                            // explicit 0 is otherwise indistinguishable
                                            // from "not set"
    uint32_t arrayStride = 0;               // Stride between array elements (0 = not an array)
    uint32_t size = 0;                      // Occupied size of one element, including
                                            // interior padding (mat3 std140 = 48, not 36)
    uint32_t columnStride = 0;              // Byte distance between matrix columns; 0 for
                                            // non-matrix types. The resolver needs it to
                                            // write a glm matrix into a padded slot

    // Dot-path source for automatic value resolution via DotPathResolver
    // JSON: "source": "entity.material.params.baseColorFactor"
    std::string source;                     // Dot-path source (e.g., "entity.transform.worldMatrix")

    // JSON "default": written when `source` does not resolve, e.g. an
    // application setting under scene.custom that was never set. One value
    // per component (16 for a mat4); empty = zeros.
    std::vector<float> defaultValue;
};

/// Usage type for buffer layouts (maps to ShaderDataUsage)
/// JSON: "push_constant", "uniform_buffer", "storage_buffer", "descriptor_set"
enum class BufferUsageType {
    PushConstant,       // vkCmdPushConstants
    UniformBuffer,      // Uniform buffer in descriptor set
    StorageBuffer,      // Storage buffer in descriptor set
    DescriptorSet       // Texture/sampler group in descriptor set
};

/// Update frequency for dot-path-driven buffers
/// JSON: "manual", "per_frame", "every_n_frames", "on_change", "once"
enum class BufferUpdateFrequency {
    Manual,         // No automatic updates (filled via setUniformField or external code)
    PerFrame,       // Updated once per frame from SceneContext via dot-path resolution
    EveryNFrames,   // Updated every N frames (configurable)
    OnChange,       // Updated only when source data changes
    Once,           // Updated once at creation, then never again
};

/// Transfer direction for staging buffer management
/// JSON: "gpu_only", "cpu_to_gpu", "gpu_to_cpu", "bidirectional"
enum class TransferDirection {
    GpuOnly,        // No CPU involvement after init
    CpuToGpu,       // CPU uploads new data to GPU each frame/period
    GpuToCpu,       // GPU results read back to CPU
    Bidirectional   // Both upload and readback
};

/// Staging strategy for buffer transfers
/// JSON: "auto", "persistent", "none"
enum class StagingStrategy {
    Auto,           // Engine decides based on transfer direction and frequency
    Persistent,     // Always keep staging buffers mapped
    None            // No staging (host-visible memory or manual management)
};

/// Memory location preference for buffers
/// JSON: "device_local", "host_visible", "host_coherent"
enum class MemoryLocation {
    DeviceLocal,    // Fast GPU memory (requires staging for CPU access)
    HostVisible,    // CPU-accessible GPU memory (slower but no staging needed)
    HostCoherent    // CPU-coherent memory (no flush needed)
};

/// Binding configuration for buffers
struct BufferBindingDesc {
    uint32_t set = 0;                       // Descriptor set index
    uint32_t binding = 0;                   // Binding index within set
    uint32_t offset = 0;                    // Push constant offset (for push_constant usage)
    std::vector<std::string> stages = {"vertex", "fragment"};  // Shader stages
};

/// Single texture binding in a descriptor set group
struct TextureBindingDesc {
    std::string name;                       // Texture name (e.g., "albedoMap")
    uint32_t binding = 0;                   // Binding index
    std::vector<std::string> stages = {"fragment"};
};

// ═══════════════════════════════════════════════════════════════
// SSBO Initialization Configuration (Declarative)
// 初期化之構 — The structure of initialization
// ═══════════════════════════════════════════════════════════════

/// Field initialization strategy for declarative SSBOs
enum class SSBOFieldInitType {
    Constant,       // Fixed value for all elements
    RandomRange,    // Uniform random in [min, max] per component
    GaussianRange,  // Phase 5: Normal distribution (mean, stddev) per component
    Grid,           // Phase 5: 3D lattice placement from element index
    Sphere          // Phase 5: Spherical distribution (surface or volume)
};

/// Per-field initialization rule
struct SSBOFieldInit {
    std::string fieldName;
    SSBOFieldInitType type = SSBOFieldInitType::Constant;
    std::array<float, 4> min = {0.f, 0.f, 0.f, 0.f};   // For RandomRange
    std::array<float, 4> max = {1.f, 1.f, 1.f, 1.f};   // For RandomRange
    std::array<float, 4> value = {0.f, 0.f, 0.f, 0.f};  // For Constant

    // Phase 5: For GaussianRange
    std::array<float, 4> mean = {0.f, 0.f, 0.f, 0.f};
    std::array<float, 4> stddev = {1.f, 1.f, 1.f, 1.f};

    // Phase 5: For Grid
    std::array<uint32_t, 3> gridDimensions = {1, 1, 1};
    std::array<float, 3> gridOrigin = {0.f, 0.f, 0.f};
    std::array<float, 3> gridSpacing = {1.f, 1.f, 1.f};
    float gridW = 1.0f;

    // Phase 5: For Sphere
    std::array<float, 3> sphereCenter = {0.f, 0.f, 0.f};
    float sphereRadius = 1.0f;
    bool sphereSurface = false;  // false=volume, true=surface
    float sphereW = 1.0f;
};

/// Complete SSBO initialization config (parsed from JSON "source" section)
struct SSBOInitConfig {
    std::string type = "none";       // "none", "initializer", "buffer_ref", "file", "scene"
    uint32_t seed = 42;
    std::vector<SSBOFieldInit> fieldInits;

    // Phase 2: buffer_ref source — reference another graph's target SSBO
    std::string bufferRef;                        // Target name to look up in SharedBufferRegistry
    std::string bufferRefFrequency = "per_frame"; // "once", "per_frame"

    // Phase 4: file source — load binary data from disk
    std::string filePath;                         // Path for "file" source type

    // "scene" source: filled from the scene every frame. "lights": the
    // scene's lights as SceneContext::PackedLight, as many as fit.
    std::string sceneData;
};

// ═══════════════════════════════════════════════════════════════
// Memory & Transfer Policies (Phase 3: Staging)
// 記憶之策 — The policy of memory flow
// ═══════════════════════════════════════════════════════════════

/// Memory allocation and transfer policy for a buffer
struct MemoryPolicy {
    MemoryLocation location = MemoryLocation::DeviceLocal;
    StagingStrategy staging = StagingStrategy::Auto;
    TransferDirection transferDirection = TransferDirection::GpuOnly;
};

/// Readback configuration for GPU→CPU data transfer
struct ReadbackPolicy {
    bool enabled = false;
    BufferUpdateFrequency frequency = BufferUpdateFrequency::Manual;
    uint32_t n = 1;              // For EveryNFrames: readback every N frames
    bool callbackEnabled = false;
    uint32_t ringDepth = 0;      // 0 = auto (maxFramesInFlight)
};

/// Result of a completed readback operation
struct ReadbackResult {
    std::string bufferName;
    const void* data = nullptr;  // Mapped staging memory, valid until next frame at this slot
    VkDeviceSize size = 0;
    uint32_t elementCount = 0;
    uint32_t elementStride = 0;
    uint64_t frameNumber = 0;

    // Image-specific fields (for render target readbacks)
    bool isImage = false;
    VkFormat imageFormat = VK_FORMAT_UNDEFINED;
    uint32_t imageWidth = 0;
    uint32_t imageHeight = 0;
};

/// Callback invoked when a readback completes
using ReadbackCallbackFn = std::function<void(const ReadbackResult&)>;

/// Phase 4: File save policy for GPU→disk persistence
/// 保存之策 — The policy of preservation
struct SavePolicy {
    bool enabled = false;
    std::string path;                    // Output file path
    std::string trigger = "manual";      // "manual", "every_n_frames", "on_readback"
    uint32_t n = 1;                      // For every_n_frames
    bool autoCreateDirectories = true;
};

/// Complete buffer/texture layout definition
/// This is the main struct parsed from JSON "bufferLayouts" section
struct BufferLayoutDesc {
    std::string name;                       // Layout name (e.g., "MaterialPushConstants")
    BufferUsageType usage = BufferUsageType::UniformBuffer;
    BufferPackingRule packing = BufferPackingRule::Std140;
    BufferUpdateFrequency updateFrequency = BufferUpdateFrequency::Manual;
    BufferBindingDesc binding;              // How this binds to the pipeline

    // For buffer types (push_constant, uniform_buffer, storage_buffer)
    std::vector<BufferFieldDesc> fields;

    // For descriptor_set type (texture groups)
    std::vector<TextureBindingDesc> textures;

    // For storage_buffer array-of-structs SSBOs
    uint32_t elementCount = 0;              // 0 = not an SSBO array, >0 = array element count
    SSBOInitConfig initConfig;              // How to initialize (from JSON "source" section)

    // Phase 2: Target — expose this buffer in SharedBufferRegistry for cross-graph sharing
    std::string target;                     // Empty = private buffer, non-empty = registered target name

    // Phase 3: Memory and transfer policies
    MemoryPolicy memoryPolicy;              // Memory allocation and transfer direction
    ReadbackPolicy readbackPolicy;          // Readback configuration (GPU→CPU)
    uint32_t updateFrequencyN = 1;          // N for EveryNFrames update frequency

    // Phase 4: File save policy
    SavePolicy savePolicy;                  // GPU→disk persistence configuration
};

// ═══════════════════════════════════════════════════════════════
// BufferLayout ↔ ShaderData Conversion Utilities
// 轉化之道 — The way of transformation
// ═══════════════════════════════════════════════════════════════

/// Numbers as a value of a buffer field's type: one for scalars, one per
/// component for vectors, 16 column-major for mat4. Invalid when the count
/// does not fit or the type has no ResolvedValue. Used for field "default"s
/// and preset values.
Shoonyakasha::ResolvedValue resolvedValueOf(BufferFieldType type, const std::vector<float>& components);

/// Compiled buffer layout - ready for runtime use
/// Uses dot-path sources for automatic value resolution
struct CompiledBufferLayout {
    std::string name;                        // Layout name from JSON
    BufferUsageType usage;                   // Usage type from JSON
    BufferUpdateFrequency updateFrequency = BufferUpdateFrequency::Manual;
    BufferBindingDesc binding;               // Binding configuration from JSON
    std::vector<TextureBindingDesc> textures; // Texture bindings (for descriptor_set type)
    uint32_t totalSize = 0;                  // Computed total size in bytes

    // Parsed fields with source paths for DotPathResolver
    std::vector<BufferFieldDesc> fields;

    // Source classification for optimization
    bool hasSceneSources = false;            // Contains scene.* paths
    bool hasEntitySources = false;           // Contains entity.* paths
    bool hasConstSources = false;            // Contains const.* paths
    bool hasPassSources = false;             // Contains pass.* paths (push constants only)

    /// Check if this layout uses dot-path sources
    bool usesDotPathSources() const {
        return hasSceneSources || hasEntitySources || hasConstSources || hasPassSources;
    }

    /// Get VkShaderStageFlags from the binding configuration
    VkShaderStageFlags getShaderStages() const;

    /// Check if this is a push constant layout
    bool isPushConstant() const { return usage == BufferUsageType::PushConstant; }

    /// Check if this is a descriptor set (textures) layout
    bool isDescriptorSet() const { return usage == BufferUsageType::DescriptorSet; }

    /// Check if this is a buffer layout (UBO/SSBO)
    bool isBuffer() const {
        return usage == BufferUsageType::UniformBuffer ||
               usage == BufferUsageType::StorageBuffer;
    }

    /// Convert to the resolver-side layout consumed by BufferLayoutResolver.
    ///
    /// Copies the compiler's offsets, sizes and strides verbatim and computes
    /// nothing. Three separate copies of this conversion used to exist in
    /// RenderGraph.cpp, each with its own hardcoded type-size table that said
    /// mat3 is 36 bytes regardless of packing, and two of which recomputed
    /// offsets with a tight-packing accumulator that ignored the compiler
    /// entirely.
    Shoonyakasha::CompiledBufferLayout toResolverLayout() const;
};

// ═══════════════════════════════════════════════════════════════
// Entity Data Binding Configuration (v4 Declarative)
// 實體結縛之設 — Configuration for entity binding
// NOTE: This section uses the generic BufferLayoutDesc system above.
//       The hardcoded PBR fields are DEPRECATED - use bufferLayouts instead!
// ═══════════════════════════════════════════════════════════════

/// Configuration for per-draw data (model matrix, etc.)
struct PerDrawBindingConfig {
    std::string method = "push_constant";  // "push_constant" or "descriptor_set"
    uint32_t offset = 0;                    // Push constant offset
    uint32_t size = 64;                     // Size in bytes (default: mat4)
    std::vector<std::string> stages = {"vertex"};  // Shader stages

    // For descriptor set method
    uint32_t setIndex = 0;
    uint32_t bindingIndex = 0;

    // NEW: Reference to a bufferLayout by name (preferred over hardcoded size)
    std::string layoutRef;                  // e.g., "MaterialPushConstants"
};

/// Configuration for material data (textures, params)
struct MaterialBindingConfig {
    std::string method = "descriptor_set";  // Usually "descriptor_set"
    uint32_t setIndex = 1;                  // Descriptor set index

    // Named texture bindings: texture name -> binding index
    // This is GENERIC - no hardcoded texture names!
    std::unordered_map<std::string, uint32_t> textureBindings;

    // Reference to a bufferLayout by name (PREFERRED - fully declarative!)
    // 緩衝之構 — The structure of buffers guides all bindings
    std::string layoutRef;                  // e.g., "MaterialTextures"
};

/// Configuration for skeleton SSBO data (per-entity bone matrices)
/// 骨之繫 — The binding of bones
struct SkeletonBindingConfig {
    std::string layoutRef;                  // e.g., "skeletonSet"
};

/// Complete binding configuration for a material type (e.g., "pbrOpaque", "pbrTransparent")
struct EntityDataBindingConfig {
    std::string name;                       // Config name (e.g., "pbrOpaque")
    PerDrawBindingConfig perDraw;           // Per-draw data configuration
    MaterialBindingConfig material;          // Material data configuration
    SkeletonBindingConfig skeleton;          // Skeleton SSBO configuration (optional)
};

// Forward declaration for Phase 3 staging
class StagingBufferManager;

// Forward declarations for analysis/debug/export (defined in separate headers)
struct AnalysisResult;
struct CullingReport;
struct BarrierAnalysis;
struct ResourceLifetime;
struct DependencyEdge;
struct ExportOptions;
class FrameGraphAnalyzer;
class FrameGraphDebugger;

// ═══════════════════════════════════════════════════════════════
// Physical Resources — compiled, real Vulkan objects
// ═══════════════════════════════════════════════════════════════

/// Image views of parts of one image, created on first request and
/// destroyed with the set.
class ImageViewSet {
public:
    explicit ImageViewSet(VkDevice device) : m_device(device) {}
    ~ImageViewSet();

    ImageViewSet(const ImageViewSet&) = delete;
    ImageViewSet& operator=(const ImageViewSet&) = delete;

    /// A view of `range` as `type`, shared by every caller asking for the
    /// same pair. Throws if the view cannot be created.
    VkImageView get(VkImage image, VkFormat format, VkImageViewType type,
                    const VkImageSubresourceRange& range);

private:
    struct Entry {
        VkImageViewType         type;
        VkImageSubresourceRange range;
        VkImageView             view;
    };
    VkDevice           m_device;
    std::vector<Entry> m_entries;
};

/// One allocation that transient images share; freed when the last image
/// holding it is destroyed. Defined in FrameGraphCompiler.cpp.
struct SharedImageMemory;

struct PhysicalImage {
    /// The memory a transient image shares with others, or null. Declared
    /// before ownedImage so the image is destroyed first.
    std::shared_ptr<SharedImageMemory> sharedMemory;
    std::unique_ptr<VulkanImage> ownedImage;    // null if imported
    VkImage         vkImage  = VK_NULL_HANDLE;
    VkImageView     view     = VK_NULL_HANDLE;  // whole image, as shaders sample it
    VkFormat        format   = VK_FORMAT_UNDEFINED;
    VkExtent2D      extent   = {};              // of mip 0
    uint32_t        depth    = 1;               // slices of mip 0; 1 unless 3d
    VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
    /// Layout of mip 0, layer 0 at the end of the frame, for readback and saves.
    VkImageLayout   currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    ImageShape         shape;                               // created mips and layers
    VkImageViewType    viewType = VK_IMAGE_VIEW_TYPE_2D;    // type of `view`
    VkImageAspectFlags aspect   = VK_IMAGE_ASPECT_COLOR_BIT; // all aspects, for barriers
    /// Every subresource's layout at the end of the frame (mip * layers + layer).
    std::vector<VkImageLayout> finalLayouts;
    /// Views of single mips and layers. Declared after ownedImage so it is
    /// destroyed first.
    std::unique_ptr<ImageViewSet> subresourceViews;

    /// Extent of one mip level.
    VkExtent2D mipExtent(uint32_t mip) const {
        return {std::max(1u, extent.width >> mip), std::max(1u, extent.height >> mip)};
    }
    /// Slices of one mip level.
    uint32_t mipDepth(uint32_t mip) const { return std::max(1u, depth >> mip); }
};

struct PhysicalBuffer {
    std::unique_ptr<VulkanBuffer> ownedBuffer;  // null if imported
    VkBuffer        vkBuffer = VK_NULL_HANDLE;
    VkDeviceSize    size     = 0;
};

using PhysicalResource = std::variant<PhysicalImage, PhysicalBuffer>;

// ═══════════════════════════════════════════════════════════════
// Compiled Pass — ready for execution
// ═══════════════════════════════════════════════════════════════

/// One attachment of a graphics pass, as vkCmdBeginRendering takes it.
struct CompiledAttachment {
    ResourceHandle      resource;
    /// View of the attached mip and layers. Null for imported images, whose
    /// view changes per swapchain image; the executor reads PhysicalImage::view.
    VkImageView         view    = VK_NULL_HANDLE;
    VkImageLayout       layout  = VK_IMAGE_LAYOUT_UNDEFINED;
    VkAttachmentLoadOp  loadOp  = VK_ATTACHMENT_LOAD_OP_LOAD;
    VkAttachmentStoreOp storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    VkClearValue        clearValue{};
    VkFormat            format  = VK_FORMAT_UNDEFINED;
    /// The single-sampled image a multisampled colour attachment resolves
    /// into, averaging its samples; null when it has none.
    ResourceHandle      resolveResource;
    VkImageView         resolveView = VK_NULL_HANDLE;
};

/// What the executor records for a pass, resolved from its execution type
/// when the graph is compiled rather than by comparing strings every frame.
enum class ExecutionKind : uint8_t {
    None,            // "none": nothing, unless an execute callback is set
    Fullscreen,      // "fullscreen"
    Draw,            // "draw"
    ComputeDispatch, // "compute_dispatch"
    ComputeImage,    // "compute_image"
    DispatchIndirect,// "dispatch_indirect"
    DrawIndirect,    // "draw_indirect", with or without a count buffer
    DrawIndexed,     // "draw_indexed"
    DrawIndexedIndirect, // "draw_indexed_indirect", with or without a count buffer
    SceneRenderer,   // "scene_geometry" and the entity geometry types
    Copy,            // "copy": vkCmdCopyImage from the transfer_src input to the transfer_dst output
    Blit,            // "blit": vkCmdBlitImage, the same, scaled and filtered
    DrawMeshTasks,   // "draw_mesh_tasks": vkCmdDrawMeshTasksEXT
    TraceRays,       // "trace_rays": vkCmdTraceRaysKHR
    Manual,          // "manual"
    Unknown          // anything else; warned about while recording
};

ExecutionKind executionKindOf(const std::string& type);

struct CompiledPass;

/// The blend state of each colour attachment of a graphics pass, in output
/// order: the output's own "blending" and "writeMask", or the pipeline's.
std::vector<VkPipelineColorBlendAttachmentState> colorBlendStates(const PassDeclaration& pass);

/// Why a multisampled colour attachment, `sourceRange` of `source`, cannot
/// resolve into `target`: the target must be a single-sampled colour image
/// of the same format, one mip as large as the rendered one, with as many
/// layers. Empty when it can.
std::string resolveTargetError(const PhysicalImage& source, const SubresourceRange& sourceRange,
                               const std::string& sourceName, const PhysicalImage& target,
                               const std::string& targetName);

/// Why a pass's stencil test cannot run on its attachments: it needs a depth
/// attachment with a stencil format, and one it may write if its operations
/// change stencil. Empty when it can, or has no stencil test.
std::string stencilAttachmentError(const PassDeclaration& pass, const CompiledPass& compiled);

/// The regions of a copy or blit pass, from its transfer_src input and
/// transfer_dst output as created. Both are images the graph creates, and
/// name the same number of mips and layers, copied level by level; a copy
/// also needs equal formats and extents. Returns false with the reason
/// otherwise.
bool compileTransferRegions(const PassDeclaration& pass, const std::vector<PhysicalResource>& resources,
                            const std::vector<ResourceDeclaration>& declarations,
                            CompiledPass& compiled, std::string& outError);

/// What is wrong with the indirect arguments of the dispatch_indirect and
/// draw_indirect passes, or "" when nothing is: an argument or count buffer
/// declared in `layouts` must be a storage buffer large enough to hold, from
/// its offset, the VkDispatchIndirectCommand, every VkDrawIndirectCommand
/// the pass may draw, or the count. Buffers not in `layouts` are registered
/// by the application, which is answerable for them.
/// What is wrong with the storage buffers filled from the scene ("source":
/// {"type": "scene"}), or "" when nothing is: a "lights" buffer's fields must
/// be the six vec4s of SceneContext::PackedLight, so each element is one
/// light as the engine packs it.
std::string sceneBuffersError(const std::vector<BufferLayoutDesc>& layouts,
                              const std::unordered_map<std::string, CompiledBufferLayout>& compiled);

std::string indirectArgumentsError(const std::vector<PassDeclaration>& passes,
                                   const std::vector<BufferLayoutDesc>& layouts,
                                   const std::unordered_map<std::string, CompiledBufferLayout>& compiled);

struct CompiledPass {
    uint32_t                            declIndex;          // into pass declarations

    // Attachments for dynamic rendering, in output order. Graphics passes only.
    std::vector<CompiledAttachment>     colorAttachments;
    CompiledAttachment                  depthAttachment;
    bool                                hasDepthAttachment = false;
    uint32_t                            layerCount = 1;     // layers rendered at once
    /// Render area: the attachments' mip extent for graphics passes, the first
    /// image output's for compute passes.
    VkExtent2D                          extent{};
    /// Slices at that mip level when the image is 3d; 1 otherwise.
    uint32_t                            extentDepth = 1;
    /// Samples of every attachment of a graphics pass.
    VkSampleCountFlagBits               samples = VK_SAMPLE_COUNT_1_BIT;

    /// Copy and blit passes: the images and one region per mip level.
    ResourceHandle                      transferSrc;
    ResourceHandle                      transferDst;
    std::vector<VkImageCopy>            copyRegions;
    std::vector<VkImageBlit>            blitRegions;
    VkFilter                            blitFilter = VK_FILTER_LINEAR;

    bool rendersAttachments() const { return !colorAttachments.empty() || hasDepthAttachment; }

    // Barriers to insert BEFORE this pass executes
    std::vector<BarrierInfo>            preBarriers;
    // Barriers to insert AFTER it, e.g. to PRESENT_SRC_KHR
    std::vector<BarrierInfo>            postBarriers;
    /// Buffer hazards before this pass, as one global memory barrier;
    /// recorded with preBarriers. Empty when there are none.
    MemoryBarrierInfo                   memoryBarrier;

    ExecutionKind                       executionKind = ExecutionKind::None;
    /// Shader stages of each of the pass's push constant ranges, in order.
    std::vector<VkShaderStageFlags>     pushConstantStages;

    // Auto-created descriptor set layouts (ordered by pass's descriptorSetRefs)
    std::vector<VkDescriptorSetLayout>  descriptorSetLayouts;
    std::vector<std::shared_ptr<VulkanDescriptorSet>> descriptorSets;

    // Auto-created pipeline (null if manually managed or no shaders specified)
    std::shared_ptr<VulkanPipeline>     pipeline;
    VkPipelineLayout                    pipelineLayout = VK_NULL_HANDLE;

    // Compute pipeline (for compute passes — separate from graphics pipeline)
    std::shared_ptr<VulkanComputePipeline> computePipeline;

    // Ray tracing pipeline and its shader binding table (ray tracing passes)
    std::shared_ptr<VulkanRayTracingPipeline> rayTracingPipeline;

    // Queue type this pass runs on
    QueueType                           queueType = QueueType::Graphics;

    // Acquire barriers for queue ownership transfers (executed at start of this pass)
    std::vector<BarrierInfo>            acquireBarriers;

    // Resolved entity data binding config for geometry passes
    // Contains perDraw.layoutRef and material.layoutRef from JSON entityDataBindings
    EntityDataBindingConfig             entityDataBinding;
    bool                                hasEntityDataBinding = false;
};

// ═══════════════════════════════════════════════════════════════
// Frame Graph Builder — declare resources and passes
// 青龍司生 — The Azure Dragon governs growth
// ═══════════════════════════════════════════════════════════════

/// A named set of pass switches and scene values (JSON "presets"), for
/// quality tiers and the like. Applied with RenderGraph::applyPreset.
struct PipelinePreset {
    std::string name;
    /// Pass name or declared name of a repeated pass -> enabled.
    std::vector<std::pair<std::string, bool>> passes;
    /// scene.custom key (without the prefix) -> components. std::nullopt
    /// (JSON null) removes the value, so a field's "default" applies again.
    std::vector<std::pair<std::string, std::optional<std::vector<float>>>> values;
};

/// What the canvas UI reads from a pipeline (JSON "ui"): the depth image it
/// tests world-canvas hits against, read after the pass `afterPass`. Both
/// are empty when the pipeline has no "ui" block.
struct PipelineUISettings {
    std::string depth;
    std::string afterPass;
};

class FrameGraphBuilder;

/// Set a preset's scene.custom values in `scene`, each with the type of the
/// buffer field in `builder` that reads it. Returns warnings for values that
/// do not fit. RenderGraph::applyPreset also switches the preset's passes.
std::vector<std::string> applyPresetValues(const FrameGraphBuilder& builder, const PipelinePreset& preset,
                                           Shoonyakasha::SceneContext& scene);

class FrameGraphBuilder {
public:
    FrameGraphBuilder();

    // ── Resource Declaration ──
    ResourceHandle declareImage(const std::string& name, const ImageDesc& desc);
    ResourceHandle declareBuffer(const std::string& name, const BufferDesc& desc);

    // Import external resources (swapchain, persistent UBOs, etc.)
    ResourceHandle importImage(const std::string& name, VkFormat format);
    ResourceHandle importBuffer(const std::string& name);

    // ── Pass Declaration ──
    void addPass(const PassDeclaration& pass);

    // ── Sampler Declaration ──
    void addSampler(const SamplerDesc& sampler);
    const SamplerDesc* getSampler(const std::string& name) const;
    bool hasSampler(const std::string& name) const;
    const std::vector<SamplerDesc>& getSamplers() const { return m_samplers; }

    // ── Uniform Buffer Declaration ──
    void addUniformBuffer(const UniformBufferDesc& ubo);
    const UniformBufferDesc* getUniformBuffer(const std::string& name) const;
    bool hasUniformBuffer(const std::string& name) const;
    const std::vector<UniformBufferDesc>& getUniformBuffers() const { return m_uniformBuffers; }

    // ── Descriptor Set Layout Declaration ──
    void addDescriptorSetLayout(const DescriptorSetLayoutDesc& layout);
    const DescriptorSetLayoutDesc* getDescriptorSetLayout(const std::string& name) const;
    bool hasDescriptorSetLayout(const std::string& name) const;
    const std::vector<DescriptorSetLayoutDesc>& getDescriptorSetLayouts() const { return m_descriptorSetLayouts; }

    // ── Entity Data Binding Configuration (v4) ──
    // Defines how entity data (model matrix, materials) binds to shaders
    void addEntityDataBinding(const EntityDataBindingConfig& config);
    const EntityDataBindingConfig* getEntityDataBinding(const std::string& name) const;
    bool hasEntityDataBinding(const std::string& name) const;
    const std::vector<EntityDataBindingConfig>& getEntityDataBindings() const { return m_entityDataBindings; }

    // ── Buffer Layout Declaration ──
    // Generic buffer/texture layout definitions - completely JSON-driven!
    // 緩衝之構 — The structure of buffers arises from declaration
    void addBufferLayout(const BufferLayoutDesc& layout);
    const BufferLayoutDesc* getBufferLayout(const std::string& name) const;
    bool hasBufferLayout(const std::string& name) const;
    const std::vector<BufferLayoutDesc>& getBufferLayouts() const { return m_bufferLayouts; }

    // ── Passes left out for a missing capability ("requires") ──
    void addUnavailablePass(const std::string& name) { m_unavailablePasses.insert(name); }
    bool isUnavailablePass(const std::string& name) const { return m_unavailablePasses.count(name) > 0; }

    // ── Presets ──
    void addPreset(PipelinePreset preset) { m_presets.push_back(std::move(preset)); }
    const std::vector<PipelinePreset>& getPresets() const { return m_presets; }
    const PipelinePreset* getPreset(const std::string& name) const {
        for (const auto& p : m_presets) if (p.name == name) return &p;
        return nullptr;
    }

    // ── Canvas UI settings ──
    void setUISettings(PipelineUISettings settings) { m_uiSettings = std::move(settings); }
    const PipelineUISettings& getUISettings() const { return m_uiSettings; }

    // ── Vertex Format Declaration ──
    // 頂點之構 — The structure of vertices arises from declaration
    void setVertexFormatRegistry(const VertexFormatRegistry& registry) { m_vertexFormats = registry; }
    const VertexFormatRegistry& getVertexFormatRegistry() const { return m_vertexFormats; }
    VertexFormatRegistry& getVertexFormatRegistry() { return m_vertexFormats; }

    // ── Lookups ──
    PassDeclaration* getPass(const std::string& name);
    const PassDeclaration* getPass(const std::string& name) const;
    ResourceHandle getResource(const std::string& name) const;
    bool hasResource(const std::string& name) const;
    bool hasPass(const std::string& name) const;

    // ── Access declarations ──
    const std::vector<ResourceDeclaration>& getResourceDeclarations() const { return m_resources; }
    const std::vector<PassDeclaration>&     getPassDeclarations() const { return m_passes; }
    std::vector<PassDeclaration>&           getPassDeclarations() { return m_passes; }

    // Mutable access to a resource declaration (for setting target/readback/save after creation)
    ResourceDeclaration* getMutableResource(const std::string& name);

    // ── Reset ──
    void clear();

private:
    std::vector<ResourceDeclaration>                m_resources;
    std::unordered_map<std::string, ResourceHandle> m_resourceLookup;
    std::vector<PassDeclaration>                    m_passes;
    std::unordered_map<std::string, uint32_t>       m_passLookup;
    std::vector<SamplerDesc>                        m_samplers;
    std::unordered_map<std::string, uint32_t>       m_samplerLookup;
    std::vector<UniformBufferDesc>                  m_uniformBuffers;
    std::unordered_map<std::string, uint32_t>       m_uniformBufferLookup;
    std::vector<DescriptorSetLayoutDesc>            m_descriptorSetLayouts;
    std::unordered_map<std::string, uint32_t>       m_descriptorSetLayoutLookup;
    // StandardBufferDesc removed — replaced by dot-path UBOs in bufferLayouts
    std::vector<EntityDataBindingConfig>            m_entityDataBindings;
    std::unordered_map<std::string, uint32_t>       m_entityDataBindingLookup;
    std::vector<BufferLayoutDesc>                   m_bufferLayouts;
    std::vector<PipelinePreset>                     m_presets;
    PipelineUISettings                              m_uiSettings;
    std::unordered_set<std::string>                 m_unavailablePasses;
    std::unordered_map<std::string, uint32_t>       m_bufferLayoutLookup;

    // Declarative vertex format registry
    VertexFormatRegistry                            m_vertexFormats;
};

// ═══════════════════════════════════════════════════════════════
// Imported Resource Info — passed to compiler at compile time
// ═══════════════════════════════════════════════════════════════

struct ImportedImageInfo {
    VkFormat    format = VK_FORMAT_UNDEFINED;
    VkExtent2D  extent = {};
    // Per-swapchain-image data. [0] for non-swapchain imports.
    std::vector<VkImage>     images;
    std::vector<VkImageView> views;
};

using ImportedImageMap = std::unordered_map<uint32_t, ImportedImageInfo>;

// ═══════════════════════════════════════════════════════════════
// Frame Graph Compiler — transform declarations into execution plan
// 玄武司察 — The Dark Warrior governs investigation
// ═══════════════════════════════════════════════════════════════

class FrameGraphCompiler {
public:
    struct CompileResult {
        std::vector<uint32_t>           executionOrder;     // Pass indices, topologically sorted
        std::vector<CompiledPass>       compiledPasses;     // One per declared pass
        std::vector<PhysicalResource>   physicalResources;  // One per declared resource
        bool                            valid = false;
        std::string                     errorMessage;

        // Auto-created samplers (from JSON "samplers" section)
        std::unordered_map<std::string, VkSampler> samplers;

        // Auto-created descriptor sets (shared across passes referencing the same layout name)
        std::unordered_map<std::string, std::shared_ptr<VulkanDescriptorSet>> namedDescriptorSets;

        // The live passes grouped by queue, in submission order
        // (planQueueBatches); one graphics batch without async compute.
        std::vector<QueueBatch>         queueBatches;

        // Compiled buffer layouts (from JSON "bufferLayouts" section)
        // 緩衝之構 — The structure of buffers, ready for runtime
        std::unordered_map<std::string, CompiledBufferLayout> bufferLayouts;
    };

    ~FrameGraphCompiler();

    // Compile the graph into an execution plan
    CompileResult compile(
        VulkanDevice& device,
        const FrameGraphBuilder& builder,
        VkExtent2D referenceExtent,
        uint32_t swapchainImageCount = 1,
        const ImportedImageMap& importedImages = {},
        uint32_t maxFramesInFlight = 2,
        const std::unordered_map<std::string, std::shared_ptr<VulkanPipeline>>& manualPipelines = {}
    );

    // ── Stage 11: Buffer layout compilation ──
    // 緩衝之構 — Transform JSON BufferLayoutDesc into compiled layouts.
    // Public and device-free: buffer packing is pure arithmetic over the JSON
    // description, so it can be exercised without a VkDevice. The byte offsets it
    // produces are the contract between the JSON, the shaders, and DotPathResolver,
    // which makes them worth testing directly.
    void compileBufferLayouts(
        const std::vector<BufferLayoutDesc>& layoutDescs,
        std::unordered_map<std::string, CompiledBufferLayout>& outLayouts);

    // ── Stage 12: Shader interface validation ──
    // Reflects every pass's SPIR-V and compares its descriptor bindings, buffer
    // blocks and push constants with the JSON (see ShaderInterfaceValidator.h).
    // Returns false and fills outError with every mismatch found. A shader file
    // that cannot be read is skipped; pipeline creation reports that itself.
    bool validateShaderInterfaces(
        const FrameGraphBuilder& builder,
        const std::unordered_map<std::string, CompiledBufferLayout>& layouts,
        std::string& outError);

private:
    // ── Compilation stages ──
    // Ordering, culling and barriers are planned in FrameGraphSchedule.h; the
    // stages here are the ones that need a device.

    /// Create every non-imported image and buffer, with its mips, layers and
    /// sampled view. Returns false and sets outError for a shape the image
    /// cannot have (a non-square cube, more mips than the size allows).
    /// Images marked in `sharedWithCompute` are shared by the graphics and
    /// compute families (VulkanDevice::shareWithCompute).
    bool createPhysicalResources(
        VulkanDevice& device,
        const std::vector<ResourceDeclaration>& declarations,
        const std::vector<PassDeclaration>& passes,
        std::vector<PhysicalResource>& outResources,
        VkExtent2D referenceExtent,
        const ImportedImageMap& importedImages,
        const std::vector<bool>& sharedWithCompute,
        std::string& outError);

    /// Fill each live pass's attachments (views, load/store ops, layouts) and
    /// extent. Returns false and sets outError when a pass's attachments
    /// disagree on extent or layer count.
    bool createAttachments(
        std::vector<CompiledPass>& compiledPasses,
        const std::vector<uint32_t>& executionOrder,
        const std::vector<PassDeclaration>& passes,
        std::vector<PhysicalResource>& physResources,
        const std::vector<ResourceDeclaration>& declarations,
        const std::vector<std::vector<AttachmentOps>>& attachmentOps,
        std::string& outError);

    /// A graph image whose creation waits until the passes are ordered.
    struct DeferredImage {
        uint32_t            resource = 0;
        VkImageCreateInfo   info{};
        VkImageAspectFlags  sampledAspect = VK_IMAGE_ASPECT_COLOR_BIT;
        VkImageViewType     viewType = VK_IMAGE_VIEW_TYPE_2D;
    };
    /// Transient images, filled by createPhysicalResources.
    std::vector<DeferredImage> m_deferredImages;

    /// Put a created image into `physImg`, with its whole-image view when its
    /// usage allows views.
    static void finishImage(PhysicalImage& physImg, const DeferredImage& deferred,
                            std::unique_ptr<VulkanImage> image);

    /// Create the deferred transient images. Those that may alias share
    /// memory blocks (packAliases over their lifetimes in `executionOrder`),
    /// and their blocks go into `scheduled`; the rest get their own memory.
    bool createTransientImages(
        VulkanDevice& device,
        const std::vector<ResourceDeclaration>& declarations,
        const std::vector<PassDeclaration>& passes,
        const std::vector<DescriptorSetLayoutDesc>& descriptorLayouts,
        const std::vector<uint32_t>& executionOrder,
        std::vector<PhysicalResource>& physResources,
        std::vector<ScheduledResource>& scheduled,
        std::string& outError);

    // ── Stage 8: Descriptor set layout creation ──
    void createDescriptorSetLayouts(
        VulkanDevice& device,
        std::vector<CompiledPass>& compiledPasses,
        const std::vector<uint32_t>& executionOrder,
        const std::vector<PassDeclaration>& passes,
        const std::vector<DescriptorSetLayoutDesc>& layoutDescs,
        std::unordered_map<std::string, std::shared_ptr<VulkanDescriptorSet>>& outNamedSets,
        uint32_t maxFramesInFlight);

    // ── Stage 3.5: Sampler creation ──
    void createSamplers(
        VulkanDevice& device,
        const std::vector<SamplerDesc>& samplerDescs,
        std::unordered_map<std::string, VkSampler>& outSamplers);

    // ── Stage 8.5: Auto-binding for descriptor sets ──
    void performAutoBindings(
        VulkanDevice& device,
        const FrameGraphBuilder& builder,
        const std::vector<DescriptorSetLayoutDesc>& layoutDescs,
        std::unordered_map<std::string, std::shared_ptr<VulkanDescriptorSet>>& namedSets,
        const std::vector<PhysicalResource>& physicalResources,
        const std::unordered_map<std::string, VkSampler>& samplers,
        uint32_t maxFramesInFlight);

    // ── Stage 9: Automatic pipeline creation ──
    /// Returns false and sets outError when a pass's pipeline cannot be made.
    bool createPipelines(
        VulkanDevice& device,
        std::vector<CompiledPass>& compiledPasses,
        const std::vector<uint32_t>& executionOrder,
        const std::vector<PassDeclaration>& passes,
        const std::vector<PhysicalResource>& physResources,
        const std::unordered_map<std::string, std::shared_ptr<VulkanPipeline>>& manualOverrides,
        const VertexFormatRegistry& vertexFormats,
        std::string& outError);


    // ── Helpers ──
    /// Create m_logger on first use. compile() and compileBufferLayouts() are both
    /// public entry points, so neither may assume the other ran first.
    void ensureLogger();

    static VkImageUsageFlags usageToImageUsageFlags(ResourceUsage usage);

    Logger* m_logger = nullptr;
};

// ═══════════════════════════════════════════════════════════════
// Parameter Value — type-safe storage for named parameters
// ═══════════════════════════════════════════════════════════════

using ParameterValue = std::variant<
    float,
    int32_t,
    uint32_t,
    std::array<float, 2>,   // vec2
    std::array<float, 3>,   // vec3
    std::array<float, 4>,   // vec4
    std::array<float, 16>   // mat4 (column-major)
>;

// Forward declaration for debugger
class FrameGraphDebugger;

// ═══════════════════════════════════════════════════════════════
// Frame Graph Executor — execute the compiled graph each frame
// 朱雀司變 — The Vermilion Bird governs transformation
// ═══════════════════════════════════════════════════════════════

class FrameGraphExecutor {
public:
    FrameGraphExecutor(VulkanDevice& device, VulkanCommandManager& cmdManager);
    ~FrameGraphExecutor();

    /// Set debugger for execution tracing (optional)
    void setDebugger(FrameGraphDebugger* debugger) { m_debugger = debugger; }

    /// Collect render stats while recording; null turns it off.
    void setStats(RenderStatsCollector* stats) { m_stats = stats; }

    /// Resolves a vec4 dot-path; false when the path has no value. It gives a
    /// pass's "viewport", (x, y, width, height) in fractions of its extent,
    /// and its pipeline's "blendConstants". Without one, passes render into
    /// all of their attachments and dot-path blend constants are 0.
    using ViewportResolver = std::function<bool(const std::string& path, glm::vec4& fractions)>;
    void setViewportResolver(ViewportResolver resolver) { m_viewportResolver = std::move(resolver); }

    /// Finds the storage buffer named `name` (a bufferLayouts entry or a
    /// registered one) for frame in flight `frameIndex`, for the arguments
    /// of dispatch_indirect and draw_indirect passes; VK_NULL_HANDLE when
    /// there is none.
    using BufferLookup = std::function<VkBuffer(const std::string& name, uint32_t frameIndex)>;
    void setBufferLookup(BufferLookup lookup) { m_bufferLookup = std::move(lookup); }

    /// Called after each pass's rendering has ended and its post-barriers
    /// are recorded, with the pass, the frame in flight and the command
    /// buffer, to record more commands there. Images are in the layouts the pass left them in, and
    /// must be returned to them.
    using PostPassHook = std::function<void(const CompiledPass& pass, const PassDeclaration& decl,
                                            uint32_t frameIndex, VkCommandBuffer commandBuffer)>;
    void setPostPassHook(PostPassHook hook) { m_postPassHook = std::move(hook); }

    /// The pixels of `extent` that `fractions` covers, rounded to whole
    /// texels and clamped to the extent; zero-sized when it covers none.
    static VkRect2D viewportRect(const glm::vec4& fractions, VkExtent2D extent);

    /// Single command-buffer execution (existing behavior — all passes on one queue)
    void execute(
        const FrameGraphCompiler::CompileResult& compiled,
        const FrameGraphBuilder& builder,
        uint32_t frameIndex,
        uint32_t swapchainImageIndex,
        VkCommandBuffer commandBuffer,
        const std::unordered_map<std::string, ParameterValue>* parameters = nullptr
    );

    /// Around the executePasses calls that record one frame: tells the
    /// debugger, if any, that the frame begins and ends.
    void beginFrame(uint32_t frameIndex);
    void endFrame(uint32_t frameIndex);

    /// Execute a specific subset of passes into the given command buffer
    void executePasses(
        const FrameGraphCompiler::CompileResult& compiled,
        const FrameGraphBuilder& builder,
        const std::vector<uint32_t>& passIndices,
        uint32_t frameIndex,
        uint32_t swapchainImageIndex,
        VkCommandBuffer commandBuffer,
        const std::unordered_map<std::string, ParameterValue>* parameters = nullptr
    );

private:
    VulkanDevice&           m_device;
    VulkanCommandManager&   m_cmdManager;
    FrameGraphDebugger*     m_debugger = nullptr;
    RenderStatsCollector*   m_stats = nullptr;
    Logger*                 m_logger = nullptr;
    ViewportResolver        m_viewportResolver;
    BufferLookup            m_bufferLookup;
    PostPassHook            m_postPassHook;

    /// The area a graphics pass renders into this frame: all of its extent,
    /// or its "viewport" rectangle.
    VkRect2D renderArea(const PassDeclaration& passDecl, const CompiledPass& compiledPass) const;

    /// Record passes in the order given; execute() and executePasses() share it.
    void recordPasses(
        const FrameGraphCompiler::CompileResult& compiled,
        const FrameGraphBuilder& builder,
        const std::vector<uint32_t>& passIndices,
        uint32_t frameIndex,
        uint32_t swapchainImageIndex,
        VkCommandBuffer commandBuffer,
        const std::unordered_map<std::string, ParameterValue>* parameters);

    /// Record `barriers` and, if given, a buffer memory barrier, in a single
    /// vkCmdPipelineBarrier2. Barriers on images with no VkImage are skipped.
    void recordBarriers(
        VkCommandBuffer commandBuffer,
        const std::vector<BarrierInfo>& barriers,
        const MemoryBarrierInfo* memory,
        const FrameGraphCompiler::CompileResult& compiled,
        const std::vector<ResourceDeclaration>& resources,
        bool notifyDebugger);

    // Scratch reused from pass to pass, so recording allocates nothing once warm.
    std::vector<VkImageMemoryBarrier2>      m_imageBarriers;
    std::vector<VkRenderingAttachmentInfo>  m_colorAttachments;
    std::vector<VkDescriptorSet>            m_descriptorSets;

    /// Execute auto-callback based on PassDeclaration::execution configuration
    void executeAutoCallback(
        const PassExecuteContext& ctx,
        const PassDeclaration& passDecl,
        const CompiledPass& compiledPass,
        const FrameGraphCompiler::CompileResult& compiled,
        const FrameGraphBuilder& builder,
        const std::unordered_map<std::string, ParameterValue>* parameters,
        VkCommandBuffer commandBuffer);
};

// ═══════════════════════════════════════════════════════════════
// Render Graph — top-level orchestrator
// 黃帝司中 — The Yellow Emperor governs the center
// ═══════════════════════════════════════════════════════════════

class RenderGraph {
public:
    RenderGraph(VulkanDevice& device, VulkanCommandManager& cmdManager);
    ~RenderGraph();

    // ── Setup phase ──
    FrameGraphBuilder& getBuilder() { return m_builder; }
    const FrameGraphBuilder& getBuilder() const { return m_builder; }

    // Load graph definition from a JSON file (populates the builder)
    void loadFromFile(const std::string& filePath);

    // Register pass execution callbacks (must be called before compile)
    void registerPassCallback(const std::string& passName, PassExecuteFn fn);

    // Register geometry renderer by execution type (pipeline-agnostic!)
    // 幾何之道 — The way of geometry rendering
    // type: "opaque", "transparent", "shadow_casters", etc.
    // The executor matches pass.execution.type to find the right renderer
    void registerGeometryRenderer(const std::string& executionType, PassExecuteFn fn);

    // Register manual pipeline override (hybrid mode: skips auto-creation for this pass)
    void registerPassPipeline(const std::string& passName, std::shared_ptr<VulkanPipeline> pipeline);

    // ── Pass enable/disable ──
    // Takes effect from the next execute(), without a recompile. A disabled
    // pass records no draws or dispatches, but its barriers and rendering
    // still run: its attachments are cleared to their JSON clear values and
    // end in the layouts later passes expect. A disabled shadow pass therefore
    // leaves a map cleared to far depth, i.e. everything lit.
    // Before the pipeline JSON is loaded (e.g. from an onInit callback) the
    // setting is kept and applied when it loads. Returns false if the loaded
    // pipeline has no pass with that name. The declared name of a JSON
    // "repeat" pass switches every instance; isPassEnabled on it is true when
    // all of them are enabled.
    bool setPassEnabled(const std::string& passName, bool enabled);
    bool isPassEnabled(const std::string& passName) const;

    // Apply a preset from the pipeline's "presets": switch its passes and set
    // its scene.custom values, each with the type of the buffer field that
    // reads it. Before the pipeline is loaded, remembered and applied at
    // load. False, with a warning, when the pipeline has no such preset.
    bool applyPreset(const std::string& name);
    std::vector<std::string> getPresetNames() const;

    // ── Application images ──
    // The image every descriptor binding with "externalImage": name samples,
    // with the binding's autoBindSampler, or `sampler` when it has none.
    // Until one is set such bindings sample white. Before the pipeline is
    // compiled, remembered; after, each frame in flight takes it the next
    // time it records, when its descriptor sets are no longer in use. The
    // caller keeps the image alive as long as the graph may sample it.
    void setExternalImage(const std::string& name, VkImageView view, VkSampler sampler);

    // ── Material textures ──
    // Material descriptor sets are shared by the textures they hold and kept
    // for the life of the graph. Before destroying a texture that materials
    // used, free the sets that hold its view, once no frame in flight binds
    // them and no material still names the texture.
    void releaseMaterialTexture(VkImageView view);

    // ── Geometry pass statistics ──
    // Entities an entity geometry pass drew, and left out as outside its
    // view, the last time it ran. Returns false if it has not run.
    bool getPassDrawStats(const std::string& passName, uint32_t& drawn, uint32_t& culled) const;

    // ── Named Parameters (for push constants) ──
    void setParameter(const std::string& name, float value);
    void setParameter(const std::string& name, int32_t value);
    void setParameter(const std::string& name, uint32_t value);
    void setParameter(const std::string& name, const std::array<float, 2>& value);  // vec2
    void setParameter(const std::string& name, const std::array<float, 3>& value);  // vec3
    void setParameter(const std::string& name, const std::array<float, 4>& value);  // vec4
    void setParameter(const std::string& name, const std::array<float, 16>& value); // mat4

    const ParameterValue* getParameter(const std::string& name) const;

    // ── Uniform Buffer Management ──
    // Register externally-managed UBO (app creates and updates it)
    void registerUniformBuffer(const std::string& name,
                               const std::vector<VkBuffer>& perFrameBuffers,
                               VkDeviceSize size);

    // ── Storage Buffer Management ──
    /// Write `size` bytes at `offset` into the buffer of bufferLayouts entry
    /// `name`: a uniform buffer, or a storage buffer with an elementCount.
    /// The graph keeps the bytes until they are set again, over recompiles
    /// too. Each frame's copy of a uniform buffer gets them after its
    /// dot-path fields are filled; a storage buffer gets the bytes that
    /// changed copied in with the frame's uploads, before its passes. Before
    /// the graph is compiled the bytes are kept and checked at compile.
    /// Returns an error message, empty when the bytes were taken.
    std::string setBufferData(const std::string& name, const void* data, size_t size, size_t offset = 0);

    // Register externally-managed SSBO (for compute shaders, particle buffers, etc.)
    void registerStorageBuffer(const std::string& name, VkBuffer buffer, VkDeviceSize size);
    void registerStorageBuffer(const std::string& name,
                               const std::vector<VkBuffer>& perFrameBuffers,
                               VkDeviceSize size);

    // Set field in framework-managed UBO (created from JSON "uniformBuffers" section)
    void setUniformField(const std::string& uboName, const std::string& fieldName, float value);
    void setUniformField(const std::string& uboName, const std::string& fieldName, int32_t value);
    void setUniformField(const std::string& uboName, const std::string& fieldName, const std::array<float, 2>& value);
    void setUniformField(const std::string& uboName, const std::string& fieldName, const std::array<float, 3>& value);
    void setUniformField(const std::string& uboName, const std::string& fieldName, const std::array<float, 4>& value);
    void setUniformField(const std::string& uboName, const std::string& fieldName, const std::array<float, 16>& value);

    // ── Import external resources per frame ──
    void importSwapchainImage(uint32_t swapchainIndex,
                               VkImage image, VkImageView view,
                               VkFormat format, VkExtent2D extent);
    void importExternalImage(const std::string& name,
                              VkImage image, VkImageView view,
                              VkFormat format, VkExtent2D extent);
    void importExternalBuffer(const std::string& name,
                               VkBuffer buffer, VkDeviceSize size);

    // ── Shared Buffer Registry (Phase 2: cross-graph SSBO sharing) ──
    void setSharedBufferRegistry(SharedBufferRegistry* registry);
    SharedBufferRegistry* getSharedBufferRegistry() const { return m_sharedBufferRegistry; }

    // ── Readback API (Phase 3: GPU→CPU data transfer) ──
    void registerReadbackCallback(const std::string& bufferName, ReadbackCallbackFn cb);
    /// The readbacks completed when this frame began; their data stays valid
    /// until the next frame's readbacks are processed.
    std::vector<ReadbackResult> pollReadbacks();

    /// Copy buffer layout or image resource `name`, which declares a
    /// readback, back at the end of the next frame recorded, whatever its
    /// readback frequency. The result arrives once that frame's fence was
    /// waited on, as many frames later as there are frames in flight. False
    /// if `name` has no readback.
    bool requestReadback(const std::string& name);

    /// The most recent completed readback of `name`, or null. A buffer's is
    /// the whole buffer; an image's is mip 0, layer 0, rows of tightly packed
    /// texels (the depth aspect of a depth format). Its data stays valid
    /// until the next frame's readbacks are processed, and is gone once the
    /// graph is compiled again.
    const ReadbackResult* latestReadback(const std::string& name) const;

    // ── Pixel readback: one texel of an image ──
    // In the next frame recorded, copies the texel at `pixel` (mip 0, layer
    // 0) of the image `resource` right after the pass `afterPass`, which
    // must render it as an attachment. The image must allow copies from it,
    // which the image named by the pipeline's "ui" block, and images with a
    // readback or save policy, do. A later request replaces one not yet
    // recorded. The value is read once the same frame in flight records
    // again, after its fence was waited on, so latestPixelReadback() lags the
    // request by the number of frames in flight. Returns false, with a
    // warning, when the graph is not compiled, either name is unknown, the
    // image does not allow copies, the pixel lies outside it or its format is
    // not one pixelReadbackTexelSize() reads; the request is then dropped.
    bool requestPixelReadback(const std::string& resource, const std::string& afterPass, VkOffset2D pixel);
    const PixelReadbackResult& latestPixelReadback() const { return m_pixelReadbacks.latest(); }

    /// The pipeline's "ui" block; empty names when it has none.
    const PipelineUISettings& getUISettings() const { return m_builder.getUISettings(); }

    // ── Save API (Phase 4: GPU→disk persistence) ──
    void triggerSave(const std::string& bufferName);

    // ── Render Target Save (Screenshot) ──
    // Save any named render target image to disk (synchronous, blocks for one frame)
    // Format auto-detected from extension: .png, .jpg, .bmp, .tga (8-bit), .hdr (float)
    bool saveRenderTarget(const std::string& resourceName, const std::string& path);

    // ── Scene Binding (for declarative v3 features) ──
    // Bind an ECS scene for automatic buffer updates and built-in scene rendering
    void bindScene(ECS::Scene* scene, ResourceManager* resourceManager = nullptr);
    void unbindScene();
    ECS::Scene* getBoundScene() const { return m_boundScene; }
    bool hasSceneBound() const { return m_boundScene != nullptr; }

    // Update standard buffers from bound scene (call once per frame before execute)
    void updateStandardBuffers(float deltaTime, uint32_t frameIndex = 0);

    // Set screen extent for screen buffer updates
    void setScreenExtent(VkExtent2D extent) { m_screenExtent = extent; }
    // Set the size the graph renders at (scene.screen.renderResolution); the
    // screen extent when unset
    void setRenderExtent(VkExtent2D extent) { m_renderExtent = extent; }


    // ═══════════════════════════════════════════════════════════════
    // Entity Binding (DotPathResolver)
    // 蓮花之縛 — The binding of the lotus (automatic, effortless)
    //
    // These methods use DotPathResolver to automatically fill push
    // constants from ECS components via dot-path sources in JSON.
    // ═══════════════════════════════════════════════════════════════

    /// Update scene context (call once per frame before rendering)
    /// Populates camera, time, screen data for scene.* path resolution
    void updateSceneContext(float deltaTime);

    /// Bind entity data using dot-path resolution (MaterialComponentV5)
    /// Automatically fills push constants from entity's ECS components
    /// @param entity The entity to bind
    /// @param registry ECS registry containing the entity
    /// @param pushConstantLayout Name of the bufferLayout to use (must have sources)
    /// @param cmd Command buffer to record into
    /// @param pipelineLayout Pipeline layout for binding
    void bindEntityData(entt::entity entity,
                        entt::registry& registry,
                        const std::string& pushConstantLayout,
                        VkCommandBuffer cmd,
                        VkPipelineLayout pipelineLayout);

    /// Fill a buffer with resolved values from dot-paths
    /// Use for UBOs or any buffer that needs source-based values
    /// @param buffer Pointer to buffer memory
    /// @param layoutName Name of the bufferLayout to use
    /// @param entity Entity for entity.* paths (entt::null for scene-only)
    /// @param registry ECS registry
    void fillBuffer(void* buffer,
                    const std::string& layoutName,
                    entt::entity entity,
                    entt::registry& registry);

    /// Bind material textures directly from MaterialComponentV5
    /// Reads GPUTextures from the entity's MaterialComponentV5 component
    /// and binds them to the specified descriptor set.
    /// @param entity The entity to bind textures from
    /// @param registry ECS registry containing the entity
    /// @param descriptorSetName Name of the descriptor set layout (e.g., "materialSet")
    /// @param cmd Command buffer to record into
    /// @param pass The pass being recorded: its layout, and the set index at
    ///             which it lists descriptorSetName
    /// @param frameIndex Current frame index
    void bindMaterialTextures(entt::entity entity,
                              entt::registry& registry,
                              const std::string& descriptorSetName,
                              VkCommandBuffer cmd,
                              const CompiledPass& pass,
                              uint32_t frameIndex);

    /// Bind per-entity skeleton SSBO (bone matrices) to a descriptor set
    /// 骨之繫 — The binding of bones
    void bindSkeletonSSBO(entt::entity entity,
                          entt::registry& registry,
                          const std::string& descriptorSetName,
                          VkCommandBuffer cmd,
                          const CompiledPass& pass,
                          uint32_t frameIndex);

    // ── Per-draw binding, planned once per pass ──
    //
    // The name-based calls above look up layouts, descriptor sets and set
    // indices by name on every draw. A geometry pass plans those lookups once
    // and then binds each entity through the plan; the plan also remembers
    // the last material set bound, so draws sharing a material skip the bind.

    struct EntityBindingPlan {
        // Push constants
        const Shoonyakasha::CompiledBufferLayout* perDraw = nullptr;
        VkShaderStageFlags perDrawStages = 0;
        uint32_t           perDrawOffset = 0;

        // Material textures
        struct ImageBinding {
            uint32_t binding = 0;
            const std::string* name = nullptr;
            const Shoonyakasha::GPUTexture* fallback = nullptr;
        };
        static constexpr size_t kMaxMaterialImages = 16;
        bool                      material = false;
        std::vector<ImageBinding> materialImages;
        uint32_t                  materialSetIndex = 0;
        VkDescriptorSetLayout     materialLayout = VK_NULL_HANDLE;
        void*                     materialSets = nullptr;   // this layout's set cache
        VkDescriptorSet           lastMaterialSet = VK_NULL_HANDLE;

        // Skeleton bone buffer
        bool                  skeleton = false;
        size_t                skeletonLayoutHash = 0;
        uint32_t              skeletonSetIndex = 0;
        VkDescriptorSetLayout skeletonLayout = VK_NULL_HANDLE;
    };

    /// Look up what `pass`'s entityDataBinding names. Problems are logged
    /// here, once per pass; the parts that cannot be bound are left out.
    EntityBindingPlan planEntityBindings(const CompiledPass& pass);

    void bindEntityData(entt::entity entity, entt::registry& registry,
                        const EntityBindingPlan& plan, VkCommandBuffer cmd, VkPipelineLayout pipelineLayout);
    void bindMaterialTextures(entt::entity entity, entt::registry& registry,
                              EntityBindingPlan& plan, VkCommandBuffer cmd, VkPipelineLayout pipelineLayout);
    void bindSkeletonSSBO(entt::entity entity, entt::registry& registry,
                          EntityBindingPlan& plan, VkCommandBuffer cmd, VkPipelineLayout pipelineLayout,
                          uint32_t frameIndex);

    /// Counts recorded frames, single- and multi-queue alike; constant while
    /// a frame's passes record.
    uint64_t getFrameNumber() const { return m_recordedFrames; }

    /// Get the scene context (for external use)
    Shoonyakasha::SceneContext& getSceneContext();
    const Shoonyakasha::SceneContext& getSceneContext() const;

    /// Check if ECS binding integration is available
    bool hasECSBindingIntegration() const { return m_ecsBindingEnabled; }

    /// Enable/disable ECS binding integration
    void setECSBindingEnabled(bool enabled) { m_ecsBindingEnabled = enabled; }

    // ── Compilation ──
    bool compile(VkExtent2D referenceExtent, uint32_t swapchainImageCount = 1,
                 uint32_t maxFramesInFlight = 2);
    bool isCompiled() const { return m_compiled.valid; }

    // ── Execution ──
    /// Record the frame. `commandBuffer`, begun, for the graphics queue, gets
    /// the uploads and the scene's acceleration structures. Without compute
    /// batches in getQueueBatches() it gets every pass and the readbacks as
    /// well. With them, each batch is recorded into a command buffer of the
    /// graph's own, per frame in flight, and the readbacks into a last
    /// graphics one. Once the caller has ended `commandBuffer`, submit()
    /// submits them all.
    void execute(uint32_t frameIndex, uint32_t swapchainImageIndex,
                 VkCommandBuffer commandBuffer);

    /// Submit what execute() recorded, one submission per command buffer, in
    /// order. Each queue signals a timeline semaphore of its own after each
    /// submission. A batch waits for the batch of the other queue it names;
    /// a compute batch that names none waits for the uploads. The first
    /// submission waits for `wait` (a binary semaphore, or VK_NULL_HANDLE) at
    /// `waitStage`. The last, on the graphics queue, waits for the last
    /// compute batch and signals `signal` (or VK_NULL_HANDLE) and `fence`, so
    /// the fence covers the whole frame.
    void submit(VkSemaphore wait, VkPipelineStageFlags2 waitStage, VkSemaphore signal, VkFence fence);

    /// The passes grouped into batches by queue, in submission order.
    const std::vector<QueueBatch>& getQueueBatches() const { return m_compiled.queueBatches; }

    // ── Hot-reload ──
    bool recompile(VkExtent2D referenceExtent, uint32_t swapchainImageCount = 1,
                   uint32_t maxFramesInFlight = 2);

    // ── Query compiled state ──
    VkImageView getResourceView(const std::string& name) const;
    const FrameGraphCompiler::CompileResult& getCompileResult() const { return m_compiled; }
    const std::string& getLastError() const { return m_compiled.errorMessage; }

    // ── Query auto-created descriptor sets ──
    std::shared_ptr<VulkanDescriptorSet> getDescriptorSet(const std::string& layoutName) const;

    // ── Query compiled buffer layouts ──
    // 緩衝之構 — Get compiled buffer layouts for runtime use
    const CompiledBufferLayout* getBufferLayout(const std::string& name) const;
    bool hasBufferLayout(const std::string& name) const;
    const std::unordered_map<std::string, CompiledBufferLayout>& getBufferLayouts() const {
        return m_compiled.bufferLayouts;
    }

    // ═══════════════════════════════════════════════════════════════
    // Analysis API — 玄武司察  深潛而見真
    // The Dark Warrior investigates — diving deep to see truth
    // ═══════════════════════════════════════════════════════════════

    // Pre-compile validation (can be called before compile())
    AnalysisResult validateDeclarations() const;
    CullingReport simulateCulling() const;

    // Post-compile analysis (requires successful compile())
    AnalysisResult analyze(VkExtent2D referenceExtent = {}) const;
    CullingReport getCullingReport() const;
    std::vector<BarrierAnalysis> getBarrierAnalysis() const;
    std::vector<ResourceLifetime> getResourceLifetimes(VkExtent2D referenceExtent = {}) const;
    std::vector<DependencyEdge> getDependencyGraph() const;

    // ═══════════════════════════════════════════════════════════════
    // Export API — 青龍司生  生發而有序
    // The Azure Dragon governs growth — making the invisible visible
    // ═══════════════════════════════════════════════════════════════

    // DOT/Graphviz export
    std::string exportToDot() const;
    std::string exportToDot(const ExportOptions& options) const;
    bool exportToDotFile(const std::string& path) const;
    bool exportToDotFile(const std::string& path, const ExportOptions& options) const;

    // JSON export
    std::string exportToJson() const;
    std::string exportToJson(const ExportOptions& options) const;
    bool exportToJsonFile(const std::string& path) const;
    bool exportToJsonFile(const std::string& path, const ExportOptions& options) const;

    // Human-readable reports
    std::string generateReport() const;
    std::string generateReport(const ExportOptions& options) const;
    std::string generateSummary() const;
    std::string generateSummary(const ExportOptions& options) const;

    // ═══════════════════════════════════════════════════════════════
    // Runtime Debug API — 朱雀司變  熾熱而速達
    // The Vermilion Bird governs transformation — blazing heat, swift arrival
    // ═══════════════════════════════════════════════════════════════

    void enableDebugging();
    void disableDebugging();
    bool isDebuggingEnabled() const;


    FrameGraphDebugger* getDebugger();
    const FrameGraphDebugger* getDebugger() const;

    // ═══════════════════════════════════════════════════════════════
    // Render statistics
    // ═══════════════════════════════════════════════════════════════

    /// Collect frame rate, frame time, draw counts and per-pass CPU and GPU
    /// times (see RenderStats.h). `gpuTiming` adds timestamp queries where
    /// the device has them. SHOONYAKASHA_STATS=1 turns this on when the graph
    /// is created and prints a summary every second.
    void setStatsEnabled(bool enabled, bool gpuTiming = true);
    bool isStatsEnabled() const { return m_stats != nullptr; }
    /// Null while stats are off.
    const RenderStats* getStats() const;

    // Quick debug queries (delegate to debugger)
    bool wasPassExecuted(const std::string& passName) const;
    double getPassTime(const std::string& passName) const;
    double getFrameTime() const;

private:
    /// Build this frame's acceleration structures and bind them, when the
    /// pipeline has an "acceleration_structure" binding and the device ray
    /// queries.
    void recordRayTracingScene(VkCommandBuffer cmd, uint32_t frameIndex);

    /// Records the waiting pixel readback when `decl` is its pass.
    void recordPixelReadback(const CompiledPass& pass, const PassDeclaration& decl, uint32_t frameIndex,
                             VkCommandBuffer cmd);
    /// Reads frame in flight `frameIndex`'s pixel readback, if it holds one.
    void collectPixelReadback(uint32_t frameIndex);

    /// Clear persistent images and put them in the layouts a frame leaves
    /// them in, which is where each frame's barriers start from for them.
    void initializePersistentImages();

    /// Where `pass` lists `descriptorSetName` among its descriptor sets.
    std::optional<uint32_t> descriptorSetIndexIn(const CompiledPass& pass,
                                                 const std::string& descriptorSetName) const;

    VulkanDevice&                   m_device;
    VulkanCommandManager&           m_cmdManager;
    FrameGraphBuilder               m_builder;
    FrameGraphCompiler              m_compiler;
    FrameGraphExecutor              m_executor;
    FrameGraphCompiler::CompileResult m_compiled;

    // Analysis support (lazily created in RenderGraph.cpp)
    mutable std::unique_ptr<FrameGraphAnalyzer> m_analyzer;
    mutable std::unique_ptr<AnalysisResult> m_cachedAnalysis;

    // Runtime debugging support
    std::unique_ptr<FrameGraphDebugger> m_debugger;
    std::unique_ptr<RenderStatsCollector> m_stats;

    // Internal helper to ensure analyzer exists
    FrameGraphAnalyzer& getOrCreateAnalyzer() const;

    // Registered pass callbacks
    std::unordered_map<std::string, PassExecuteFn> m_callbacks;

    // Registered scene renderer callbacks (for "scene_geometry" execution type)
    // DEPRECATED: Use m_geometryTypeRenderers instead
    std::unordered_map<std::string, PassExecuteFn> m_sceneRenderers;

    // Geometry renderers by execution type (pipeline-agnostic!)
    // Key: "opaque", "transparent", "shadow_casters", etc.
    std::unordered_map<std::string, PassExecuteFn> m_geometryTypeRenderers;

    // setPassEnabled calls made before the pipeline was loaded
    std::unordered_map<std::string, bool> m_pendingPassEnabled;
    // applyPreset called before the pipeline was loaded
    std::string m_pendingPreset;

    // Manual pipeline overrides (hybrid mode)
    std::unordered_map<std::string, std::shared_ptr<VulkanPipeline>> m_manualPipelines;

    // Named parameters (for push constants auto-binding)
    std::unordered_map<std::string, ParameterValue> m_parameters;

    // Registered external UBOs
    struct RegisteredUBO {
        std::vector<VkBuffer> perFrameBuffers;
        VkDeviceSize size = 0;
    };
    std::unordered_map<std::string, RegisteredUBO> m_registeredUBOs;

    // Registered external SSBOs (storage buffers)
    struct RegisteredSSBO {
        std::vector<VkBuffer> buffers;  // Single buffer or per-frame
        VkDeviceSize size = 0;
    };
    std::unordered_map<std::string, RegisteredSSBO> m_registeredSSBOs;
    // Views of storage buffers bound as texel buffers, made when they are bound.
    std::vector<VkBufferView> m_texelBufferViews;

    // Framework-managed UBOs (created from JSON uniformBuffers section)
    struct ManagedUBO {
        std::vector<std::unique_ptr<VulkanBuffer>> perFrameBuffers;
        VkDeviceSize size = 0;
        std::unordered_map<std::string, uint32_t> fieldOffsets;  // field name -> byte offset
        std::unordered_map<std::string, uint32_t> fieldSizes;    // field name -> byte size
        bool dirty = false;  // Mark for update
    };
    std::unordered_map<std::string, ManagedUBO> m_managedUBOs;

    // Dot-path-driven UBOs (auto-filled from SceneContext via buffer layout sources)
    // These replace StandardBufferManager — the JSON declares buffer contents, the engine fills them
    struct DotPathUBO {
        std::vector<std::unique_ptr<VulkanBuffer>> perFrameBuffers;
        VkDeviceSize size = 0;
        std::string layoutName;                    // Reference to CompiledBufferLayout
        BufferUpdateFrequency updateFrequency;
        Shoonyakasha::CompiledBufferLayout resolvedLayout;  // Pre-converted for BufferLayoutResolver
        bool hubManaged = false;  // Phase 5b: buffer comes from ResourceHub, not framework

        // Target/readback/save policies (mirrors SSBO functionality)
        std::string target;
        MemoryPolicy memoryPolicy;
        ReadbackPolicy readbackPolicy;
        SavePolicy savePolicy;
        bool savePending = false;
    };
    std::unordered_map<std::string, DotPathUBO> m_dotPathUBOs;
    void createDotPathUBOs(uint32_t maxFramesInFlight);
    void updateDotPathUBOs(uint32_t frameIndex);

    // Dot-path-driven SSBOs (declarative storage buffers with JSON initialization)
    // JSON declares struct layout, element count, and initialization rules.
    // Engine creates device-local buffers, generates initial data, uploads via staging.
    struct DotPathSSBO {
        std::unique_ptr<VulkanBuffer> buffer;
        VkDeviceSize size = 0;
        std::string layoutName;
        uint32_t elementCount = 0;
        uint32_t elementStride = 0;

        // Phase 3: Memory and transfer policies
        MemoryPolicy memoryPolicy;
        ReadbackPolicy readbackPolicy;
        BufferUpdateFrequency updateFrequency = BufferUpdateFrequency::Manual;
        uint32_t updateFrequencyN = 1;

        // Phase 4: File save policy
        SavePolicy savePolicy;
        bool savePending = false;  // Set by triggerSave(), cleared after save
    };
    std::unordered_map<std::string, DotPathSSBO> m_dotPathSSBOs;
    void createDotPathSSBOs(uint32_t maxFramesInFlight);

    // Bytes the application set into a uniform or storage buffer
    // (setBufferData), each with a flag per byte that is set and, for storage
    // buffers, per byte still to copy in.
    struct BufferData {
        std::vector<uint8_t> bytes, isSet, toCopy;
        bool pending = false;     // toCopy has bytes
        std::vector<std::unique_ptr<VulkanBuffer>> staging;   // per frame in flight
    };
    std::unordered_map<std::string, BufferData> m_bufferData;
    /// The size of the buffer setBufferData writes for `name`, 0 if none.
    VkDeviceSize bufferDataCapacity(const std::string& name) const;
    /// After compiling: drop data that no longer fits, copy the rest in again.
    void checkBufferData();
    void recordBufferDataCopies(VkCommandBuffer cmd, uint32_t frameIndex);

    // Storage buffers filled from the scene every frame ("source": {"type":
    // "scene"}): one host-visible, host-coherent buffer per frame in flight,
    // mapped while it lives, written after that frame's fence.
    struct SceneSSBO {
        std::string data;               // "lights", or "rayInstances" (filled with the acceleration structure)
        uint32_t capacity = 0;          // elements it holds
        uint32_t elementSize = 0;
        std::vector<std::unique_ptr<VulkanBuffer>> perFrame;
    };
    std::unordered_map<std::string, SceneSSBO> m_sceneSSBOs;
    void createSceneSSBO(const BufferLayoutDesc& desc, const CompiledBufferLayout& layout,
                         uint32_t maxFramesInFlight);
    void fillSceneSSBOs(uint32_t frameIndex);

    // Phase 2: Shared buffer registry for cross-graph SSBO/UBO/image targets/references
    SharedBufferRegistry* m_sharedBufferRegistry = nullptr;
    void registerTargets();      // Register SSBOs, UBOs, and render targets in shared registry
    void resolveBufferRefs();

    // Phase 3: Staging buffer management for CPU↔GPU transfers
    std::unique_ptr<StagingBufferManager> m_stagingManager;
    std::unique_ptr<Shoonyakasha::RayTracingScene> m_rayTracingScene;
    std::vector<std::pair<std::string, std::string>> m_accelerationBindings;   // (layout, binding)
    // Bindings with "source": "sceneTextures", and per frame in flight the
    // (view, sampler) last written to each of their slots.
    struct SceneTextureBinding {
        std::string layout, binding;
        uint32_t count = 0;
        std::vector<std::vector<std::pair<VkImageView, VkSampler>>> written;
    };
    std::vector<SceneTextureBinding> m_sceneTextureBindings;
    bool m_sceneTextureBindingsFound = false;
    Shoonyakasha::SceneTextureTable m_sceneTextureTable;
    // Build this frame's scene texture table and write the slots that changed.
    void updateSceneTextures(uint32_t frameIndex);
    void writeRayTracingSceneData(uint32_t frameIndex);
    uint64_t m_globalFrameNumber = 0;
    uint64_t m_recordedFrames = 0;   // see getFrameNumber
    std::unordered_map<std::string, ReadbackCallbackFn> m_readbackCallbacks;
    void createStagingBuffers(uint32_t maxFramesInFlight);

    // Pixel readback: a mapped, host-coherent buffer per frame in flight.
    PixelReadbackQueue m_pixelReadbacks;
    std::vector<std::unique_ptr<VulkanBuffer>> m_pixelReadbackBuffers;

    // Tracked render targets for readback/save/target (image data flow)
    struct TrackedRenderTarget {
        std::string resourceName;
        std::string target;
        ReadbackPolicy readbackPolicy;
        SavePolicy savePolicy;
        bool savePending = false;
    };
    std::unordered_map<std::string, TrackedRenderTarget> m_trackedRenderTargets;

    // Scene binding
    ECS::Scene* m_boundScene = nullptr;
    ResourceManager* m_resourceManager = nullptr;
    VkExtent2D m_screenExtent = {};
    VkExtent2D m_renderExtent = {};
    float m_lastDeltaTime = 0.0f;

    uint32_t m_maxFramesInFlight = 2;

    // DotPathResolver integration for ECS binding
    // 蓮花之道 — The path of the lotus (effortless binding)
    std::unique_ptr<Shoonyakasha::DotPathResolver> m_pathResolver;
    std::unique_ptr<Shoonyakasha::BufferLayoutResolver> m_bufferResolver;
    std::unique_ptr<Shoonyakasha::SceneContext> m_sceneContext;
    std::unique_ptr<Shoonyakasha::FrameGraphRenderer> m_frameGraphRenderer;  // Auto geometry rendering
    bool m_ecsBindingEnabled = true;

    /// Resolver-side layouts, converted once and keyed by layout name.
    ///
    /// The conversion carries no entity-dependent data, so one entry serves every
    /// entity and every frame. bindEntityData used to rebuild the whole thing on
    /// every draw call — a layout struct, one BufferField per field, and two
    /// std::string allocations each — roughly 16 heap allocations per draw.
    ///
    /// Cleared at the top of compile(), which is where m_compiled is overwritten.
    /// unordered_map never invalidates references to existing elements, so the
    /// pointers handed out below stay valid.
    mutable std::unordered_map<std::string, Shoonyakasha::CompiledBufferLayout> m_resolvedLayoutCache;

    /// Cached conversion of a compiled layout, converting on first use.
    /// Returns nullptr if the layout name is unknown.
    const Shoonyakasha::CompiledBufferLayout* getResolvedLayout(const std::string& name) const;

    // Material Descriptor Cache - per-entity texture descriptor sets
    // Key: (entity_id, layoutName_hash, frameIndex) -> VkDescriptorSet
    struct MaterialDescriptorCacheKey {
        uint32_t entityId;
        size_t layoutHash;
        uint32_t frameIndex;

        bool operator==(const MaterialDescriptorCacheKey& other) const {
            return entityId == other.entityId &&
                   layoutHash == other.layoutHash &&
                   frameIndex == other.frameIndex;
        }
    };
    struct MaterialDescriptorCacheKeyHash {
        size_t operator()(const MaterialDescriptorCacheKey& k) const {
            size_t h = std::hash<uint32_t>{}(k.entityId);
            h ^= k.layoutHash + 0x9e3779b9 + (h << 6) + (h >> 2);
            h ^= std::hash<uint32_t>{}(k.frameIndex) + 0x9e3779b9 + (h << 6) + (h >> 2);
            return h;
        }
    };
    std::unordered_map<MaterialDescriptorCacheKey, VkDescriptorSet, MaterialDescriptorCacheKeyHash> m_materialDescriptorCache;
    /// The bone buffer each cached skeleton set was last written with. A set
    /// is rewritten only when that changes: updating a set that an earlier
    /// pass of the frame bound invalidates the command buffer.
    std::unordered_map<VkDescriptorSet, std::pair<VkBuffer, VkDeviceSize>> m_skeletonSetBuffers;
    VkDescriptorPool m_materialDescriptorPool = VK_NULL_HANDLE;
    /// Earlier material pools that ran out of room. Their sets stay live; a new
    /// pool takes the next allocations.
    std::vector<VkDescriptorPool> m_fullMaterialDescriptorPools;
    /// The pool each per-entity set came from, to free it back there.
    std::unordered_map<VkDescriptorSet, VkDescriptorPool> m_descriptorSetPools;
    /// Material texture sets, shared by every entity whose material binds the
    /// same textures under the same layout. Written once, never changed.
    /// Keyed by layout name, then by the textures in a fixed-size key, so a
    /// draw finds its set without building a string.
    struct MaterialTexture {
        uint32_t binding = 0;
        VkImageView view = VK_NULL_HANDLE;
        VkSampler sampler = VK_NULL_HANDLE;
        bool operator==(const MaterialTexture&) const = default;
    };
    struct MaterialTextureKey {
        uint32_t count = 0;
        std::array<MaterialTexture, EntityBindingPlan::kMaxMaterialImages> textures{};
        bool operator==(const MaterialTextureKey& o) const {
            return count == o.count && std::equal(textures.begin(), textures.begin() + count, o.textures.begin());
        }
    };
    struct MaterialTextureKeyHash {
        size_t operator()(const MaterialTextureKey& k) const {
            size_t h = k.count;
            for (uint32_t i = 0; i < k.count; ++i) {
                for (size_t v : {size_t{k.textures[i].binding}, reinterpret_cast<size_t>(k.textures[i].view),
                                 reinterpret_cast<size_t>(k.textures[i].sampler)}) {
                    h ^= std::hash<size_t>{}(v) + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
                }
            }
            return h;
        }
    };
    using MaterialSetMap = std::unordered_map<MaterialTextureKey, VkDescriptorSet, MaterialTextureKeyHash>;
    std::unordered_map<std::string, MaterialSetMap> m_materialTextureSets;

    /// The compiled layout of the descriptor set `name`, from any pass that
    /// lists it.
    VkDescriptorSetLayout compiledSetLayout(const std::string& name) const;
    void ensureDefaultTextures();
    bool planPerDraw(const std::string& name, EntityBindingPlan& plan);
    bool planMaterial(const std::string& name, const CompiledPass& pass, EntityBindingPlan& plan);
    bool planSkeleton(const std::string& name, const CompiledPass& pass, EntityBindingPlan& plan);

    // Default textures for fallback when material textures are missing
    Shoonyakasha::GPUResourceFactory::DefaultTextures m_defaultTextures;
    bool m_defaultTexturesCreated = false;

    // Initialize ECS binding systems (called from constructor)
    void initSystems();

    // Create descriptor pool for material textures
    void createMaterialDescriptorPool(uint32_t maxSets = 4096);
    // Destroys every material pool, and with them every set allocated from them.
    void destroyMaterialDescriptorPools();
    // A set with `layout` from the material pools, chaining a new pool when the
    // current one is full. VK_NULL_HANDLE on failure.
    VkDescriptorSet allocateMaterialDescriptorSet(VkDescriptorSetLayout layout);

    // Frees the cached descriptor sets of entities that are no longer valid in
    // the bound scene. Only sets cached for frameIndex are freed, because the
    // caller has waited on that frame's fence and the GPU no longer reads them.
    void releaseDestroyedEntityDescriptors(uint32_t frameIndex);

    // createStandardBuffers removed — replaced by createDotPathUBOs

    // Imported resource data — supports per-swapchain-image entries
    struct ImportedImageData {
        VkFormat    format = VK_FORMAT_UNDEFINED;
        VkExtent2D  extent = {};
        struct Entry {
            VkImage     image = VK_NULL_HANDLE;
            VkImageView view  = VK_NULL_HANDLE;
        };
        std::vector<Entry> entries;  // [swapchainIndex] for swapchain; [0] for others
    };
    struct ImportedBufferData {
        VkBuffer    buffer = VK_NULL_HANDLE;
        VkDeviceSize size = 0;
    };
    std::unordered_map<std::string, ImportedImageData>  m_importedImages;
    std::unordered_map<std::string, ImportedBufferData> m_importedBuffers;

    // Build import map for compiler (resource index → ImportedImageInfo)
    ImportedImageMap buildImportMap() const;

    // Create framework-managed UBOs from builder declarations
    void createManagedUBOs(uint32_t maxFramesInFlight);

    // Bind UBOs to descriptor sets after compilation (handles autoBindBuffer)
    void bindUBOsToDescriptorSets(uint32_t maxFramesInFlight);

    // Application images (setExternalImage): what each name holds, and the
    // frames in flight whose descriptor sets do not have it yet (a bit each).
    struct ExternalImage {
        VkImageView view = VK_NULL_HANDLE;
        VkSampler sampler = VK_NULL_HANDLE;
        uint32_t pendingFrames = 0;
    };
    std::unordered_map<std::string, ExternalImage> m_externalImages;
    bool m_externalImagesPending = false;

    // Write the application's images, or white, into frame `frame`'s
    // descriptor sets: every "externalImage" binding, or only those whose
    // image changed since that frame last took it.
    void bindExternalImages(uint32_t frame, bool onlyPending);

    // Helper template for writing to managed UBOs
    template<typename T>
    void writeUniformField(const std::string& uboName, const std::string& fieldName, const T& value);

    // Apply callbacks and per-frame imports
    void applyCallbacks();
    void applyImports(uint32_t swapchainImageIndex);

    // Set up auto geometry renderers for passes with entityDataBinding
    void setupAutoGeometryRenderers();

    // Multi-queue synchronization: a timeline semaphore per QueueType, and
    // the value it was last signalled with
    VkSemaphore m_timelines[2] = {VK_NULL_HANDLE, VK_NULL_HANDLE};
    uint64_t    m_timelineValues[2] = {0, 0};

    /// The command buffers execute() recorded this frame, in submission
    /// order; the first is the caller's. `wait` is the segment on the other
    /// queue to wait for, -1 for none; `value` is set when submitted.
    struct FrameSegment {
        QueueType       queue = QueueType::Graphics;
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        int32_t         wait = -1;
        uint64_t        value = 0;
    };
    std::vector<FrameSegment> m_frameSegments;
    /// Command buffers for segments after the first, per frame in flight,
    /// indexed by QueueType.
    std::vector<std::vector<VkCommandBuffer>> m_segmentBuffers[2];
    /// Begin the `n`th segment command buffer of `queue` for `frameIndex`.
    VkCommandBuffer beginSegment(QueueType queue, uint32_t frameIndex, uint32_t n);

    void createSyncPrimitives();
    void destroySyncPrimitives();

    Logger* m_logger = nullptr;
};

} // namespace FrameGraph
} // namespace Shoonyakasha

// Backward compatibility alias
namespace FrameGraph = Shoonyakasha::FrameGraph;

// Backward compatibility — types formerly at global scope, now in Shoonyakasha::FrameGraph
using Shoonyakasha::FrameGraph::BufferFieldType;
using Shoonyakasha::FrameGraph::BufferFieldDesc;
using Shoonyakasha::FrameGraph::BufferPackingRule;
using Shoonyakasha::FrameGraph::BufferUsageType;
using Shoonyakasha::FrameGraph::BufferUpdateFrequency;
using Shoonyakasha::FrameGraph::TransferDirection;
using Shoonyakasha::FrameGraph::StagingStrategy;
using Shoonyakasha::FrameGraph::MemoryLocation;
using Shoonyakasha::FrameGraph::BufferBindingDesc;
using Shoonyakasha::FrameGraph::TextureBindingDesc;
using Shoonyakasha::FrameGraph::SSBOFieldInitType;
using Shoonyakasha::FrameGraph::SSBOFieldInit;
using Shoonyakasha::FrameGraph::SSBOInitConfig;
using Shoonyakasha::FrameGraph::MemoryPolicy;
using Shoonyakasha::FrameGraph::ReadbackPolicy;
using Shoonyakasha::FrameGraph::ReadbackResult;
using Shoonyakasha::FrameGraph::ReadbackCallbackFn;
using Shoonyakasha::FrameGraph::SavePolicy;
using Shoonyakasha::FrameGraph::BufferLayoutDesc;
// NOTE: CompiledBufferLayout NOT aliased — name collision with Shoonyakasha::CompiledBufferLayout
// in DotPathResolver.h. Use FrameGraph::CompiledBufferLayout or Shoonyakasha::CompiledBufferLayout explicitly.
using Shoonyakasha::FrameGraph::PerDrawBindingConfig;
using Shoonyakasha::FrameGraph::MaterialBindingConfig;
using Shoonyakasha::FrameGraph::SkeletonBindingConfig;
using Shoonyakasha::FrameGraph::EntityDataBindingConfig;
