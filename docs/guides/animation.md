# Animation

How to load and play skeletal animations in the Shoonyakasha engine. Python
examples are shown first, with C++ details below.

> **Prerequisite:** This guide assumes you have completed the
> [Python Quickstart](../getting-started/python-quickstart.md) or
> [C++ Quickstart](../getting-started/cpp-quickstart.md) and have a working
> engine setup. You should also be familiar with
> [Loading Scenes](loading-scenes.md) since animations come from glTF files.

---

## Skeletal Animation Overview

Skeletal animation in Shoonyakasha uses a standard bone-based pipeline:

1. **Joints** form a skeleton hierarchy (parent-child tree of bones)
2. **Keyframes** define per-joint transforms (translation, rotation, scale) at
   specific times
3. **The engine samples keyframes** on CPU each frame, computing the final bone
   matrix for every joint
4. **Bone matrices are uploaded** to the GPU via an SSBO (Shader Storage Buffer
   Object)
5. **The vertex shader** applies skinning -- each vertex is transformed by a
   weighted blend of up to four bone matrices

This all happens automatically once you load an animated model and start
playback. You do not need to write any skinning code.

---

## Loading Animated Models

Animated models must be in glTF format (`.gltf` or `.glb`) with skeletal data
embedded. Load them with `load_skins=True` and `load_animations=True` (both are
`True` by default):

```python
result = engine.load_gltf_scene("models/Fox.glb",
    load_skins=True,
    load_animations=True,
)
```

The result object contains animation metadata you can inspect immediately:

```python
if result.success:
    print(f"Skeletons: {result.skeleton_count}")
    print(f"Animation clips:")
    for name, duration in result.animation_clips:
        print(f"  {name} ({duration:.2f}s)")
```

`result.animation_clips` is a list of `(name, duration)` tuples describing
every animation clip found in the file. `result.skeleton_count` tells you how
many skeletons were loaded (usually one per character model).

---

## Querying Animation Data

After loading, you can query animation clips per entity through the Scene API.
Not every entity from a glTF file has animations -- only entities with a
skeleton and playback component will report clips.

```python
scene = engine.scene

for entity in result.entities:
    count = scene.get_animation_clip_count(entity)
    if count > 0:
        name = scene.get_name(entity)
        print(f"Entity '{name}' has {count} clips:")
        for i in range(count):
            clip_name = scene.get_animation_clip_name(entity, i)
            duration = scene.get_animation_clip_duration(entity, i)
            print(f"  Clip {i}: {clip_name} ({duration:.2f}s)")
```

Typical output for a character model:

```
Entity 'fox_Fox' has 3 clips:
  Clip 0: Survey (2.42s)
  Clip 1: Walk (0.83s)
  Clip 2: Run (0.71s)
```

---

## Playing Animations

Start playback by calling `play_animation` with the entity handle and a clip
index:

```python
scene = engine.scene

# Play the first clip
scene.play_animation(entity, 0)

# Enable looping so it repeats
scene.set_animation_looping(entity, True)
```

The animation starts from time zero and advances automatically each frame. If
looping is enabled, the animation wraps around when it reaches the end.

### Finding and Playing on All Animated Entities

A loaded model may produce multiple entities. To start animation on all of
them:

```python
scene = engine.scene

for entity in result.entities:
    if scene.get_animation_clip_count(entity) > 0:
        scene.set_animation_looping(entity, True)
        scene.play_animation(entity, 0)
```

---

## Playback Control

### Speed

Adjust the playback speed multiplier. `1.0` is normal speed, `2.0` is double,
`0.5` is half.

```python
scene.set_animation_speed(entity, 1.5)   # 1.5x speed
scene.set_animation_speed(entity, 0.5)   # half speed

speed = scene.get_animation_speed(entity)
print(f"Current speed: {speed}x")
```

### Time (Scrubbing)

Set the playback time directly to jump to a specific point in the animation:

```python
scene.set_animation_time(entity, 0.0)    # jump to beginning
scene.set_animation_time(entity, 1.5)    # jump to 1.5 seconds in

t = scene.get_animation_time(entity)
print(f"Current time: {t:.2f}s")
```

### Stop and Resume

```python
# Stop playback (pauses and resets time)
scene.stop_animation(entity)

# Check if playing
if scene.is_animation_playing(entity):
    print("Animation is running")

# Resume the current clip
current = scene.get_current_animation_clip(entity)
if current >= 0:
    scene.play_animation(entity, current)
```

### Looping

```python
scene.set_animation_looping(entity, True)   # loop forever
scene.set_animation_looping(entity, False)  # play once and stop

if scene.is_animation_looping(entity):
    print("Animation loops")
```

---

## Switching Clips

Call `play_animation` with a different clip index to switch animations. Playback
resets to time zero and starts the new clip immediately.

```python
scene = engine.scene

def on_key_pressed(key):
    for entity in animated_entities:
        clip_count = scene.get_animation_clip_count(entity)

        if key == 52 and clip_count > 0:       # '4' -- first clip (e.g. idle)
            scene.play_animation(entity, 0)
            print(f"Playing: {scene.get_animation_clip_name(entity, 0)}")

        elif key == 53 and clip_count > 1:     # '5' -- second clip (e.g. walk)
            scene.play_animation(entity, 1)
            print(f"Playing: {scene.get_animation_clip_name(entity, 1)}")

        elif key == 54 and clip_count > 2:     # '6' -- third clip (e.g. run)
            scene.play_animation(entity, 2)
            print(f"Playing: {scene.get_animation_clip_name(entity, 2)}")
```

---

## Full Example: Animated Fox

This is a condensed version of the `skinned_fox_demo.py` example. It loads the
Fox model, starts animation playback, and allows switching clips and controlling
speed with the keyboard.

```python
import shoonyakasha as sk

engine = sk.Engine(
    title="Animated Fox",
    pipeline_json_path="skinned_pipeline.json",
    hdr_environment_path="environment.hdr",
)

animated_entities = []
anim_speed = 1.0

def on_init():
    engine.create_camera(pos=(0, 40, 200), fov=60.0, speed=50.0,
                         near_plane=1.0, far_plane=2000.0)
    engine.create_directional_light((-0.5, -1.0, -0.3),
                                    color=(1.0, 0.975, 0.95), intensity=3.0)

    result = engine.load_gltf_scene("models/Fox.glb",
        load_skins=True,
        load_animations=True,
    )

    if not result.success:
        print(f"Failed to load: {result.error}")
        return

    # Print animation info
    print(f"Skeletons: {result.skeleton_count}")
    for i, (name, dur) in enumerate(result.animation_clips):
        print(f"  [{i}] {name} ({dur:.2f}s)")

    # Start playing first clip on all animated entities
    scene = engine.scene
    for entity in result.entities:
        if scene.get_animation_clip_count(entity) > 0:
            animated_entities.append(entity)
            scene.set_animation_looping(entity, True)
            scene.play_animation(entity, 0)

def on_key_pressed(key):
    global anim_speed
    scene = engine.scene

    for entity in animated_entities:
        clip_count = scene.get_animation_clip_count(entity)

        # Number keys switch clips
        if key == 52 and clip_count > 0:
            scene.play_animation(entity, 0)
        elif key == 53 and clip_count > 1:
            scene.play_animation(entity, 1)
        elif key == 54 and clip_count > 2:
            scene.play_animation(entity, 2)

        # Space toggles pause/resume
        elif key == 32:
            if scene.is_animation_playing(entity):
                scene.stop_animation(entity)
            else:
                current = scene.get_current_animation_clip(entity)
                if current >= 0:
                    scene.play_animation(entity, current)

        # +/- adjust speed
        elif key in (61, 334):
            anim_speed = min(anim_speed * 1.5, 10.0)
            scene.set_animation_speed(entity, anim_speed)
        elif key in (45, 333):
            anim_speed = max(anim_speed / 1.5, 0.1)
            scene.set_animation_speed(entity, anim_speed)

engine.set_on_init(on_init)
engine.set_on_key_pressed(on_key_pressed)
engine.run()
```

---

## How It Works Under the Hood

Understanding the internal pipeline is not required for using animations, but
it helps when debugging or extending the system.

### Components

| Component | Purpose |
|-----------|---------|
| `SkeletonComponent` | Holds the joint hierarchy (`Skeleton`), the per-frame `boneMatrices` array, and a GPU SSBO for shader upload. Multiple entities can share the same `Skeleton` definition, but each gets its own bone matrices. |
| `AnimationPlaybackComponent` | Controls playback state: which clip is active, current time, speed, looping flag. Stores the list of available `AnimationClip` objects. |

### Per-Frame Pipeline

1. **`SkeletalAnimationSystem`** iterates all entities with both
   `SkeletonComponent` and `AnimationPlaybackComponent`
2. For each entity, it advances `currentTime` by `dt * speed`
3. It evaluates every `AnimationChannel` in the active clip, sampling keyframes
   to produce per-joint local transforms (translation, rotation, scale)
4. It walks the skeleton hierarchy to compute global transforms, then multiplies
   by inverse bind matrices to produce final bone matrices:
   ```
   boneMatrices[i] = globalTransform[i] * inverseBindMatrix[i]
   ```
5. The `boneMatrices` array is uploaded to the entity's `boneSSBO` GPU buffer
6. The vertex shader reads bone matrices from the SSBO and applies per-vertex
   skinning using joint indices and weights from the vertex attributes

### Interpolation Modes

Animation channels support three interpolation modes for sampling between
keyframes:

| Mode | Description |
|------|-------------|
| **Step** | Snaps to the nearest keyframe value (no interpolation). |
| **Linear** | Linearly interpolates between keyframes (lerp for position/scale, slerp for rotation). |
| **CubicSpline** | Cubic spline interpolation using tangent data embedded in the glTF file. Smoothest result. |

The interpolation mode is set per-channel in the glTF file and handled
automatically by the engine.

---

## C++ Animation API

In C++, animation is controlled through the `SceneAPI` facade (same methods as
Python) or by working directly with the ECS components.

### Facade API

```cpp
auto& scene = engine.getScene();

// Query clips
uint32_t count = scene.getAnimationClipCount(entity);
std::string name = scene.getAnimationClipName(entity, 0);
float duration = scene.getAnimationClipDuration(entity, 0);

// Play
scene.playAnimation(entity, 0);
scene.setAnimationLooping(entity, true);
scene.setAnimationSpeed(entity, 1.5f);

// Stop
scene.stopAnimation(entity);

// Query state
bool playing = scene.isAnimationPlaying(entity);
int32_t current = scene.getCurrentAnimationClip(entity);
float time = scene.getAnimationTime(entity);
scene.setAnimationTime(entity, 0.f);
```

### Direct ECS Access (Advanced)

If you are working with `ApplicationBase` directly, you can manipulate the
animation components in the EnTT registry:

```cpp
#include "ECS/SkeletonComponents.h"
#include "Resources/AnimationData.h"

auto& playback = registry.get<AnimationPlaybackComponent>(entity);
playback.playing   = true;
playback.looping   = true;
playback.speed     = 2.0f;
playback.clipIndex = 1;

// Get the current clip
const AnimationClip* clip = playback.getCurrentClip();
if (clip) {
    printf("Playing '%s' (%.2fs)\n", clip->name.c_str(), clip->duration);
}

// Access skeleton data
auto& skeleton = registry.get<SkeletonComponent>(entity);
printf("Joint count: %u\n", skeleton.jointCount());
printf("Bone SSBO size: %zu bytes\n",
       skeleton.boneMatrices.size() * sizeof(glm::mat4));
```

---

## GltfResult Animation Info

The `GltfResult` returned by `load_gltf_scene` provides a summary of all
animation data found in the file, before you need to query individual entities.

### Python

```python
result = engine.load_gltf_scene("models/Character.glb",
    load_skins=True,
    load_animations=True,
)

# Number of skeletons loaded
print(f"Skeletons: {result.skeleton_count}")

# List of (name, duration) tuples for all clips
for name, duration in result.animation_clips:
    print(f"  '{name}': {duration:.2f}s")
```

### C++ (Facade)

```cpp
auto result = engine.loadGltfScene("models/Character.glb", options);

printf("Skeletons: %zu\n", result.skeletonCount);
for (auto& clip : result.animationClips) {
    printf("  '%s': %.2fs\n", clip.name.c_str(), clip.duration);
}
```

---

## Tips

- **Always enable looping** for ambient animations (idle, walk). Without
  looping, the animation plays once and stops at the last frame.
- **Use `name_prefix`** when loading multiple copies of the same animated model
  so each instance can be controlled independently.
- **Skinned meshes need a skinned shader.** The default pipeline JSON typically
  handles this, but if you use a custom pipeline, ensure your vertex shader
  reads from the bone SSBO and applies skinning. See `skinned_pipeline.json` and
  `skinned_gbuffer.vert` in the examples for reference.
- **Animation clips are shared.** The `AnimationClip` data is shared across
  entities via `shared_ptr`. Each entity has its own playback state (time, speed,
  clip index), so multiple instances of the same model can play different clips
  at different speeds.

---

## See Also

- [Python Scene API -- Animation section](../api/python/scene.md#animation) --
  complete method reference for all `scene.*_animation_*` methods
- [ECS Components](../api/cpp/ecs-components.md) -- `SkeletonComponent` and
  `AnimationPlaybackComponent` struct definitions
- [Loading Scenes](loading-scenes.md) -- how to load glTF files with skin and
  animation options
- [C++ SceneAPI](../api/cpp/scene-api.md) -- C++ facade animation methods
- [Python GltfResult API](../api/python/gltf-result.md) -- `animation_clips`
  and `skeleton_count` fields
