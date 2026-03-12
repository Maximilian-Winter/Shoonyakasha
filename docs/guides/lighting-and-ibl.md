# Lighting and Image-Based Lighting (IBL)

How to illuminate scenes in Shoonyakasha using dynamic lights and HDR
environment-based ambient lighting. Python examples are shown first, with C++
equivalents below.

> **Prerequisite:** This guide assumes you have completed the
> [Python Quickstart](../getting-started/python-quickstart.md) or
> [C++ Quickstart](../getting-started/cpp-quickstart.md) and have a running
> engine with a loaded scene.

---

## Light Types

Shoonyakasha supports three light types, matching the constants exposed in both
Python and C++:

| Type | Constant | Behavior |
|------|----------|----------|
| **Directional** | `LIGHT_DIRECTIONAL` (0) | Infinite-range light from a direction. Simulates the sun -- direction matters, position does not. All objects receive parallel rays. |
| **Point** | `LIGHT_POINT` (1) | Light emitted from a position in all directions. Falls off with distance according to attenuation and range. |
| **Spot** | `LIGHT_SPOT` (2) | Like a point light but confined to a cone defined by inner and outer cone angles, plus a direction. |

Light type values are integers. In Python you can compare or pass them directly:

```python
import shoonyakasha as sk

print(sk.LIGHT_DIRECTIONAL)  # 0
print(sk.LIGHT_POINT)        # 1
print(sk.LIGHT_SPOT)         # 2
```

In C++ the equivalent enum is `Facade::LightType` (`Directional`, `Point`,
`Spot`), or internally `ECS::LightComponent::Type`.

---

## Creating Lights

### Python

The `Engine` class provides convenience methods that create a fully configured
light entity in one call.

#### Directional Light

```python
sun = engine.create_directional_light(
    direction=(-0.5, -1.0, -0.3),   # vec3: direction the light travels
    color=(1.0, 0.975, 0.95),       # vec3: RGB color (warm white)
    intensity=3.0,                   # float: brightness multiplier
)
```

The `direction` vector points **from** the light source (toward the scene).
It does not need to be normalized -- the shader normalizes it.

#### Point Light

```python
lamp = engine.create_point_light(
    pos=(0.0, 3.0, 0.0),           # vec3: world-space position
    color=(1.0, 0.8, 0.6),         # vec3: warm orange
    intensity=5.0,                  # float: brightness multiplier
    range=15.0,                     # float: maximum reach in world units
)
```

Point lights attenuate over distance. Beyond `range`, the light contribution is
zero.

### C++

The same helpers exist on `Facade::EngineAPI`:

```cpp
auto sun = engine.createDirectionalLight(
    glm::vec3(-0.5f, -1.0f, -0.3f),    // direction
    glm::vec3(1.0f, 0.975f, 0.95f),    // color
    3.0f                                 // intensity
);

auto lamp = engine.createPointLight(
    glm::vec3(0.0f, 3.0f, 0.0f),       // position
    glm::vec3(1.0f, 0.8f, 0.6f),       // color
    5.0f,                                // intensity
    15.0f                                // range
);
```

Or with the low-level `EntityBuilder`:

```cpp
auto spotlight = ECS::EntityBuilder(registry)
    .withName("spotlight")
    .withTransform(glm::vec3(0, 5, 0))
    .withLight(ECS::LightComponent::Spot, glm::vec3(1, 1, 1), 4.0f)
    .build();

auto& light = registry.get<ECS::LightComponent>(spotlight);
light.innerCone = 20.0f;
light.outerCone = 35.0f;
light.range = 25.0f;
```

---

## Light Properties

Every light entity has a `LightComponent` with the following properties:

| Property | Type | Applies To | Default | Description |
|----------|------|-----------|---------|-------------|
| `type` | enum | All | `Point` | `Directional`, `Point`, or `Spot` |
| `color` | vec3 | All | `(1, 1, 1)` | RGB color of the emitted light |
| `intensity` | float | All | `1.0` | Brightness multiplier |
| `range` | float | Point, Spot | `10.0` | Maximum distance the light reaches. Beyond this, contribution is clamped to zero. |
| `constant` | float | Point, Spot | `1.0` | Constant attenuation factor |
| `linear` | float | Point, Spot | `0.09` | Linear attenuation factor |
| `quadratic` | float | Point, Spot | `0.032` | Quadratic attenuation factor |
| `innerCone` | float | Spot | `30.0` | Inner cone half-angle in degrees (full intensity) |
| `outerCone` | float | Spot | `45.0` | Outer cone half-angle in degrees (fade-out edge) |
| `castShadows` | bool | All | `false` | Whether this light generates shadow maps |
| `shadowMapSize` | uint32 | All | `1024` | Resolution of the shadow map texture |

Attenuation for point and spot lights follows the formula:

```
attenuation = 1.0 / (constant + linear * d + quadratic * d^2)
```

where `d` is the distance from the light to the surface. The result is further
multiplied by a range falloff: `clamp(1.0 - d / range, 0.0, 1.0)`.

---

## Modifying Lights at Runtime

### Python

Use the `Scene` methods to read and write light properties on any light entity:

```python
scene = engine.scene

# Change color to red
scene.set_light_color(entity, (1.0, 0.0, 0.0))

# Increase brightness
scene.set_light_intensity(entity, 8.0)

# Extend range
scene.set_light_range(entity, 30.0)

# Enable shadow casting
scene.set_light_cast_shadows(entity, True)

# Change light type
scene.set_light_type(entity, sk.LIGHT_SPOT)

# Read current values
color = scene.get_light_color(entity)        # (r, g, b) tuple
intensity = scene.get_light_intensity(entity) # float
light_type = scene.get_light_type(entity)     # int (0, 1, or 2)
```

### C++

Through the Facade:

```cpp
auto& scene = engine.getScene();

scene.setLightColor(entity, glm::vec3(1.0f, 0.0f, 0.0f));
scene.setLightIntensity(entity, 8.0f);
scene.setLightRange(entity, 30.0f);
scene.setLightCastShadows(entity, true);
scene.setLightType(entity, Facade::LightType::Spot);

glm::vec3 color = scene.getLightColor(entity);
float intensity = scene.getLightIntensity(entity);
```

Or directly on the ECS component:

```cpp
auto& light = registry.get<ECS::LightComponent>(entity);
light.color = glm::vec3(1.0f, 0.0f, 0.0f);
light.intensity = 8.0f;
light.innerCone = 25.0f;
light.outerCone = 40.0f;
```

---

## Dynamic Lights

Lights can be created at runtime in response to game events. The demo.py
example spawns a point light at the camera position when the L key is pressed:

```python
light_count = 0

def on_update(dt):
    global light_count
    inp = engine.input

    if inp.is_key_down(76):  # 'L' key
        scene = engine.scene
        cam = engine.camera_entity
        pos = scene.get_position(cam)

        engine.create_point_light(
            pos=pos,
            color=(1.0, 0.8, 0.6),
            intensity=5.0,
            range=20.0,
        )
        light_count += 1
        print(f"Point light #{light_count} at {pos}")
```

> **Performance note.** The engine supports up to 16 lights in the default
> pipeline (defined by `MAX_LIGHTS` in the shader and the array size in the
> `LightsUBO` buffer layout). Lights beyond this limit are silently ignored.
> Modify the pipeline JSON and shader to increase the limit if needed.

---

## HDR Environment (Image-Based Lighting)

### What It Is

Image-Based Lighting (IBL) uses an HDR panoramic image to provide physically
realistic ambient lighting for the entire scene. Instead of flat ambient color,
every surface receives light from the environment based on its orientation and
roughness.

An HDR `.hdr` file (equirectangular format) is processed at engine startup into
three textures:

| Texture | Purpose | Sampling |
|---------|---------|----------|
| **Irradiance Map** | Diffuse IBL -- smooth, low-frequency ambient light from all directions | Sampled with the surface normal |
| **Prefilter Map** | Specular IBL -- environment reflections at varying roughness levels (mip chain) | Sampled with the reflection vector at a mip level based on roughness |
| **BRDF LUT** | Split-sum approximation lookup table for combining Fresnel with specular | Sampled with `(NdotV, roughness)` |

### Configuration

Set the `hdr_environment_path` when creating the engine:

```python
engine = sk.Engine(
    pipeline_json_path="pbr_ibl_pipeline_v3.json",
    hdr_environment_path="cubemaps_hdrs/kloofendal_28d_misty_8k.hdr",
)
```

In C++:

```cpp
Facade::EngineConfig config;
config.pipelineJsonPath   = "pbr_ibl_pipeline_v3.json";
config.hdrEnvironmentPath = "cubemaps_hdrs/kloofendal_28d_misty_8k.hdr";

Facade::EngineAPI engine(config);
```

If `hdr_environment_path` is left empty, no IBL textures are generated and
ambient lighting falls back to the shader's default (a dark background tint).

### What Happens Under the Hood

When the engine initializes with an HDR path:

1. `IBLGenerator` loads the `.hdr` file as a 2D equirectangular texture.
2. A GPU compute shader converts it to a cubemap (`environmentMap`).
3. A convolution compute shader produces the diffuse `irradianceMap`.
4. GGX importance sampling produces the specular `prefilterMap` with mip levels for roughness 0..1.
5. A BRDF integration compute shader produces the `brdfLUT`.
6. All three IBL textures are stored in `SceneEnvironment` and made available to the render pipeline via dot-paths.

This process runs once at startup. The textures persist for the lifetime of the
engine.

---

## IBL in the JSON Pipeline

The JSON render pipeline accesses IBL textures through an `iblSet` descriptor
set layout:

```json
"iblSet": {
  "bindings": [
    { "binding": 0, "type": "combined_image_sampler", "stages": ["fragment"],
      "name": "irradianceMap" },
    { "binding": 1, "type": "combined_image_sampler", "stages": ["fragment"],
      "name": "prefilterMap" },
    { "binding": 2, "type": "combined_image_sampler", "stages": ["fragment"],
      "name": "brdfLUT" }
  ]
}
```

The frame graph automatically binds these textures from the `SceneEnvironment`.
The dot-paths used internally are:

| Dot-Path | Texture |
|----------|---------|
| `scene.environment.irradianceMap` | Diffuse IBL cubemap |
| `scene.environment.prefilterMap` | Specular IBL cubemap (mip-mapped) |
| `scene.environment.brdfLUT` | BRDF integration LUT |

Passes that need IBL include `iblSet` in their `descriptorSets` array. In the
default pipeline, the `IBLLightingPass` (deferred) and `ForwardTransparencyPass`
both bind it:

```json
{
  "name": "IBLLightingPass",
  "type": "graphics",
  "execution": { "type": "fullscreen" },
  "descriptorSets": ["gbufferReadSet", "iblSet", "cameraSet", "lightsSet"]
}
```

---

## How Lights Flow to Shaders

Dynamic lights from ECS entities are packed into a `LightsUBO` uniform buffer
and updated every frame by the frame graph. The JSON buffer layout declares the
data sources using `scene.lights[i].*` dot-paths:

```json
"LightsUBO": {
  "usage": "uniform_buffer",
  "packing": "std140",
  "updateFrequency": "per_frame",
  "fields": [
    { "name": "lightCount",           "type": "uint",  "source": "scene.lights.count" },
    { "name": "lightsPositionType",   "type": "vec4",  "arrayCount": 16,
      "source": "scene.lights[i].positionType" },
    { "name": "lightsColorIntensity", "type": "vec4",  "arrayCount": 16,
      "source": "scene.lights[i].colorIntensity" },
    { "name": "lightsDirectionRange", "type": "vec4",  "arrayCount": 16,
      "source": "scene.lights[i].directionRange" },
    { "name": "lightsAttenuation",    "type": "vec4",  "arrayCount": 16,
      "source": "scene.lights[i].attenuation" }
  ]
}
```

Each `scene.lights[i].*` dot-path resolves to a packed `vec4` combining related
properties:

| Dot-Path | Contents |
|----------|----------|
| `scene.lights[i].positionType` | `xyz` = world position, `w` = light type (0 = directional, 1 = point, 2 = spot) |
| `scene.lights[i].colorIntensity` | `xyz` = RGB color, `w` = intensity |
| `scene.lights[i].directionRange` | `xyz` = direction vector, `w` = range |
| `scene.lights[i].attenuation` | `x` = constant, `y` = linear, `z` = quadratic, `w` = cos(outerCone) |

The `scene.lights.count` path provides the total number of active light entities
in the scene.

In the PBR fragment shader, the UBO is consumed with a simple loop:

```glsl
#define MAX_LIGHTS 16

layout(set = 3, binding = 0) uniform LightsUBO {
    uint lightCount;
    // ... padding ...
    vec4 lightsPositionType[MAX_LIGHTS];
    vec4 lightsColorIntensity[MAX_LIGHTS];
    vec4 lightsDirectionRange[MAX_LIGHTS];
    vec4 lightsAttenuation[MAX_LIGHTS];
};

for (uint i = 0u; i < min(lightCount, uint(MAX_LIGHTS)); i++) {
    float lightType = lightsPositionType[i].w;
    vec3 lColor     = lightsColorIntensity[i].xyz;
    float lIntensity = lightsColorIntensity[i].w;
    // ... compute contribution ...
}
```

---

## Complete Example

A minimal scene with IBL and dynamic lights:

```python
import shoonyakasha as sk

engine = sk.Engine(
    title="Lighting Demo",
    width=1920, height=1080,
    pipeline_json_path="pbr_ibl_pipeline_v3.json",
    hdr_environment_path="cubemaps_hdrs/kloofendal_28d_misty_8k.hdr",
)

def on_init():
    engine.create_camera(pos=(0, 5, 15), fov=60.0)

    # Sun light
    engine.create_directional_light(
        direction=(-0.5, -1.0, -0.3),
        color=(1.0, 0.975, 0.95),
        intensity=3.0,
    )

    # Load a glTF scene (materials + IBL = PBR rendering)
    engine.load_gltf_scene("scene.gltf")

    # Accent light
    accent = engine.create_point_light(
        pos=(2.0, 4.0, -1.0),
        color=(0.3, 0.5, 1.0),
        intensity=4.0,
        range=12.0,
    )

    # Modify the accent light
    scene = engine.scene
    scene.set_light_cast_shadows(accent, True)

engine.set_on_init(on_init)
engine.run()
```

---

## See Also

- [Constants Reference](../api/python/constants.md) -- `LIGHT_DIRECTIONAL`, `LIGHT_POINT`, `LIGHT_SPOT`
- [Scene API (Python)](../api/python/scene.md) -- `set_light_color`, `set_light_intensity`, `get_light_type`, etc.
- [Scene API (C++)](../api/cpp/scene-api.md) -- `setLightColor`, `setLightIntensity`, `getLightType`, etc.
- [IBL Generator Reference](../api/cpp/ibl-generator.md) -- `IBLGenerator`, `IBLResources`, `IBLGenerationParams`
- [JSON Render Pipeline Guide](json-render-pipeline.md) -- buffer layouts, descriptor sets, dot-path resolution
- [ECS Components Reference](../api/cpp/ecs-components.md) -- `LightComponent` fields
- [Materials Guide](materials.md) -- surface properties that interact with lighting
