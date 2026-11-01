"""
Props in the physics bubble (shoonyakasha.planet.bubble): crates, each a
box body in the bubble's physics and a mesh entity drawn where the body
is.

`Props.update` keeps each crate on the terrain mesh (collider_probes'
TerrainCollider): a dynamic crate that is awake and not over the mesh
(FREEZE_MARGIN inside its edges) turns static until the mesh is under it
again, so it does not fall through the ground; a crate farther than
Bubble.RADIUS from the bubble's origin is removed. `Props.draw` places the
entities relative to the camera, subtracted in 64-bit floats.
"""

import math
from dataclasses import dataclass

import numpy as np

from shoonyakasha.planet.vec import add, cross, euler_from_basis, length, scale, sub

from flight import quat_from_basis, quat_rotate

FREEZE_MARGIN = 0.5              # m inside the terrain mesh's edges a crate counts as over it
CRATE_COLOUR = (0.62, 0.45, 0.25)


@dataclass
class Crate:
    body: int                    # physics body id
    entity: int                  # mesh entity
    size: float                  # m along each edge
    frozen: bool = False         # turned static while off the terrain mesh


class Props:
    def __init__(self, engine, mesh):
        """`mesh`: (positions, normals) of a unit cube centred on the origin."""
        self.engine = engine
        self.mesh = mesh
        self.crates = []

    def spawn_crate(self, bubble, position, rotation, size=0.6, density=500.0, colour=CRATE_COLOUR):
        """A crate of `size` metres at bubble coordinates `position`, turned by `rotation`."""
        half = 0.5 * size
        body = bubble.physics.create_box((half, half, half), position, rotation, motion="dynamic",
                                         density=density, friction=0.7, restitution=0.1)
        positions, normals = self.mesh
        entity = self.engine.create_mesh(positions, normals=normals, name="Crate")
        scene = self.engine.scene
        scene.set_scale(entity, (size, size, size))
        scene.set_material_vec4(entity, "baseColorFactor", tuple(colour) + (1.0,))
        scene.set_material_float(entity, "gridStrength", 0.0)
        scene.set_render_layer_mask(entity, 1)
        crate = Crate(body, entity, size)
        self.crates.append(crate)
        return crate

    def remove(self, bubble, crate):
        if bubble is not None and bubble.physics.contains(crate.body):
            bubble.physics.remove(crate.body)
        self.engine.scene.destroy_entity(crate.entity)
        self.crates.remove(crate)

    def clear(self, bubble):
        for crate in list(self.crates):
            self.remove(bubble, crate)

    def transforms(self, bubble):
        """(position, rotation) of each crate in bubble coordinates, in their order."""
        if not self.crates:
            return []
        data = np.frombuffer(bubble.physics.get_transforms([c.body for c in self.crates]),
                             dtype=np.float64).reshape(-1, 7)
        return [(tuple(row[:3]), tuple(row[3:])) for row in data]

    def update(self, bubble, terrain):
        """Freeze crates off the terrain mesh, thaw those over it, and
        remove those beyond the bubble's radius."""
        physics = bubble.physics
        for crate, (p, _) in list(zip(self.crates, self.transforms(bubble))):
            if length(p) > bubble.RADIUS:
                self.remove(bubble, crate)
                continue
            over = terrain.covers(p, FREEZE_MARGIN + crate.size)
            if crate.frozen and over:
                physics.set_motion(crate.body, "dynamic")
                crate.frozen = False
            elif not crate.frozen and not over and physics.is_active(crate.body):
                physics.set_motion(crate.body, "static")
                crate.frozen = True

    def draw(self, bubble, camera):
        """The crates' entities where their bodies are, relative to `camera` (from the planet's centre)."""
        scene = self.engine.scene
        for crate, (p, q) in zip(self.crates, self.transforms(bubble)):
            scene.set_position(crate.entity, sub(bubble.to_world(p), camera))
            x, y, z = quat_rotate(q, (1.0, 0.0, 0.0)), quat_rotate(q, (0.0, 1.0, 0.0)), quat_rotate(q, (0.0, 0.0, 1.0))
            scene.set_rotation(crate.entity, euler_from_basis(x, y, scale(z, -1.0)))

    def awake(self, bubble):
        return sum(1 for c in self.crates if not c.frozen and bubble.physics.is_active(c.body))


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
