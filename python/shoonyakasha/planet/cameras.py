"""
Cameras for a PlanetWorld, driven by the mouse and the keys.

Each camera has `update(dt, active=True)`, called once a frame (`active`
False while the pointer is over the user interface, which then gets the
mouse), `scroll(steps)` for the mouse wheel, and gives `position` and
`basis` (right, up, forward), both in planet space, for
PlanetWorld.end_frame. `body` is the body it is about.

GlobeCamera, like a digital globe: it looks at a target point on a body's
ground from `range` metres away, turned `heading` from north (clockwise
seen from above) and tilted `tilt` from straight down.
  - Dragging with the left button grabs the ground under the pointer: the
    view turns about the body's centre so that the grabbed point stays
    under the pointer. Let go while moving and it coasts, slowing down.
  - The wheel zooms towards the point under the pointer, by 20 % a step.
  - Dragging with the right (or middle) button turns the heading
    (sideways) and the tilt (up and down), about the target.
  - A double-click flies to the point under the pointer, closer by 70 %.
  - W, A, S, D move the target over the ground, at the range a second; Q
    and E turn the heading; N turns the view to north up, U straight down.
  - The target's height is the terrain's, from a probe (the world's last
    extra probe); the camera stays 2 m above the ground under it.

FocusCamera, like a planetarium of the system: it circles a body's centre
at `distance`, at `yaw` around and `pitch` above the moon's orbital plane
(the planet's equator without a moon).
  - Dragging with the left button circles the body; let go while moving and
    it coasts.
  - The wheel zooms in and out, by 15 % a step, from just above the body's
    ground out to 40 times the planet's radius.
  - A click (pressed and let go without moving) on a body focuses the
    camera on it: the view moves there over a second and a half.
  - Q and E turn about the body (yaw), R and F up and down (pitch).
"""

import math

from .. import keys
from .vec import add, cross, dot, length, local_frame, normalize, rotate, scale, sub

DOUBLE_CLICK_SECONDS = 0.35
CLICK_PIXELS = 5.0               # pressed and let go within this many pixels: a click, not a drag
COAST_DECAY = 4.0                # per second, how fast a coasting view slows down
TURN_PER_PIXEL = 0.005           # radians of heading, tilt, yaw or pitch per pixel dragged


def smoothstep(t):
    t = max(0.0, min(1.0, t))
    return t * t * (3.0 - 2.0 * t)


def cursor_ray(cursor, size, basis, fov):
    """The unit direction from the camera through the window point
    `cursor` (pixels from the top left) of a window `size` pixels, for a
    camera with `basis` (right, up, forward) and a vertical field of view
    `fov` degrees."""
    width, height = size
    right, up, forward = basis
    if width <= 0 or height <= 0:
        return forward
    x = 2.0 * cursor[0] / width - 1.0
    y = 1.0 - 2.0 * cursor[1] / height
    t = math.tan(math.radians(fov) * 0.5)
    return normalize(add(forward, add(scale(right, x * t * width / height), scale(up, y * t))))


def ray_sphere(origin, direction, centre, radius):
    """The distance along the unit `direction` from `origin` to where it
    first meets the sphere, or None when it misses or the sphere is behind."""
    to_centre = sub(centre, origin)
    along = dot(to_centre, direction)
    closest = length(sub(to_centre, scale(direction, along)))
    if closest > radius:
        return None
    half = math.sqrt(radius * radius - closest * closest)
    near = along - half
    if near >= 0.0:
        return near
    far = along + half
    return far if far >= 0.0 else None


def turn_between(a, b):
    """(axis, angle) of the rotation taking the unit vector `a` onto `b`;
    angle 0 when they are parallel."""
    axis = cross(a, b)
    size = length(axis)
    if size < 1e-12:
        return (0.0, 0.0, 1.0), 0.0
    return scale(axis, 1.0 / size), math.atan2(size, dot(a, b))


class _Mouse:
    """The mouse's buttons, position and clicks, from the engine's input."""

    def __init__(self, engine):
        self.engine = engine
        self.held = set()
        self.pressed_at = {}             # button: (cursor, time) of its last press
        self.moved = {}                  # button: the most pixels it moved from its press while held
        self.last_click = None           # (cursor, time) of the last left click
        self.time = 0.0

    def read(self, dt, active):
        """This frame's (pressed, released, clicked, double_clicked): the
        buttons pressed and let go, and whether the left button was clicked
        or double-clicked. Nothing is pressed while not `active`."""
        self.time += dt
        cursor = self.engine.input.get_mouse_position()
        pressed, released = set(), set()
        clicked = double = False
        for button in (0, 1, 2):
            down = self.engine.input.is_mouse_button_down(button)
            if down and button not in self.held and active:
                self.held.add(button)
                pressed.add(button)
                self.pressed_at[button] = (cursor, self.time)
                self.moved[button] = 0.0
                if button == 0 and self.last_click is not None:
                    where, when = self.last_click
                    if (self.time - when < DOUBLE_CLICK_SECONDS
                            and length((cursor[0] - where[0], cursor[1] - where[1], 0.0)) < CLICK_PIXELS):
                        double = True
            elif down and button in self.held:
                start = self.pressed_at[button][0]
                self.moved[button] = max(self.moved[button],
                                         length((cursor[0] - start[0], cursor[1] - start[1], 0.0)))
            elif not down and button in self.held:
                self.held.discard(button)
                released.add(button)
                if button == 0 and self.moved.get(0, 0.0) < CLICK_PIXELS:
                    clicked = True
                    self.last_click = (cursor, self.time)
        if double:
            self.last_click = None
        return pressed, released, clicked, double

    @property
    def cursor(self):
        return self.engine.input.get_mouse_position()


class GlobeCamera:
    """See the module's description."""

    MIN_RANGE = 5.0
    MAX_RANGE_RADII = 40.0           # the farthest range, in radii of the planet
    TILT_MAX = math.radians(85.0)
    ZOOM_STEP = 0.8
    CLEARANCE = 2.0                  # metres the camera stays above the ground under it
    FLY_SECONDS = 1.5

    def __init__(self, world, body=None, target=None, range_=20_000.0, heading=0.0, tilt=0.0):
        self.world = world
        self.engine = world.engine
        self.body = body or world.planet
        self.target = normalize(target) if target is not None else world.site_frame()[1]
        self.range = range_
        self.heading = heading
        self.tilt = tilt
        self.target_height = 0.0         # the ground at the target, from the probe
        self.mouse = _Mouse(self.engine)
        self.grab = None                 # the grabbed point's direction from the body's centre
        self.coast = None                # (axis, radians a second) while coasting
        self.animation = None            # (start, end, seconds, elapsed) of a flight or a turn
        self.position = (0.0, 0.0, 0.0)
        self.basis = ((1.0, 0.0, 0.0), (0.0, 1.0, 0.0), (0.0, 0.0, -1.0))
        self.place()

    # ── Geometry ──────────────────────────────────────────────────────────

    def ground_radius(self):
        """The radius of the sphere the pointer grabs: the body's, at the
        target's ground (at sea level where that is below it on a body with
        an ocean)."""
        height = max(self.target_height, 0.0) if self.body.ocean else self.target_height
        return self.body.radius + height

    def horizontal(self):
        """The heading's direction along the ground at the target."""
        east, north = local_frame(self.target)
        return add(scale(north, math.cos(self.heading)), scale(east, math.sin(self.heading)))

    def target_point(self):
        return add(self.body.centre(), scale(self.target, self.ground_radius()))

    def place(self):
        """The camera's position and basis from the target, range, heading and tilt."""
        up = self.target
        ahead = self.horizontal()
        offset = sub(scale(up, math.cos(self.tilt)), scale(ahead, math.sin(self.tilt)))
        point = self.target_point()
        eye = add(point, scale(offset, self.range))
        lifted = self.world.keep_above_ground(eye, self.CLEARANCE)
        forward = normalize(sub(point, lifted)) if lifted != eye else scale(offset, -1.0)
        screen_up = normalize(add(scale(up, math.sin(self.tilt)), scale(ahead, math.cos(self.tilt))))
        if lifted != eye:
            screen_up = normalize(sub(screen_up, scale(forward, dot(screen_up, forward))))
        right = normalize(cross(forward, screen_up))
        self.position = lifted
        self.basis = (right, cross(right, forward), forward)

    def hit(self, cursor):
        """The direction from the body's centre of the ground point under
        `cursor`, or None when the pointer is off the body."""
        ray = cursor_ray(cursor, self.engine.framebuffer_size, self.basis, self.world.fov)
        centre = self.body.centre()
        t = ray_sphere(self.position, ray, centre, self.ground_radius())
        if t is None:
            return None
        return normalize(sub(add(self.position, scale(ray, t)), centre))

    def turn(self, axis, angle):
        """The view turned by `angle` about `axis` through the body's centre,
        keeping its heading relative to the ground."""
        if angle == 0.0:
            return
        ahead = rotate(self.horizontal(), axis, angle)
        self.target = normalize(rotate(self.target, axis, angle))
        east, north = local_frame(self.target)
        self.heading = math.atan2(dot(ahead, east), dot(ahead, north))

    def max_range(self):
        return self.MAX_RANGE_RADII * self.world.planet.radius

    # ── Actions ───────────────────────────────────────────────────────────

    def look_at(self, body, target, range_=None, heading=None, tilt=None):
        """The view set at once: on `body`, at `target` (a direction from its centre)."""
        self.body = body
        self.target = normalize(target)
        self.target_height = 0.0
        if range_ is not None:
            self.range = range_
        if heading is not None:
            self.heading = heading
        if tilt is not None:
            self.tilt = tilt
        self.coast = self.animation = self.grab = None
        self.place()

    def fly_to(self, target, range_=None, heading=None, tilt=None, seconds=None):
        """The view moved over `seconds` to `target` (a direction from the
        body's centre), and to the range, heading and tilt given."""
        start = (self.target, self.range, self.heading, self.tilt)
        end = (normalize(target), self.range if range_ is None else range_,
               self.heading if heading is None else heading, self.tilt if tilt is None else tilt)
        self.animation = [start, end, seconds or self.FLY_SECONDS, 0.0]
        self.coast = None

    def north_up(self):
        self.fly_to(self.target, heading=0.0, seconds=0.6)

    def straight_down(self):
        self.fly_to(self.target, tilt=0.0, seconds=0.6)

    def scroll(self, steps):
        """Zoom by ZOOM_STEP a step towards (or away from) the point under the pointer."""
        if steps == 0.0:
            return
        factor = self.ZOOM_STEP ** steps
        new_range = max(self.MIN_RANGE, min(self.max_range(), self.range * factor))
        factor = new_range / self.range
        under = self.hit(self.mouse.cursor)
        if under is not None:
            axis, angle = turn_between(self.target, under)
            self.turn(axis, angle * (1.0 - factor))
        self.range = new_range
        self.animation = None

    # ── Per frame ─────────────────────────────────────────────────────────

    def read_target_height(self):
        """The ground's height at the target from the probe asked for last,
        and the probe for this frame asked for."""
        world = self.world
        for (direction, body), height in world.extra_heights:
            if body is self.body:
                self.target_height = height
        world.extra_probes = [(self.target, self.body)]

    def update(self, dt, active=True):
        engine = self.engine
        self.read_target_height()
        pressed, released, clicked, double = self.mouse.read(dt, active)
        dx, dy = engine.input.get_mouse_delta()
        cursor = self.mouse.cursor

        if 0 in pressed:
            self.grab = self.hit(cursor)
            self.coast = self.animation = None
        if double:
            under = self.hit(cursor)
            self.grab = None
            if under is not None:
                self.fly_to(under, range_=max(self.MIN_RANGE, self.range * 0.3))
        if 0 in self.mouse.held and self.grab is not None:
            under = self.hit(cursor)
            if under is not None:
                axis, angle = turn_between(under, self.grab)
                self.turn(axis, angle)
                self.coast = (axis, angle / dt) if dt > 0.0 and angle > 0.0 else None
        if 0 in released:
            self.grab = None
            if self.mouse.moved.get(0, 0.0) < CLICK_PIXELS:
                self.coast = None
        if (1 in self.mouse.held or 2 in self.mouse.held) and (dx or dy):
            self.heading += dx * TURN_PER_PIXEL
            self.tilt = max(0.0, min(self.TILT_MAX, self.tilt - dy * TURN_PER_PIXEL))
            self.animation = None

        self.keys(dt, active)
        if self.coast is not None and self.grab is None:
            axis, rate = self.coast
            self.turn(axis, rate * dt)
            rate *= math.exp(-COAST_DECAY * dt)
            self.coast = (axis, rate) if abs(rate) > 1e-5 else None
        if self.animation is not None:
            self.animate(dt)
        self.range = max(self.MIN_RANGE, min(self.max_range(), self.range))
        self.place()

    def keys(self, dt, active):
        if not active:
            return
        down = self.engine.input.is_key_down
        move = (float(down(keys.W)) - float(down(keys.S)), float(down(keys.D)) - float(down(keys.A)))
        if move != (0.0, 0.0):
            ahead = self.horizontal()
            right = cross(ahead, self.target)
            direction = add(scale(ahead, move[0]), scale(right, move[1]))
            axis = normalize(cross(self.target, direction))
            self.turn(axis, self.range / self.ground_radius() * dt)
            self.animation = None
        turn = float(down(keys.E)) - float(down(keys.Q))
        if turn:
            self.heading += turn * dt
            self.animation = None

    def animate(self, dt):
        start, end, seconds, elapsed = self.animation
        elapsed += dt
        s = smoothstep(elapsed / seconds)
        axis, angle = turn_between(start[0], end[0])
        self.target = normalize(rotate(start[0], axis, angle * s))
        self.range = math.exp(math.log(start[1]) + (math.log(end[1]) - math.log(start[1])) * s)
        turn = (end[2] - start[2] + math.pi) % (2.0 * math.pi) - math.pi
        self.heading = start[2] + turn * s
        self.tilt = start[3] + (end[3] - start[3]) * s
        self.animation = None if elapsed >= seconds else [start, end, seconds, elapsed]


class FocusCamera:
    """See the module's description."""

    ZOOM_STEP = 0.85
    MAX_DISTANCE_RADII = 40.0        # the farthest distance, in radii of the planet
    PITCH_MAX = math.radians(89.0)
    CLEARANCE = 50.0                 # metres the camera stays above the ground under it
    FOCUS_SECONDS = 1.5

    def __init__(self, world, body=None, distance=None, yaw=0.0, pitch=math.radians(20.0)):
        self.world = world
        self.engine = world.engine
        self.body = body or world.planet
        self.distance = distance or 3.0 * self.body.radius
        self.yaw, self.pitch = yaw, pitch
        self.mouse = _Mouse(self.engine)
        self.spin = (0.0, 0.0)           # yaw and pitch radians a second while coasting
        self.transition = None           # (from body, from distance, seconds, elapsed) while refocusing
        self.position = (0.0, 0.0, 0.0)
        self.basis = ((1.0, 0.0, 0.0), (0.0, 1.0, 0.0), (0.0, 0.0, -1.0))
        self.place()

    def axes(self):
        """(along, across, normal): the orbital plane's axes and its normal."""
        moon = self.world.physics.moon
        if moon is not None:
            return moon.u, moon.v, normalize(cross(moon.u, moon.v))
        return (1.0, 0.0, 0.0), (0.0, 0.0, -1.0), (0.0, 1.0, 0.0)

    def min_distance(self, body):
        height = max(self.world.ground or 0.0, 0.0)
        return body.radius + height + self.CLEARANCE

    def centre(self):
        """The point looked at: the body's centre, or on the way to it while refocusing."""
        if self.transition is None:
            return self.body.centre()
        start, _, seconds, elapsed = self.transition
        s = smoothstep(elapsed / seconds)
        return add(scale(start.centre(), 1.0 - s), scale(self.body.centre(), s))

    def place(self):
        along, across, normal = self.axes()
        level = add(scale(along, math.cos(self.yaw)), scale(across, math.sin(self.yaw)))
        offset = add(scale(level, math.cos(self.pitch)), scale(normal, math.sin(self.pitch)))
        centre = self.centre()
        self.position = add(centre, scale(offset, self.distance))
        forward = scale(offset, -1.0)
        right = normalize(cross(forward, normal))
        self.basis = (right, cross(right, forward), forward)

    def focus(self, body):
        """Move the view to `body` over FOCUS_SECONDS, ending 3 of its radii from its centre."""
        if body is self.body and self.transition is None:
            return
        self.transition = (self.body, self.distance, self.FOCUS_SECONDS, 0.0)
        self.body = body
        self.spin = (0.0, 0.0)

    def scroll(self, steps):
        self.distance *= self.ZOOM_STEP ** steps
        self.clamp()

    def clamp(self):
        far = self.MAX_DISTANCE_RADII * self.world.planet.radius
        self.distance = max(self.min_distance(self.body), min(far, self.distance))

    def pick(self, cursor):
        """The body under `cursor`, the nearest, or None."""
        ray = cursor_ray(cursor, self.engine.framebuffer_size, self.basis, self.world.fov)
        best = None
        for body in self.world.bodies:
            t = ray_sphere(self.position, ray, body.centre(), body.radius)
            if t is not None and (best is None or t < best[0]):
                best = (t, body)
        return best[1] if best else None

    def update(self, dt, active=True):
        engine = self.engine
        pressed, released, clicked, _ = self.mouse.read(dt, active)
        dx, dy = engine.input.get_mouse_delta()
        if clicked:
            body = self.pick(self.mouse.cursor)
            if body is not None:
                self.focus(body)
        if 0 in self.mouse.held and (dx or dy):
            step = (-dx * TURN_PER_PIXEL, dy * TURN_PER_PIXEL)
            self.yaw += step[0]
            self.pitch = max(-self.PITCH_MAX, min(self.PITCH_MAX, self.pitch + step[1]))
            self.spin = (step[0] / dt, step[1] / dt) if dt > 0.0 else (0.0, 0.0)
        elif 0 in pressed:
            self.spin = (0.0, 0.0)
        elif 0 not in self.mouse.held:
            decay = math.exp(-COAST_DECAY * dt)
            self.yaw += self.spin[0] * dt
            self.pitch = max(-self.PITCH_MAX, min(self.PITCH_MAX, self.pitch + self.spin[1] * dt))
            self.spin = (self.spin[0] * decay, self.spin[1] * decay)
        if active:
            down = engine.input.is_key_down
            self.yaw += (float(down(keys.E)) - float(down(keys.Q))) * dt
            self.pitch = max(-self.PITCH_MAX, min(self.PITCH_MAX, self.pitch
                                                  + (float(down(keys.R)) - float(down(keys.F))) * dt))
        if self.transition is not None:
            start, start_distance, seconds, elapsed = self.transition
            elapsed += dt
            s = smoothstep(elapsed / seconds)
            end_distance = 3.0 * self.body.radius
            self.distance = math.exp(math.log(start_distance) + (math.log(end_distance) - math.log(start_distance)) * s)
            self.transition = None if elapsed >= seconds else (start, start_distance, seconds, elapsed)
        self.clamp()
        self.place()
