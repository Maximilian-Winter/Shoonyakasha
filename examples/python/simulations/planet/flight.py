"""
A ship's flight over the planet, in Python floats (64-bit), with the
planet's centre at the origin.

The ship is a rigid body: position and velocity, an orientation (a unit
quaternion from the ship's body to the world) and an angular velocity in
the body. Its body axes are those of a camera: forward is -Z, up +Y,
right +X.

Forces: gravity falling off with the square of the distance from the
centre; drag against the velocity, and lift along the ship's up axis for
air flowing along its forward axis, both with the air's density, which
falls off exponentially with height; and the thrusters. `Ship.step`
advances it by one time step with semi-implicit Euler: the velocity
first, then the position with the new velocity.

Flight assist: the stick asks for rotation rates, which the ship's
torque holds (always). With translation assist on, the throttle asks for
a forward speed and the vertical thrusters for a climb rate; the
thrusters push towards that velocity and hold the ship against gravity,
drag and lift. With it off, the throttle drives the main engine and the
vertical thrusters push up or down.
"""

import math
from dataclasses import dataclass, field


# ── Vectors and quaternions (tuples) ───────────────────────────────────────

def add(a, b):
    return (a[0] + b[0], a[1] + b[1], a[2] + b[2])


def sub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def scale(a, s):
    return (a[0] * s, a[1] * s, a[2] * s)


def dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def length(a):
    return math.sqrt(dot(a, a))


def normalize(a):
    return scale(a, 1.0 / length(a))


def quat_multiply(a, b):
    """The Hamilton product a b of quaternions (w, x, y, z)."""
    aw, ax, ay, az = a
    bw, bx, by, bz = b
    return (aw * bw - ax * bx - ay * by - az * bz,
            aw * bx + ax * bw + ay * bz - az * by,
            aw * by - ax * bz + ay * bw + az * bx,
            aw * bz + ax * by - ay * bx + az * bw)


def quat_rotate(q, v):
    """`v` turned by the unit quaternion `q`."""
    w, x, y, z = q
    u = (x, y, z)
    t = scale(cross(u, v), 2.0)
    return add(add(v, scale(t, w)), cross(u, t))


def quat_normalize(q):
    n = math.sqrt(sum(c * c for c in q))
    return tuple(c / n for c in q)


def quat_from_basis(right, up, back):
    """The unit quaternion turning the body axes X, Y, Z onto `right`, `up`
    and `back` (orthonormal, right-handed)."""
    m00, m10, m20 = right
    m01, m11, m21 = up
    m02, m12, m22 = back
    trace = m00 + m11 + m22
    if trace > 0.0:
        s = math.sqrt(trace + 1.0) * 2.0
        q = (0.25 * s, (m21 - m12) / s, (m02 - m20) / s, (m10 - m01) / s)
    elif m00 > m11 and m00 > m22:
        s = math.sqrt(1.0 + m00 - m11 - m22) * 2.0
        q = ((m21 - m12) / s, 0.25 * s, (m01 + m10) / s, (m02 + m20) / s)
    elif m11 > m22:
        s = math.sqrt(1.0 + m11 - m00 - m22) * 2.0
        q = ((m02 - m20) / s, (m01 + m10) / s, 0.25 * s, (m12 + m21) / s)
    else:
        s = math.sqrt(1.0 + m22 - m00 - m11) * 2.0
        q = ((m10 - m01) / s, (m02 + m20) / s, (m12 + m21) / s, 0.25 * s)
    return quat_normalize(q)


# ── The planet and the ship ────────────────────────────────────────────────

@dataclass
class Planet:
    radius: float = 100_000.0           # m
    surface_gravity: float = 4.905      # m/s^2 at the radius
    air_density: float = 1.225          # kg/m^3 at the radius
    scale_height: float = 2_500.0       # m; the atmosphere's Rayleigh scale height
    density_factor: float = 1.0         # times the air's density

    @property
    def gm(self):
        return self.surface_gravity * self.radius * self.radius

    def gravity(self, position):
        r = length(position)
        return scale(position, -self.gm / (r * r * r))

    def density(self, position):
        height = max(length(position) - self.radius, -2_000.0)
        return self.air_density * self.density_factor * math.exp(-height / self.scale_height)


@dataclass
class ShipParams:
    mass: float = 20_000.0                              # kg
    inertia: tuple = (4e5, 5e5, 3e5)                    # kg m^2 about the body's X, Y, Z
    main_thrust: float = 6e5                            # N, forward
    rcs_thrust: float = 1.5e5                           # N, along each other direction
    max_torque: float = 2e5                             # N m about each axis
    drag_area: float = 12.0                             # m^2, drag coefficient times area
    lift_area: float = 8.0                              # m^2, lift coefficient times area
    max_rates: tuple = (0.8, 0.6, 1.5)                  # rad/s of pitch, yaw, roll at full stick
    rotation_gain: float = 4.0                          # 1/s
    assist_max_speed: float = 1_500.0                   # m/s forward at full throttle, assisted
    assist_climb_rate: float = 60.0                     # m/s up at full vertical stick, assisted
    assist_gain: float = 1.5                            # 1/s


@dataclass
class Controls:
    throttle: float = 0.0       # 0 .. 1
    pitch: float = 0.0          # -1 .. 1, nose up
    yaw: float = 0.0            # -1 .. 1, nose right
    roll: float = 0.0           # -1 .. 1, right wing down
    vertical: float = 0.0       # -1 .. 1, up
    assist: bool = True         # translation assist


@dataclass
class Ship:
    position: tuple
    velocity: tuple = (0.0, 0.0, 0.0)
    orientation: tuple = (1.0, 0.0, 0.0, 0.0)       # body to world
    angular_velocity: tuple = (0.0, 0.0, 0.0)       # rad/s, in the body
    params: ShipParams = field(default_factory=ShipParams)
    thrust: tuple = (0.0, 0.0, 0.0)                 # N, in the body, the last step's

    def axes(self):
        """(right, up, forward) in the world."""
        q = self.orientation
        return quat_rotate(q, (1.0, 0.0, 0.0)), quat_rotate(q, (0.0, 1.0, 0.0)), quat_rotate(q, (0.0, 0.0, -1.0))

    def aerodynamic_force(self, planet):
        """Drag and lift (N, in the world)."""
        rho = planet.density(self.position)
        v = self.velocity
        speed = length(v)
        if speed < 1e-6 or rho == 0.0:
            return (0.0, 0.0, 0.0)
        drag = scale(v, -0.5 * rho * speed * self.params.drag_area)
        _, up, forward = self.axes()
        along = dot(v, forward)
        lift = scale(up, 0.5 * rho * along * abs(along) * self.params.lift_area)
        return add(drag, lift)

    def _thrust(self, planet, controls, gravity, aero):
        """The thrusters' force in the body: (right, up, back) components."""
        p = self.params
        right, up, forward = self.axes()
        if controls.assist:
            wanted = add(scale(forward, controls.throttle * p.assist_max_speed),
                         scale(up, controls.vertical * p.assist_climb_rate))
            accel = sub(scale(sub(wanted, self.velocity), p.assist_gain),
                        add(gravity, scale(aero, 1.0 / p.mass)))
            force = scale(accel, p.mass)
            body = (dot(force, right), dot(force, up), -dot(force, forward))
        else:
            body = (0.0, controls.vertical * p.rcs_thrust, -controls.throttle * p.main_thrust)
        return (max(-p.rcs_thrust, min(p.rcs_thrust, body[0])),
                max(-p.rcs_thrust, min(p.rcs_thrust, body[1])),
                max(-p.main_thrust, min(p.rcs_thrust, body[2])))

    def step(self, planet, controls, dt):
        p = self.params
        gravity = planet.gravity(self.position)
        aero = self.aerodynamic_force(planet)
        self.thrust = self._thrust(planet, controls, gravity, aero)
        right, up, forward = self.axes()
        thrust_world = add(add(scale(right, self.thrust[0]), scale(up, self.thrust[1])),
                           scale(forward, -self.thrust[2]))
        accel = add(gravity, scale(add(thrust_world, aero), 1.0 / p.mass))
        self.velocity = add(self.velocity, scale(accel, dt))
        self.position = add(self.position, scale(self.velocity, dt))
        self._rotate(controls, dt)

    def _rotate(self, controls, dt):
        p = self.params
        wanted = (controls.pitch * p.max_rates[0], -controls.yaw * p.max_rates[1], -controls.roll * p.max_rates[2])
        w = self.angular_velocity
        inertia = p.inertia
        torque = tuple(max(-p.max_torque, min(p.max_torque, inertia[i] * (wanted[i] - w[i]) * p.rotation_gain))
                       for i in range(3))
        spin = cross(w, tuple(inertia[i] * w[i] for i in range(3)))   # the gyroscopic term, w x (I w)
        w = tuple(w[i] + (torque[i] - spin[i]) / inertia[i] * dt for i in range(3))
        self.angular_velocity = w
        dq = quat_multiply(self.orientation, (0.0, w[0], w[1], w[2]))
        self.orientation = quat_normalize(tuple(self.orientation[i] + 0.5 * dt * dq[i] for i in range(4)))

    def rest_on(self, planet, ground, clearance, friction):
        """Keep the ship `clearance` metres above a surface `ground` metres
        above the radius: on touching it, stop falling into it, and lose
        `friction` of the speed along it. Returns whether it touches."""
        r = length(self.position)
        floor = planet.radius + ground + clearance
        if r >= floor:
            return False
        radial = scale(self.position, 1.0 / r)
        self.position = scale(radial, floor)
        into = dot(self.velocity, radial)
        if into < 0.0:
            self.velocity = sub(self.velocity, scale(radial, into))
        along = sub(self.velocity, scale(radial, dot(self.velocity, radial)))
        self.velocity = sub(self.velocity, scale(along, friction))
        return True


def orbit(planet, position, velocity):
    """(periapsis, apoapsis) heights above the radius of the path the ship
    would coast along around the planet; apoapsis is inf when it would
    escape."""
    r = length(position)
    v2 = dot(velocity, velocity)
    energy = 0.5 * v2 - planet.gm / r
    h = length(cross(position, velocity))
    e = math.sqrt(max(0.0, 1.0 + 2.0 * energy * h * h / (planet.gm * planet.gm)))
    if energy >= 0.0:
        periapsis = h * h / (planet.gm * (1.0 + e))
        return periapsis - planet.radius, math.inf
    a = -planet.gm / (2.0 * energy)
    return a * (1.0 - e) - planet.radius, a * (1.0 + e) - planet.radius


def circular_speed(planet, height):
    return math.sqrt(planet.gm / (planet.radius + height))
