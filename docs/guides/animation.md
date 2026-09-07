# Skeletal animation

The glTF loader can import skins and animation clips. The facade creates bone buffers and evaluates skeletal animation before rendering. A custom `ApplicationBase` application must arrange the corresponding system and renderer setup; follow [SkinnedMeshTest](../../examples/cpp/animation/skinned_mesh_test).

## Run the reference example

After installing the Python extension, from the repository root:

```sh
cd examples/python/animation/skinned_fox_demo
python skinned_fox_demo.py
```

Use this directory even if an older comment in the script suggests the C++ example directory. The demo has its own pipeline and shader sources and uses bundled Fox/environment assets.

## Load and play

Use this fragment during init with a skinned pipeline:

```python
result = engine.load_gltf_scene("models/Fox.glb", load_skins=True, load_animations=True)
if result.success:
    for entity in result.entities:
        if engine.scene.get_animation_clip_count(entity) > 0:
            engine.scene.set_animation_looping(entity, True)
            engine.scene.play_animation(entity, 0)
```

C++ uses `loadGltfScene`, `getScene().getAnimationClipCount`, `setAnimationLooping`, and `playAnimation`. Inspect result clip metadata or per-entity clip names/durations before selecting an index.

Playback time/duration use seconds, indices start at zero, and speed defaults to 1. Play selects a clip and resets time; stop pauses and resets time to zero. Set speed to zero to freeze progression while retaining the selected clip/time, or adjust time explicitly.

## Rendering requirements

Use skinned vertex attributes, a skeleton descriptor binding, matching shaders, and `skinned_geometry` / `skinned_transparent` execution. The example's [pipeline](../../examples/python/animation/skinned_fox_demo/skinned_pipeline.json) shows the complete configuration. A static geometry shader cannot animate merely because clips were loaded.

The public controls select one current clip; they do not expose an animation state machine, blending tree, or retargeting API. See [Scene reference](../api/python/scene.md) and [native components](../api/cpp/ecs-components.md).
