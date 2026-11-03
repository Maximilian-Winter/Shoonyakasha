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

## Controllers

`set_controller(animator, controller)` plays a state machine on the animator.
- `controller` is an `AnimationController` (`shoonyakasha.animation_controller`), a dict of its JSON form, or the path of a JSON file.
- Clip and joint names are checked against the animator. A `RuntimeError` names the place in the definition that is wrong.
- While a controller is set, `play` returns False.
- Parameter setters raise `RuntimeError` for an unknown parameter or one of another type.
- `take_events` returns event names in the order they fired. The animator keeps the last 256 until they are taken.

## Root motion

- `set_root_motion(animator, True, joint="")` holds the joint's horizontal translation, perpendicular to world up (by default the skeleton's root joint), at each clip's first frame, and collects its movement in world space.
- `take_root_motion` returns that movement and clears it. With a controller, the movement comes from the first layer.

See the [animation guide](../../guides/animation.md#animation-controllers) for states, blends, transitions and layers.

## Humanoids

- `make_humanoid(animator, mapping=None)` maps the skeleton onto the standard humanoid bones and returns the mapping as `{bone: joint}`.
- `retarget_clips(target, source, clips, name)` bakes clips of one humanoid animator onto another.
- `load_humanoid_clips(target, path, name, mapping)` bakes the clips of a glTF file with its own skin onto a humanoid animator; `mapping` applies to the file's skeleton.
- Retargeted clips are named as `load_clips` names them. They play, blend and drive controllers like any other clip.

See the [animation guide](../../guides/animation.md#humanoids-and-retargeting) for bone names, presets and what retargeting does, and `shoonyakasha.character` for a one-call loader.

Meshes driven by an animator ignore the per-entity `scene.play_animation` controls.

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
| `play(animator, clip, fade=0.0, loop=True)` | bool | Play a clip from its start, blending from the clip playing before over ``fade`` seconds. Returns False when there is no such clip, or while a controller is set. |
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
| `set_controller(animator, controller)` | None | Play an animation state machine on the animator. ``controller`` is an ``AnimationController`` (shoonyakasha.animation_controller), a dict of its JSON form, or the path of a JSON file. Its clip and joint names must be the animator's. While a controller is set, ``play`` returns False. |
| `clear_controller(animator)` | None | Remove the controller; the animator plays clips again. |
| `has_controller(animator)` | bool | Calls native `hasController`; see the class contract above. |
| `set_float(animator, parameter, value)` | None | Set a float parameter. Raises RuntimeError without a controller, for an unknown parameter or one of another type (as do the other parameter setters). |
| `set_int(animator, parameter, value)` | None | Set an int parameter. |
| `set_bool(animator, parameter, value)` | None | Set a bool parameter. |
| `set_trigger(animator, parameter)` | None | Set a trigger; the transition that uses it clears it. |
| `reset_trigger(animator, parameter)` | None | Clear a trigger. |
| `get_parameter(animator, parameter)` | float | Value of a parameter as a float; bools and triggers are 0.0 or 1.0. |
| `current_state(animator, layer)` | str | Name of the layer's current state. |
| `is_in_transition(animator, layer)` | bool | Whether the layer is fading from its previous state. |
| `set_layer_weight(animator, layer, weight)` | None | Weight of a layer; starts at the definition's. |
| `get_layer_weight(animator, layer)` | float | Calls native `getLayerWeight`; see the class contract above. |
| `take_events(animator)` | list[str] | Names of the controller's events fired since the last call, oldest first, and clear them. At most the last 256 are kept. |
| `set_root_motion(animator, enabled, joint="")` | None | Turn root motion on or off. While on, the horizontal translation (perpendicular to world up) of ``joint`` (the skeleton's root joint when empty) stays at each clip's first frame, and its movement is collected in world space; collect it with ``take_root_motion``. Raises RuntimeError for an unknown joint. |
| `is_root_motion(animator)` | bool | Calls native `isRootMotion`; see the class contract above. |
| `take_root_motion(animator)` | 3-tuple | World-space movement (x, y, z) collected since the last call, and clear it. |
| `make_humanoid(animator, mapping=None)` | dict[str, str] | Map the animator's skeleton onto the standard humanoid bones ("Hips", "LeftUpperArm", ...): by joint name (Mixamo, Unreal mannequin, VRM and Rigify names, then name parts), with ``mapping`` ({humanoid bone: joint name}) on top; an empty joint name unmaps a bone. Returns the mapping made as a dict. |
| `is_humanoid(animator)` | bool | Calls native `isHumanoid`; see the class contract above. |
| `humanoid_mapping(animator)` | dict[str, str] | {humanoid bone: joint name}; empty when the animator is not a humanoid. |
| `retarget_clips(target, source, clips=None, name="")` | list[str] | Retarget clips of humanoid animator ``source`` (all when ``clips`` is None) onto humanoid animator ``target`` and add them, named as ``load_clips`` names. Returns the names added. |
| `load_humanoid_clips(target, path, name="", mapping=None)` | list[str] | Read a glTF / GLB file with a skin and animations (a Mixamo download exported with its skin, for example), map its skeleton as ``make_humanoid`` does (``mapping`` on top), and retarget its clips onto humanoid animator ``target``, named as ``load_clips`` names. Returns the names added. |

<!-- END SOURCE API -->
