"""
The scatter's rocks in the physics bubble (shoonyakasha.planet.bubble).

`Rocks.update` gives each rock that shoonyakasha.planet.nearby_trees lists
on the bubble's body, whose cell is not taken, a static convex hull: its
kind's points (vegetation.rock_points) below its base raised to the base,
scaled as the scatter draws it (across by its width factor times its
height, up by its height) and turned by its turn about the body's up; the
body's origin is half its height above its base. It keeps the rock's ore
grade (0 for a plain rock) and colour (rocks.rock_colour, tinted). Strikes
on it are watched (sk.PlanetPhysics.watch_strikes). As with the trunks
(trunks.py), a list adds rocks and does not remove them; a rock more than
KEEP beyond the list's radius from the camera is removed, and one listed
again with another base or kind is made again.

`Rocks.promote` lets a static rock come loose, when the gravity gun grabs,
pulls or punts it or a dynamic body strikes it: its body turns dynamic
where it is, strikes on it stay watched only for an ore boulder, its cell
goes into the world's taken rocks (the scatter leaves it out from this
frame on), and it becomes a loose rock in Props, drawn by a mesh entity of
its kind (vegetation.rock_triangles, with the same origin and scale) in
its colour, with an ore rock's glowing specks (`specks`). `restore` makes
a loose rock out of the physics from a record (props.py's Props.record).
`released` puts a loose rock's cell back when Props removes it.
`grabbable()` gives (body, radius, mass) of the static rocks for the gun:
the mass from its hull's volume at rocks.DENSITY.

An ore boulder, fixed or loose, struck at BREAK_SPEED or faster
(`breaks`) splits (`split`) into 3 to 6 chunks, their number and shares of
its mass drawn from its cell: each a loose rock of the small rocks' kind,
of its share of the boulder's mass and grade, its size the boulder's
times the cube root of its share and SHRINK (across, also times the ratio
of the two kinds' widths), placed around the boulder's centre OFFSET of
its size out, at the boulder's velocity plus BURST outwards. Its cell stays
taken; the chunks have none.
"""

import math
import random
import zlib
from dataclasses import dataclass

import numpy as np

from shoonyakasha.planet import vegetation
from shoonyakasha.planet.rocks import DENSITY, rock_colour, tinted
from shoonyakasha.planet.vec import add, cross, length, normalize, quat_from_basis, quat_rotate, scale, sub

from viewmodel import white_texture

KEEP = 10.0                      # m beyond the list's radius a rock is kept
MOVED = 0.05                     # m a base may move and still be the same rock
BREAK_SPEED = 15.0               # m/s along the contact's normal an ore boulder breaks at
SHRINK = 0.8
OFFSET = 0.28
BURST = 1.5                      # m/s
SPECK_SHARE = 0.12               # of a rock mesh's triangles that hold a speck
SPECK_SIZE = 0.03                # a speck's radius, in units of the mesh (its height)
SPECK_COLOUR = (1.0, 0.72, 0.28)
SPECK_GLOW = 1.5                 # the specks' emissive strength (the Screens pass)


@dataclass
class StaticRock:
    body: int
    kind: str
    base: tuple                  # relative to the body's centre
    scale: tuple                 # (across, up, across) in metres per unit of its mesh
    radius: float                # m, the sphere around its origin
    mass: float                  # kg
    colour: tuple
    grade: float = 0.0           # ore richness, 0 for a plain rock


def rock_frame(up, turn):
    """(right, up, ahead) of a rock turned by (cos, sin) about `up`, as vegetation.glsl's vegetationFrame."""
    east = cross((0.0, 1.0, 0.0), up)
    east = normalize(east) if length(east) > 1e-6 else (1.0, 0.0, 0.0)
    north = cross(up, east)
    right = add(scale(east, turn[0]), scale(north, turn[1]))
    return right, up, cross(right, up)


def specks(positions, normals):
    """Triangles (positions, normals) of a rock mesh's glowing specks: an
    octahedron of SPECK_SIZE on SPECK_SHARE of its triangles (picked by a
    hash of their index), at their centre, a speck's size out along the
    normal."""
    out_p, out_n = [], []
    axes = [(1.0, 0.0, 0.0), (0.0, 1.0, 0.0), (0.0, 0.0, 1.0)]
    for t in range(len(positions) // 3):
        if (zlib.crc32(t.to_bytes(4, "little")) & 0xFFFF) / 65536.0 >= SPECK_SHARE:
            continue
        a, b, c = (tuple(float(v) for v in positions[3 * t + k]) for k in range(3))
        n = tuple(float(v) for v in normals[3 * t])
        centre = add(scale(add(add(a, b), c), 1.0 / 3.0), scale(n, SPECK_SIZE))
        for sx in (1.0, -1.0):
            for sy in (1.0, -1.0):
                for sz in (1.0, -1.0):
                    x, y, z = scale(axes[0], sx), scale(axes[1], sy), scale(axes[2], sz)
                    corners = [add(centre, scale(v, SPECK_SIZE)) for v in (x, y, z)]
                    if sx * sy * sz < 0.0:
                        corners = [corners[0], corners[2], corners[1]]
                    out_p += corners
                    out_n += [normalize((sx, sy, sz))] * 3
    return out_p, out_n


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
        self.speck_meshes = {}       # kind: (positions, normals) of its specks

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
            self.speck_meshes[kind] = specks(*self.triangles[kind][:2])
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
                              tinted(rock_colour(body.index, tree.grade), tree.tint), tree.grade)
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
        physics.watch_strikes(body, rock.kind == "boulder" and rock.grade > 0.0)
        physics.set_motion(body, "dynamic")
        points = (self.unit_points[rock.kind] * np.array(rock.scale)).astype(np.float32)
        entity, glow = self.entity(rock.kind, rock.scale, rock.colour, rock.grade)
        self.props.add_rock(body, entity, 2.0 * rock.radius, rock.radius, key, ("hull", points, physics.mass(body)),
                            0.5 * rock.scale[1], bubble.body, rock.kind, rock.scale, rock.colour, rock.grade, glow)
        return True

    def entity(self, kind, extent, colour, grade=0.0):
        """A mesh entity of a rock of `kind`, scaled by `extent`, in
        `colour`, and for an ore rock (`grade` above 0) an entity of its
        glowing specks, drawn unlit (the Screens pass); else None."""
        positions, normals, shades = self.triangles[kind]
        entity = self.engine.create_mesh([tuple(p) for p in positions], normals=[tuple(n) for n in normals],
                                         colors=[tuple(s) for s in shades], name="Rock")
        scene = self.engine.scene
        scene.set_scale(entity, tuple(extent))
        scene.set_material_vec4(entity, "baseColorFactor", tuple(colour) + (1.0,))
        scene.set_material_float(entity, "gridStrength", 0.0)
        scene.set_render_layer_mask(entity, 1)
        if grade <= 0.0:
            return entity, None
        speck_positions, speck_normals = self.speck_meshes[kind]
        glow = self.engine.create_mesh(speck_positions, normals=speck_normals, name="OreSpecks")
        scene.set_scale(glow, tuple(extent))
        scene.set_render_layer_mask(glow, 2)
        scene.set_material_texture(glow, "emissiveMap", white_texture())
        scene.set_material_vec4(glow, "emissiveFactor", tuple(c * SPECK_GLOW for c in SPECK_COLOUR) + (0.0,))
        return entity, glow

    def restore(self, physics, record, bodies):
        """A loose rock out of the physics from a record (Props.record);
        `bodies` the world bodies by name."""
        points, _ = self.kind_data(physics, record["kind"])
        extent = record["extent"]
        entity, glow = self.entity(record["kind"], extent, record["colour"], record.get("grade", 0.0))
        return self.props.restore(record, bodies, entity, (points * np.array(extent)).astype(np.float32), glow)

    def released(self, prop):
        """A loose rock's cell put back (Props' on_remove)."""
        self.world.taken_rocks.discard(prop.key)

    # ── Breaking ore boulders ──

    def breaks(self, body, speed):
        """Whether `body` is an ore boulder, fixed or loose, that a strike at `speed` (m/s) breaks."""
        if speed < BREAK_SPEED:
            return False
        key = self.bodies.get(body)
        if key is not None:
            rock = self.rocks[key]
            return rock.kind == "boulder" and rock.grade > 0.0
        prop = self.props.find(body)
        return prop is not None and prop.breakable

    def split(self, bubble, body):
        """The ore boulder `body` split into chunks (see the module's
        docstring); the chunks (Props' loose rocks), or [] when it is not
        one or its cell cannot be taken."""
        physics = bubble.physics
        p, q = physics.get_transform(body)
        key = self.bodies.get(body)
        if key is not None:
            rock = self.rocks[key]
            if not (rock.kind == "boulder" and rock.grade > 0.0) or not self.world.taken_rocks.add(key):
                return []
            self.remove(physics, key)
            extent, mass, colour, grade = rock.scale, rock.mass, rock.colour, rock.grade
            v, w = (0.0, 0.0, 0.0), (0.0, 0.0, 0.0)
        else:
            prop = self.props.find(body)
            if prop is None or not prop.breakable:
                return []
            key, extent, mass, colour, grade = prop.key, prop.extent, prop.mass, prop.colour, prop.grade
            v, w = physics.get_velocity(body)
            prop.key = None                  # its cell stays taken
            self.props.remove(bubble, prop)
        draw = random.Random(zlib.crc32(repr(key).encode()))
        count = draw.randint(3, 6)
        shares = [draw.uniform(0.5, 1.5) for _ in range(count)]
        total = sum(shares)
        shares = [s / total for s in shares]
        boulder_points, _ = self.kind_data(physics, "boulder")
        points, _ = self.kind_data(physics, "rock")
        ratio = np.ptp(boulder_points[:, 0]) / np.ptp(points[:, 0])
        right, up, ahead = quat_rotate(q, (1.0, 0.0, 0.0)), quat_rotate(q, (0.0, 1.0, 0.0)), quat_rotate(q, (0.0, 0.0, 1.0))
        chunks = []
        for k, share in enumerate(shares):
            angle = 2.0 * math.pi * (k + draw.uniform(-0.2, 0.2)) / count
            local = normalize((math.cos(angle), 0.5 if k % 2 == 0 else -0.5, math.sin(angle)))
            out = add(add(scale(right, local[0] * extent[0]), scale(up, local[1] * extent[1])),
                      scale(ahead, local[2] * extent[2]))
            f = share ** (1.0 / 3.0) * SHRINK
            size = (extent[0] * f * ratio, extent[1] * f, extent[2] * f * ratio)
            hull = (points * np.array(size)).astype(np.float32)
            chunk = physics.create_convex_hull(hull, add(p, scale(out, OFFSET)), q, motion="dynamic",
                                               mass=mass * share, friction=0.8, restitution=0.05)
            physics.set_velocity(chunk, add(v, scale(normalize(out), BURST)), w)
            radius = float(np.linalg.norm(hull, axis=1).max())
            entity, glow = self.entity("rock", size, colour, grade)
            chunks.append(self.props.add_rock(chunk, entity, 2.0 * radius, radius, None,
                                              ("hull", hull, physics.mass(chunk)), 0.5 * size[1], bubble.body,
                                              "rock", size, colour, grade, glow))
        return chunks
