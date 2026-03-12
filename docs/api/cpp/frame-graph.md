# Frame Graph

`Shoonyakasha::FrameGraph::RenderGraph`, `DotPathResolver`, `FrameGraphRenderer` -- the declarative JSON-driven rendering pipeline that compiles render pass graphs into Vulkan execution.

> **JSON is the engine.** The Frame Graph lets you declare resources, passes, bindings, and execution in JSON. The RenderGraph compiles it to Vulkan objects. The FrameGraphRenderer executes geometry passes automatically.

**Headers:**
- `#include "Vulkan/FrameGraph/FrameGraphJson.h"` -- JSON load/save
- `#include "Vulkan/FrameGraph/FrameGraphResource.h"` -- resource types
- `#include "Vulkan/FrameGraph/FrameGraphPass.h"` -- pass types, pipeline config, execution
- `#include "FrameGraph/DotPathResolver.h"` -- dot-path resolution
- `#include "FrameGraph/FrameGraphRenderer.h"` -- automatic entity rendering

**Namespace:** `Shoonyakasha::FrameGraph` (resources, passes, JSON), `Shoonyakasha` (renderer, dot-path)

---

## RenderGraph JSON Loading

Functions to populate a `FrameGraphBuilder` from JSON or serialize it back.

### loadGraphFromJson

```cpp
void loadGraphFromJson(FrameGraphBuilder& builder, const nlohmann::json& json);
```

Populates `builder` with resources, passes, descriptor set layouts, samplers, uniform buffers, and pipeline configuration declared in the JSON object. The JSON structure mirrors the `FrameGraphBuilder` API -- resources, passes, and their bindings are all declared in one document.

### loadGraphFromFile

```cpp
void loadGraphFromFile(FrameGraphBuilder& builder, const std::string& filePath);
```

Reads a JSON file from disk and calls `loadGraphFromJson`. Throws on file-not-found or parse errors.

### saveGraphToJson

```cpp
nlohmann::json saveGraphToJson(const FrameGraphBuilder& builder);
```

Serializes the builder's current state (resources, passes, layouts, samplers, pipelines) to a `nlohmann::json` object. Useful for saving modified graphs or generating templates.

### saveGraphToFile

```cpp
void saveGraphToFile(const FrameGraphBuilder& builder, const std::string& filePath);
```

Serializes the builder to JSON and writes it to disk.

### Example

```cpp
FrameGraph::FrameGraphBuilder builder;
FrameGraph::loadGraphFromFile(builder, "pipelines/pbr_forward.json");

// Modify programmatically if needed...

// Save a copy
FrameGraph::saveGraphToFile(builder, "pipelines/pbr_forward_modified.json");
```

---

## JSON String Conversion Utilities

The `FrameGraph::JsonUtils` namespace provides bidirectional string-to-enum conversion for all types used in JSON serialization.

```cpp
namespace JsonUtils {
    VkFormat           stringToFormat(const std::string& str);
    std::string        formatToString(VkFormat format);

    ResourceUsage      stringToResourceUsage(const std::string& str);
    std::string        resourceUsageToString(ResourceUsage usage);

    PassType           stringToPassType(const std::string& str);
    std::string        passTypeToString(PassType type);

    ResourceKind       stringToResourceKind(const std::string& str);
    std::string        resourceKindToString(ResourceKind kind);

    QueueType          stringToQueueType(const std::string& str);
    std::string        queueTypeToString(QueueType type);

    VkShaderStageFlags stringToShaderStage(const std::string& str);
    VkShaderStageFlags stringsToShaderStages(const std::vector<std::string>& strs);

    VkDescriptorType   stringToDescriptorType(const std::string& str);
    std::string        descriptorTypeToString(VkDescriptorType type);

    // Sampler parameters
    VkFilter              stringToFilter(const std::string& str);
    VkSamplerMipmapMode   stringToMipmapMode(const std::string& str);
    VkSamplerAddressMode  stringToAddressMode(const std::string& str);
    VkBorderColor         stringToBorderColor(const std::string& str);
    VkCompareOp           stringToCompareOp(const std::string& str);
}
```

---

## Key Enums

### PassType

Determines the Vulkan execution model for a pass.

```cpp
enum class PassType {
    Graphics,   // Needs a VkRenderPass + framebuffer
    Compute,    // Dispatches only, no render pass
    Transfer    // Copies / blits (readback, mipmap generation)
};
```

| Value | Description |
|-------|-------------|
| `Graphics` | Rasterization pass. A VkRenderPass and framebuffer are created from the pass's outputs. |
| `Compute` | Compute-only pass. No render pass, only dispatch calls. |
| `Transfer` | Copy/blit operations. Used for readback, mipmap generation, resource transfers. |

### ResourceKind

Distinguishes image and buffer resources in the graph.

```cpp
enum class ResourceKind {
    Image,
    Buffer
};
```

### ResourceUsage

Declares how a pass accesses a resource. The graph uses this to insert barriers and determine image layouts.

```cpp
enum class ResourceUsage {
    ColorAttachmentWrite,    // Render target output
    ColorAttachmentBlend,    // Color attachment with alpha blending (read-modify-write)
    DepthStencilWrite,       // Depth buffer output
    DepthStencilReadOnly,    // Depth read (shadow sampling)
    ShaderReadOnly,          // Sampled image / UBO / SSBO read
    ShaderReadWrite,         // Storage image / SSBO read+write
    StorageImageWrite,       // Storage image write-only (compute output)
    InputAttachment,         // Vulkan input attachment (subpass read)
    TransferSrc,             // Copy source
    TransferDst,             // Copy destination
    Present                  // Swapchain image presentation
};
```

| Value | Layout | Typical Use |
|-------|--------|-------------|
| `ColorAttachmentWrite` | `COLOR_ATTACHMENT_OPTIMAL` | Writing to a render target |
| `ColorAttachmentBlend` | `COLOR_ATTACHMENT_OPTIMAL` | Transparent pass with alpha blending |
| `DepthStencilWrite` | `DEPTH_STENCIL_ATTACHMENT_OPTIMAL` | Depth buffer write |
| `DepthStencilReadOnly` | `DEPTH_STENCIL_READ_ONLY_OPTIMAL` | Shadow map sampling |
| `ShaderReadOnly` | `SHADER_READ_ONLY_OPTIMAL` | Sampling a texture or reading a UBO |
| `ShaderReadWrite` | `GENERAL` | Storage image/SSBO read-modify-write |
| `StorageImageWrite` | `GENERAL` | Compute shader writing an image |
| `InputAttachment` | `SHADER_READ_ONLY_OPTIMAL` | Vulkan subpass input attachment |
| `TransferSrc` | `TRANSFER_SRC_OPTIMAL` | Source for a copy/blit |
| `TransferDst` | `TRANSFER_DST_OPTIMAL` | Destination for a copy/blit |
| `Present` | `PRESENT_SRC_KHR` | Swapchain presentation |

### QueueType

Which Vulkan queue a pass executes on.

```cpp
enum class QueueType : uint32_t {
    Graphics = 0,
    Compute  = 1
};
```

---

## Resource Declarations

### ResourceHandle

Lightweight typed reference into the graph. Used to refer to resources in pass inputs/outputs.

```cpp
struct ResourceHandle {
    uint32_t index;

    bool valid() const;
    auto operator<=>(const ResourceHandle&) const = default;
    bool operator==(const ResourceHandle&) const = default;
};
```

### ImageDesc

Declares the properties of an image resource.

```cpp
struct ImageDesc {
    uint32_t width  = 0;            // 0 = match reference extent
    uint32_t height = 0;            // 0 = match reference extent
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
    VkImageUsageFlags additionalUsage = 0;

    float widthScale  = 1.0f;       // Scale relative to reference extent
    float heightScale = 1.0f;

    uint32_t mipLevels   = 1;
    uint32_t arrayLayers = 1;

    bool transient = false;         // For future transient aliasing
};
```

When `width` and `height` are `0`, the image size is derived from the reference extent (typically the swapchain extent) multiplied by `widthScale`/`heightScale`. This allows resolution-independent render targets.

### BufferDesc

Declares the properties of a buffer resource.

```cpp
struct BufferDesc {
    VkDeviceSize size = 0;
    VkBufferUsageFlags usage = 0;
    bool persistentlyMapped = false;
};
```

### ResourceDeclaration

A named resource in the graph with optional readback and save policies.

```cpp
struct ResourceDeclaration {
    std::string     name;
    ResourceKind    kind = ResourceKind::Image;
    ImageDesc       imageDesc{};
    BufferDesc      bufferDesc{};
    bool            imported = false;

    std::string     target;         // Register in shared resource registry

    ResourceReadbackPolicy readbackPolicy;
    ResourceSavePolicy     savePolicy;
};
```

| Field | Description |
|-------|-------------|
| `name` | Unique name within the graph (referenced by passes). |
| `kind` | `Image` or `Buffer`. |
| `imported` | `true` for externally-owned resources (swapchain, external UBOs). |
| `target` | If non-empty, registers the resource in a shared registry for cross-graph access. |
| `readbackPolicy` | Configuration for GPU-to-CPU readback (frequency, ring buffer depth, callback). |
| `savePolicy` | Configuration for saving resource data to disk (path, trigger, frequency). |

---

## Pass Declarations

### ResourceAccess

Declares how a pass references a resource.

```cpp
struct ResourceAccess {
    ResourceHandle  handle;
    ResourceUsage   usage;

    bool            hasClearValue = false;
    VkClearValue    clearValue{};

    // Resolved at compile time:
    VkImageLayout        requiredLayout;
    VkPipelineStageFlags stageMask;
    VkAccessFlags        accessMask;
};
```

### PipelineDesc

JSON-serializable pipeline state for a pass.

```cpp
struct PipelineDesc {
    std::string vertexShader;
    std::string fragmentShader;
    std::string computeShader;

    bool depthTest  = true;
    bool depthWrite = true;
    std::string cullMode    = "back";           // "none", "front", "back", "front_and_back"
    std::string blending    = "none";           // "none", "alpha", "additive"
    std::string topology    = "triangle_list";  // "triangle_list", "triangle_strip", "line_list", "point_list"
    std::string vertexInput = "default";        // "default" (Vertex type), "none" (fullscreen)
    bool wireframe = false;
};
```

| Field | Values | Description |
|-------|--------|-------------|
| `cullMode` | `"none"`, `"front"`, `"back"`, `"front_and_back"` | Face culling mode |
| `blending` | `"none"`, `"alpha"`, `"additive"` | Blend mode |
| `topology` | `"triangle_list"`, `"triangle_strip"`, `"line_list"`, `"point_list"` | Primitive topology |
| `vertexInput` | `"default"`, `"none"` | `"default"` uses the engine Vertex type; `"none"` for fullscreen passes |

### SamplerDesc

JSON-declared sampler configuration.

```cpp
struct SamplerDesc {
    std::string name;

    std::string magFilter    = "linear";       // "linear", "nearest"
    std::string minFilter    = "linear";
    std::string mipmapMode   = "linear";       // "linear", "nearest"

    std::string addressModeU = "repeat";       // "repeat", "clamp_to_edge", "clamp_to_border", "mirrored_repeat"
    std::string addressModeV = "repeat";
    std::string addressModeW = "repeat";
    std::string addressMode  = "";             // Shorthand: sets U, V, W together

    std::string borderColor  = "float_opaque_black";

    bool  anisotropyEnable = false;
    float maxAnisotropy    = 1.0f;

    bool        compareEnable = false;
    std::string compareOp     = "less";

    float minLod     = 0.0f;
    float maxLod     = 0.0f;
    float mipLodBias = 0.0f;
};
```

### UniformBufferDesc

JSON-declared uniform buffer with optional field layout.

```cpp
struct UniformFieldDesc {
    std::string name;       // Field name for setUniformField()
    std::string type;       // "float", "int", "vec2", "vec3", "vec4", "mat4"
    uint32_t    offset;     // Byte offset within UBO
    uint32_t    size;       // Auto-calculated from type if not specified
};

struct UniformBufferDesc {
    std::string name;
    uint32_t    size = 0;                   // Total buffer size in bytes
    bool        perFrame = true;            // One buffer per frame in flight
    bool        frameworkManaged = false;    // Framework creates/owns the buffer
    std::vector<UniformFieldDesc> fields;   // Optional field layout
};
```

### DescriptorSetLayoutDesc

JSON-declared descriptor set layout with auto-binding support.

```cpp
struct DescriptorBindingDesc {
    uint32_t                 binding = 0;
    std::string              type;          // "uniform_buffer", "combined_image_sampler", etc.
    std::vector<std::string> stages;        // "vertex", "fragment", "compute"
    uint32_t                 count = 1;
    std::string              name;          // Optional name for resource lookup

    // Auto-binding: resolve at compile time
    std::string autoBindResource;           // Graph image resource name
    std::string autoBindSampler;            // Graph sampler name
    std::string autoBindBuffer;             // Buffer name (for external UBOs)
};

struct DescriptorSetLayoutDesc {
    std::string                        name;
    std::vector<DescriptorBindingDesc> bindings;
};
```

### PushConstantDesc

Push constant range with named parameter bindings.

```cpp
struct PushConstantBindingDesc {
    std::string name;       // Parameter name (e.g., "elapsedTime")
    uint32_t    offset;     // Offset in push constant range
    std::string type;       // "float", "int", "vec2", "vec3", "vec4", "mat4"
};

struct PushConstantDesc {
    std::vector<std::string>             stages;    // "vertex", "fragment", "compute"
    uint32_t                             size;
    uint32_t                             offset;
    std::vector<PushConstantBindingDesc> bindings;
};
```

### PassDeclaration

A complete pass declaration in the graph.

```cpp
struct PassDeclaration {
    std::string                 name;
    PassType                    type = PassType::Graphics;

    std::vector<ResourceAccess> inputs;
    std::vector<ResourceAccess> outputs;

    PipelineDesc                pipelineDesc{};
    std::vector<std::string>    descriptorSetRefs;
    std::vector<PushConstantDesc> pushConstants;

    ExecutionDesc               execution{};
    PassExecuteFn               executeFn;          // Manual override callback
    PassExecuteFn               sceneRendererFn;    // For "scene_geometry" type

    QueueType                   queueType = QueueType::Graphics;
    bool                        hasSideEffects = false;
    bool                        enabled = true;
};
```

| Field | Description |
|-------|-------------|
| `descriptorSetRefs` | Names of descriptor set layouts (resolved from graph layout registry). |
| `executeFn` | Manual execute callback. If set, takes precedence over `execution` config. |
| `sceneRendererFn` | For `"scene_geometry"` execution type: framework binds, callback draws. |
| `hasSideEffects` | If `true`, pass will not be culled even without tracked outputs. |
| `enabled` | If `false`, pass is skipped during execution. |

---

## ExecutionDesc

Configures how a pass executes its work automatically, without a manual callback.

```cpp
struct ExecutionDesc {
    std::string type = "none";

    // "draw" type
    DispatchDimension vertexCount;
    uint32_t instanceCount  = 1;
    uint32_t firstVertex    = 0;
    uint32_t firstInstance  = 0;

    // "compute_dispatch" type
    std::array<uint32_t, 3>          workgroupSize = {16, 16, 1};
    std::array<DispatchDimension, 3> dispatch;

    // Built-in geometry types
    std::string sortMode         = "none";
    std::string entityDataBinding;
    uint32_t    renderLayerMask  = 0xFFFFFFFF;
    int32_t     lightIndex       = -1;

    // Common
    bool bindDescriptorSets = true;
    bool bindPipeline       = true;
};
```

### Execution Type Presets

| Type | Description |
|------|-------------|
| `"none"` | No auto-execution. Use a manual callback (`executeFn`). |
| `"fullscreen"` | Draw 3 vertices for a fullscreen triangle. Used for post-processing. |
| `"draw"` | Draw with a specified vertex count (via `vertexCount` dimension). |
| `"compute_dispatch"` | Compute shader dispatch with configurable group counts. |
| `"compute_image"` | Compute dispatch sized from an image resource's dimensions. |
| `"scene_geometry"` | Hybrid: framework binds pipeline/descriptors, callback does the draw calls. |
| `"opaque_geometry"` | Built-in: queries and renders opaque entities. Default sort: front-to-back. |
| `"transparent_geometry"` | Built-in: queries and renders transparent entities. Default sort: back-to-front. |
| `"shadow_casters"` | Built-in: renders entities with `castShadows = true`. Uses `lightIndex` for shadow VP. |
| `"skinned_geometry"` | Built-in: renders skinned (animated) opaque entities. |
| `"skinned_transparent"` | Built-in: renders skinned transparent entities. |

### Geometry Execution Fields

| Field | Description |
|-------|-------------|
| `sortMode` | `"none"`, `"front_to_back"`, `"back_to_front"` -- draw call ordering. |
| `entityDataBinding` | Name of the entity data binding configuration (e.g., `"pbrOpaque"`). References a compiled binding layout that defines push constants and material texture sets. |
| `renderLayerMask` | Bitmask for render layer filtering. Default `0xFFFFFFFF` renders all layers. |
| `lightIndex` | For `"shadow_casters"`: index of the light whose view-projection to use. `-1` = default. |

### Common Options

| Field | Default | Description |
|-------|---------|-------------|
| `bindDescriptorSets` | `true` | Automatically bind the pass's descriptor sets before execution. |
| `bindPipeline` | `true` | Automatically bind the pipeline before execution. |

---

## DispatchDimension

Specifies how to calculate a dimension value (vertex count, dispatch group count) at execution time. Supports three modes.

```cpp
struct DispatchDimension {
    // Option 1: Fixed value
    uint32_t    value = 0;

    // Option 2: From resource dimensions
    std::string resource;       // Resource name from the graph
    std::string dimension;      // "width", "height", "depth"
    uint32_t    divisor = 1;    // Divide by workgroup size

    // Option 3: From named parameter
    std::string parameter;      // Parameter name (for dynamic dispatch)

    bool isFixed() const;
    bool isFromResource() const;
    bool isFromParameter() const;
};
```

### Mode 1: Fixed Value

Set `value` to a constant. Example: `vertexCount = { .value = 3 }` for a fullscreen triangle.

### Mode 2: Resource-Based

Derive the dimension from a graph resource's size, optionally divided by a workgroup size.

```json
{
  "dispatch": {
    "x": { "resource": "outputImage", "dimension": "width", "divisor": 16 },
    "y": { "resource": "outputImage", "dimension": "height", "divisor": 16 },
    "z": 1
  }
}
```

This dispatches `ceil(width/16)` x `ceil(height/16)` x `1` workgroups.

### Mode 3: Parameter-Based

Read the value from a named parameter at runtime. Useful for dynamic dispatch sizes that change per-frame.

```json
{
  "dispatch": {
    "x": { "parameter": "particleCount" }
  }
}
```

---

## DotPathResolver

Resolves dot-path expressions from JSON to actual runtime data. This is the bridge between the declarative JSON pipeline and the ECS.

```cpp
class DotPathResolver {
public:
    ResolvedValue resolveScene(const std::string& path, const SceneContext& scene) const;
    ResolvedValue resolveEntity(const std::string& path, entt::entity entity,
                                entt::registry& registry) const;
    ResolvedValue resolve(const std::string& path, const SceneContext& scene,
                          entt::entity entity, entt::registry& registry) const;

    static PathRoot getPathRoot(const std::string& path);
    static bool isScenePath(const std::string& path);
    static bool isEntityPath(const std::string& path);
    static bool isConstPath(const std::string& path);
    static bool isResourcePath(const std::string& path);

    std::string validatePath(const std::string& path) const;
    MaterialParam::Type getExpectedType(const std::string& path) const;
};
```

### ResolvedValue

Type-safe container returned by dot-path resolution. Wraps a `std::variant` of all supported types.

```cpp
struct ResolvedValue {
    using ValueType = std::variant<
        std::monostate,     // Invalid/not found
        float,
        glm::vec2,
        glm::vec3,
        glm::vec4,
        glm::mat3,
        glm::mat4,
        int32_t,
        uint32_t,
        GPUTexture
    >;

    ValueType value;

    // Type checking
    bool isValid() const;
    bool isFloat() const;
    bool isVec2() const;
    bool isVec3() const;
    bool isVec4() const;
    bool isMat3() const;
    bool isMat4() const;
    bool isInt() const;
    bool isUInt() const;
    bool isTexture() const;

    // Type-safe getters
    template<typename T> T as() const;                  // Returns T{} if wrong type
    template<typename T> std::optional<T> tryAs() const; // Returns nullopt if wrong type

    size_t byteSize() const;    // Size in bytes (0 for monostate and GPUTexture)
    void copyTo(void* dest) const;  // Copy raw bytes to buffer
};
```

### PathRoot

Classifies the root prefix of a dot-path.

```cpp
enum class PathRoot {
    Scene,      // "scene.*"
    Entity,     // "entity.*"
    Const,      // "const.*"
    Resource,   // Plain name (graph resource)
    Invalid
};
```

### Supported Dot-Path Syntax

#### Entity Paths (`entity.*`)

| Path | Return Type | Description |
|------|-------------|-------------|
| `entity.transform.worldMatrix` | `mat4` | World transform matrix from `TransformComponent` |
| `entity.material.params.<name>` | varies | Material parameter value from `MaterialComponentV5` |
| `entity.material.textures.<name>` | `GPUTexture` | Material texture by slot name |

#### Scene Paths (`scene.*`)

| Path | Return Type | Description |
|------|-------------|-------------|
| `scene.camera.view` | `mat4` | Camera view matrix |
| `scene.camera.projection` | `mat4` | Camera projection matrix (Vulkan Y-flipped) |
| `scene.camera.viewProjection` | `mat4` | Combined view-projection matrix |
| `scene.camera.position` | `vec3` | Camera world position |
| `scene.camera.invView` | `mat4` | Inverse view matrix |
| `scene.camera.invProj` | `mat4` | Inverse projection matrix |
| `scene.camera.fov` | `float` | Field of view in degrees |
| `scene.camera.nearPlane` | `float` | Near clip plane distance |
| `scene.camera.farPlane` | `float` | Far clip plane distance |
| `scene.camera.aspect` | `float` | Aspect ratio (width/height) |
| `scene.environment.irradianceMap` | `GPUTexture` | Diffuse IBL cubemap |
| `scene.environment.prefilterMap` | `GPUTexture` | Specular IBL cubemap |
| `scene.environment.brdfLUT` | `GPUTexture` | BRDF integration lookup texture |
| `scene.lights[N].positionType` | `vec4` | Light position (xyz) and type (w: 0=dir, 1=point, 2=spot) |
| `scene.lights[N].colorIntensity` | `vec4` | Light color (xyz) and intensity (w) |
| `scene.lights[N].directionRange` | `vec4` | Light direction (xyz) and range (w) |
| `scene.lights[N].attenuation` | `vec4` | Attenuation factors (xyz) and cos(outerCone) (w) |
| `scene.time.elapsed` | `float` | Seconds since start |
| `scene.time.delta` | `float` | Frame delta time |
| `scene.time.frame` | `uint32_t` | Frame counter |
| `scene.screen.width` | `float` | Viewport width |
| `scene.screen.height` | `float` | Viewport height |
| `scene.custom.<name>` | varies | Application-defined custom value |

#### Constant Paths (`const.*`)

| Path | Return Type | Description |
|------|-------------|-------------|
| `const.<number>` | `float` | Literal float constant, e.g. `const.0`, `const.1.5` |
| `const.vec4(r,g,b,a)` | `vec4` | Literal vec4 constant, e.g. `const.vec4(1,0,0,1)` |

### SceneContext

Global scene data accessible via `scene.*` paths. Updated each frame from the ECS registry.

```cpp
struct SceneContext {
    // Camera
    glm::mat4 cameraView, cameraProjection, cameraViewProjection;
    glm::mat4 cameraInvView, cameraInvProj;
    glm::vec3 cameraPosition;
    float cameraFov, cameraNearPlane, cameraFarPlane, cameraAspect;

    // Environment (IBL)
    const SceneEnvironment* environment = nullptr;

    // Time
    float timeElapsed, timeDelta;
    uint32_t timeFrame;

    // Screen
    float screenWidth, screenHeight;

    // Lights (up to 16)
    static constexpr uint32_t MAX_SCENE_LIGHTS = 16;
    struct PackedLight {
        glm::vec4 positionType;
        glm::vec4 colorIntensity;
        glm::vec4 directionRange;
        glm::vec4 attenuation;
    };
    std::array<PackedLight, MAX_SCENE_LIGHTS> lights;
    uint32_t lightCount;

    // Custom values
    std::unordered_map<std::string, ResolvedValue> customValues;

    void setCustom(const std::string& key, float value);
    void setCustom(const std::string& key, uint32_t value);
    void setCustom(const std::string& key, int32_t value);
    void setCustom(const std::string& key, const glm::vec2& value);
    void setCustom(const std::string& key, const glm::vec3& value);
    void setCustom(const std::string& key, const glm::vec4& value);
    void setCustom(const std::string& key, const glm::mat4& value);

    void updateFromRegistry(entt::registry& registry);
};
```

### BufferLayoutResolver

Fills byte buffers from compiled buffer layouts and resolved dot-paths. Used internally by the frame graph to populate UBOs and push constants.

```cpp
class BufferLayoutResolver {
public:
    explicit BufferLayoutResolver(const DotPathResolver& pathResolver);

    // Fill with scene-only data (per-frame UBOs)
    void fillSceneBuffer(void* buffer, const CompiledBufferLayout& layout,
                         const SceneContext& scene) const;

    // Fill with entity data (per-draw push constants)
    void fillEntityBuffer(void* buffer, const CompiledBufferLayout& layout,
                          const SceneContext& scene, entt::entity entity,
                          entt::registry& registry) const;

    // Fill with mixed sources
    void fillBuffer(void* buffer, const CompiledBufferLayout& layout,
                    const SceneContext& scene, entt::entity entity,
                    entt::registry& registry) const;
};
```

---

## FrameGraphRenderer

Executes geometry passes from a compiled RenderGraph. Queries the ECS registry for renderable entities, sorts them, and issues draw calls automatically based on the compiled pass configuration.

```cpp
class FrameGraphRenderer {
public:
    explicit FrameGraphRenderer(FrameGraph::RenderGraph& renderGraph);

    // Configuration
    void setRegistry(entt::registry* registry);
    void setCameraPosition(const glm::vec3& pos);
    void clearCameraPosition();

    // Execute a geometry pass
    uint32_t executeGeometryPass(
        const FrameGraph::CompiledPass& pass,
        const FrameGraph::PassDeclaration& passDecl,
        VkCommandBuffer cmd,
        uint32_t frameIndex
    );

    // Query entities (for custom rendering or debugging)
    std::vector<RenderableEntity> queryEntities(
        EntityFilter filter,
        EntitySortMode sortMode
    ) const;

    // Statistics
    uint32_t getLastDrawCount() const;
    uint32_t getLastQueryCount() const;
};
```

### setRegistry

```cpp
void setRegistry(entt::registry* registry);
```

Sets the ECS registry to query entities from. Must be called before `executeGeometryPass`.

### setCameraPosition / clearCameraPosition

```cpp
void setCameraPosition(const glm::vec3& pos);
void clearCameraPosition();
```

Overrides the camera position used for distance calculations (sorting). If not set, the position is taken from the `SceneContext`.

### executeGeometryPass

```cpp
uint32_t executeGeometryPass(
    const FrameGraph::CompiledPass& pass,
    const FrameGraph::PassDeclaration& passDecl,
    VkCommandBuffer cmd,
    uint32_t frameIndex
);
```

Executes a geometry pass by:
1. Reading the execution type from `passDecl.execution.type`
2. Querying entities matching the corresponding `EntityFilter`
3. Sorting by `passDecl.execution.sortMode`
4. Binding and drawing each entity using the compiled pass's entity data bindings

Returns the number of draw calls issued. All binding information is read from the `CompiledPass` -- no strings or JSON lookups at runtime.

### queryEntities

```cpp
std::vector<RenderableEntity> queryEntities(
    EntityFilter filter,
    EntitySortMode sortMode
) const;
```

Queries the registry for entities matching the filter. Returns a sorted vector of `RenderableEntity` structs with pointers to their components. Useful for custom rendering or debugging.

---

## EntityFilter

Determines which entities to include in a geometry pass.

```cpp
enum class EntityFilter {
    All,                // All renderable entities
    Opaque,             // AlphaMode::Opaque or Mask (excludes skinned)
    Transparent,        // AlphaMode::Blend (excludes skinned)
    ShadowCasters,      // Entities with castShadows = true
    Skinned,            // Skinned opaque entities (has SkeletonComponent)
    SkinnedTransparent  // Skinned transparent entities
};
```

| Value | Includes | Excludes |
|-------|----------|----------|
| `All` | Every renderable entity | None |
| `Opaque` | Opaque and alpha-masked, non-skinned | Transparent, skinned |
| `Transparent` | Alpha-blended, non-skinned | Opaque, skinned |
| `ShadowCasters` | Opaque/masked with `castShadows = true` | Transparent |
| `Skinned` | Opaque/masked with `SkeletonComponent` | Transparent, non-skinned |
| `SkinnedTransparent` | Alpha-blended with `SkeletonComponent` | Opaque, non-skinned |

---

## EntitySortMode

Controls the draw call ordering for a geometry pass.

```cpp
enum class EntitySortMode {
    None,           // No sorting (iteration order)
    FrontToBack,    // Closest to camera first (opaque, depth prepass)
    BackToFront     // Farthest from camera first (transparency)
};
```

| Value | Use Case |
|-------|----------|
| `None` | No sorting needed (e.g., shadow maps). |
| `FrontToBack` | Opaque geometry. Maximizes early-z rejection. |
| `BackToFront` | Transparent geometry. Required for correct alpha blending. |

Distance is calculated as squared distance from the entity's world position to the camera position (faster than true distance, same ordering).

---

## RenderableEntity

Query result struct with direct pointers to ECS components. Pointers are valid for the current frame only.

```cpp
struct RenderableEntity {
    entt::entity entity = entt::null;
    float distanceToCamera = 0.0f;

    const MeshComponent*          mesh      = nullptr;
    const MaterialComponentV5*    material  = nullptr;
    const ECS::TransformComponent* transform = nullptr;
    const RenderableTagComponent* tag       = nullptr;
};
```

| Field | Description |
|-------|-------------|
| `entity` | The EnTT entity handle. |
| `distanceToCamera` | Squared distance to the camera (for sorting). |
| `mesh` | Pointer to vertex/index buffer data. |
| `material` | Pointer to textures and material parameters. |
| `transform` | Pointer to world transform matrix. |
| `tag` | Pointer to render flags (visibility, shadow casting, render layer). |

---

## PassExecuteContext

Injected into manual pass callbacks. Provides access to compiled physical resources and auto-created pipelines.

```cpp
struct PassExecuteContext {
    VulkanCommandBuilder& cmd;
    uint32_t frameIndex;
    uint32_t swapchainIndex;
    VkExtent2D renderExtent;

    VulkanPipeline*        pipeline;
    VulkanComputePipeline* computePipeline;
    VkPipelineLayout       pipelineLayout;

    const std::vector<std::shared_ptr<VulkanDescriptorSet>>* descriptorSets;

    // Resource accessors
    VkImageView getImageView(ResourceHandle h) const;
    VkImage     getImage(ResourceHandle h) const;
    VkBuffer    getBuffer(ResourceHandle h) const;

    std::shared_ptr<VulkanDescriptorSet> getDescriptorSet(uint32_t setIndex) const;
};
```

| Method | Description |
|--------|-------------|
| `getImageView(h)` | Returns the `VkImageView` for a graph resource handle. |
| `getImage(h)` | Returns the `VkImage` for a graph resource handle. |
| `getBuffer(h)` | Returns the `VkBuffer` for a graph resource handle. |
| `getDescriptorSet(i)` | Returns the auto-created descriptor set at index `i`. |
