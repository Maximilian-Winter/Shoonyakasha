"""
The physics bubble: a frame for rigid bodies (sk.PlanetPhysics) near a
point on one body, the planet or the moon.

Bubble coordinates are positions relative to the body's centre, minus
`origin`, a point near what the bubble follows (body-relative, 64-bit). The
axes are the body's, so the frame does not turn: rebasing moves `origin`
and every body in the physics by the same offset, and leaves velocities
and rotations as they are. A point in the bubble is
`body.centre() + origin + p` from the planet's centre. On the moon the
frame moves with the moon, so a body at rest on it stays at rest.

Gravity: the physics has none of its own. `step` pulls each awake dynamic
body towards the body's centre with GM / r^2 before stepping.

`anchor` is a unit direction from the body's centre fixed while the bubble
stays within ANCHOR_REACH metres of it (along the tangent plane); the
terrain collider's grid (collider_probes) lies in the plane tangent to the
sphere there, so its samples fall on the same directions however often
the bubble rebases.
"""

from .vec import add, dot, length, local_frame, normalize, scale, sub


class Bubble:
    REBASE_DISTANCE = 500.0          # m from the origin before rebasing
    RADIUS = 2_000.0                 # m from the origin: the bubble's reach
    ANCHOR_REACH = 1_000.0           # m the origin may move from the anchor before the anchor moves to it

    def __init__(self, physics, body, gm, origin):
        """A bubble in `physics` (an sk.PlanetPhysics) around `origin`
        (relative to `body`'s centre); `gm` is the body's GM (m^3/s^2)."""
        self.physics = physics
        self.body = body
        self.gm = gm
        self.origin = tuple(origin)
        self.set_anchor(normalize(self.origin))
        self.listeners = []          # callables taking the offset of each rebase

    def set_anchor(self, direction):
        self.anchor = direction
        self.east, self.north = local_frame(direction)

    def to_bubble(self, point):
        """`point` (from the planet's centre) in bubble coordinates."""
        return sub(self.body.relative(point), self.origin)

    def to_world(self, p):
        """Bubble coordinates `p` from the planet's centre."""
        return add(self.body.centre(), add(self.origin, p))

    def body_relative(self, p):
        """Bubble coordinates `p` relative to the body's centre."""
        return add(self.origin, p)

    def up(self, p):
        """The unit direction away from the body's centre at `p`."""
        return normalize(self.body_relative(p))

    def gravity(self, p):
        """The acceleration of gravity (m/s^2) at bubble coordinates `p`."""
        r = self.body_relative(p)
        r2 = dot(r, r)
        return scale(r, -self.gm / (r2 * r2 ** 0.5))

    def step(self, dt):
        """Gravity on the awake dynamic bodies, then one physics step of `dt` seconds."""
        self.physics.apply_central_gravity(scale(self.origin, -1.0), self.gm)
        self.physics.step(dt)

    def rebase(self, offset):
        """The origin moved by `offset`, every body moved by -offset, and the
        listeners told; the anchor moved to the origin once the origin is
        more than ANCHOR_REACH from it."""
        self.physics.shift_origin(offset)
        self.origin = add(self.origin, offset)
        up = normalize(self.origin)
        across = sub(up, scale(self.anchor, dot(up, self.anchor)))
        if length(across) * length(self.origin) > self.ANCHOR_REACH:
            self.set_anchor(up)
        for listener in self.listeners:
            listener(offset)

    def maybe_rebase(self, focus):
        """Rebase to `focus` (bubble coordinates) when it is REBASE_DISTANCE
        or more from the origin; True when it did."""
        if length(focus) < self.REBASE_DISTANCE:
            return False
        self.rebase(focus)
        return True
