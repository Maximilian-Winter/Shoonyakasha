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

## Animators

An animator plays clips for a whole model. It lives on the model's root entity and drives every skinned mesh of one skeleton below it, so a character split into several meshes is evaluated once per frame. It adds cross-fades between clips, clips from other files, and entities attached to joints.

```python
result = engine.load_gltf_scene("models/rumba-dancing-mixamo/gltf/Rumba-Dancing.gltf")
anim = engine.animation
dancer = next(r for r in result.root_entities if anim.create_animator(r))
anim.load_clips(dancer, "anims/Idle.gltf", name="idle")   # joints matched by name
anim.play(dancer, "idle")
anim.play(dancer, "mixamo_com", fade=0.4)                   # blend over 0.4 s
anim.attach(sword, dancer, "mixamorig_RightHand", offset_position=(0, 8, 0))
```

- The animator starts with the file's clips and the playback state of the model's first skinned mesh.
- `load_clips` adds the animations of another glTF / GLB file whose node names match the skeleton's joint names. Mixamo downloads of one character share joint names. A file with one animation can be renamed with `name`.
- An attached entity's offset is in the joint's space, including the model's scale. In a model with centimetre joints under a 0.01-scaled root, the offsets above are centimetres.

Meshes driven by an animator ignore the per-entity `scene.play_animation` controls. Run `examples/python/animation/character_animator/character_animator.py` from the repository root to see a Mixamo dancer, the Fox cross-fading between its clips, and a cube attached to the dancer's hand.

C++ code uses `engine.getAnimation()` ([AnimationAPI](../api/cpp/animation-api.md)), or adds `AnimatorComponent`s with `AnimatorEvaluator::createAnimator` directly. `Pose` and `PoseOps` (`Animation/Pose.h`) sample clips into local joint transforms and blend them: per pose, per joint, or additively.

## Rendering requirements

Use skinned vertex attributes, a skeleton descriptor binding, matching shaders, and `skinned_geometry` / `skinned_transparent` execution. The example's [pipeline](../../examples/python/animation/skinned_fox_demo/skinned_pipeline.json) shows the complete configuration. A static geometry shader cannot animate merely because clips were loaded.

The bone buffer holds this frame's matrices and then last frame's, so a shader can compute motion vectors: last frame's bone `j` is `bones[j + bones.length() / 2]`. The [default pipeline's](../../python/shoonyakasha/pipelines/default/README.md) `gbuffer_skinned.vert` does this.

## Morph targets

The loader reads a glTF primitive's morph targets (position, normal and tangent deltas) and its mesh's default weights, or the node's. Each entity drawing such a mesh gets its own weights, and every frame whose weights changed the CPU blends the vertices into a buffer of the entity's own, one per frame in flight, which every pass draws and ray tracing builds from. Skinning applies on top, so a mesh can have both. Morphed meshes are never culled, and their motion vectors leave out what the weights move.

```python
weights = engine.scene.get_morph_weights(face)     # one per target
engine.scene.set_morph_weights(face, [0.0, 1.0])
```

Clips also carry glTF `weights` channels. Entities with morph targets get the load's clips; playing one (`play_animation`) drives the weights of the entities made from each animated node, and overwrites weights set by hand while it plays. A file without skins loads its clips with their weight channels.

See the [Animation reference](../api/python/animation.md), the [Scene reference](../api/python/scene.md) and [native components](../api/cpp/ecs-components.md).
