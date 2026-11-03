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

Meshes driven by an animator ignore the per-entity `scene.play_animation` controls. Run `examples/python/animation/character_animator/character_animator.py` from the repository root to see a Mixamo dancer with a cube attached to its hand and root motion on the M key, and the Fox run by a controller.

C++ code uses `engine.getAnimation()` ([AnimationAPI](../api/cpp/animation-api.md)), or adds `AnimatorComponent`s with `AnimatorEvaluator::createAnimator` directly. `Pose` and `PoseOps` (`Animation/Pose.h`) sample clips into local joint transforms and blend them: per pose, per joint, or additively.

## Animation controllers

A controller is a state machine that plays on an animator in place of `play`. It is built in Python with `sk.AnimationController`, or written as JSON, and set with `set_controller`. Gameplay code then only sets its parameters.

```python
import shoonyakasha as sk

c = sk.AnimationController()
c.float("speed")
c.trigger("jump")
base = c.layer("base")
base.clip("idle", "Survey")
base.blend1d("move", "speed", [("Walk", 1.0), ("Run", 3.0)], events=[(0.0, "step"), (0.5, "step")])
base.clip("jump", "Jump", loop=False)
base.transition("idle", "move", duration=0.3, when=[("speed", ">", 0.1)])
base.transition("move", "idle", duration=0.4, when=[("speed", "<", 0.1)])
base.transition("any", "jump", duration=0.1, when=[("jump",)])
base.transition("jump", "idle", duration=0.2, exit_time=0.9)

anim = engine.animation
anim.set_controller(hero, c)           # or a dict, or the path of a JSON file
anim.set_float(hero, "speed", 2.0)
anim.set_trigger(hero, "jump")
anim.current_state(hero, "base")       # "move", "jump", ...
for event in anim.take_events(hero):   # in the update callback
    ...
```

**Parameters** are float, int, bool or trigger. A trigger stays set until a transition whose conditions test it is taken.

**States** play one of three motions:

- A clip.
- A 1D blend of clips by thresholds on one parameter.
- A 2D blend of clips placed at points on a plane, weighted by gradient band interpolation of two parameters (`blend2d("strafe", ("vx", "vz"), [("idle", 0, 0), ("forward", 0, 1), ...])`).

The clips of a blend share one normalized phase. The phase advances over the weighted average of their durations, so blended walk and run cycles stay in step. Each state also has:

- a speed, optionally multiplied by a parameter (`speed_parameter`);
- looping;
- events: names fired when the phase passes their normalized time.

**Transitions** leave a named state, or `"any"` state other than their target. A transition is taken when:

- all its conditions hold: `(bool or trigger,)`, or `(parameter, op, value)` with `>`, `<`, `==` or `!=`;
- and the phase passes its exit time, when it has one. In a state that does not loop, the transition is also taken once the phase is past the exit time.

When several transitions qualify, the first in list order is taken. A layer takes no transition while it is still fading from the last one.

**Layers** apply in order over the rest pose, each weighted by its weight (`set_layer_weight`):

- An **override** layer blends toward its pose.
- An **additive** layer adds its pose's difference from the rest pose.
- A `mask` limits a layer to the named joints and their descendants.

The JSON form is documented at the top of `include/Animation/AnimationController.h`; `AnimationController.to_json()` writes it. Errors name the place in the definition, for example `layers[0].states.jump: no clip 'Jump'`.

## Root motion

`set_root_motion(animator, True)` holds the horizontal translation (perpendicular to world up) of the skeleton's root joint at each clip's first frame. Pass `joint="..."` for another joint. The joint's movement is collected in world space instead, and blended like the pose. `take_root_motion` returns it and clears it. Apply it to the model's root entity, or to a character body, in the update callback:

```python
dx, dy, dz = anim.take_root_motion(dancer)
x, y, z = engine.scene.get_position(dancer)
engine.scene.set_position(dancer, (x + dx, y + dy, z + dz))
```

The movement taken in the update callback comes from the previous frame's evaluation, so the model trails it by one frame. With controllers, root motion comes from the first layer.

## Humanoids and retargeting

`make_humanoid(animator)` maps the animator's skeleton onto 55 standard humanoid bones: Hips, Spine, Chest, UpperChest, Neck, Head, Jaw, the eyes, shoulders, arms, hands, legs, feet, toes, and three segments of each finger.
- Joints are matched by name, first against Mixamo (`mixamorig:` or `mixamorig_`), Unreal mannequin, VRM and Rigify names.
- Bones still missing are then matched on name parts (side, body part, finger) whose joint lies below the bone's parent.
- A `mapping` of `{bone: joint}` overrides or adds bones. The required bones are Hips, Spine, Head and both arms, hands, legs and feet; when one is missing the error names it.

A humanoid can take the clips of another humanoid, with a different skeleton, proportions or axes:

```python
anim.make_humanoid(dancer)
anim.make_humanoid(cesium, mapping=CESIUM_MAPPING)   # {"Hips": "Skeleton_torso_joint_1", ...}
anim.retarget_clips(dancer, cesium, ["animation_0"], "walk")        # from another humanoid animator
anim.load_humanoid_clips(dancer, "anims/Walking.gltf", name="walk")  # from a file with its own skin
anim.play(dancer, "walk", fade=0.3)
```

Retargeting bakes a new clip for the target skeleton, sampled at the source clip's keyframe times:
- Each avatar's body frame (up from hips to head, sideways between the upper legs, measured in the rest pose) cancels differences in axes, facing and scale.
- Each mapped bone takes the source bone's rotation away from its rest pose. That rotation is applied to the target bone's rest rotation, after turning the target bone so its rest direction matches the source's, so an A-pose source drives a T-pose target.
- The hips' movement is scaled by the ratio of the hip heights.
- Joints not mapped in both avatars keep their rest transform.

`shoonyakasha.character.load_humanoid` loads a model, makes it a humanoid, retargets animation files onto it and sets a controller:

```python
from shoonyakasha.character import load_humanoid

hero = load_humanoid(engine, "models/hero.gltf",
                     animations=[("anims/Idle.gltf", "idle"), ("anims/Walking.gltf", "walk")],
                     controller="locomotion.json", root_motion=True)
hero.attach(sword, "RightHand", offset_position=(0, 8, 0))   # humanoid bone names work as joints
hero.set_float("speed", 1.2)
hero.apply_root_motion()                                      # in the update callback
```

Run `examples/python/animation/humanoid_retarget/humanoid_retarget.py` from the repository root to see four characters sharing their clips.

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
