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
#include "VertexFormatRegistry.h"  // Declarative vertex format definitions
#include "FrameGraph/DotPathResolver.h"  // For Shoonyakasha::CompiledBufferLayout in DotPathUBO
#include "FrameGraph/SharedBufferRegistry.h"  // Phase 2: cross-graph SSBO sharing
#include "GPU/GPUResourceFactory.h"  // Default textures for fallback

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <entt/entt.hpp>
#include <vector>
#include <unordered_map>
#include <memory>
#include <string>
#include <variant>
#include <optional>
#include <functional>

// Forward declarations
namespace Shoonyakasha {
class VulkanDevice;
class VulkanRenderPass;
class VulkanPipeline;
class VulkanComputePipeline;
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

/// Field type for buffer layouts (maps to ShaderData FieldType)
/// JSON: "float", "vec2", "vec3", "vec4", "mat4", etc.
enum class BufferFieldType {
    Float, Double, Int, UInt, Bool,
    Vec2, Vec3, Vec4,
    IVec2, IVec3, IVec4,
    UVec2, UVec3, UVec4,
    Mat2, Mat3, Mat4
};

/// Single field in a buffer layout
struct BufferFieldDesc {
    std::string name;                       // Field name (e.g., "model", "baseColorFactor")
    BufferFieldType type = BufferFieldType::Float;
    uint32_t arrayCount = 1;                // Array size (1 = not an array)
    uint32_t offset = 0;                    // Explicit offset (0 = auto-calculate)
    uint32_t arrayStride = 0;               // Stride between array elements (0 = auto from packing)

    // Dot-path source for automatic value resolution via DotPathResolver
    // JSON: "source": "entity.material.params.baseColorFactor"
    std::string source;                     // Dot-path source (e.g., "entity.transform.worldMatrix")
};

/// Packing rule for buffer layouts (maps to ShaderDataLayout::PackingRule)
/// JSON: "std140", "std430", "scalar", "push_constant"
enum class BufferPackingRule {
    Std140,         // UBO standard packing
    Std430,         // SSBO tighter packing
    Scalar,         // VK_EXT_scalar_block_layout
    PushConstant    // Push constant layout (≤128 bytes)
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
    std::string type = "none";       // "none", "initializer", "buffer_ref", "file"
    uint32_t seed = 42;
    std::vector<SSBOFieldInit> fieldInits;

    // Phase 2: buffer_ref source — reference another graph's target SSBO
    std::string bufferRef;                        // Target name to look up in SharedBufferRegistry
    std::string bufferRefFrequency = "per_frame"; // "once", "per_frame"

    // Phase 4: file source — load binary data from disk
    std::string filePath;                         // Path for "file" source type
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

    /// Check if this layout uses dot-path sources
    bool usesDotPathSources() const { return hasSceneSources || hasEntitySources || hasConstSources; }

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

struct PhysicalImage {
    std::unique_ptr<VulkanImage> ownedImage;    // null if imported
    VkImage         vkImage  = VK_NULL_HANDLE;
    VkImageView     view     = VK_NULL_HANDLE;
    VkFormat        format   = VK_FORMAT_UNDEFINED;
    VkExtent2D      extent   = {};
    VkImageLayout   currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
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

struct BarrierInfo {
    ResourceHandle          resource;
    VkImageLayout           oldLayout;
    VkImageLayout           newLayout;
    VkPipelineStageFlags    srcStage;
    VkPipelineStageFlags    dstStage;
    VkAccessFlags           srcAccess;
    VkAccessFlags           dstAccess;

    // Queue ownership transfer (VK_QUEUE_FAMILY_IGNORED = no transfer)
    uint32_t                srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    uint32_t                dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
};

struct CompiledPass {
    uint32_t                            declIndex;          // into pass declarations
    std::shared_ptr<VulkanRenderPass>   renderPass;         // null for compute/transfer
    std::vector<VkFramebuffer>          framebuffers;       // [swapchainImageIndex] or single entry
    std::vector<VkClearValue>           clearValues;
    VkExtent2D                          extent{};

    // Barriers to insert BEFORE this pass executes
    std::vector<BarrierInfo>            preBarriers;

    // Auto-created descriptor set layouts (ordered by pass's descriptorSetRefs)
    std::vector<VkDescriptorSetLayout>  descriptorSetLayouts;
    std::vector<std::shared_ptr<VulkanDescriptorSet>> descriptorSets;

    // Auto-created pipeline (null if manually managed or no shaders specified)
    std::shared_ptr<VulkanPipeline>     pipeline;
    VkPipelineLayout                    pipelineLayout = VK_NULL_HANDLE;

    // Compute pipeline (for compute passes — separate from graphics pipeline)
    std::shared_ptr<VulkanComputePipeline> computePipeline;

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

// ═══════════════════════════════════════════════════════════════
// Multi-Queue Synchronization Primitives
// 雙流並行  計算與繪製各得其道
// ═══════════════════════════════════════════════════════════════

struct SyncPoint {
    uint32_t signalQueue;     // queue family that signals
    uint32_t waitQueue;       // queue family that waits
    uint64_t timelineValue;   // timeline semaphore value
};

struct QueueSubmitBatch {
    // Passes grouped by queue, in submission order
    // Each entry: (queue type, list of pass indices in that batch)
    std::vector<std::pair<QueueType, std::vector<uint32_t>>> batches;

    // Sync points between batches
    std::vector<SyncPoint> syncPoints;
};

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

        // Multi-queue execution batches and sync points
        QueueSubmitBatch                queueBatches;

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

private:
    // ── Compilation stages ──
    bool topologicalSort(
        const std::vector<PassDeclaration>& passes,
        const std::vector<ResourceDeclaration>& resources,
        std::vector<uint32_t>& outOrder,
        std::string& outError);

    void cullDeadPasses(
        std::vector<uint32_t>& order,
        const std::vector<PassDeclaration>& passes,
        const std::vector<ResourceDeclaration>& resources);

    void createPhysicalResources(
        VulkanDevice& device,
        const std::vector<ResourceDeclaration>& declarations,
        const std::vector<PassDeclaration>& passes,
        std::vector<PhysicalResource>& outResources,
        VkExtent2D referenceExtent,
        const ImportedImageMap& importedImages);

    void resolveLayoutsAndInsertBarriers(
        VulkanDevice& device,
        std::vector<CompiledPass>& compiledPasses,
        const std::vector<uint32_t>& executionOrder,
        const std::vector<PassDeclaration>& passes,
        std::vector<PhysicalResource>& physResources);

    void createRenderPasses(
        VulkanDevice& device,
        std::vector<CompiledPass>& compiledPasses,
        const std::vector<uint32_t>& executionOrder,
        const std::vector<PassDeclaration>& passes,
        const std::vector<PhysicalResource>& physResources);

    void createFramebuffers(
        VulkanDevice& device,
        std::vector<CompiledPass>& compiledPasses,
        const std::vector<uint32_t>& executionOrder,
        const std::vector<PassDeclaration>& passes,
        const std::vector<PhysicalResource>& physResources,
        uint32_t swapchainImageCount,
        const ImportedImageMap& importedImages);

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
    void createPipelines(
        VulkanDevice& device,
        std::vector<CompiledPass>& compiledPasses,
        const std::vector<uint32_t>& executionOrder,
        const std::vector<PassDeclaration>& passes,
        const std::vector<PhysicalResource>& physResources,
        const std::unordered_map<std::string, std::shared_ptr<VulkanPipeline>>& manualOverrides,
        const VertexFormatRegistry& vertexFormats);

    // ── Stage 10: Multi-queue batch generation ──
    void generateQueueBatches(
        VulkanDevice& device,
        const std::vector<CompiledPass>& compiledPasses,
        const std::vector<uint32_t>& executionOrder,
        const std::vector<PassDeclaration>& passes,
        QueueSubmitBatch& outBatches);

    // ── Stage 11: Buffer layout compilation ──
    // 緩衝之構 — Transform JSON BufferLayoutDesc into compiled layouts
    void compileBufferLayouts(
        const std::vector<BufferLayoutDesc>& layoutDescs,
        std::unordered_map<std::string, CompiledBufferLayout>& outLayouts);

    // ── Helpers ──
    static VkImageLayout usageToLayout(ResourceUsage usage);
    static VkPipelineStageFlags usageToStageMask(ResourceUsage usage, PassType passType);
    static VkAccessFlags usageToAccessMask(ResourceUsage usage);
    static VkImageUsageFlags usageToImageUsageFlags(ResourceUsage usage);
    static VkCullModeFlags stringToCullMode(const std::string& str);
    static VkPrimitiveTopology stringToTopology(const std::string& str);
    static VkBlendFactor stringToBlendFactor(const std::string& str);
    static VkBlendOp stringToBlendOp(const std::string& str);

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

    /// Single command-buffer execution (existing behavior — all passes on one queue)
    void execute(
        const FrameGraphCompiler::CompileResult& compiled,
        const FrameGraphBuilder& builder,
        uint32_t frameIndex,
        uint32_t swapchainImageIndex,
        VkCommandBuffer commandBuffer,
        const std::unordered_map<std::string, ParameterValue>* parameters = nullptr
    );

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
    Logger*                 m_logger = nullptr;

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
    std::vector<ReadbackResult> pollReadbacks();

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
    /// @param pipelineLayout Pipeline layout for binding
    /// @param frameIndex Current frame index
    void bindMaterialTextures(entt::entity entity,
                              entt::registry& registry,
                              const std::string& descriptorSetName,
                              VkCommandBuffer cmd,
                              VkPipelineLayout pipelineLayout,
                              uint32_t frameIndex);

    /// Bind per-entity skeleton SSBO (bone matrices) to a descriptor set
    /// 骨之繫 — The binding of bones
    void bindSkeletonSSBO(entt::entity entity,
                          entt::registry& registry,
                          const std::string& descriptorSetName,
                          VkCommandBuffer cmd,
                          VkPipelineLayout pipelineLayout,
                          uint32_t frameIndex);

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
    /// Single command-buffer execution (existing API — all on graphics queue)
    void execute(uint32_t frameIndex, uint32_t swapchainImageIndex,
                 VkCommandBuffer commandBuffer);

    /// Multi-queue execution with separate graphics and compute command buffers
    /// graphicsCmdBuf and computeCmdBuf must be already begun
    void executeMultiQueue(uint32_t frameIndex, uint32_t swapchainImageIndex,
                           VkCommandBuffer graphicsCmdBuf, VkCommandBuffer computeCmdBuf);

    /// Get timeline semaphore sync info for multi-queue submission
    const QueueSubmitBatch& getQueueBatches() const { return m_compiled.queueBatches; }
    bool needsMultiQueueSubmit() const;

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

    void enableGpuTiming(uint32_t queryPoolSize = 256);
    void disableGpuTiming();

    FrameGraphDebugger* getDebugger();
    const FrameGraphDebugger* getDebugger() const;

    // Quick debug queries (delegate to debugger)
    bool wasPassExecuted(const std::string& passName) const;
    double getPassTime(const std::string& passName) const;
    double getFrameTime() const;

private:
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

    // Phase 2: Shared buffer registry for cross-graph SSBO/UBO/image targets/references
    SharedBufferRegistry* m_sharedBufferRegistry = nullptr;
    void registerTargets();      // Register SSBOs, UBOs, and render targets in shared registry
    void resolveBufferRefs();

    // Phase 3: Staging buffer management for CPU↔GPU transfers
    std::unique_ptr<StagingBufferManager> m_stagingManager;
    uint64_t m_globalFrameNumber = 0;
    std::unordered_map<std::string, ReadbackCallbackFn> m_readbackCallbacks;
    void createStagingBuffers(uint32_t maxFramesInFlight);

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
    float m_lastDeltaTime = 0.0f;

    uint32_t m_maxFramesInFlight = 2;

    // DotPathResolver integration for ECS binding
    // 蓮花之道 — The path of the lotus (effortless binding)
    std::unique_ptr<Shoonyakasha::DotPathResolver> m_pathResolver;
    std::unique_ptr<Shoonyakasha::BufferLayoutResolver> m_bufferResolver;
    std::unique_ptr<Shoonyakasha::SceneContext> m_sceneContext;
    std::unique_ptr<Shoonyakasha::FrameGraphRenderer> m_frameGraphRenderer;  // Auto geometry rendering
    bool m_ecsBindingEnabled = true;

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
    VkDescriptorPool m_materialDescriptorPool = VK_NULL_HANDLE;

    // Default textures for fallback when material textures are missing
    Shoonyakasha::GPUResourceFactory::DefaultTextures m_defaultTextures;
    bool m_defaultTexturesCreated = false;

    // Initialize ECS binding systems (called from constructor)
    void initSystems();

    // Create descriptor pool for material textures
    void createMaterialDescriptorPool(uint32_t maxSets = 4096);

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

    // Helper template for writing to managed UBOs
    template<typename T>
    void writeUniformField(const std::string& uboName, const std::string& fieldName, const T& value);

    // Apply callbacks and per-frame imports
    void applyCallbacks();
    void applyImports(uint32_t swapchainImageIndex);

    // Set up auto geometry renderers for passes with entityDataBinding
    void setupAutoGeometryRenderers();

    // Multi-queue synchronization
    VkSemaphore m_timelineSemaphore = VK_NULL_HANDLE;
    uint64_t    m_timelineValue = 0;

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
