"""Load a humanoid character with its animations in one call.

    import shoonyakasha as sk
    from shoonyakasha.character import load_humanoid

    hero = load_humanoid(engine, "models/hero.gltf",
                         animations=[("anims/Idle.gltf", "idle"), ("anims/Walking.gltf", "walk")],
                         controller="locomotion.json", root_motion=True)
    hero.set_float("speed", 1.2)
    hero.attach(sword, "RightHand", offset_position=(0, 8, 0))

    def on_update(dt):
        hero.apply_root_motion()

Everything here goes through ``engine.animation``; ``Humanoid.root`` is the
animator's entity for calls not wrapped here.
"""

__all__ = ["Humanoid", "load_humanoid"]


class Humanoid:
    """A humanoid animator on a model's root entity."""

    def __init__(self, engine, root):
        self.engine = engine
        self.root = root

    @property
    def animation(self):
        return self.engine.animation

    @property
    def mapping(self):
        """{humanoid bone: joint name}."""
        return self.animation.humanoid_mapping(self.root)

    @property
    def clips(self):
        return self.animation.clip_names(self.root)

    def joint(self, bone):
        """The joint playing humanoid bone ``bone``; a name that is not a
        mapped humanoid bone is returned as it is, as a joint name."""
        return self.mapping.get(bone, bone)

    def load_animations(self, *sources, mapping=None):
        """Retarget the clips of glTF files onto this humanoid. Each source is
        a path, or a (path, name) pair naming its clips as ``load_clips``
        does. ``mapping`` applies to the files' skeletons. Returns the names
        added."""
        added = []
        for source in sources:
            path, name = (source, "") if isinstance(source, (str, bytes)) or hasattr(source, "__fspath__") else source
            added += self.animation.load_humanoid_clips(self.root, path, name, mapping)
        return added

    def play(self, clip, fade=0.0, loop=True):
        return self.animation.play(self.root, clip, fade, loop)

    def stop(self):
        self.animation.stop(self.root)

    def set_controller(self, controller):
        self.animation.set_controller(self.root, controller)

    def set_float(self, parameter, value):
        self.animation.set_float(self.root, parameter, value)

    def set_int(self, parameter, value):
        self.animation.set_int(self.root, parameter, value)

    def set_bool(self, parameter, value):
        self.animation.set_bool(self.root, parameter, value)

    def set_trigger(self, parameter):
        self.animation.set_trigger(self.root, parameter)

    def state(self, layer="base"):
        return self.animation.current_state(self.root, layer)

    def take_events(self):
        return self.animation.take_events(self.root)

    def attach(self, entity, bone, offset_position=(0.0, 0.0, 0.0), offset_rotation=(0.0, 0.0, 0.0)):
        """Attach ``entity`` to humanoid bone (or joint) ``bone``."""
        return self.animation.attach(entity, self.root, self.joint(bone), offset_position, offset_rotation)

    def bone_position(self, bone):
        """World position of humanoid bone (or joint) ``bone``."""
        return self.animation.joint_world_position(self.root, self.joint(bone))

    def reach(self, limb, position, weight=1.0, pole=None, rotation=None, normal=None):
        """Reach world ``position`` with "LeftHand", "RightHand", "LeftFoot",
        "RightFoot" or a chain set with ``set_ik_chain``, as ``set_ik_goal``
        does."""
        self.animation.set_ik_goal(self.root, limb, position, weight, pole, rotation, normal)

    def release(self, limb):
        """Let ``limb`` follow the animation again."""
        self.animation.clear_ik_goal(self.root, limb)

    def place_feet(self, ground, hips_smoothing=0.25, max_drop=0.5):
        """Keep both feet on the ground, for one frame; call it from the update
        callback.

        ``ground(x, z)`` returns the ground height there, or (height, normal).
        Each foot is set at its animated height above the model's floor (the
        root entity's height) above the ground under its animated position,
        tilted to the normal when there is one, so steps still lift. The hips
        move down toward the lowest ground below the floor (at most
        ``max_drop``), by ``hips_smoothing`` of the remaining distance per call.
        Returns the hips' vertical offset."""
        anim = self.animation
        floor = self.engine.scene.get_position(self.root)[1]
        lowest = 0.0
        for limb in ("LeftFoot", "RightFoot"):
            x, y, z = anim.animated_joint_world_position(self.root, self.joint(limb))
            found = ground(x, z)
            height, normal = (found, None) if isinstance(found, (int, float)) else found
            anim.set_ik_goal(self.root, limb, (x, height + (y - floor), z), 1.0, None, None, normal)
            lowest = min(lowest, height - floor)
        target = max(lowest, -max_drop)
        current = anim.get_hips_offset(self.root)[1]
        offset = current + (target - current) * hips_smoothing
        anim.set_hips_offset(self.root, (0.0, offset, 0.0))
        return offset

    def release_feet(self):
        """Stop placing the feet: clear their goals and the hips offset."""
        for limb in ("LeftFoot", "RightFoot"):
            self.animation.clear_ik_goal(self.root, limb)
        self.animation.set_hips_offset(self.root, (0.0, 0.0, 0.0))

    def set_root_motion(self, enabled=True, bone="Hips"):
        self.animation.set_root_motion(self.root, enabled, self.joint(bone))

    def apply_root_motion(self):
        """Move the root entity by the root motion collected since the last
        call. Returns the movement (x, y, z)."""
        dx, dy, dz = self.animation.take_root_motion(self.root)
        if dx or dy or dz:
            x, y, z = self.engine.scene.get_position(self.root)
            self.engine.scene.set_position(self.root, (x + dx, y + dy, z + dz))
        return dx, dy, dz


def load_humanoid(engine, path, animations=(), controller=None, mapping=None, animation_mapping=None,
                  position=None, root_motion=False):
    """Load a skinned glTF model, put a humanoid animator on its first
    top-level node with a skinned mesh below it, retarget ``animations``
    (paths or (path, name) pairs) onto it, and set ``controller`` (as
    ``set_controller`` takes it) when given. ``mapping`` adds to the model's
    humanoid mapping, ``animation_mapping`` to the animation files'.

    Raises RuntimeError when the model cannot be loaded or has no skinned
    mesh, and as the animation calls do."""
    result = engine.load_gltf_scene(str(path), load_skins=True, load_animations=True)
    if not result.success:
        raise RuntimeError("cannot load %s: %s" % (path, result.error))
    root = next((r for r in result.root_entities if engine.animation.create_animator(r)), None)
    if root is None:
        raise RuntimeError("%s has no skinned mesh" % (path,))
    engine.animation.make_humanoid(root, mapping)
    hero = Humanoid(engine, root)
    if position is not None:
        engine.scene.set_position(root, position)
    if animations:
        hero.load_animations(*animations, mapping=animation_mapping)
    if controller is not None:
        hero.set_controller(controller)
    if root_motion:
        hero.set_root_motion(True)
    return hero
