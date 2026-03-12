# IBL Generator

`Shoonyakasha::IBLGenerator` -- generates image-based lighting resources for PBR rendering from HDR environment maps using GPU compute shaders.

> **HDR in, PBR lighting out.** Feed an equirectangular HDR image and receive four textures: environment cubemap, irradiance map (diffuse IBL), prefilter map (specular IBL with roughness mip levels), and BRDF LUT (split-sum approximation).

**Header:** `#include "IBL/IBLGenerator.h"`
**Namespace:** `Shoonyakasha`

**Dependencies:** VulkanDevice, VulkanCubemap, VulkanTexture, VulkanComputePipeline.

---

## PBR IBL Pipeline Overview

Image-Based Lighting uses pre-computed textures to provide realistic ambient lighting from an environment map. The IBL generator performs four GPU compute stages:

```
HDR Image (.hdr)
     |
     v
[1] Equirect-to-Cubemap -----> environmentMap (cubemap, 6 faces)
     |
     +---> [2] Irradiance Convolution -----> irradianceMap (small cubemap)
     |         Cosine-weighted hemisphere integral
     |         Used for: diffuse ambient lighting
     |
     +---> [3] Prefilter Convolution ------> prefilterMap (cubemap with mip chain)
     |         GGX importance sampling per roughness level
     |         Used for: specular reflections at varying roughness
     |
     +---> [4] BRDF Integration LUT -------> brdfLUT (2D texture)
               Split-sum approximation: F0 scale + F0 bias
               Used for: combining specular with Fresnel
```

In a PBR fragment shader, the three maps are used together:

- **Irradiance map**: sampled with the surface normal to get diffuse ambient light.
- **Prefilter map**: sampled with the reflection vector at a mip level corresponding to material roughness to get specular ambient light.
- **BRDF LUT**: sampled with `(NdotV, roughness)` to get the Fresnel-weighted scale and bias for the specular term.

---

## IBLGenerationParams

Controls the resolution and quality of generated IBL textures.

```cpp
struct IBLGenerationParams {
    uint32_t environmentSize   = 1024;  // Environment cubemap face size
    uint32_t irradianceSize    = 32;    // Irradiance cubemap face size
    uint32_t prefilterSize     = 512;   // Prefilter cubemap face size (highest mip)
    uint32_t brdfLUTSize       = 512;   // BRDF LUT dimensions (square)
    uint32_t irradianceSamples = 2048;  // Hemisphere samples for irradiance
    uint32_t prefilterSamples  = 1024;  // GGX samples per roughness level
    uint32_t brdfSamples       = 1024;  // BRDF integration samples
};
```

| Field | Default | Description |
|-------|---------|-------------|
| `environmentSize` | `1024` | Face resolution of the converted environment cubemap (6 faces, each `size x size`). Higher values preserve HDR detail for reflections. |
| `irradianceSize` | `32` | Face resolution of the irradiance cubemap. Low resolution is sufficient because irradiance varies smoothly across directions. |
| `prefilterSize` | `512` | Face resolution of the prefilter cubemap at mip level 0 (smoothest reflections). Each successive mip level represents higher roughness. |
| `brdfLUTSize` | `512` | Width and height of the 2D BRDF integration LUT. |
| `irradianceSamples` | `2048` | Number of hemisphere samples for the irradiance convolution. More samples reduce noise. |
| `prefilterSamples` | `1024` | Number of GGX importance samples per roughness level for the prefilter convolution. |
| `brdfSamples` | `1024` | Number of samples for the BRDF split-sum integration. |

### Quality Presets

For quick prototyping, use smaller sizes:

```cpp
IBLGenerationParams fast{
    .environmentSize   = 512,
    .irradianceSize    = 16,
    .prefilterSize     = 256,
    .brdfLUTSize       = 256,
    .irradianceSamples = 512,
    .prefilterSamples  = 256,
    .brdfSamples       = 256
};
```

For production quality, increase sample counts:

```cpp
IBLGenerationParams quality{
    .environmentSize   = 2048,
    .irradianceSize    = 64,
    .prefilterSize     = 1024,
    .brdfLUTSize       = 512,
    .irradianceSamples = 4096,
    .prefilterSamples  = 2048,
    .brdfSamples       = 2048
};
```

---

## IBLResources

Holds all four textures produced by the IBL generation process.

```cpp
struct IBLResources {
    VulkanCubemap* environmentMap = nullptr;  // Converted from equirectangular
    VulkanCubemap* irradianceMap  = nullptr;  // Diffuse IBL
    VulkanCubemap* prefilterMap   = nullptr;  // Specular IBL with roughness mips
    VulkanTexture* brdfLUT        = nullptr;  // Split-sum approximation LUT

    bool isValid() const;
    void destroy();
};
```

| Field | Type | Description |
|-------|------|-------------|
| `environmentMap` | `VulkanCubemap*` | The HDR environment converted to a cubemap. Can be used for skybox rendering. |
| `irradianceMap` | `VulkanCubemap*` | Diffuse IBL cubemap. Small, smooth. Sample with the surface normal. |
| `prefilterMap` | `VulkanCubemap*` | Specular IBL cubemap with mip chain. Each mip level corresponds to increasing roughness. Sample with the reflection vector at `roughness * maxMipLevel`. |
| `brdfLUT` | `VulkanTexture*` | 2D lookup texture. Sample with `(NdotV, roughness)` to get `(F0_scale, F0_bias)`. |

### isValid

```cpp
bool isValid() const;
```

Returns `true` if all four resource pointers are non-null.

### destroy

```cpp
void destroy();
```

Deletes all four resources and sets pointers to `nullptr`. The caller is responsible for calling `destroy()` when the IBL resources are no longer needed.

> **Ownership note.** `IBLResources` owns the allocated textures. If you transfer ownership elsewhere, set the pointers to `nullptr` before the struct is destroyed to avoid double-free.

---

## IBLGenerator

The generator class. Uses compute shaders to perform all IBL processing on the GPU.

```cpp
class IBLGenerator {
public:
    explicit IBLGenerator(VulkanDevice& device,
                          const std::string& shaderBasePath = "shaders/ibl/");
    ~IBLGenerator();

    // Non-copyable
    IBLGenerator(const IBLGenerator&) = delete;
    IBLGenerator& operator=(const IBLGenerator&) = delete;

    // Full pipeline
    IBLResources generate(const std::string& hdrPath,
                          const IBLGenerationParams& params = IBLGenerationParams{});

    // Individual steps
    VulkanCubemap* convertEquirectToCubemap(VulkanTexture* equirect, uint32_t cubeSize);
    VulkanCubemap* generateIrradianceMap(VulkanCubemap* environment, uint32_t size,
                                         uint32_t samples);
    VulkanCubemap* generatePrefilterMap(VulkanCubemap* environment, uint32_t size,
                                        uint32_t samples);
    VulkanTexture* generateBRDFLUT(uint32_t size, uint32_t samples);
};
```

### Constructor

```cpp
explicit IBLGenerator(VulkanDevice& device,
                      const std::string& shaderBasePath = "shaders/ibl/");
```

Creates the generator with the required Vulkan device and path to IBL compute shaders.

| Parameter | Default | Description |
|-----------|---------|-------------|
| `device` | -- | Vulkan device for resource allocation and compute dispatch. |
| `shaderBasePath` | `"shaders/ibl/"` | Directory containing the IBL compute shader SPIR-V files. |

The constructor creates descriptor set layouts, a descriptor pool, and samplers needed by the compute pipelines.

### generate

```cpp
IBLResources generate(const std::string& hdrPath,
                      const IBLGenerationParams& params = IBLGenerationParams{});
```

The main entry point. Performs the full IBL pipeline:

1. Loads the HDR image from `hdrPath` as a 2D equirectangular texture.
2. Converts it to a cubemap (`environmentMap`).
3. Convolves the cubemap to produce the irradiance map (`irradianceMap`).
4. Prefilters the cubemap at multiple roughness levels (`prefilterMap`).
5. Generates the BRDF integration LUT (`brdfLUT`).

Returns an `IBLResources` struct. Check `isValid()` to confirm all textures were created successfully.

```cpp
IBLGenerator generator(device);
auto ibl = generator.generate("textures/environment.hdr");

if (ibl.isValid()) {
    // Use ibl.irradianceMap, ibl.prefilterMap, ibl.brdfLUT in PBR shaders
    // Use ibl.environmentMap for skybox rendering
}

// When done:
ibl.destroy();
```

### convertEquirectToCubemap

```cpp
VulkanCubemap* convertEquirectToCubemap(VulkanTexture* equirect, uint32_t cubeSize);
```

Converts a 2D equirectangular HDR texture to a cubemap with 6 faces. Each face has dimensions `cubeSize x cubeSize`.

| Parameter | Description |
|-----------|-------------|
| `equirect` | Source equirectangular texture (typically loaded from an HDR file). |
| `cubeSize` | Face resolution of the output cubemap. |

Returns a newly allocated `VulkanCubemap*`. The caller owns the returned pointer.

### generateIrradianceMap

```cpp
VulkanCubemap* generateIrradianceMap(VulkanCubemap* environment, uint32_t size,
                                     uint32_t samples);
```

Generates the diffuse irradiance map by convolving the environment cubemap with a cosine-weighted hemisphere integral.

| Parameter | Description |
|-----------|-------------|
| `environment` | Source environment cubemap. |
| `size` | Face resolution of the output irradiance cubemap. |
| `samples` | Number of hemisphere samples. More samples = less noise, slower. |

Returns a newly allocated `VulkanCubemap*`. Typical output sizes are 32x32 or 64x64 per face -- irradiance is inherently low-frequency.

### generatePrefilterMap

```cpp
VulkanCubemap* generatePrefilterMap(VulkanCubemap* environment, uint32_t size,
                                    uint32_t samples);
```

Generates the specular prefilter map using GGX importance sampling. The output cubemap has a full mip chain where each mip level represents a different roughness value (mip 0 = mirror-smooth, highest mip = fully rough).

| Parameter | Description |
|-----------|-------------|
| `environment` | Source environment cubemap. |
| `size` | Face resolution at mip level 0. |
| `samples` | Number of GGX samples per roughness level. |

Returns a newly allocated `VulkanCubemap*` with mipmaps.

### generateBRDFLUT

```cpp
VulkanTexture* generateBRDFLUT(uint32_t size, uint32_t samples);
```

Generates a 2D BRDF integration lookup table for the split-sum approximation. The texture encodes `(F0_scale, F0_bias)` indexed by `(NdotV, roughness)`.

| Parameter | Description |
|-----------|-------------|
| `size` | Width and height of the output texture. |
| `samples` | Number of integration samples. |

Returns a newly allocated `VulkanTexture*`. This texture is view-independent and can be shared across all materials.

---

## Usage with the Frame Graph

The generated IBL resources integrate with the rendering pipeline through the `SceneContext` and dot-path system:

```cpp
// Generate IBL
IBLGenerator generator(device);
auto ibl = generator.generate("textures/studio.hdr");

// Register with SceneEnvironment (makes them available via dot-paths)
SceneEnvironment env;
env.irradianceMap = /* wrap ibl.irradianceMap as GPUTexture */;
env.prefilterMap  = /* wrap ibl.prefilterMap as GPUTexture */;
env.brdfLUT       = /* wrap ibl.brdfLUT as GPUTexture */;

sceneContext.environment = &env;
```

In the JSON render pipeline, these textures are accessed via dot-paths:

```json
{
  "source": "scene.environment.irradianceMap"
}
```

```json
{
  "source": "scene.environment.prefilterMap"
}
```

```json
{
  "source": "scene.environment.brdfLUT"
}
```

See the [Frame Graph](frame-graph.md) reference for full dot-path documentation.
