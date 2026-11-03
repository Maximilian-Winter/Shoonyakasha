# Animation

Access: `engine.animation`. [Other language reference](../cpp/animation-api.md).

[Binding implementation](../../../python/shoonyakasha/_shoonyakasha.pyx). Import the package as `import shoonyakasha as sk`.

Controls per-model animators. `create_animator(root)` puts an animator on an entity whose subtree holds the model's skinned meshes: one of `result.root_entities` of `engine.load_gltf_scene`. A file whose skeleton and meshes are separate top-level nodes needs the mesh's root. It drives every skinned mesh of one skeleton in that subtree, evaluating the skeleton once per frame for all of them. Every other call takes that root entity as `animator`.

## Clips and playback

- An animator starts with the clips the file loaded with, and the playback state of its first skinned mesh.
- `load_clips(animator, path, name)` adds the animations of another glTF / GLB file. Channels are matched to joints by joint name, so the file must use the skeleton's joint names.
- Clips are addressed by name. When several have the same name, the first one is used.
- `play(animator, clip, fade, loop)` starts a clip from time 0. With `fade` > 0 and a clip already playing, the pose blends from the old clip, which keeps advancing, to the new one over `fade` seconds.
- A clip played with `loop=False` stops at its end and holds its last frame.
- `stop` clears the clip, and the skeleton returns to its rest pose.
- `set_speed` multiplies the advance of every clip; 0 pauses.

Times are in seconds.

## Joint attachments

- `attach(entity, animator, joint, offset_position, offset_rotation)` sets the entity's world matrix every frame: the skinned mesh's world matrix, times the joint's transform, times the offset. The entity's descendants follow it.
- The offset is in the joint's space, which includes the model's scale.
- The engine evaluates animators after the frame's update callbacks, so in an update callback an attached entity's world matrix is the previous frame's placement.
- `detach` returns the entity to its own transform.

Meshes driven by an animator ignore the per-entity `scene.play_animation` controls. See the [animation guide](../../guides/animation.md).

<!-- BEGIN SOURCE API -->

## Members

Signatures and short descriptions below are extracted from the Cython wrapper; return shapes follow its conversions and the native declarations.

| Member | Returns / property value | Description |
|---|---|---|
| `create_animator(root)` | bool | Add an animator to ``root``, driving the skinned meshes of the first skeleton in root's subtree. It takes over the clips and playback state of the first such mesh. Returns False when the subtree has no skinned mesh; True when root already has an animator. |
| `has_animator(entity)` | bool | Calls native `hasAnimator`; see the class contract above. |
| `load_clips(animator, path, name="")` | list[str] | Read the animations of a glTF / GLB file and add those that animate joints of the animator's skeleton, matched by joint name. With ``name``, a single added clip is called ``name`` and several are called ``"<name>:<clip name>"``. Returns the names added. |
| `clip_names(animator)` | list[str] | Names of the animator's clips, in the order they were added. |
| `clip_duration(animator, clip)` | float | Duration in seconds of the clip; 0.0 if there is none. |
| `play(animator, clip, fade=0.0, loop=True)` | bool | Play a clip from its start, blending from the clip playing before over ``fade`` seconds. Returns False when there is no such clip. |
| `stop(animator)` | None | Stop playback; the skeleton returns to its rest pose. |
| `is_playing(animator)` | bool | Whether the current clip is advancing. A clip played with ``loop=False`` stops at its end and holds its last frame. |
| `current_clip(animator)` | str | Name of the current clip; '' when there is none. |
| `is_fading(animator)` | bool | Whether the animator is blending from a previous clip. |
| `get_time(animator)` | float | Playback time of the current clip, in seconds. |
| `set_time(animator, seconds)` | None | Calls native `setTime`; see the class contract above. |
| `get_speed(animator)` | float | Multiplier on the advance of every clip; 0 pauses. |
| `set_speed(animator, speed)` | None | Calls native `setSpeed`; see the class contract above. |
| `joint_names(animator)` | list[str] | Names of the skeleton's joints, in joint index order. |
| `joint_world_position(animator, joint)` | 3-tuple | World position (x, y, z) of a joint as of the last evaluation. Raises RuntimeError for an unknown animator or joint. |
| `attach(entity, animator, joint, offset_position=(0.0, 0.0, 0.0), offset_rotation=(0.0, 0.0, 0.0))` | bool | Place ``entity`` on a joint every frame, offset in the joint's space by a translation and an Euler rotation in radians (applied as the transform component applies rotations). Returns False for an unknown animator or joint, or an entity without a transform. |
| `detach(entity)` | None | Stop placing ``entity`` on a joint. |

<!-- END SOURCE API -->
