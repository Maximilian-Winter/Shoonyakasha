"""
Props in the physics bubble (shoonyakasha.planet.bubble): crates and loose
rocks, each a body in the bubble's physics and a mesh entity drawn where
the body is.

A crate is a box; a loose rock (rocks.py) is a rock taken out of the
scatter, a convex hull, named by its cell (`key`). Each prop has a radius,
that of the sphere around it, which the gravity gun holds it out by.

`Props.update` keeps each prop on the terrain mesh (collider_probes'
TerrainCollider): a dynamic prop that is awake and not over the mesh
(FREEZE_MARGIN plus its size inside its edges) turns static until the
mesh is under it again, so it does not fall through the ground; a prop
farther than Bubble.RADIUS from the bubble's origin is removed, a rock's
removal told to `on_remove`. `Props.draw` places the entities relative to
the camera, subtracted in 64-bit floats.
"""

import math
from dataclasses import dataclass

import numpy as np

from shoonyakasha.planet.vec import add, cross, euler_from_basis, length, scale, sub

from flight import quat_from_basis, quat_rotate

FREEZE_MARGIN = 0.5              # m inside the terrain mesh's edges a prop counts as over it
CRATE_COLOUR = (0.62, 0.45, 0.25)


@dataclass
class Prop:
    body: int                    # physics body id
    entity: int                  # mesh entity
    size: float                  # m: a crate's edge, a rock's diameter
    radius: float                # m: the sphere around it
    key: tuple = None            # a rock's cell (shoonyakasha.planet.rocks)
    frozen: bool = False         # turned static while off the terrain mesh


class Props:
    def __init__(self, engine, mesh, on_remove=None):
        """`mesh`: (positions, normals) of a unit cube centred on the origin;
        `on_remove(prop)` is called for each rock removed."""
        self.engine = engine
        self.mesh = mesh
        self.on_remove = on_remove
        self.crates = []
        self.rocks = []

    @property
    def all(self):
        return self.crates + self.rocks

    def spawn_crate(self, bubble, position, rotation, size=0.6, density=500.0, colour=CRATE_COLOUR, mass=None):
        """A crate of `size` metres at bubble coordinates `position`, turned
        by `rotation`, of `mass` kg, or of `density` when None."""
        half = 0.5 * size
        body = bubble.physics.create_box((half, half, half), position, rotation, motion="dynamic",
                                         density=density, mass=mass or 0.0, friction=0.7, restitution=0.1)
        positions, normals = self.mesh
        entity = self.engine.create_mesh(positions, normals=normals, name="Crate")
        scene = self.engine.scene
        scene.set_scale(entity, (size, size, size))
        scene.set_material_vec4(entity, "baseColorFactor", tuple(colour) + (1.0,))
        scene.set_material_float(entity, "gridStrength", 0.0)
        scene.set_render_layer_mask(entity, 1)
        crate = Prop(body, entity, size, 0.5 * math.sqrt(3.0) * size)
        self.crates.append(crate)
        return crate

    def add_rock(self, body, entity, size, radius, key):
        """A loose rock: its body (dynamic) and entity, made by rocks.py."""
        rock = Prop(body, entity, size, radius, key)
        self.rocks.append(rock)
        return rock

    def remove(self, bubble, prop):
        if bubble is not None and bubble.physics.contains(prop.body):
            bubble.physics.remove(prop.body)
        self.engine.scene.destroy_entity(prop.entity)
        if prop.key is not None:
            self.rocks.remove(prop)
            if self.on_remove is not None:
                self.on_remove(prop)
        else:
            self.crates.remove(prop)

    def clear(self, bubble):
        for prop in self.all:
            self.remove(bubble, prop)

    def transforms(self, bubble, props=None):
        """(position, rotation) of each prop (of `props`, all when None) in bubble coordinates, in their order."""
        props = self.all if props is None else props
        if not props:
            return []
        data = np.frombuffer(bubble.physics.get_transforms([p.body for p in props]), dtype=np.float64).reshape(-1, 7)
        return [(tuple(row[:3]), tuple(row[3:])) for row in data]

    def update(self, bubble, terrain):
        """Freeze props off the terrain mesh, thaw those over it, and
        remove those beyond the bubble's radius."""
        physics = bubble.physics
        props = self.all
        for prop, (p, _) in zip(props, self.transforms(bubble, props)):
            if length(p) > bubble.RADIUS:
                self.remove(bubble, prop)
                continue
            over = terrain.covers(p, FREEZE_MARGIN + prop.size)
            if prop.frozen and over:
                physics.set_motion(prop.body, "dynamic")
                prop.frozen = False
            elif not prop.frozen and not over and physics.is_active(prop.body):
                physics.set_motion(prop.body, "static")
                prop.frozen = True

    def draw(self, bubble, camera):
        """The props' entities where their bodies are, relative to `camera` (from the planet's centre)."""
        scene = self.engine.scene
        props = self.all
        for prop, (p, q) in zip(props, self.transforms(bubble, props)):
            scene.set_position(prop.entity, sub(bubble.to_world(p), camera))
            x, y, z = quat_rotate(q, (1.0, 0.0, 0.0)), quat_rotate(q, (0.0, 1.0, 0.0)), quat_rotate(q, (0.0, 0.0, 1.0))
            scene.set_rotation(prop.entity, euler_from_basis(x, y, scale(z, -1.0)))

    def grabbable(self, physics):
        """(body, radius, mass) of each prop that is not frozen."""
        return [(p.body, p.radius, physics.mass(p.body)) for p in self.all if not p.frozen]

    def awake(self, bubble):
        return sum(1 for p in self.all if not p.frozen and bubble.physics.is_active(p.body))


def stack_offsets(count, size):
    """Heights (m) of the centres of `count` crates of `size` stacked from the ground up."""
    gap = 0.02 * size
    return [0.5 * size + k * (size + gap) for k in range(count)]


def yaw_rotation(right, up, back, angle):
    """The quaternion of the basis (right, up, back) turned `angle` radians about `up`."""
    c, s = math.cos(angle), math.sin(angle)
    r = add(scale(right, c), scale(back, -s))
    b = cross(r, up)
    return quat_from_basis(r, up, b)
