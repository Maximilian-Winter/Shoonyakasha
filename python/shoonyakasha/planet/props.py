"""
Props: crates and loose rocks, each drawn by a mesh entity, in one of
three states:

- "jolt": a body in the physics bubble (shoonyakasha.planet.bubble), near
  (on the fine terrain mesh) while over that mesh, far (on the coarse one)
  otherwise (sk.PlanetPhysics.set_far);
- "flying": out of the physics, coasting around the body it is reckoned
  around: its position and velocity from that body's centre in doubles,
  pulled towards the centre by the body's gravity and, on the planet,
  dragged by the air moving with the wind (PlanetWorld.wind_at, at its
  height above its ground as last measured; drag coefficient DRAG on its
  cross-section), turning
  at a constant angular velocity; it changes body at the moon's sphere of
  influence (orbits.reference);
- "resting": out of the physics, lying where it came down.

A crate is a box; a loose rock (`add_rock`) is a convex hull: a rock taken
out of the scatter, named by its cell (`key`), or a chunk of a broken ore
boulder, with none. An ore rock has its grade (its ore richness), its
resource (shoonyakasha.planet.resources) and an entity of glowing specks
placed with its own. Each prop has a radius
(the sphere around it, which the gravity gun holds it out by), a rest
height (from its centre down to the ground it lies on) and its shape and
mass, to be made again in the physics.

`Props.update` (each frame there is a bubble) moves props between the
states. A prop in the physics leaves it when it is more than
Bubble.RADIUS from the bubble's origin, asleep and not over the fine
terrain mesh (MARGIN plus its size inside its edges), or over neither
mesh and less than CLEARANCE above the ground: resting when asleep or
slower than REST_SPEED on the ground as last measured (within LANDED of
its rest height), else flying. A prop out of the physics on the bubble's
body comes back within Bubble.RADIUS less REENTER of the origin: a resting
one over the fine mesh, asleep, a flying one over either mesh or more
than twice CLEARANCE above the ground. In the physics a prop faster than FAST_SPEED
is not damped (it keeps its speed, as in flight), a slower one is by
DAMPING, which helps it settle. `Props.drag` (each physics step) drags the
props in the physics on the planet moving faster than DRAG_SPEED through
the air (their velocity less the wind's, PlanetWorld.wind_at) as flying
ones are; an asleep one only when the drag is more than STICK of its
weight, so the wind wakes only what it can move. `Props.fly` moves the flying props on by
seconds of the world's clock, in steps of at most FLY_STEP; one whose
height above the ground as last measured, less its speed downwards times
the probes' latency, is below its rest height lands there and rests.
`Props.probe` measures the ground under the flying props and those in the
physics over neither mesh with the world's extra probes, the longest
unmeasured first, while nothing else uses those probes; until a prop's
ground is measured it is taken as the highest its body's terrain reaches
(the top of its `heights`). `Props.leave_all` takes every prop out of the physics (when
the bubble goes). `Props.draw` places the entities relative to the
camera, subtracted in 64-bit floats. `on_remove(prop)` is told of each
rock with a cell removed, unless `Props.remove` is asked not to release
it (a rock loaded into the ship keeps its cell taken).

`Props.record` gives a prop as a record, a dict of plain values for a save
file: its kind, the name of the body it is reckoned around, its state
(one in the physics resting when asleep, else flying), its position from
that body's centre, rotation, velocity and spin, its size, radius, rest
height, mass, entity scale and colour, a rock's cell, its ground as last
measured, its ore grade and its resource. `Props.restore` makes a prop out of the physics from one,
which comes back into the physics by the rules above; an ore rock's record
without a resource is ferrite.
"""

import math
from dataclasses import dataclass

import numpy as np

from shoonyakasha.planet.orbits import reference
from shoonyakasha.planet.resources import FERRITE
from shoonyakasha.planet.vec import (add, cross, dot, euler_from_basis, length, normalize, quat_from_basis,
                                     quat_multiply, quat_normalize, quat_rotate, scale, sub)

MARGIN = 0.5                     # m inside a terrain mesh's edges (plus its size) a prop counts as over it
CRATE_COLOUR = (0.62, 0.45, 0.25)
CLEARANCE = 50.0                 # m above the ground a prop may be in the physics with no terrain mesh under it
REENTER = 100.0                  # m inside Bubble.RADIUS a prop comes back into the physics
REST_SPEED = 0.5                 # m/s: slower than this, a prop leaving the physics on the ground rests
LANDED = 0.3                     # m above its rest height a prop counts as on the ground
FLY_STEP = 1.0 / 120.0           # s
DRAG = 1.0                       # drag coefficient
DRAG_SPEED = 5.0                 # m/s: slower props in the physics feel no drag
STICK = 0.7                      # of its weight the drag must beat to move an asleep prop
FAST_SPEED = 5.0                 # m/s: faster props in the physics are not damped
DAMPING = 0.05                   # the physics' linear damping (Jolt's default) of slower ones


@dataclass
class Prop:
    body: int                    # physics body id while in the physics, else None
    entity: int                  # mesh entity
    size: float                  # m: a crate's edge, a rock's diameter
    radius: float                # m: the sphere around it
    shape: tuple                 # ("box", half extents, mass) or ("hull", points (N x 3 float32), mass)
    rest: float                  # m from its centre down to the ground it lies on
    on: object                   # the world body it is reckoned around
    key: tuple = None            # a rock's cell (shoonyakasha.planet.rocks)
    state: str = "jolt"
    far: bool = False            # in the physics: on the coarse terrain mesh
    fast: bool = False           # in the physics: faster than FAST_SPEED, undamped
    position: tuple = None       # out of the physics: from its body's centre
    velocity: tuple = (0.0, 0.0, 0.0)
    rotation: tuple = (1.0, 0.0, 0.0, 0.0)
    spin: tuple = (0.0, 0.0, 0.0)  # angular velocity, rad/s
    ground: float = None         # m: the terrain's height under it as last measured
    measured: int = -1           # the probe round that measured it
    kind: str = "crate"          # "crate", or a rock's kind (vegetation.KINDS' "rock" or "boulder")
    extent: tuple = None         # its entity's scale
    colour: tuple = CRATE_COLOUR
    grade: float = 0.0           # an ore rock's ore richness, else 0
    resource: int = 0            # an ore rock's resource (shoonyakasha.planet.resources), else 0
    glow: int = None             # an ore rock's entity of glowing specks, placed with its entity

    @property
    def mass(self):
        return self.shape[2]

    @property
    def breakable(self):
        """An ore boulder (a strike can break it), whose strikes are watched in the physics."""
        return self.kind == "boulder" and self.grade > 0.0


class Props:
    def __init__(self, engine, mesh, world, on_remove=None):
        """`mesh`: (positions, normals) of a unit cube centred on the origin."""
        self.engine = engine
        self.mesh = mesh
        self.world = world
        self.on_remove = on_remove
        self.crates = []
        self.rocks = []
        self.asked = None            # the probes asked for: [(direction, body)]
        self.asking = []             # the props they were asked for
        self.round = 0               # probe rounds taken

    @property
    def all(self):
        return self.crates + self.rocks

    def in_physics(self):
        return [p for p in self.all if p.body is not None]

    def spawn_crate(self, bubble, position, rotation, size=0.6, density=500.0, colour=CRATE_COLOUR, mass=None):
        """A crate of `size` metres at bubble coordinates `position`, turned
        by `rotation`, of `mass` kg, or of `density` when None."""
        half = 0.5 * size
        physics = bubble.physics
        body = physics.create_box((half, half, half), position, rotation, motion="dynamic", density=density,
                                  mass=mass or 0.0, friction=0.7, restitution=0.1)
        crate = Prop(body, self.crate_entity(size, colour), size, 0.5 * math.sqrt(3.0) * size,
                     ("box", (half, half, half), physics.mass(body)), half, bubble.body,
                     extent=(size, size, size), colour=tuple(colour))
        self.crates.append(crate)
        return crate

    def crate_entity(self, size, colour):
        positions, normals = self.mesh
        entity = self.engine.create_mesh(positions, normals=normals, name="Crate")
        scene = self.engine.scene
        scene.set_scale(entity, (size, size, size))
        scene.set_material_vec4(entity, "baseColorFactor", tuple(colour) + (1.0,))
        scene.set_material_float(entity, "gridStrength", 0.0)
        scene.set_render_layer_mask(entity, 1)
        return entity

    def add_rock(self, body, entity, size, radius, key, shape, rest, on, kind, extent, colour, grade=0.0,
                 glow=None, resource=None):
        """A loose rock: its body (dynamic) and entities, made by the caller;
        an ore rock with no `resource` is ferrite."""
        if resource is None:
            resource = FERRITE if grade > 0.0 else 0
        rock = Prop(body, entity, size, radius, shape, rest, on, key, kind=kind, extent=tuple(extent),
                    colour=tuple(colour), grade=grade, resource=resource, glow=glow)
        self.rocks.append(rock)
        return rock

    # ── Records ──

    def record(self, bubble, prop):
        """`prop` as a record (a dict of plain values, for JSON): out of the
        physics as it is; in the physics, resting when asleep, else flying."""
        if prop.body is not None:
            physics = bubble.physics
            p, q = physics.get_transform(prop.body)
            on, position = bubble.body, bubble.body_relative(p)
            if physics.is_active(prop.body):
                state, (v, w) = "flying", physics.get_velocity(prop.body)
            else:
                state, v, w = "resting", (0.0, 0.0, 0.0), (0.0, 0.0, 0.0)
        else:
            on, position, q, state, v, w = prop.on, prop.position, prop.rotation, prop.state, prop.velocity, prop.spin
        return {"kind": prop.kind, "on": on.name, "state": state, "position": list(position), "rotation": list(q),
                "velocity": list(v), "spin": list(w), "size": prop.size, "radius": prop.radius, "rest": prop.rest,
                "mass": prop.mass, "extent": list(prop.extent), "colour": list(prop.colour),
                "key": list(prop.key) if prop.key is not None else None, "ground": prop.ground, "grade": prop.grade,
                "resource": prop.resource}

    def records(self, bubble):
        return [self.record(bubble, prop) for prop in self.all]

    def restore(self, record, bodies, entity=None, points=None, glow=None):
        """A prop out of the physics from `record`; `bodies` the world
        bodies by name. A rock needs its `entity` (and an ore rock's `glow`)
        and its hull's `points`; a crate's are made here."""
        size, mass = record["size"], record["mass"]
        if record["kind"] == "crate":
            half = 0.5 * size
            shape = ("box", (half, half, half), mass)
            entity = self.crate_entity(size, record["colour"])
        else:
            shape = ("hull", points, mass)
        key = tuple(record["key"]) if record["key"] is not None else None
        grade = record.get("grade", 0.0)
        resource = record.get("resource", FERRITE if grade > 0.0 else 0)
        prop = Prop(None, entity, size, record["radius"], shape, record["rest"], bodies[record["on"]], key,
                    state=record["state"], position=tuple(record["position"]), velocity=tuple(record["velocity"]),
                    rotation=tuple(record["rotation"]), spin=tuple(record["spin"]), ground=record["ground"],
                    kind=record["kind"], extent=tuple(record["extent"]), colour=tuple(record["colour"]),
                    grade=grade, resource=resource, glow=glow)
        (self.crates if prop.kind == "crate" else self.rocks).append(prop)
        return prop

    def find(self, body):
        return next((p for p in self.all if p.body == body), None)

    def remove(self, bubble, prop, release=True):
        """`prop` gone from the world; with `release`, `on_remove` is told
        of a rock with a cell."""
        if prop.body is not None and bubble is not None and bubble.physics.contains(prop.body):
            bubble.physics.remove(prop.body)
        self.engine.scene.destroy_entity(prop.entity)
        if prop.glow is not None:
            self.engine.scene.destroy_entity(prop.glow)
        if prop.kind == "crate":
            self.crates.remove(prop)
        else:
            self.rocks.remove(prop)
            if release and prop.key is not None and self.on_remove is not None:
                self.on_remove(prop)

    def clear(self, bubble):
        for prop in self.all:
            self.remove(bubble, prop)

    def transforms(self, bubble, props=None):
        """(position, rotation) of each prop in the physics (of `props`, all
        of them when None) in bubble coordinates, in their order."""
        props = self.in_physics() if props is None else props
        if not props:
            return []
        data = np.frombuffer(bubble.physics.get_transforms([p.body for p in props]), dtype=np.float64).reshape(-1, 7)
        return [(tuple(row[:3]), tuple(row[3:])) for row in data]

    # ── Into and out of the physics ──

    def leave(self, bubble, prop):
        """`prop` out of the physics where it is: resting when asleep, or
        slow on its measured ground; else flying."""
        physics = bubble.physics
        asleep = not physics.is_active(prop.body)
        p, q = physics.get_transform(prop.body)
        v, w = physics.get_velocity(prop.body)
        physics.remove(prop.body)
        prop.body = None
        prop.on = bubble.body
        prop.position = bubble.body_relative(p)
        prop.rotation = q
        grounded = prop.ground is not None and self.height(prop, prop.position) < prop.rest + LANDED
        if asleep or (length(v) <= REST_SPEED and grounded):
            prop.state, prop.velocity, prop.spin = "resting", (0.0, 0.0, 0.0), (0.0, 0.0, 0.0)
        else:
            prop.state, prop.velocity, prop.spin = "flying", v, w

    def enter(self, bubble, prop):
        """`prop` into the physics where it is, moving as it was; a resting
        one asleep; strikes on an ore boulder watched."""
        physics = bubble.physics
        p = sub(prop.position, bubble.origin)
        kind, form, mass = prop.shape
        if kind == "box":
            body = physics.create_box(form, p, prop.rotation, motion="dynamic", mass=mass, friction=0.7,
                                      restitution=0.1)
        else:
            body = physics.create_convex_hull(form, p, prop.rotation, motion="dynamic", mass=mass, friction=0.8,
                                              restitution=0.05)
        physics.set_velocity(body, prop.velocity, prop.spin)
        if prop.breakable:
            physics.watch_strikes(body)
        if prop.state == "resting":
            physics.deactivate(body)
        prop.body, prop.state, prop.far, prop.fast = body, "jolt", False, False

    def leave_all(self, bubble):
        for prop in self.in_physics():
            self.leave(bubble, prop)

    def height(self, prop, position):
        """m above the ground under `position` (from the prop's body's centre)."""
        ground = prop.ground if prop.ground is not None else prop.on.heights[1]
        return length(position) - prop.on.radius - ground

    def update(self, bubble, fine, coarse):
        """The props moved between the states (see the module's docstring);
        `fine` and `coarse` are the terrain colliders."""
        physics = bubble.physics
        inside = props = self.in_physics()
        for prop, (p, _) in zip(props, self.transforms(bubble, props)):
            margin = MARGIN + prop.size
            if length(p) > bubble.RADIUS:
                self.leave(bubble, prop)
                continue
            if fine.covers(p, margin):
                far = False
            elif not physics.is_active(prop.body):
                self.leave(bubble, prop)
                continue
            elif coarse.covers(p, margin):
                far = True
            elif self.height(prop, bubble.body_relative(p)) < CLEARANCE:
                self.leave(bubble, prop)
                continue
            else:
                far = True
            if far != prop.far:
                physics.set_far(prop.body, far)
                prop.far = far
            fast = length(physics.get_velocity(prop.body)[0]) > FAST_SPEED
            if fast != prop.fast:
                physics.set_linear_damping(prop.body, 0.0 if fast else DAMPING)
                prop.fast = fast
        for prop in self.all:
            if prop.body is not None or prop in inside or prop.on is not bubble.body:
                continue
            p = sub(prop.position, bubble.origin)
            if length(p) > bubble.RADIUS - REENTER:
                continue
            margin = MARGIN + prop.size
            if prop.state == "resting":
                back = fine.covers(p, margin)
            else:
                back = (fine.covers(p, margin) or coarse.covers(p, margin)
                        or self.height(prop, prop.position) > 2.0 * CLEARANCE)
            if back:
                self.enter(bubble, prop)

    def drag(self, bubble):
        """For the next step, the air's drag on the props in the physics
        moving faster than DRAG_SPEED through the air (their velocity less
        the wind's), on the planet; on an asleep one only when it is more
        than STICK of its weight."""
        world = self.world
        if bubble.body is not world.planet:
            return
        physics = bubble.physics
        for prop in self.in_physics():
            p = bubble.body_relative(physics.get_transform(prop.body)[0])
            v = sub(physics.get_velocity(prop.body)[0], world.wind_at(bubble.body, p))
            speed = length(v)
            if speed < DRAG_SPEED:
                continue
            area = 0.6 * math.pi * prop.radius * prop.radius
            k = 0.5 * world.physics.density(p) * speed * DRAG * area
            if not physics.is_active(prop.body) and k * speed < STICK * prop.mass * length(bubble.gravity(
                    physics.get_transform(prop.body)[0])):
                continue
            physics.add_force(prop.body, scale(v, -k))

    # ── Out of the physics ──

    def body_of(self, orbiting):
        """The world body of an orbits body (orbits.Planet or orbits.Moon)."""
        world = self.world
        return world.moon if orbiting is world.physics.moon else world.planet

    def gm(self, body):
        world = self.world
        return world.physics.moon.gm if body is world.moon else world.physics.gm

    def fly(self, dt, latency):
        """The flying props moved on by `dt` seconds of the world's clock;
        `latency` the probes' in seconds."""
        world = self.world
        for prop in self.all:
            if prop.state != "flying" or prop.body is not None:
                continue
            # Reckoned around the body whose sphere of influence it is in.
            centre, motion = prop.on.centre(), prop.on.velocity()
            orbiting, p, v = reference(world.physics, add(centre, prop.position), add(motion, prop.velocity))
            body = self.body_of(orbiting)
            if body is not prop.on:
                prop.on, prop.position, prop.velocity, prop.ground = body, p, v, None
            gm = self.gm(prop.on)
            on_planet = prop.on is world.planet
            area = 0.6 * math.pi * prop.radius * prop.radius
            p, v, q, w = prop.position, prop.velocity, prop.rotation, prop.spin
            spin = math.sqrt(dot(w, w))
            left = dt
            while left > 1e-12:
                h = min(FLY_STEP, left)
                r = length(p)
                a = scale(p, -gm / (r * r * r))
                if on_planet:
                    air = sub(v, world.wind_at(prop.on, p, None if prop.ground is None
                                               else r - prop.on.radius - prop.ground))
                    speed = length(air)
                    a = add(a, scale(air, -0.5 * world.physics.density(p) * speed * DRAG * area / prop.mass))
                v = add(v, scale(a, h))
                p = add(p, scale(v, h))
                if spin > 0.0:
                    q = quat_normalize(quat_multiply(axis_angle(scale(w, 1.0 / spin), spin * h), q))
                left -= h
            prop.position, prop.velocity, prop.rotation = p, v, q
            if prop.ground is None:
                continue
            up = normalize(p)
            down = max(-dot(v, up), 0.0)
            if self.height(prop, p) - down * latency < prop.rest:
                prop.position = scale(up, prop.on.radius + prop.ground + prop.rest)
                prop.state, prop.velocity, prop.spin = "resting", (0.0, 0.0, 0.0), (0.0, 0.0, 0.0)

    def needs_ground(self, bubble):
        """The props whose ground is measured: flying, or in the physics over neither terrain mesh."""
        props = [p for p in self.all if p.state == "flying" and p.body is None]
        if bubble is not None:
            props += [p for p in self.in_physics() if p.far]
        return props

    def probe(self, bubble, probes):
        """The ground under the props that need it, with `probes` of the
        world's extra probes a round, while nothing else uses them."""
        world = self.world
        if world.extra_probes and world.extra_probes != self.asked:
            return
        if self.asked is not None:
            if [probe for probe, _ in world.extra_heights] != self.asked:
                return
            self.round += 1
            for prop, (_, height) in zip(self.asking, world.extra_heights):
                prop.ground, prop.measured = height, self.round
            self.asked = None
        wanted = sorted(self.needs_ground(bubble), key=lambda p: p.measured)[:probes]
        if not wanted:
            world.extra_probes = []
            return
        asked = []
        for prop in wanted:
            if prop.body is not None:
                position = bubble.body_relative(bubble.physics.get_transform(prop.body)[0])
            else:
                position = prop.position
            asked.append((normalize(position), prop.on))
        world.extra_probes = asked
        self.asked, self.asking = asked, wanted

    # ── Per frame ──

    def draw(self, bubble, camera):
        """The props' entities where they are, relative to `camera` (from the planet's centre)."""
        scene = self.engine.scene
        placed = []
        if bubble is not None:
            props = self.in_physics()
            placed = [(prop, bubble.to_world(p), q) for prop, (p, q) in zip(props, self.transforms(bubble, props))]
        placed += [(prop, add(prop.on.centre(), prop.position), prop.rotation)
                   for prop in self.all if prop.body is None]
        for prop, world_position, q in placed:
            x, y, z = quat_rotate(q, (1.0, 0.0, 0.0)), quat_rotate(q, (0.0, 1.0, 0.0)), quat_rotate(q, (0.0, 0.0, 1.0))
            rotation = euler_from_basis(x, y, scale(z, -1.0))
            for entity in (prop.entity, prop.glow):
                if entity is not None:
                    scene.set_position(entity, sub(world_position, camera))
                    scene.set_rotation(entity, rotation)

    def grabbable(self, physics):
        """(body, radius, mass) of each prop in the physics."""
        return [(p.body, p.radius, physics.mass(p.body)) for p in self.in_physics()]

    def awake(self, bubble):
        return sum(1 for p in self.in_physics() if bubble.physics.is_active(p.body))


def axis_angle(axis, angle):
    s = math.sin(0.5 * angle)
    return (math.cos(0.5 * angle), axis[0] * s, axis[1] * s, axis[2] * s)


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
