"""
The scatter's rocks in the physics bubble (shoonyakasha.planet.bubble).

`Rocks.update` gives each rock that shoonyakasha.planet.nearby_trees lists
on the bubble's body, whose cell is not taken, a static convex hull: its
kind's points (vegetation.rock_points) below its base raised to the base,
scaled as the scatter draws it (across by its width factor times its
height, up by its height) and turned by its turn about the body's up; the
body's origin is half its height above its base. Strikes on it are
watched (sk.PlanetPhysics.watch_strikes). As with the trunks (trunks.py),
a list adds rocks and does not remove them; a rock more than KEEP beyond
the list's radius from the camera is removed, and one listed again with
another base or kind is made again.

`Rocks.promote` lets a static rock come loose, when the gravity gun grabs,
pulls or punts it or a dynamic body strikes it: its body turns dynamic
where it is, strikes on it are no longer watched, its cell goes into the
world's taken rocks (the scatter leaves it out from this frame on), and
it becomes a loose rock in Props, drawn by a mesh entity of its kind
(vegetation.rock_triangles, with the same origin and scale) in its colour
and tint. `restore` makes a loose rock out of the physics from a record
(props.py's Props.record). `released` puts a loose rock's cell back when
Props removes it.
`grabbable()` gives (body, radius, mass) of the static rocks for the gun:
the mass from its hull's volume at rocks.DENSITY.
"""

from dataclasses import dataclass

import numpy as np

from shoonyakasha.planet import vegetation
from shoonyakasha.planet.rocks import DENSITY, ROCK_COLOURS, tinted
from shoonyakasha.planet.vec import add, cross, length, normalize, scale, sub

from flight import quat_from_basis

KEEP = 10.0                      # m beyond the list's radius a rock is kept
MOVED = 0.05                     # m a base may move and still be the same rock


@dataclass
class StaticRock:
    body: int
    kind: str
    base: tuple                  # relative to the body's centre
    scale: tuple                 # (across, up, across) in metres per unit of its mesh
    radius: float                # m, the sphere around its origin
    mass: float                  # kg
    colour: tuple


def rock_frame(up, turn):
    """(right, up, ahead) of a rock turned by (cos, sin) about `up`, as vegetation.glsl's vegetationFrame."""
    east = cross((0.0, 1.0, 0.0), up)
    east = normalize(east) if length(east) > 1e-6 else (1.0, 0.0, 0.0)
    north = cross(up, east)
    right = add(scale(east, turn[0]), scale(north, turn[1]))
    return right, up, cross(right, up)


class Rocks:
    def __init__(self, engine, world, props):
        self.engine = engine
        self.world = world
        self.props = props
        self.rocks = {}              # key: StaticRock
        self.bodies = {}             # body: key
        self.unit_points = {}        # kind: the hull's points, unit height, origin at half height
        self.unit_volume = {}        # kind: the hull's volume at unit scale
        self.triangles = {}          # kind: (positions, normals, shades) around the same origin

    def kind_data(self, physics, kind):
        if kind not in self.unit_points:
            points = vegetation.rock_points(kind).astype(np.float64)
            points[:, 1] = np.maximum(points[:, 1], 0.0) - 0.5
            self.unit_points[kind] = points
            probe = physics.create_convex_hull(points.astype(np.float32), (0.0, 0.0, 0.0), motion="dynamic",
                                               density=1.0)
            self.unit_volume[kind] = physics.mass(probe)
            physics.remove(probe)
            positions, normals, shades = vegetation.rock_triangles(kind)
            self.triangles[kind] = (positions - (0.0, 0.5, 0.0), normals, shades)
        return self.unit_points[kind], self.unit_volume[kind]

    def update(self, bubble, trees, camera, radius):
        """The static rocks of `trees` (nearby_trees.NearbyTree; those on
        the bubble's body, not taken) in `bubble`; those farther than
        `radius` + KEEP from `camera` (planet space) removed."""
        physics, body = bubble.physics, bubble.body
        taken = self.world.taken_rocks
        for tree in trees:
            if not tree.rock or tree.body != body.index or tree.key in taken:
                continue
            base = tree.base
            rock = self.rocks.get(tree.key)
            if rock is not None and rock.kind == tree.kind and length(sub(rock.base, base)) <= MOVED:
                continue
            if rock is not None:
                self.remove(physics, tree.key)
            points, volume = self.kind_data(physics, tree.kind)
            across = tree.height * tree.width
            sizes = np.array([across, tree.height, across])
            p = sub(base, bubble.origin)
            right, up, ahead = rock_frame(bubble.up(p), tree.turn)
            centre = add(p, scale(up, 0.5 * tree.height))
            hull = physics.create_convex_hull((points * sizes).astype(np.float32), centre,
                                              quat_from_basis(right, up, ahead), motion="static", density=DENSITY,
                                              friction=0.8, restitution=0.05)
            physics.watch_strikes(hull)
            rock = StaticRock(hull, tree.kind, base, tuple(sizes), float(np.linalg.norm(points * sizes, axis=1).max()),
                              DENSITY * volume * across * across * tree.height,
                              tinted(ROCK_COLOURS[body.index], tree.tint))
            self.rocks[tree.key] = rock
            self.bodies[hull] = tree.key
        here = body.relative(camera)
        for key in [k for k, r in self.rocks.items() if length(sub(r.base, here)) > radius + KEEP]:
            self.remove(physics, key)

    def remove(self, physics, key):
        rock = self.rocks.pop(key)
        del self.bodies[rock.body]
        if physics.contains(rock.body):
            physics.remove(rock.body)

    def clear(self, bubble):
        for key in list(self.rocks):
            self.remove(bubble.physics, key)

    def grabbable(self):
        return [(r.body, r.radius, r.mass) for r in self.rocks.values()]

    def is_static_rock(self, body):
        return body in self.bodies

    def promote(self, bubble, body):
        """The static rock `body` loose (see the module's docstring); False
        when it is not one or the taken rocks are full."""
        key = self.bodies.get(body)
        if key is None or not self.world.taken_rocks.add(key):
            return False
        rock = self.rocks.pop(key)
        del self.bodies[body]
        physics = bubble.physics
        physics.watch_strikes(body, False)
        physics.set_motion(body, "dynamic")
        points = (self.unit_points[rock.kind] * np.array(rock.scale)).astype(np.float32)
        self.props.add_rock(body, self.entity(rock.kind, rock.scale, rock.colour), 2.0 * rock.radius, rock.radius,
                            key, ("hull", points, physics.mass(body)), 0.5 * rock.scale[1], bubble.body, rock.kind,
                            rock.scale, rock.colour)
        return True

    def entity(self, kind, extent, colour):
        """A mesh entity of a rock of `kind`, scaled by `extent`, in `colour`."""
        positions, normals, shades = self.triangles[kind]
        entity = self.engine.create_mesh([tuple(p) for p in positions], normals=[tuple(n) for n in normals],
                                         colors=[tuple(s) for s in shades], name="Rock")
        scene = self.engine.scene
        scene.set_scale(entity, tuple(extent))
        scene.set_material_vec4(entity, "baseColorFactor", tuple(colour) + (1.0,))
        scene.set_material_float(entity, "gridStrength", 0.0)
        scene.set_render_layer_mask(entity, 1)
        return entity

    def restore(self, physics, record, bodies):
        """A loose rock out of the physics from a record (Props.record);
        `bodies` the world bodies by name."""
        points, _ = self.kind_data(physics, record["kind"])
        extent = record["extent"]
        return self.props.restore(record, bodies, self.entity(record["kind"], extent, record["colour"]),
                                  (points * np.array(extent)).astype(np.float32))

    def released(self, prop):
        """A loose rock's cell put back (Props' on_remove)."""
        self.world.taken_rocks.discard(prop.key)
