# Loading Scenes

How to load 3D models and scenes into the Shoonyakasha engine using glTF 2.0
files. Python examples are shown first, with C++ equivalents below.

> **Prerequisite:** This guide assumes you have completed the
> [Python Quickstart](../getting-started/python-quickstart.md) or
> [C++ Quickstart](../getting-started/cpp-quickstart.md) and have a working
> engine setup with a pipeline JSON and camera.

---

## glTF Format Overview

Shoonyakasha uses **glTF 2.0** as its primary 3D asset format. glTF (GL
Transmission Format) is an open standard designed for efficient delivery of 3D
content, and it is the format the engine's loader is built around.

**What glTF supports:**

- **Meshes** -- triangle geometry with positions, normals, UVs, and tangents
- **Materials** -- PBR metallic-roughness workflow (base color, metallic,
  roughness, normal, AO, emissive)
- **Textures** -- embedded or external image files (PNG, JPEG, KTX2)
- **Scene hierarchy** -- parent-child node transforms
- **Skeletal animation** -- skins with joint hierarchies, inverse bind matrices,
  and keyframe animation clips

**Supported file extensions:**

| Extension | Format | Description |
|-----------|--------|-------------|
| `.gltf`   | JSON + separate files | Human-readable JSON with external `.bin` and image files. Good for development and version control. |
| `.glb`    | Binary bundle | Everything packed into a single binary file. Best for distribution and faster loading. |

Both formats contain identical data -- the engine handles them interchangeably.
Use `.gltf` during development when you want to inspect or diff the scene
structure, and `.glb` for shipping where single-file convenience and faster
parse times matter.

---

## Basic Loading (Python)

The simplest way to load a 3D scene is a single call to
`engine.load_gltf_scene()` inside your `on_init` callback:

```python
import shoonyakasha as sk

engine = sk.Engine(
    title="Scene Loader",
    pipeline_json_path="pbr_ibl_pipeline_v3.json",
    hdr_environment_path="environment.hdr",
)

def on_init():
    engine.create_camera(pos=(0, 5, 15))
    engine.create_directional_light((-0.5, -1.0, -0.3), intensity=3.0)

    result = engine.load_gltf_scene("models/MyScene.glb")
    if result.success:
        print(f"Loaded {result.total_vertices} verts, "
              f"{result.total_materials} materials")
        print(f"Created {len(result.entities)} entities")
    else:
        print(f"Failed to load scene: {result.error}")

engine.set_on_init(on_init)
engine.run()
```

With default options, `load_gltf_scene` loads everything the file contains:
textures, materials, meshes, skins, and animations. Each mesh primitive becomes
an ECS entity with the appropriate rendering components attached. The entities
are ready to render immediately -- no additional setup required.

---

## Loading with Options (Python)

Pass keyword arguments to control exactly what gets loaded:

```python
result = engine.load_gltf_scene("models/Character.glb",
    load_textures=True,
    load_materials=True,
    load_skins=True,
    load_animations=True,
    flatten_hierarchy=True,
    generate_mipmaps=True,
    name_prefix="player_",
)
```

### Option Reference

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `load_textures` | `bool` | `True` | Load and upload texture images to the GPU. Set `False` to skip textures (e.g. for a collision-only mesh). |
| `load_materials` | `bool` | `True` | Parse PBR material parameters (base color factor, metallic, roughness, emissive). |
| `create_entities` | `bool` | `True` | Create ECS entities with mesh, material, and renderable components. Set `False` if you only need raw primitive data. |
| `load_skins` | `bool` | `True` | Load skeletal skin data (joint hierarchies and inverse bind matrices). Required for animated characters. |
| `load_animations` | `bool` | `True` | Load animation clips (keyframe data). Only meaningful when `load_skins` is also `True`. |
| `flatten_hierarchy` | `bool` | `True` | Bake the glTF node hierarchy into world-space transforms. Best for static meshes that do not need a runtime scene graph. Skinned meshes are unaffected by this option. |
| `max_texture_size` | `int` | `0` | Maximum texture dimension in pixels. `0` means no limit. Textures exceeding this are downscaled during loading. Useful for capping VRAM usage on lower-end hardware. |
| `generate_mipmaps` | `bool` | `True` | Generate mipmap chains for loaded textures. Improves rendering quality at a distance and reduces aliasing. |
| `srgb_albedo` | `bool` | `True` | Interpret albedo/base color textures as sRGB. Normal maps and metallic-roughness maps are always loaded as linear regardless of this setting. |
| `name_prefix` | `str` | `""` | A string prepended to all entity names created from this file. Essential when loading multiple copies of the same model to avoid name collisions. |

**When to change defaults:**

- **Static environment (Sponza, buildings):** Use defaults. You typically do not
  need skins or animations, but leaving them on is harmless if the file has none.
- **Animated character:** Keep `load_skins=True` and `load_animations=True`.
- **Performance-constrained scene:** Set `max_texture_size=2048` and
  `load_animations=False` if the model has no animations.
- **Multiple instances of the same model:** Always set `name_prefix` so each
  instance's entities have unique names.

---

## C++ Equivalent

The C++ facade API (`EngineAPI`) mirrors the Python API. Options are passed via a
`GltfOptions` struct instead of keyword arguments.

### Basic Loading

```cpp
#include "Facade/EngineAPI.h"

using namespace Shoonyakasha::Facade;

// Inside your onInit callback:
auto result = engine.loadGltfScene("models/MyScene.glb");
if (result.success) {
    printf("Loaded %zu vertices, %zu materials\n",
           result.totalVertices, result.totalMaterials);
    printf("Created %zu entities\n", result.entities.size());
} else {
    printf("Failed: %s\n", result.error.c_str());
}
```

### Loading with Options

```cpp
GltfOptions options;
options.loadTextures     = true;
options.loadMaterials    = true;
options.loadSkins        = true;
options.loadAnimations   = true;
options.flattenHierarchy = true;
options.generateMipmaps  = true;
options.namePrefix       = "player_";

auto result = engine.loadGltfScene("models/Character.glb", options);
```

### Direct Loader (Advanced C++)

If you are working with `ApplicationBase` directly (not the facade), use the
`GltfSceneLoader` class, which takes a `VulkanDevice` and an optional
`ECS::Scene`:

```cpp
#include "Resources/GltfSceneLoader.h"

GltfSceneLoader loader(device);

GltfLoadOptions options;
options.maxTextureSize = 2048;
options.namePrefix     = "sponza";

auto result = loader.load("models/Sponza.gltf", scene, options);

if (result.success) {
    // result.primitives  -- raw GPU buffers and textures
    // result.entities    -- ECS entities (if scene was provided)
    // result.skeletons   -- skeleton data
    // result.animationClips -- animation clip data
}
```

---

## Inspecting Results

Every `load_gltf_scene` call returns a result object containing success status,
statistics, and entity handles. Always check `success` before using any other
field.

### Python

```python
result = engine.load_gltf_scene("models/Scene.glb")

# Always check success first
if not result.success:
    print(f"Load failed: {result.error}")
    return

# Statistics
print(f"Vertices:  {result.total_vertices}")
print(f"Indices:   {result.total_indices}")
print(f"Textures:  {result.total_textures}")
print(f"Materials: {result.total_materials}")
print(f"Entities:  {len(result.entities)}")

# Animation info (populated when load_animations=True)
print(f"Skeletons: {result.skeleton_count}")
for name, duration in result.animation_clips:
    print(f"  Clip '{name}': {duration:.2f}s")

# Quick summary via repr
print(result)
# GltfResult(success=True, entities=42, vertices=128000, textures=16)
```

### C++ (Facade)

```cpp
auto result = engine.loadGltfScene("models/Scene.glb");

if (!result.success) {
    printf("Load failed: %s\n", result.error.c_str());
    return;
}

printf("Vertices:  %zu\n", result.totalVertices);
printf("Indices:   %zu\n", result.totalIndices);
printf("Textures:  %zu\n", result.totalTextures);
printf("Materials: %zu\n", result.totalMaterials);
printf("Entities:  %zu\n", result.entities.size());
printf("Skeletons: %zu\n", result.skeletonCount);

for (auto& clip : result.animationClips) {
    printf("  Clip '%s': %.2fs\n", clip.name.c_str(), clip.duration);
}
```

---

## Working with Loaded Entities

Once a scene is loaded, the entities it created are live in the ECS. You can
find, iterate, and manipulate them through the Scene API.

### Find by Name

Entity names come from the glTF node names (prefixed with `name_prefix` if you
set one):

```python
scene = engine.scene

# Find a specific entity
player_mesh = scene.find_entity_by_name("player_Mesh")
if player_mesh != sk.NULL_ENTITY:
    pos = scene.get_position(player_mesh)
    print(f"Found player mesh at {pos}")
```

### Iterate All Loaded Entities

The `result.entities` list contains every entity handle created during loading:

```python
result = engine.load_gltf_scene("models/Scene.glb")

scene = engine.scene
for entity in result.entities:
    name = scene.get_name(entity)
    pos = scene.get_position(entity)
    print(f"  {name}: position={pos}")
```

### Modify Materials at Runtime

Each loaded entity has PBR material parameters you can adjust:

```python
scene = engine.scene

# Make a surface shinier
scene.set_material_float(entity, "roughness", 0.1)
scene.set_material_float(entity, "metallic", 1.0)

# Tint the base color
scene.set_material_vec4(entity, "baseColorFactor", (0.8, 0.2, 0.1, 1.0))

# Add emissive glow
scene.set_material_vec3(entity, "emissiveColor", (1.0, 0.5, 0.0))

# Check if a parameter exists before reading
if scene.has_material_param(entity, "roughness"):
    r = scene.get_material_float(entity, "roughness")
    print(f"Roughness: {r}")
```

### Toggle Visibility

Hide or show individual entities without destroying them:

```python
scene = engine.scene

# Hide an entity from rendering
scene.set_visible(entity, False)

# Show it again
scene.set_visible(entity, True)

# Also control shadow casting independently
scene.set_cast_shadows(entity, False)
```

### Deactivate Entities

For a more thorough disable that skips all systems (not just rendering):

```python
scene.set_active(entity, False)   # skipped by all systems
scene.set_active(entity, True)    # re-enable
```

---

## Loading Multiple Models

Real scenes typically combine several glTF files: an environment, characters,
and props. Load each separately and position them with `set_position`.

```python
def on_init():
    engine.create_camera(pos=(0, 5, 15))
    engine.create_directional_light((-0.5, -1.0, -0.3), intensity=3.0)

    # Load environment
    env = engine.load_gltf_scene(
        "models/Sponza.gltf",
        name_prefix="env_",
    )
    print(f"Environment: {len(env.entities)} entities")

    # Load character
    char = engine.load_gltf_scene(
        "models/Character.glb",
        load_skins=True,
        load_animations=True,
        name_prefix="hero_",
    )
    print(f"Character: {len(char.entities)} entities")

    # Position the character in the scene
    scene = engine.scene
    for entity in char.entities:
        scene.set_position(entity, (0.0, 0.0, 5.0))

    # Load a second character instance
    companion = engine.load_gltf_scene(
        "models/Character.glb",
        load_skins=True,
        load_animations=True,
        name_prefix="companion_",
    )

    # Place it elsewhere
    for entity in companion.entities:
        scene.set_position(entity, (3.0, 0.0, 5.0))
```

Using `name_prefix` is critical when loading the same file more than once. It
ensures each instance's entities have unique names, allowing you to find them
later with `find_entity_by_name`.

---

## Animated Models

Loading an animated model follows the same pattern, but you also get skeleton
and animation clip data in the result.

### Loading and Inspecting Animations

```python
result = engine.load_gltf_scene(
    "models/Fox.glb",
    load_skins=True,
    load_animations=True,
    name_prefix="fox_",
)

if result.success:
    print(f"Skeletons: {result.skeleton_count}")
    print(f"Animation clips:")
    for i, (name, duration) in enumerate(result.animation_clips):
        print(f"  [{i}] {name} ({duration:.2f}s)")
```

### Starting Playback

Animation playback is controlled per-entity through the Scene API. Find the
entities that have animations attached and start playing:

```python
scene = engine.scene

for entity in result.entities:
    clip_count = scene.get_animation_clip_count(entity)
    if clip_count > 0:
        # Play the first clip, looping
        scene.set_animation_looping(entity, True)
        scene.play_animation(entity, 0)
        print(f"Playing '{scene.get_animation_clip_name(entity, 0)}' "
              f"on entity {scene.get_name(entity)}")
```

### Switching Clips and Controlling Speed

```python
# Switch to a different clip
scene.play_animation(entity, 1)

# Adjust playback speed
scene.set_animation_speed(entity, 2.0)   # double speed
scene.set_animation_speed(entity, 0.5)   # half speed

# Pause and resume
scene.stop_animation(entity)
current_clip = scene.get_current_animation_clip(entity)
if current_clip >= 0:
    scene.play_animation(entity, current_clip)  # resume

# Scrub to a specific time
scene.set_animation_time(entity, 1.5)
```

For the full animation API and more advanced patterns (blending, events),
see the [Animation guide](animation.md).

---

## Common Patterns

### Load During Init, Not During Update

Scene loading involves disk I/O, texture uploads, and GPU buffer creation. Do
this in `on_init` or `on_post_init`, never in `on_update`:

```python
# Good -- load once during initialization
def on_init():
    engine.load_gltf_scene("models/Scene.glb")

# Bad -- loading every frame kills performance
def on_update(dt):
    engine.load_gltf_scene("models/Scene.glb")  # DO NOT do this
```

### Always Check result.success

The loader can fail for several reasons: missing file, corrupt data,
unsupported features, or GPU resource allocation failure. Always check before
using the result:

```python
result = engine.load_gltf_scene("models/Scene.glb")
if not result.success:
    print(f"Load failed: {result.error}")
    return

# Safe to use result.entities from here
for entity in result.entities:
    ...
```

### Use name_prefix for Multiple Instances

When loading the same model more than once, always set a unique `name_prefix`.
Without it, both instances will have identically-named entities, making
`find_entity_by_name` unreliable:

```python
# Load two copies of the same model with distinct prefixes
npc_a = engine.load_gltf_scene("models/NPC.glb", name_prefix="npc_a_")
npc_b = engine.load_gltf_scene("models/NPC.glb", name_prefix="npc_b_")

# Now you can find each one independently
scene = engine.scene
a_mesh = scene.find_entity_by_name("npc_a_Body")
b_mesh = scene.find_entity_by_name("npc_b_Body")
```

### Cap Texture Size for Performance

If VRAM is limited or you are prototyping and want faster load times, cap the
maximum texture dimension:

```python
result = engine.load_gltf_scene(
    "models/LargeScene.glb",
    max_texture_size=1024,
)
```

---

## See Also

- [Animation](animation.md) -- skeletal animation playback, clip switching, speed control
- [Materials](materials.md) -- PBR material parameters, runtime modification
- [Python Engine API](../api/python/engine.md) -- full `engine.load_gltf_scene()` reference
- [Python GltfResult API](../api/python/gltf-result.md) -- result object fields and methods
- [Python Scene API](../api/python/scene.md) -- entity queries, transforms, visibility
- [C++ EngineAPI](../api/cpp/engine-api.md) -- C++ `loadGltfScene()` reference
- [C++ glTF Loader](../api/cpp/gltf-loader.md) -- low-level `GltfSceneLoader` class
