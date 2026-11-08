"""
The outposts' parts in the walker's physics (outposts.Pads, built from
shoonyakasha.planet.outpost_layout's site models).

`OutpostBodies.update(bubble, viewpoint)` gives the outpost on the
bubble's body whose pad's centre is within REACH of `viewpoint` (planet
space) its bodies: a static convex hull for each hull of its static parts,
and each loose part (containers and barriers) a dynamic one of its mass,
asleep, LIFT above where it stands, on a static footing (FOOTING thick,
reaching FOOTING_OUT beyond it, its top at the ground) when it stands on
the ground, drawn by its own mesh entity while the
outpost's merged loose mesh is hidden (Pads.loose_shown). Once the
viewpoint is beyond LEAVE of it, or the bubble is another, or the outpost
is built again, its bodies and entities go and its loose parts are drawn
where they stood. `draw(camera)` places the loose parts' entities where
the physics has them. `loose_bodies()` gives (body, outpost) of the loose
parts in the physics, `static_count()` the static bodies, `outpost` the
outpost with bodies or None.
"""

import numpy as np

from shoonyakasha.planet import outpost_kit
from shoonyakasha.planet.vec import euler_from_basis, length, quat_rotate, scale, sub

REACH = 300.0                    # m from an outpost's pad's centre its bodies are made within
LEAVE = 400.0                    # m beyond which they go
LIFT = 0.02                      # m above where a loose part stands it starts
FOOTING, FOOTING_OUT = 0.5, 0.3  # m: a loose part's footing's thickness, and its reach beyond the part
ON_GROUND = 0.1                  # m from the ground a loose part's bottom counts as on it
FRICTION = 0.8


class OutpostBodies:
    def __init__(self, engine, pads):
        self.engine = engine
        self.pads = pads
        self.built = None            # outposts.Built with bodies
        self.bubble = None
        self.static = []             # physics bodies
        self.loose = []              # (physics body, entity)

    @property
    def outpost(self):
        return self.built.outpost if self.built is not None else None

    def update(self, bubble, viewpoint):
        if self.built is not None:
            far = length(sub(self.built.point((0.0, 0.0, 0.0)), viewpoint)) > LEAVE
            if bubble is not self.bubble or far or self.built not in self.pads.built:
                self.clear()
        if self.built is not None or bubble is None:
            return
        for built in self.pads.built:
            o = built.outpost
            if o.body is bubble.body and length(sub(built.point((0.0, 0.0, 0.0)), viewpoint)) <= REACH:
                self.make(bubble, built)
                return

    def make(self, bubble, built):
        physics = bubble.physics
        self.built, self.bubble = built, bubble
        model = built.model
        for body in model.bodies:
            frame = outpost_kit.yaw(body.heading)
            hull = np.ascontiguousarray(built.to_frame(np.asarray(body.hull, np.float64) @ frame.T), np.float32)
            point = bubble.to_bubble(built.point(body.position))
            if body.dynamic:
                piece = model.loose[body.loose]
                footing = self.footing(built, body)
                if footing is not None:
                    self.static.append(physics.create_convex_hull(footing, point, (1.0, 0.0, 0.0, 0.0),
                                                                  motion="static", friction=FRICTION))
                point = tuple(np.asarray(point) + LIFT * np.asarray(built.outpost.direction))
                handle = physics.create_convex_hull(hull, point, (1.0, 0.0, 0.0, 0.0), motion="dynamic",
                                                    mass=body.mass, friction=FRICTION, restitution=0.05)
                physics.deactivate(handle)
                self.loose.append((handle, self.entity(built, piece)))
            else:
                self.static.append(physics.create_convex_hull(hull, point, (1.0, 0.0, 0.0, 0.0), motion="static",
                                                              friction=FRICTION))
        self.pads.loose_shown[built.outpost.name] = False

    def footing(self, built, body):
        """The points of a loose part's footing along the planet's axes from its centre, or None when the part
        does not stand on the ground."""
        hull = np.asarray(body.hull, np.float64)
        x, y, z = body.position
        drop = (x * x + z * z) / (2.0 * built.outpost.body.radius)
        bottom = hull[:, 1].min()
        if abs(y + bottom + drop) > ON_GROUND:
            return None
        low, high = hull.min(axis=0), hull.max(axis=0)
        size = (high[0] - low[0] + 2.0 * FOOTING_OUT, FOOTING, high[2] - low[2] + 2.0 * FOOTING_OUT)
        centre = (0.5 * (low[0] + high[0]), bottom - 0.5 * FOOTING, 0.5 * (low[2] + high[2]))
        points = outpost_kit.box_points(centre, size) @ outpost_kit.yaw(body.heading).T
        return np.ascontiguousarray(built.to_frame(points), np.float32)

    def entity(self, built, piece):
        positions, normals, colours = piece.triangles
        frame = outpost_kit.yaw(piece.heading)
        positions, normals, colours = built.triangles((positions.astype(np.float64) @ frame.T,
                                                       normals.astype(np.float64) @ frame.T, colours))
        scene = self.engine.scene
        entity = self.engine.create_mesh(positions, normals=normals, colors=colours, name="OutpostPart")
        scene.set_material_vec4(entity, "baseColorFactor", (1.0, 1.0, 1.0, 1.0))
        scene.set_material_float(entity, "gridStrength", 0.0)
        scene.set_render_layer_mask(entity, 1)
        return entity

    def clear(self):
        if self.built is None:
            return
        physics = self.bubble.physics
        for handle in self.static:
            physics.remove(handle)
        for handle, entity in self.loose:
            physics.remove(handle)
            self.engine.scene.destroy_entity(entity)
        self.pads.loose_shown.pop(self.built.outpost.name, None)
        self.static, self.loose = [], []
        self.built = self.bubble = None

    def draw(self, camera):
        if not self.loose:
            return
        bubble = self.bubble
        scene = self.engine.scene
        data = np.frombuffer(bubble.physics.get_transforms([h for h, _ in self.loose]), dtype=np.float64).reshape(-1, 7)
        for (_, entity), row in zip(self.loose, data):
            q = tuple(row[3:])
            x, y, z = quat_rotate(q, (1.0, 0.0, 0.0)), quat_rotate(q, (0.0, 1.0, 0.0)), quat_rotate(q, (0.0, 0.0, 1.0))
            scene.set_position(entity, sub(bubble.to_world(tuple(row[:3])), camera))
            scene.set_rotation(entity, euler_from_basis(x, y, scale(z, -1.0)))

    def loose_bodies(self):
        return [(handle, self.built.outpost) for handle, _ in self.loose]

    def static_count(self):
        return len(self.static)
