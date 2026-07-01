# JSON Render Pipeline

The JSON render pipeline is the defining feature of Shoonyakasha. Instead of writing C++ code to create render passes, descriptor sets, pipeline states, and buffer bindings, you **declare** your entire rendering pipeline in a single JSON file. The engine compiles that JSON into Vulkan resources and executes it automatically every frame.

This guide covers every aspect of the system: philosophy, structure, syntax, and real-world examples drawn from the engine's own test pipelines.

---

## "JSON is the Engine" Philosophy

In a traditional Vulkan renderer, you write hundreds of lines of C++ to:

- Create render passes with subpasses, attachments, and dependencies
- Allocate and bind descriptor sets for each combination of resources
- Manage pipeline state objects (depth test, blending, cull mode, shaders)
- Upload uniform buffer data and push constants every frame
- Insert image layout transitions and memory barriers between passes

Shoonyakasha replaces all of that with a declarative pipeline file. The core loop is:

1. **JSON declares** buffer layouts, passes, bindings, and data sources
2. **ECS holds** the runtime data (meshes, materials, transforms, lights)
3. **RenderGraph compiles** the JSON into Vulkan resources at load time
4. **FrameGraphRenderer executes** the compiled graph automatically each frame

You never write `vkCmdBindDescriptorSets` or `vkCmdPushConstants`. The engine reads your JSON, resolves data from the ECS via dot-path expressions, and issues the correct Vulkan calls.

---

## Pipeline File Structure

A JSON pipeline file is a single object with these top-level keys:

| Key | Type | Required | Purpose |
|-----|------|----------|---------|
| `version` | int | Yes | Schema version (currently `2` or `3`) |
| `name` | string | Yes | Pipeline name for logging and debugging |
| `vertexFormats` | object | No | Custom vertex attribute layouts |
| `bufferLayouts` | object | No | UBO, SSBO, and push constant structures |
| `entityDataBindings` | object | No | Per-draw and per-material data mappings |
| `samplers` | object | No | Texture sampler configurations |
| `descriptorSetLayouts` | object | No | Descriptor set definitions with bindings |
| `resources` | array | Yes | Render target images and buffers |
| `passes` | array | Yes | Render and compute passes in execution order |

Here is the skeleton of a minimal pipeline:

```json
{
  "version": 3,
  "name": "MyPipeline",

  "bufferLayouts": { },
  "entityDataBindings": { },
  "samplers": { },
  "descriptorSetLayouts": { },
  "resources": [ ],
  "passes": [ ]
}
```

The following sections explain each key in depth, with examples from the engine's real pipeline files.

---

## Resources

The `resources` array defines every image and buffer that passes read from or write to. The engine creates the actual Vulkan images/buffers at compile time and manages their lifetimes.

### Image Resources

```json
{
  "name": "gAlbedo",
  "kind": "image",
  "image": { "format": "R8G8B8A8_SRGB" }
}
```

By default, image resources match the swapchain dimensions. You can override this with explicit sizing or scale factors.

### Swapchain Resource

The swapchain is always declared as an imported resource. It represents the window surface that is presented to the screen:

```json
{ "name": "swapchain", "kind": "image", "imported": true }
```

Every pipeline must include this resource and must have at least one pass that writes to it (typically the final tonemap or composite pass).

### Common Formats

| Format String | Vulkan Format | Typical Use |
|---------------|---------------|-------------|
| `R8G8B8A8_SRGB` | `VK_FORMAT_R8G8B8A8_SRGB` | Albedo, final color |
| `R8G8B8A8_UNORM` | `VK_FORMAT_R8G8B8A8_UNORM` | Linear color data |
| `R8G8_UNORM` | `VK_FORMAT_R8G8_UNORM` | Metallic/roughness pair |
| `R16G16B16A16_SFLOAT` | `VK_FORMAT_R16G16B16A16_SFLOAT` | HDR color, world positions, normals |
| `D32_SFLOAT` | `VK_FORMAT_D32_SFLOAT` | Depth buffer |

### Half-Resolution Resources

For effects like bloom that operate at reduced resolution, use `widthScale` and `heightScale`:

```json
{
  "name": "bloomBrightPass",
  "kind": "image",
  "image": {
    "format": "R16G16B16A16_SFLOAT",
    "widthScale": 0.5,
    "heightScale": 0.5
  }
}
```

This creates an image at half the swapchain resolution. The engine automatically handles the size calculation when the window is resized.

### Real Example: GBuffer Resources

From [`examples/declarative_sponza_test/pbr_ibl_pipeline_v3.json`](../../examples/declarative_sponza_test/pbr_ibl_pipeline_v3.json):

```json
"resources": [
  { "name": "swapchain",            "kind": "image", "imported": true },
  { "name": "gPosition",            "kind": "image", "image": { "format": "R16G16B16A16_SFLOAT" } },
  { "name": "gNormal",              "kind": "image", "image": { "format": "R16G16B16A16_SFLOAT" } },
  { "name": "gAlbedo",              "kind": "image", "image": { "format": "R8G8B8A8_SRGB" } },
  { "name": "gMetallicRoughness",   "kind": "image", "image": { "format": "R8G8_UNORM" } },
  { "name": "gDepth",               "kind": "image", "image": { "format": "D32_SFLOAT" } },
  { "name": "litColorHDR",          "kind": "image", "image": { "format": "R16G16B16A16_SFLOAT" } }
]
```

---

## Buffer Layouts

The `bufferLayouts` object defines the structure of uniform buffers (UBOs), storage buffers (SSBOs), and push constants. Each layout specifies its fields, packing rule, and data sources.

### Uniform Buffer (UBO)

```json
"CameraUBO": {
  "usage": "uniform_buffer",
  "packing": "std140",
  "updateFrequency": "per_frame",
  "fields": [
    { "name": "view",      "type": "mat4", "source": "scene.camera.view" },
    { "name": "proj",      "type": "mat4", "source": "scene.camera.projection" },
    { "name": "invView",   "type": "mat4", "source": "scene.camera.invView" },
    { "name": "invProj",   "type": "mat4", "source": "scene.camera.invProj" },
    { "name": "position",  "type": "vec4", "source": "scene.camera.positionVec4" },
    { "name": "params",    "type": "vec4", "source": "scene.camera.nearFarFovAspect" }
  ]
}
```

The `source` field uses **dot-path expressions** (see [Dot-Path Syntax](#dot-path-syntax) below) to connect each buffer field to live ECS data. The engine resolves these paths every frame and uploads the values automatically.

### Push Constants

Push constants use `"usage": "push_constant"` and the `scalar` packing rule. They are the fastest way to pass per-draw data (model matrix, material parameters):

```json
"MaterialPushConstants": {
  "usage": "push_constant",
  "packing": "scalar",
  "binding": {
    "offset": 0,
    "stages": ["vertex", "fragment"]
  },
  "fields": [
    { "name": "model",            "type": "mat4",  "source": "entity.transform.worldMatrix" },
    { "name": "baseColorFactor",  "type": "vec4",  "source": "entity.material.params.baseColorFactor" },
    { "name": "metallicFactor",   "type": "float", "source": "entity.material.params.metallicFactor" },
    { "name": "roughnessFactor",  "type": "float", "source": "entity.material.params.roughnessFactor" },
    { "name": "hasNormalMap",     "type": "float", "source": "entity.material.textures.normalMap.exists" },
    { "name": "hasMetalRoughMap", "type": "float", "source": "entity.material.textures.metallicRoughnessMap.exists" },
    { "name": "alphaCutoff",      "type": "float", "source": "entity.material.alphaCutoff" },
    { "name": "padding1",         "type": "float", "source": "const.0" }
  ]
}
```

Note the `entity.*` sources -- these are resolved per-entity for each draw call. The `const.0` source provides a literal zero for padding.

### Storage Buffer (SSBO)

SSBOs support large, structured data. They can even be initialized declaratively:

```json
"particleSSBO": {
  "usage": "storage_buffer",
  "packing": "std430",
  "elementCount": 50000,
  "fields": [
    { "name": "position", "type": "vec4" },
    { "name": "velocity", "type": "vec4" }
  ],
  "source": {
    "type": "initializer",
    "seed": 42,
    "fields": {
      "position": {
        "randomRange": {
          "min": [-12.0, 0.0, -5.0, 2.0],
          "max": [12.0, 8.0, 5.0, 5.0]
        }
      },
      "velocity": {
        "randomRange": {
          "min": [-0.5, 0.5, -0.5, 0.8],
          "max": [0.5, 1.5, 0.5, 1.2]
        }
      }
    }
  }
}
```

This declares a 50,000-element particle buffer with random initial positions and velocities. The engine allocates the GPU memory, generates the initial data, and uploads it -- all from this JSON definition.

### Array Fields

Fields can be arrays. Use `arrayCount` and the `[i]` iterator in the source path:

```json
{ "name": "lightsPositionType", "type": "vec4", "arrayCount": 16, "source": "scene.lights[i].positionType" }
```

At fill time, the engine expands `[i]` to `[0]`, `[1]`, ..., `[15]` and writes each element at the correct stride offset.

### Field Types

| Type | Size (bytes) | Description |
|------|-------------|-------------|
| `float` | 4 | 32-bit float |
| `vec2` | 8 | 2-component float vector |
| `vec3` | 12 | 3-component float vector |
| `vec4` | 16 | 4-component float vector |
| `mat3` | 36 | 3x3 float matrix |
| `mat4` | 64 | 4x4 float matrix |
| `int` | 4 | 32-bit signed integer |
| `uint` | 4 | 32-bit unsigned integer |
| `ivec2` | 8 | 2-component signed integer vector |
| `ivec3` | 12 | 3-component signed integer vector |
| `ivec4` | 16 | 4-component signed integer vector |
| `uvec2` | 8 | 2-component unsigned integer vector |
| `uvec3` | 12 | 3-component unsigned integer vector |
| `uvec4` | 16 | 4-component unsigned integer vector |
| `double` | 8 | 64-bit float |
| `bool` | 4 | Boolean (4 bytes in std140) |

### Packing Rules

| Rule | JSON Value | When to Use |
|------|-----------|-------------|
| std140 | `"std140"` | UBOs (required by Vulkan spec for uniform buffers) |
| std430 | `"std430"` | SSBOs (tighter packing, no vec3 rounding) |
| scalar | `"scalar"` | Push constants (requires `VK_EXT_scalar_block_layout`) |
| push_constant | `"push_constant"` | Alias for scalar packing with push constant semantics |

### Update Frequency

| Frequency | JSON Value | Behavior |
|-----------|-----------|----------|
| Per-frame | `"per_frame"` | Updated from SceneContext every frame (default for UBOs) |
| Manual | `"manual"` | No automatic updates; fill via `setUniformField()` |
| Every N frames | `"every_n_frames"` | Updated periodically |
| On change | `"on_change"` | Updated only when source data changes |
| Once | `"once"` | Updated at creation, never again |

---

## Dot-Path Syntax

Dot-paths are the bridge between declarative JSON and live ECS data. Every `"source"` field in a buffer layout uses a dot-path to specify where the runtime value comes from.

The `DotPathResolver` (see `include/FrameGraph/DotPathResolver.h`) classifies paths by their root prefix:

| Prefix | Scope | When Resolved |
|--------|-------|---------------|
| `scene.*` | Global scene data | Once per frame |
| `entity.*` | Per-entity ECS data | Once per draw call |
| `const.*` | Literal constants | At compile time |

### Scene Paths

Scene paths read from the `SceneContext`, which is updated from the ECS registry each frame.

#### Camera

| Path | Type | Description |
|------|------|-------------|
| `scene.camera.view` | mat4 | View matrix |
| `scene.camera.projection` | mat4 | Projection matrix (Vulkan Y-flipped) |
| `scene.camera.viewProjection` | mat4 | Combined view-projection |
| `scene.camera.invView` | mat4 | Inverse view matrix |
| `scene.camera.invProj` | mat4 | Inverse projection matrix |
| `scene.camera.position` | vec3 | Camera world position |
| `scene.camera.positionVec4` | vec4 | Camera position as vec4 (w=1.0) |
| `scene.camera.nearFarFovAspect` | vec4 | Packed (near, far, fov, aspect) |
| `scene.camera.fov` | float | Field of view in degrees |
| `scene.camera.nearPlane` | float | Near clip plane distance |
| `scene.camera.farPlane` | float | Far clip plane distance |
| `scene.camera.aspect` | float | Aspect ratio (width/height) |

#### Lights

| Path | Type | Description |
|------|------|-------------|
| `scene.lights.count` | uint | Number of active lights (max 16) |
| `scene.lights[N].positionType` | vec4 | xyz=position, w=type (0=dir, 1=point, 2=spot) |
| `scene.lights[N].colorIntensity` | vec4 | xyz=color, w=intensity |
| `scene.lights[N].directionRange` | vec4 | xyz=direction, w=range |
| `scene.lights[N].attenuation` | vec4 | x=constant, y=linear, z=quadratic, w=cos(outerCone) |

Use `[i]` in array fields to iterate over all lights automatically (see [Array Fields](#array-fields) above).

#### Environment (IBL)

| Path | Type | Description |
|------|------|-------------|
| `scene.environment.irradianceMap` | GPUTexture | Diffuse IBL cubemap |
| `scene.environment.prefilterMap` | GPUTexture | Specular IBL cubemap |
| `scene.environment.brdfLUT` | GPUTexture | BRDF integration LUT |
| `scene.environment.environmentMap` | GPUTexture | Full environment cubemap |

#### Time

| Path | Type | Description |
|------|------|-------------|
| `scene.time.elapsed` | float | Seconds since engine start |
| `scene.time.delta` | float | Frame delta time in seconds |
| `scene.time.frame` | uint | Frame counter |

#### Screen

| Path | Type | Description |
|------|------|-------------|
| `scene.screen.width` | float | Viewport width in pixels |
| `scene.screen.height` | float | Viewport height in pixels |
| `scene.screen.resolution` | vec2 | (width, height) packed |

#### Custom Values

Application-specific data set from C++ or Python via `set_custom_float`, `set_custom_vec4`, etc.:

| Path | Type | Description |
|------|------|-------------|
| `scene.custom.<key>` | any | User-defined value |

Keys use dots for namespacing. For example, `scene.custom.particles.gravity` maps to a custom value set with key `"particles.gravity"`. In Python:

```python
scene.set_custom_float("particles.gravity", -9.81)
scene.set_custom_uint("particles.count", 50000)
scene.set_custom_vec4("particles.wind", 1.0, 0.0, 0.5, 0.0)
```

In C++ (via SceneContext):

```cpp
sceneContext.setCustom("particles.gravity", -9.81f);
sceneContext.setCustom("particles.count", uint32_t(50000));
sceneContext.setCustom("particles.wind", glm::vec4(1.0f, 0.0f, 0.5f, 0.0f));
```

### Entity Paths

Entity paths are resolved per-entity during draw call iteration. They pull data from the entity's ECS components.

#### Transform

| Path | Type | Description |
|------|------|-------------|
| `entity.transform.worldMatrix` | mat4 | World-space transform matrix |
| `entity.transform.localMatrix` | mat4 | Local-space transform matrix |
| `entity.transform.position` | vec3 | World position |
| `entity.transform.rotation` | vec4 | Euler rotation as vec4 (xyz, w=0) |
| `entity.transform.scale` | vec3 | Scale factor |

#### Material Parameters

| Path | Type | Description |
|------|------|-------------|
| `entity.material.params.<name>` | varies | Named material parameter |
| `entity.material.textures.<name>` | GPUTexture | Named texture binding |
| `entity.material.textures.<name>.exists` | float | 1.0 if texture is bound, 0.0 otherwise |
| `entity.material.alphaCutoff` | float | Alpha test threshold |
| `entity.material.alphaMode` | uint | 0=Opaque, 1=Mask, 2=Blend |
| `entity.material.doubleSided` | float | 1.0 if double-sided, 0.0 otherwise |

Common parameter names from PBR materials:

- `entity.material.params.baseColorFactor` (vec4)
- `entity.material.params.metallicFactor` (float)
- `entity.material.params.roughnessFactor` (float)

Common texture names:

- `entity.material.textures.albedoMap`
- `entity.material.textures.normalMap`
- `entity.material.textures.metallicRoughnessMap`

#### Mesh

| Path | Type | Description |
|------|------|-------------|
| `entity.mesh.vertexCount` | uint | Number of vertices |
| `entity.mesh.indexCount` | uint | Number of indices |

#### Skeleton

| Path | Type | Description |
|------|------|-------------|
| `entity.skeleton.hasSkeleton` | float | 1.0 if entity has skeleton, 0.0 otherwise |
| `entity.skeleton.jointCount` | uint | Number of joints in skeleton |

### Constant Paths

Constant paths provide literal values directly in the pipeline definition. They are useful for padding fields and default values.

| Path | Result Type | Value |
|------|-------------|-------|
| `const.0` | float | 0.0 |
| `const.1` | float | 1.0 |
| `const.3.14` | float | 3.14 |
| `const.1.0.0.0` | vec3 | (1.0, 0.0, 0.0) |
| `const.1.0.0.0.1.0` | vec4 | (1.0, 0.0, 0.0, 1.0) |

Dots separate vector components in constant paths. `const.1.0.0.1` is a vec4 with components (1.0, 0.0, 0.0, 1.0). Constant paths with a single value (no extra dots) resolve to a float.

---

## Entity Data Bindings

The `entityDataBindings` object connects geometry passes to their per-entity data. Each binding configuration names a push constant layout for per-draw data and a descriptor set layout for per-material data.

```json
"entityDataBindings": {
  "pbrOpaque": {
    "perDraw": { "layoutRef": "MaterialPushConstants" },
    "material": { "layoutRef": "materialSet" }
  },
  "pbrTransparent": {
    "perDraw": { "layoutRef": "MaterialPushConstants" },
    "material": { "layoutRef": "materialSet" }
  }
}
```

- **`perDraw.layoutRef`** -- references a buffer layout in `bufferLayouts`. For each entity drawn, the engine resolves all `entity.*` dot-paths in that layout and pushes the result as push constants.
- **`material.layoutRef`** -- references a descriptor set layout in `descriptorSetLayouts`. The engine binds the entity's material textures into this set automatically.

For skinned meshes, add a `skeleton` binding:

```json
"pbrSkinned": {
  "perDraw": { "layoutRef": "MaterialPushConstants" },
  "material": { "layoutRef": "materialSet" },
  "skeleton": { "layoutRef": "skeletonSet" }
}
```

This tells the engine to also bind the skeleton's bone matrices SSBO when rendering skinned entities. From [`examples/skinned_mesh_test/skinned_pipeline.json`](../../examples/skinned_mesh_test/skinned_pipeline.json):

```json
"skeletonSet": {
  "bindings": [
    {
      "binding": 0,
      "type": "storage_buffer",
      "stages": ["vertex"],
      "name": "boneMatrices",
      "autoBindBuffer": "entity.skeleton.boneMatrices"
    }
  ]
}
```

Passes reference entity data bindings via the `execution.entityDataBinding` field (see [Passes](#passes)).

---

## Descriptor Set Layouts

The `descriptorSetLayouts` object defines named descriptor set configurations. Each layout lists its bindings with type, stage visibility, and optional auto-binding directives.

### Manual Binding (Material Textures)

Material texture sets are bound per-entity by the `FrameGraphRenderer`. The engine matches binding names (`albedoMap`, `normalMap`, etc.) to the entity's `MaterialComponentV5` textures:

```json
"materialSet": {
  "bindings": [
    { "binding": 0, "type": "combined_image_sampler", "stages": ["fragment"], "name": "albedoMap" },
    { "binding": 1, "type": "combined_image_sampler", "stages": ["fragment"], "name": "normalMap" },
    { "binding": 2, "type": "combined_image_sampler", "stages": ["fragment"], "name": "metallicRoughnessMap" }
  ]
}
```

### Auto-Binding (UBOs and Graph Resources)

For resources that are global (not per-entity), use auto-binding directives. The engine resolves these at compile time and binds them automatically.

**Auto-bind a buffer** (UBO or SSBO):

```json
"cameraSet": {
  "bindings": [
    {
      "binding": 0,
      "type": "uniform_buffer",
      "stages": ["vertex", "fragment"],
      "name": "CameraUBO",
      "autoBindBuffer": "CameraUBO"
    }
  ]
}
```

The `autoBindBuffer` value references a layout name from `bufferLayouts`. The engine creates the buffer, updates it from dot-paths each frame, and binds it to this set.

**Auto-bind a render target image** with a sampler:

```json
"gbufferReadSet": {
  "bindings": [
    {
      "binding": 0,
      "type": "combined_image_sampler",
      "stages": ["fragment"],
      "name": "gPosition",
      "autoBindResource": "gPosition",
      "autoBindSampler": "nearestClamp"
    },
    {
      "binding": 1,
      "type": "combined_image_sampler",
      "stages": ["fragment"],
      "name": "gNormal",
      "autoBindResource": "gNormal",
      "autoBindSampler": "nearestClamp"
    }
  ]
}
```

The `autoBindResource` references a resource from the `resources` array. The `autoBindSampler` references a sampler from the `samplers` object. The engine creates the descriptor set with the correct image view and sampler already bound.

### Binding Types

| Type String | Vulkan Type | Typical Use |
|-------------|-------------|-------------|
| `uniform_buffer` | `VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER` | Camera, lights, parameters |
| `storage_buffer` | `VK_DESCRIPTOR_TYPE_STORAGE_BUFFER` | Particles, bone matrices, large data |
| `combined_image_sampler` | `VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER` | Textures, GBuffer reads |
| `storage_image` | `VK_DESCRIPTOR_TYPE_STORAGE_IMAGE` | Compute output images |
| `input_attachment` | `VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT` | Subpass input reads |

### Shader Stages

| Stage String | Vulkan Flag | Description |
|-------------|-------------|-------------|
| `vertex` | `VK_SHADER_STAGE_VERTEX_BIT` | Vertex shader |
| `fragment` | `VK_SHADER_STAGE_FRAGMENT_BIT` | Fragment shader |
| `compute` | `VK_SHADER_STAGE_COMPUTE_BIT` | Compute shader |

---

## Samplers

The `samplers` object defines reusable texture sampling configurations referenced by `autoBindSampler` directives in descriptor set bindings.

```json
"samplers": {
  "nearestClamp": {
    "magFilter": "nearest",
    "minFilter": "nearest",
    "addressMode": "clamp_to_edge"
  },
  "linearClamp": {
    "magFilter": "linear",
    "minFilter": "linear",
    "mipmapMode": "linear",
    "addressMode": "clamp_to_edge"
  }
}
```

### Sampler Properties

| Property | Values | Default | Description |
|----------|--------|---------|-------------|
| `magFilter` | `"linear"`, `"nearest"` | `"linear"` | Magnification filter |
| `minFilter` | `"linear"`, `"nearest"` | `"linear"` | Minification filter |
| `mipmapMode` | `"linear"`, `"nearest"` | `"linear"` | Mipmap interpolation |
| `addressMode` | `"repeat"`, `"clamp_to_edge"`, `"clamp_to_border"`, `"mirrored_repeat"` | `"repeat"` | Shorthand for U, V, W |
| `addressModeU` | (same as above) | `"repeat"` | U-axis address mode |
| `addressModeV` | (same as above) | `"repeat"` | V-axis address mode |
| `addressModeW` | (same as above) | `"repeat"` | W-axis address mode |
| `borderColor` | `"float_opaque_black"`, `"float_opaque_white"`, `"float_transparent_black"` | `"float_opaque_black"` | Border color for clamp_to_border |
| `anisotropyEnable` | bool | `false` | Enable anisotropic filtering |
| `maxAnisotropy` | float | `1.0` | Maximum anisotropy level |
| `compareEnable` | bool | `false` | Enable depth comparison (for shadow maps) |
| `compareOp` | `"less"`, `"greater"`, `"equal"`, `"never"`, `"always"`, etc. | `"less"` | Comparison operator |
| `minLod` | float | `0.0` | Minimum LOD level |
| `maxLod` | float | `0.0` | Maximum LOD level |
| `mipLodBias` | float | `0.0` | LOD bias |

---

## Passes

The `passes` array defines the rendering operations in execution order. Each pass specifies its type, inputs, outputs, pipeline state, and execution mode. The engine handles all Vulkan synchronization (barriers, layout transitions) between passes automatically.

### Graphics Pass -- Geometry Rendering

The most common pass type renders entities from the ECS. The `execution` block determines which entities to render and how to bind their data:

```json
{
  "name": "GBufferPass",
  "type": "graphics",
  "execution": {
    "type": "opaque_geometry",
    "sortMode": "front_to_back",
    "entityDataBinding": "pbrOpaque"
  },
  "outputs": [
    { "resource": "gPosition",          "usage": "color_write", "clear": [0.0, 0.0, 0.0, 0.0] },
    { "resource": "gNormal",            "usage": "color_write", "clear": [0.5, 0.5, 1.0, 0.0] },
    { "resource": "gAlbedo",            "usage": "color_write", "clear": [0.0, 0.0, 0.0, 0.0] },
    { "resource": "gMetallicRoughness", "usage": "color_write", "clear": [0.0, 0.5, 0.0, 0.0] },
    { "resource": "gDepth",             "usage": "depth_write", "clear": { "depth": 1.0, "stencil": 0 } }
  ],
  "pipeline": {
    "vertexShader": "shaders/pbr_gbuffer.vert.spv",
    "fragmentShader": "shaders/pbr_gbuffer.frag.spv",
    "depthTest": true,
    "depthWrite": true,
    "cullMode": "none"
  },
  "descriptorSets": ["cameraSet", "materialSet"],
  "pushConstants": [{ "stages": ["vertex", "fragment"], "size": 104, "offset": 0 }]
}
```

### Graphics Pass -- Fullscreen

Fullscreen passes draw a single triangle covering the screen. They are used for lighting, post-processing, and compositing. No entity iteration occurs:

```json
{
  "name": "IBLLightingPass",
  "type": "graphics",
  "execution": { "type": "fullscreen" },
  "inputs": [
    { "resource": "gPosition",          "usage": "shader_read" },
    { "resource": "gNormal",            "usage": "shader_read" },
    { "resource": "gAlbedo",            "usage": "shader_read" },
    { "resource": "gMetallicRoughness", "usage": "shader_read" }
  ],
  "outputs": [
    { "resource": "litColorHDR", "usage": "color_write", "clear": [0.0, 0.0, 0.0, 1.0] }
  ],
  "pipeline": {
    "vertexShader": "shaders/fullscreen.vert.spv",
    "fragmentShader": "shaders/pbr_ibl_lighting.frag.spv",
    "depthTest": false,
    "depthWrite": false,
    "cullMode": "none",
    "vertexInput": "none"
  },
  "descriptorSets": ["gbufferReadSet", "iblSet", "cameraSet", "lightsSet"]
}
```

Note `"vertexInput": "none"` -- the fullscreen vertex shader generates vertex positions procedurally.

### Graphics Pass -- Custom Draw

For non-entity rendering (particles, debug visualizations), use the `"draw"` execution type with a specified vertex count:

```json
{
  "name": "ParticleRender",
  "type": "graphics",
  "execution": {
    "type": "draw",
    "vertexCount": { "parameter": "particleCount" },
    "instanceCount": 1,
    "bindDescriptorSets": true
  },
  "pipeline": {
    "vertexShader": "shaders/particle.vert.spv",
    "fragmentShader": "shaders/particle.frag.spv",
    "topology": "point_list",
    "depthTest": true,
    "depthWrite": false,
    "vertexInput": "none",
    "cullMode": "none",
    "blending": "additive"
  },
  "descriptorSets": ["cameraSet", "particleRenderSet"],
  "outputs": [
    { "resource": "litColorHDR", "usage": "color_blend" },
    { "resource": "gDepth", "usage": "depth_read" }
  ]
}
```

The `vertexCount` uses a named parameter (`"particleCount"`) that is set from application code. The vertex shader reads particle positions from an SSBO rather than from vertex attributes.

### Execution Types

| Type | Category | Description |
|------|----------|-------------|
| `none` | Manual | No auto-execution; provide a manual callback |
| `fullscreen` | Graphics | Draw 3 vertices for a fullscreen triangle |
| `draw` | Graphics | Draw with specified `vertexCount` and `instanceCount` |
| `opaque_geometry` | Graphics | Render opaque entities (AlphaMode::Opaque or Mask) |
| `transparent_geometry` | Graphics | Render transparent entities (AlphaMode::Blend) |
| `shadow_casters` | Graphics | Render entities with `castShadows = true` |
| `skinned_geometry` | Graphics | Render skinned (animated) opaque entities |
| `skinned_transparent` | Graphics | Render skinned transparent entities |
| `compute_dispatch` | Compute | Dispatch a compute shader |

### Sort Modes

| Mode | Typical Use |
|------|-------------|
| `"none"` | No sorting (iteration order) |
| `"front_to_back"` | Opaque geometry (minimizes overdraw via early-Z) |
| `"back_to_front"` | Transparent geometry (correct alpha blending order) |

### Resource Usages in Passes

**Outputs** (write targets):

| Usage | Description |
|-------|-------------|
| `color_write` | Write to a color attachment (clears then writes) |
| `color_blend` | Write to a color attachment with alpha blending (read-modify-write) |
| `depth_write` | Write to the depth buffer |
| `storage_image_write` | Write to a storage image (compute passes) |

**Inputs** (read sources):

| Usage | Description |
|-------|-------------|
| `shader_read` | Sample the resource in a shader |
| `depth_read` | Read the depth buffer (depth test without writing) |

### Pipeline State

The `pipeline` object in a pass defines the Vulkan pipeline state:

| Property | Values | Default | Description |
|----------|--------|---------|-------------|
| `vertexShader` | path | (required) | SPIR-V vertex shader path |
| `fragmentShader` | path | (required for graphics) | SPIR-V fragment shader path |
| `computeShader` | path | (required for compute) | SPIR-V compute shader path |
| `depthTest` | bool | `true` | Enable depth testing |
| `depthWrite` | bool | `true` | Enable depth writing |
| `cullMode` | `"none"`, `"front"`, `"back"`, `"front_and_back"` | `"back"` | Face culling mode |
| `blending` | `"none"`, `"alpha"`, `"additive"`, `"custom"` | `"none"` | Color blending mode |
| `topology` | `"triangle_list"`, `"triangle_strip"`, `"line_list"`, `"point_list"` | `"triangle_list"` | Primitive topology |
| `vertexInput` | `"default"`, `"none"`, or custom name | `"default"` | Vertex attribute layout |
| `wireframe` | bool | `false` | Render in wireframe mode |

### Custom Blending

`"alpha"` (standard non-premultiplied alpha-over) and `"additive"` cover
most cases, but when you need something else — premultiplied alpha,
multiply/screen-style blend modes, etc. — set `"blending": "custom"` and
provide explicit factors/ops:

```json
"pipeline": {
  "vertexShader": "shaders/sprite.vert.spv",
  "fragmentShader": "shaders/sprite.frag.spv",
  "blending": "custom",
  "srcColorFactor": "one",
  "dstColorFactor": "one_minus_src_alpha",
  "colorBlendOp": "add",
  "srcAlphaFactor": "one",
  "dstAlphaFactor": "one_minus_src_alpha",
  "alphaBlendOp": "add"
}
```

(The example above is premultiplied-alpha blending — use it when your
texture's RGB channels are already multiplied by alpha, which avoids the
dark fringing that plain `"alpha"` blending can produce at soft edges of
sprites/text with mipmapping or filtering.)

| Property | Default | Description |
|----------|---------|-------------|
| `srcColorFactor` | `"src_alpha"` | Source color blend factor |
| `dstColorFactor` | `"one_minus_src_alpha"` | Destination color blend factor |
| `colorBlendOp` | `"add"` | Color blend operation |
| `srcAlphaFactor` | `"one"` | Source alpha blend factor |
| `dstAlphaFactor` | `"zero"` | Destination alpha blend factor |
| `alphaBlendOp` | `"add"` | Alpha blend operation |

The defaults above match the `"alpha"` preset, so a `"custom"` block that
only overrides a couple of fields still behaves sensibly for the rest.

**Blend factors** (`srcColorFactor`/`dstColorFactor`/`srcAlphaFactor`/`dstAlphaFactor`):
`"zero"`, `"one"`, `"src_color"`, `"one_minus_src_color"`, `"dst_color"`,
`"one_minus_dst_color"`, `"src_alpha"`, `"one_minus_src_alpha"`,
`"dst_alpha"`, `"one_minus_dst_alpha"`, `"constant_color"`,
`"one_minus_constant_color"`, `"constant_alpha"`,
`"one_minus_constant_alpha"`, `"src_alpha_saturate"`.

**Blend ops** (`colorBlendOp`/`alphaBlendOp`): `"add"`, `"subtract"`,
`"reverse_subtract"`, `"min"`, `"max"`.

Applies uniformly to every color attachment of the pass — there's no
per-attachment blend state (a pass with multiple color outputs, e.g. a
G-buffer pass, gets the same blend state on all of them).

---

## Compute Passes

Compute passes dispatch compute shaders. They do not have a render pass or framebuffer -- they operate on buffers and storage images.

### Basic Compute Pass

```json
{
  "name": "ParticleSimulate",
  "type": "compute",
  "execution": {
    "type": "compute_dispatch",
    "workgroupSize": [256, 1, 1],
    "dispatch": {
      "x": { "parameter": "particleCount", "divisor": 256 },
      "y": 1,
      "z": 1
    },
    "bindDescriptorSets": true
  },
  "pipeline": {
    "computeShader": "shaders/particle_sim.comp.spv"
  },
  "descriptorSets": ["particleComputeSet"],
  "hasSideEffects": true
}
```

The `hasSideEffects` flag prevents the pass from being culled by the frame graph optimizer. Without it, a compute pass that does not write to any tracked output resource would be removed as dead code.

### Dispatch Sizing

Each dispatch dimension (`x`, `y`, `z`) can be specified in three ways:

**Fixed value:**

```json
"y": 1
```

**From a named parameter (set from application code):**

```json
"x": { "parameter": "particleCount", "divisor": 256 }
```

The engine divides the parameter value by the divisor and rounds up, giving the number of workgroups.

**From a resource's dimensions:**

```json
"x": { "resource": "bloomBrightPass", "dimension": "width", "divisor": 16 },
"y": { "resource": "bloomBrightPass", "dimension": "height", "divisor": 16 }
```

This is the standard pattern for image-processing compute passes. From [`examples/bloom_test/bloom_pipeline.json`](../../examples/bloom_test/bloom_pipeline.json):

```json
{
  "name": "BrightExtract",
  "type": "compute",
  "execution": {
    "type": "compute_dispatch",
    "workgroupSize": [16, 16, 1],
    "dispatch": {
      "x": { "resource": "bloomBrightPass", "dimension": "width", "divisor": 16 },
      "y": { "resource": "bloomBrightPass", "dimension": "height", "divisor": 16 },
      "z": 1
    }
  },
  "pipeline": {
    "computeShader": "shaders/bright_extract.comp.spv"
  },
  "descriptorSets": ["brightExtractSet"],
  "inputs": [
    { "resource": "sceneColor", "usage": "shader_read" }
  ],
  "outputs": [
    { "resource": "bloomBrightPass", "usage": "storage_image_write" }
  ]
}
```

---

## Vertex Formats

The optional `vertexFormats` object declares custom vertex attribute layouts. Passes reference them via `"vertexInput": "<format_name>"` in their pipeline description.

```json
"vertexFormats": {
  "standard": {
    "attributes": [
      { "name": "position",  "type": "vec3", "location": 0 },
      { "name": "color",     "type": "vec3", "location": 1 },
      { "name": "texCoord",  "type": "vec2", "location": 2 },
      { "name": "normal",    "type": "vec3", "location": 3 }
    ]
  },
  "skinned": {
    "attributes": [
      { "name": "position",     "type": "vec3",  "location": 0 },
      { "name": "normal",       "type": "vec3",  "location": 1 },
      { "name": "texCoord",     "type": "vec2",  "location": 2 },
      { "name": "jointIndices", "type": "uvec4", "location": 3 },
      { "name": "jointWeights", "type": "vec4",  "location": 4 }
    ]
  }
}
```

The skinned vertex format adds joint indices and weights for skeletal animation. Use `"vertexInput": "none"` for passes that generate vertices procedurally (fullscreen triangles, point-list particles).

---

## Complete Example: Deferred PBR Pipeline

This walkthrough traces data flow through a simplified deferred PBR pipeline with three passes: GBuffer, Lighting, and Tonemap. This is the pattern used by the engine's Sponza demo.

### Pass 1: GBuffer

The GBuffer pass iterates over all opaque entities, sorted front-to-back. For each entity, it:

1. Resolves the `MaterialPushConstants` layout (model matrix, material params) via entity dot-paths
2. Pushes those values as push constants
3. Binds the entity's material textures into `materialSet`
4. Binds the camera UBO (auto-bound in `cameraSet`)
5. Issues a draw call for the entity's mesh

The pass writes world positions, normals, albedo, and metallic/roughness into four GBuffer render targets, plus a depth buffer.

```
ECS Entities ──> [GBufferPass] ──> gPosition, gNormal, gAlbedo, gMetallicRoughness, gDepth
                     |
                     ├── entity.transform.worldMatrix ──> push constants
                     ├── entity.material.params.* ──> push constants
                     ├── entity.material.textures.* ──> materialSet
                     └── scene.camera.* ──> CameraUBO ──> cameraSet
```

### Pass 2: IBL Lighting

The lighting pass is a fullscreen pass. It reads all four GBuffer textures (auto-bound via `gbufferReadSet`), the IBL environment maps (bound via `iblSet`), camera data, and light data. It computes PBR lighting for every pixel and writes the result to an HDR color buffer.

```
gPosition, gNormal, gAlbedo, gMetallicRoughness ──> [IBLLightingPass] ──> litColorHDR
                                                           |
                                                           ├── gbufferReadSet (auto-bound)
                                                           ├── iblSet (IBL textures)
                                                           ├── cameraSet (auto-bound)
                                                           └── lightsSet (auto-bound)
```

### Pass 3: Tonemap

The final pass reads the HDR color buffer and applies tonemapping, writing the LDR result to the swapchain for presentation.

```
litColorHDR ──> [TonemapPass] ──> swapchain
                     |
                     └── tonemapInputSet (auto-bound litColorHDR + linearClamp sampler)
```

### Data Flow Summary

```
Frame Start
  │
  ├── SceneContext updates from ECS (camera, lights, custom values)
  ├── CameraUBO filled from scene.camera.* dot-paths
  ├── LightsUBO filled from scene.lights.* dot-paths
  │
  ├── GBufferPass: iterate opaque entities
  │     For each entity:
  │       resolve entity.transform.worldMatrix → push constants
  │       resolve entity.material.params.* → push constants
  │       bind entity textures → materialSet
  │       vkCmdDrawIndexed()
  │
  ├── [barrier: GBuffer color_write → shader_read]
  │
  ├── IBLLightingPass: fullscreen triangle
  │     bind gbufferReadSet, iblSet, cameraSet, lightsSet
  │     vkCmdDraw(3)
  │
  ├── [barrier: litColorHDR color_write → shader_read]
  │
  ├── TonemapPass: fullscreen triangle
  │     bind tonemapInputSet
  │     vkCmdDraw(3)
  │
  └── Present swapchain
```

The barriers are inserted automatically by the frame graph compiler. You never write synchronization code.

---

## Common Patterns

### Deferred Rendering

Multiple geometry outputs followed by a fullscreen lighting pass. The GBuffer stores material properties at each pixel, and the lighting pass evaluates them in screen space.

```
GBufferPass (opaque_geometry) ──> gPosition, gNormal, gAlbedo, gDepth
IBLLightingPass (fullscreen)  ──> litColorHDR
TonemapPass (fullscreen)      ──> swapchain
```

### Forward Transparency

After deferred lighting, transparent objects are rendered in a forward pass that reads the existing depth buffer but does not write to it:

```json
{
  "name": "ForwardTransparencyPass",
  "type": "graphics",
  "execution": {
    "type": "transparent_geometry",
    "sortMode": "back_to_front",
    "entityDataBinding": "pbrTransparent"
  },
  "inputs": [
    { "resource": "gDepth", "usage": "depth_read" }
  ],
  "outputs": [
    { "resource": "litColorHDR", "usage": "color_blend" },
    { "resource": "gDepth", "usage": "depth_read" }
  ],
  "pipeline": {
    "depthTest": true,
    "depthWrite": false,
    "blending": "alpha"
  }
}
```

Note `"color_blend"` on the output and `"blending": "alpha"` in the pipeline state.

### Post-Processing Chain

A series of fullscreen passes where each reads the previous pass's output:

```
SceneColor ──> [BrightExtract] ──> bloomBright
                                       │
bloomBright ──> [BlurHorizontal] ──> bloomBlurH
                                       │
bloomBlurH  ──> [BlurVertical]   ──> bloomBlurV
                                       │
sceneColor + bloomBlurV ──> [Composite] ──> swapchain
```

See [`examples/bloom_test/bloom_pipeline.json`](../../examples/bloom_test/bloom_pipeline.json) for the full implementation of this pattern.

### Half-Resolution Effects

Use `widthScale`/`heightScale` on resources to process effects at reduced resolution. Common for bloom, ambient occlusion, and screen-space reflections:

```json
{ "name": "bloomBrightPass", "kind": "image", "image": { "format": "R16G16B16A16_SFLOAT", "widthScale": 0.5, "heightScale": 0.5 } },
{ "name": "bloomBlurH",      "kind": "image", "image": { "format": "R16G16B16A16_SFLOAT", "widthScale": 0.5, "heightScale": 0.5 } },
{ "name": "bloomBlurV",      "kind": "image", "image": { "format": "R16G16B16A16_SFLOAT", "widthScale": 0.5, "heightScale": 0.5 } }
```

Compute dispatch dimensions automatically adapt via `"resource": "bloomBrightPass", "dimension": "width"`.

### Compute-then-Render

Run a compute pass to simulate or transform data, then render the results in a graphics pass:

```
[ParticleSimulate] (compute) ──> SSBO modified in place
[ParticleRender]   (draw, point_list) ──> reads SSBO in vertex shader ──> swapchain
```

The compute pass writes to the SSBO via a `storage_buffer` binding. The render pass reads the same SSBO via a separate `storage_buffer` binding in the vertex stage. The frame graph inserts the correct buffer memory barrier between the two passes.

### Mixed Static and Skinned Geometry

Use separate GBuffer passes with different execution types and vertex formats:

```
GBufferPass_Static  (opaque_geometry, "standard" vertex) ──> GBuffer
GBufferPass_Skinned (skinned_geometry, "skinned" vertex) ──> GBuffer (continued)
IBLLightingPass     (fullscreen) ──> litColorHDR
```

From [`examples/skinned_mesh_test/skinned_pipeline.json`](../../examples/skinned_mesh_test/skinned_pipeline.json), the skinned pass uses a separate vertex format with joint indices and weights, and its entity data binding includes a skeleton set.

---

## Reference: Pipeline Files in the Codebase

| Pipeline | File | Features Demonstrated |
|----------|------|----------------------|
| PBR + IBL + Particles | [`examples/declarative_sponza_test/pbr_ibl_pipeline_v3.json`](../../examples/declarative_sponza_test/pbr_ibl_pipeline_v3.json) | Deferred PBR, IBL, transparency, compute particles, SSBO readback |
| Bloom | [`examples/bloom_test/bloom_pipeline.json`](../../examples/bloom_test/bloom_pipeline.json) | Compute post-processing chain, half-res resources, storage images |
| Particles | [`examples/particle_test/particle_pipeline.json`](../../examples/particle_test/particle_pipeline.json) | Compute simulation, point-list rendering, double-buffered SSBO |
| Skinned Mesh | [`examples/skinned_mesh_test/skinned_pipeline.json`](../../examples/skinned_mesh_test/skinned_pipeline.json) | Skinned vertex format, skeleton SSBO, multiple GBuffer passes |
| Physics | [`examples/physics_test/pipeline.json`](../../examples/physics_test/pipeline.json) | Standard deferred PBR (same pipeline, physics-driven entities) |
