# GPU Types

`Shoonyakasha::GPUBuffer`, `GPUTexture`, `MaterialParam`, `AlphaMode`, `IndexType` -- thin wrappers around Vulkan handles and GPU data.

> **Data structs, not managers.** These types hold GPU handles but do **not** manage their lifetime. Allocation and deallocation is the responsibility of the owning system (ResourceManager, GltfSceneLoader, etc.).

**Header:** `#include "GPU/GPUTypes.h"`
**Namespace:** `Shoonyakasha`

**Used by:** `MeshComponent`, `MaterialComponentV5`, `FrameGraphRenderer`, and throughout the render pipeline.

---

## AlphaMode

Controls how transparency is handled during rendering.

```cpp
enum class AlphaMode : uint8_t {
    Opaque = 0,   // Fully opaque, no alpha testing
    Mask   = 1,   // Alpha cutoff testing (discard if below threshold)
    Blend  = 2    // Alpha blending (sorted back-to-front)
};
```

| Value | Numeric | Description |
|-------|---------|-------------|
| `Opaque` | `0` | Fully opaque. No alpha testing or blending. |
| `Mask` | `1` | Alpha cutoff testing. Fragments below the threshold are discarded. |
| `Blend` | `2` | Alpha blending. Requires back-to-front sorting for correct results. |

### toString (AlphaMode)

```cpp
const char* toString(AlphaMode mode);
```

Returns `"Opaque"`, `"Mask"`, `"Blend"`, or `"Unknown"`.

---

## IndexType

Specifies the element size for index buffers.

```cpp
enum class IndexType : uint8_t {
    UInt16 = 0,
    UInt32 = 1
};
```

| Value | Numeric | Vulkan Equivalent |
|-------|---------|-------------------|
| `UInt16` | `0` | `VK_INDEX_TYPE_UINT16` |
| `UInt32` | `1` | `VK_INDEX_TYPE_UINT32` |

### toVkIndexType

```cpp
VkIndexType toVkIndexType(IndexType type);
```

Converts to the Vulkan enum value.

---

## GPUBuffer

A thin wrapper around a Vulkan buffer handle and its VMA allocation.

```cpp
struct GPUBuffer {
    VkBuffer      buffer     = VK_NULL_HANDLE;
    VmaAllocation allocation = nullptr;
    VkDeviceSize  size       = 0;

    bool isValid() const;
    void reset();
};
```

### Fields

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `buffer` | `VkBuffer` | `VK_NULL_HANDLE` | Vulkan buffer handle. |
| `allocation` | `VmaAllocation` | `nullptr` | VMA memory allocation. |
| `size` | `VkDeviceSize` | `0` | Buffer size in bytes. |

### Methods

#### isValid

```cpp
bool isValid() const;
```

Returns `true` if `buffer != VK_NULL_HANDLE` (i.e., the buffer has been allocated).

#### reset

```cpp
void reset();
```

Resets all fields to their defaults (null handle, nullptr allocation, zero size). **Does not free GPU memory** -- call the appropriate deallocation function first.

---

## GPUTexture

A complete texture ready for shader binding: image, view, and sampler bundled together.

```cpp
struct GPUTexture {
    VkImage       image      = VK_NULL_HANDLE;
    VkImageView   view       = VK_NULL_HANDLE;
    VkSampler     sampler    = VK_NULL_HANDLE;
    VmaAllocation allocation = nullptr;

    VkFormat      format     = VK_FORMAT_UNDEFINED;
    uint32_t      width      = 0;
    uint32_t      height     = 0;
    uint32_t      mipLevels  = 1;

    bool exists = false;

    bool isValid() const;
    void reset();
};
```

### Fields

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `image` | `VkImage` | `VK_NULL_HANDLE` | Vulkan image handle. |
| `view` | `VkImageView` | `VK_NULL_HANDLE` | Image view for shader access. |
| `sampler` | `VkSampler` | `VK_NULL_HANDLE` | Texture sampler (filtering, addressing). |
| `allocation` | `VmaAllocation` | `nullptr` | VMA memory allocation. |
| `format` | `VkFormat` | `VK_FORMAT_UNDEFINED` | Pixel format (e.g. `VK_FORMAT_R8G8B8A8_SRGB`). |
| `width` | `uint32_t` | `0` | Texture width in pixels. |
| `height` | `uint32_t` | `0` | Texture height in pixels. |
| `mipLevels` | `uint32_t` | `1` | Number of mipmap levels. |
| `exists` | `bool` | `false` | `true` if loaded from actual data; `false` if this is a default/fallback texture. Shaders can use this to branch (e.g. skip normal mapping when no normal map is present). |

### Methods

#### isValid

```cpp
bool isValid() const;
```

Returns `true` if all three handles are non-null (`image`, `view`, and `sampler`). This confirms the texture is ready for `combined_image_sampler` descriptor binding.

#### reset

```cpp
void reset();
```

Resets all fields to their defaults. **Does not free GPU memory.**

---

## MaterialParam

Type-safe parameter storage for material values. Stores scalar, vector, or matrix data with runtime type information. Used in material component parameter maps for generic PBR properties.

```cpp
struct MaterialParam {
    enum class Type : uint8_t {
        Float = 0,
        Vec2  = 1,
        Vec3  = 2,
        Vec4  = 3,
        Mat3  = 4,
        Mat4  = 5,
        Int   = 6,
        UInt  = 7
    };

    Type type = Type::Float;
    std::array<uint8_t, 64> data = {};   // Large enough for mat4 (64 bytes)
};
```

### Type Enum

| Value | Numeric | C++ Type | Size |
|-------|---------|----------|------|
| `Float` | `0` | `float` | 4 bytes |
| `Vec2` | `1` | `glm::vec2` | 8 bytes |
| `Vec3` | `2` | `glm::vec3` | 12 bytes |
| `Vec4` | `3` | `glm::vec4` | 16 bytes |
| `Mat3` | `4` | `glm::mat3` | 36 bytes |
| `Mat4` | `5` | `glm::mat4` | 64 bytes |
| `Int` | `6` | `int32_t` | 4 bytes |
| `UInt` | `7` | `uint32_t` | 4 bytes |

### Factory Methods — from()

Static factory methods that create a `MaterialParam` with the correct type tag and data.

```cpp
static MaterialParam from(float v);
static MaterialParam from(const glm::vec2& v);
static MaterialParam from(const glm::vec3& v);
static MaterialParam from(const glm::vec4& v);
static MaterialParam from(const glm::mat3& m);
static MaterialParam from(const glm::mat4& m);
static MaterialParam from(int32_t v);
static MaterialParam from(uint32_t v);
```

**Example:**

```cpp
auto param = MaterialParam::from(glm::vec4(1.0f, 0.0f, 0.0f, 1.0f));
// param.type == MaterialParam::Type::Vec4
```

### Accessor — as\<T\>()

```cpp
template<typename T>
T as() const;
```

Reads the stored data as type `T`. The caller is responsible for matching `T` to the stored `type`. Static-asserts that `sizeof(T) <= 64`.

**Example:**

```cpp
glm::vec4 color = param.as<glm::vec4>();
```

### Size Helpers

#### byteSize

```cpp
size_t byteSize() const;
```

Returns the byte size of the stored type (e.g. 16 for `Vec4`, 64 for `Mat4`).

#### rawData

```cpp
const void* rawData() const;
void* rawData();
```

Returns a raw pointer to the internal data array. Useful for `memcpy` into uniform buffers.

### toString (MaterialParam::Type)

```cpp
const char* toString(MaterialParam::Type type);
```

Returns the GLSL-style name: `"float"`, `"vec2"`, `"vec3"`, `"vec4"`, `"mat3"`, `"mat4"`, `"int"`, `"uint"`, or `"unknown"`.

---

## Usage Example

```cpp
#include "GPU/GPUTypes.h"

// Check if a buffer is allocated
GPUBuffer vertexBuffer;
if (!vertexBuffer.isValid()) {
    // ... allocate buffer ...
}

// Check texture readiness
GPUTexture albedo;
if (albedo.isValid() && albedo.exists) {
    // Bind as combined_image_sampler
}

// Material parameter storage
std::unordered_map<std::string, MaterialParam> params;
params["baseColorFactor"]  = MaterialParam::from(glm::vec4(1, 0, 0, 1));
params["metallicFactor"]   = MaterialParam::from(0.0f);
params["roughnessFactor"]  = MaterialParam::from(0.5f);

// Read back
glm::vec4 color = params["baseColorFactor"].as<glm::vec4>();
float roughness = params["roughnessFactor"].as<float>();
```

---

## See Also

- [ECS Components](ecs-components.md) -- `MeshComponent` and `MaterialComponentV5` use these types
- [ApplicationBase](application-base.md) -- application framework
- [EngineAPI](engine-api.md) -- facade alternative
