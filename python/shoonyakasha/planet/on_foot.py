"""
A first-person player on foot on a PlanetWorld: the physics bubble
(shoonyakasha.planet.bubble) around it with its terrain meshes, the trunks
of the trees near the camera, the props, the character
(shoonyakasha.planet.character) and its gravity gun
(shoonyakasha.planet.gravity_gun), stepped together in fixed steps.

`OnFoot(engine, world, crate_mesh, make_rocks=None)` owns the physics
(sk.PlanetPhysics), the collider probes, the nearby-trees readback, the
trunks and the props; `make_rocks(props)` gives an object for the
scatter's rocks in the bubble (update, clear, grabbable, promote, breaks,
split, released), or None for none.

There is at most one bubble, on the body under the point it was made
around (`ensure_bubble`). `step` each frame keeps it following its focus,
the player's feet, or without a player the viewpoint: without a player it
is dropped when the viewpoint is over another body or more than
Bubble.RADIUS above the ground; otherwise it is rebased to the focus once
far enough from its origin, its fine and coarse terrain meshes are kept
around the focus (ahead along its velocity by the probes' latency; a move
of JUMP_DISTANCE or more in a frame counts as no velocity), its trunks
(on the planet) and rocks are updated from the nearby trees that came back
(all removed when the terrain or the vegetation changed), its props are
moved between their states, and its physics is stepped by the frame's
time in fixed steps of STEP, at most MAX_STEPS a frame: each step the
player (with `controls` while `active`, standing still otherwise; held
where the fine mesh does not reach within HOLD_MARGIN), while `active`
the gun (which drops what it holds otherwise), `before_step(bubble,
fraction)`, the props' drag and the bubble. Strikes on watched rocks then
break an ore boulder struck fast enough (rocks.breaks) or knock a fixed
rock loose struck with `strike_momentum` or more.

`read_controls(input)` fills `controls` and `gun_controls` from the keys,
the mouse and the gamepad and turns the view (see its docstring).
"""

import math

import shoonyakasha as sk
from shoonyakasha import keys
from shoonyakasha.planet.bubble import Bubble
from shoonyakasha.planet.character import Controls, Player
from shoonyakasha.planet.collider_probes import ColliderProbes, TerrainCollider
from shoonyakasha.planet.gravity_gun import Gun, GunControls
from shoonyakasha.planet.nearby_trees import NearbyTrees
from shoonyakasha.planet.props import Props
from shoonyakasha.planet.trunks import Trunks
from shoonyakasha.planet.vec import length, scale, sub

STEP = 1.0 / 120.0               # s per physics step
MAX_STEPS = 8                    # physics steps a frame, at most
COLLIDER_RADIUS = 40.0           # m from the camera the trees, cacti and rocks get colliders
COARSE_SPACING = 4.0             # m between the coarse terrain mesh's points (65 x 65, 256 m across)
JUMP_DISTANCE = 50.0             # m the focus may move in a frame and still count as moving, not jumping
HOLD_MARGIN = 1.0                # m inside the terrain mesh's edges the player must be to move
FOOT_MAX_LEVEL = 14              # the deepest level the body under the player splits to (0.6 m quads)
STRIKE_REPORTED = 100.0          # kg m/s a strike needs to be reported
STRIKE_MOMENTUM = 300.0          # kg m/s a body must strike a fixed rock with to knock it loose
GAMEPAD_DEAD_ZONE = 0.12         # of a stick's travel, read as 0
PAD_LOOK_PIXELS = 12.0           # pixels of mouse movement a frame at full right stick


def stick(value):
    """A gamepad axis with GAMEPAD_DEAD_ZONE around 0 cut out and the rest
    stretched back to -1 .. 1."""
    size = abs(value)
    if size <= GAMEPAD_DEAD_ZONE:
        return 0.0
    return math.copysign((min(size, 1.0) - GAMEPAD_DEAD_ZONE) / (1.0 - GAMEPAD_DEAD_ZONE), value)


def trigger(input, axis):
    """A gamepad trigger from 0 (released) to 1."""
    return 0.5 * (input.get_gamepad_axis(axis) + 1.0)


class OnFoot:
    def __init__(self, engine, world, crate_mesh, make_rocks=None, strike_momentum=STRIKE_MOMENTUM,
                 strike_reported=STRIKE_REPORTED):
        """`crate_mesh`: (positions, normals) of a unit cube centred on the
        origin, for the props' crates."""
        self.engine = engine
        self.world = world
        self.physics = sk.PlanetPhysics()
        self.physics.set_strike_momentum(strike_reported)
        self.strike_momentum = strike_momentum
        self.collider_probes = ColliderProbes(engine)
        self.nearby_trees = NearbyTrees(engine)
        self.trunks = Trunks()
        self.rocks = None
        self.props = Props(engine, crate_mesh, world,
                           on_remove=lambda prop: self.rocks.released(prop) if self.rocks is not None else None)
        if make_rocks is not None:
            self.rocks = make_rocks(self.props)
        self.bubble = None
        self.fine = None             # the bubble's terrain mesh (TerrainCollider)
        self.coarse = None           # its coarse terrain mesh, for far bodies
        self.player = None           # the character (character.Player), while there is one
        self.gun = None              # its gravity gun (gravity_gun.Gun), while there is a player
        self.gun_params = None       # the GunParams each gun spawn makes takes (its defaults when None)
        self.controls = Controls()
        self.gun_controls = GunControls()
        self.view = None             # (right, up, forward) of the player's view, from `place_eye`
        self.look_delta = (0.0, 0.0)  # pixels `read_controls` turned the view by this frame
        self.head_bob = True
        self.mouse_sensitivity = 1.0  # the mouse's turning of the view, times
        self.invert_y = False        # moving the mouse or the right stick up looks down
        self.on_drop = None          # called by drop_bubble after the player is removed
        self._accumulator = 0.0      # seconds of physics not yet stepped
        self._focus = None           # the focus relative to the bubble's body last frame
        self._plants = None          # (terrain version, vegetation, density, rocks) the trunks were made under
        self._punt_held = False
        self._pad_jump = False

    # ── The bubble ──

    def gm(self, body):
        physics = self.world.physics
        return physics.gm if body is self.world.planet else physics.moon.gm

    def ensure_bubble(self, point):
        """The bubble on the body under `point` (planet space), made around it
        if there is none there; a bubble on another body is dropped first."""
        world = self.world
        body = world.body_under(point)
        bubble = self.bubble
        if bubble is not None and bubble.body is body:
            return bubble
        self.drop_bubble()
        bubble = Bubble(self.physics, body, self.gm(body), body.relative(point))
        self.bubble = bubble
        self.fine = TerrainCollider(bubble, self.collider_probes)
        self.coarse = TerrainCollider(bubble, self.collider_probes, spacing=COARSE_SPACING, coarse=True)
        self._accumulator = 0.0
        self._focus = None
        return bubble

    def drop_bubble(self):
        """The bubble, its trunks, fixed rocks, the player and its terrain
        meshes gone; its props out of the physics, flying or resting where
        they are; then `on_drop()`."""
        bubble = self.bubble
        if bubble is None:
            return
        self.props.leave_all(bubble)
        self.trunks.clear(bubble)
        if self.rocks is not None:
            self.rocks.clear(bubble)
        self.remove_player()
        if self.on_drop is not None:
            self.on_drop()
        self.fine.drop()
        self.coarse.drop()
        self.bubble = self.fine = self.coarse = None

    # ── The player ──

    def grabbable(self):
        rocks = self.rocks.grabbable() if self.rocks is not None else []
        return self.props.grabbable(self.physics) + rocks

    def spawn(self, feet, heading):
        """A player with its feet at `feet` (planet space) in the bubble,
        facing `heading`, with its gravity gun (of `gun_params`); any player
        before removed."""
        self.remove_player()
        bubble = self.bubble
        self.player = Player(bubble, bubble.to_bubble(feet), heading)
        promote = None
        if self.rocks is not None:
            promote = lambda body: self.rocks.promote(self.bubble, body)  # noqa: E731
        self.gun = Gun(self.player, self.grabbable, promote=promote, params=self.gun_params)
        return self.player

    def remove_player(self):
        if self.player is not None:
            self.gun.drop()
            self.gun = None
            self.player.remove()
            self.player = None
            self.view = None

    def place_eye(self):
        """The player's eye in planet space, its view's basis kept in `view`;
        None without a player."""
        if self.player is None:
            return None
        self.view = self.player.basis()
        return self.bubble.to_world(self.player.eye())

    def max_level(self, body, active=True):
        """FOOT_MAX_LEVEL for the body under an `active` player, else None."""
        if active and self.player is not None and self.bubble.body is body:
            return FOOT_MAX_LEVEL
        return None

    # ── Input ──

    def read_controls(self, input):
        """The player's and the gravity gun's controls from the keys, the
        mouse and the gamepad (left stick to walk, right stick to look, A
        to jump and jet, the left trigger to grab, the right to punt). The
        mouse turns the view, by `mouse_sensitivity` times its movement,
        while captured, or with the right button held; with `invert_y`,
        the mouse's and the right stick's movement up and down turns it the
        other way;
        while it is captured, the right button grabs and the left punts,
        and with R held the mouse turns the held body instead of the view.
        Keys: W S A D to walk, left Shift to run, left Ctrl to crouch,
        Space to jet."""
        c, gc = self.controls, self.gun_controls
        down = input.is_key_down
        forward = (1.0 if down(keys.W) else 0.0) - (1.0 if down(keys.S) else 0.0)
        strafe = (1.0 if down(keys.D) else 0.0) - (1.0 if down(keys.A) else 0.0)
        c.run = down(keys.LEFT_SHIFT)
        c.crouch = down(keys.LEFT_CONTROL)
        c.jet = down(keys.SPACE)
        player, gun = self.player, self.gun
        captured = input.is_mouse_captured()
        mouse = input.is_mouse_button_down
        grab = captured and mouse(keys.MOUSE_RIGHT)
        punt = captured and mouse(keys.MOUSE_LEFT)
        look = (0.0, 0.0)
        if input.is_gamepad_connected():
            forward -= stick(input.get_gamepad_axis(keys.GAMEPAD_LEFT_Y))
            strafe += stick(input.get_gamepad_axis(keys.GAMEPAD_LEFT_X))
            a = input.is_gamepad_button_down(keys.GAMEPAD_A)
            if a and not self._pad_jump:
                c.jump = True
            self._pad_jump = a
            c.jet = c.jet or a
            grab = grab or trigger(input, keys.GAMEPAD_LEFT_TRIGGER) > 0.5
            punt = punt or trigger(input, keys.GAMEPAD_RIGHT_TRIGGER) > 0.5
            look = (stick(input.get_gamepad_axis(keys.GAMEPAD_RIGHT_X)) * PAD_LOOK_PIXELS,
                    stick(input.get_gamepad_axis(keys.GAMEPAD_RIGHT_Y)) * PAD_LOOK_PIXELS)
        c.forward, c.strafe = max(-1.0, min(1.0, forward)), max(-1.0, min(1.0, strafe))
        gc.grab = grab
        gc.punt_held = punt
        if punt and not self._punt_held:
            gc.punt = True
        self._punt_held = punt
        if player is not None and (captured or input.is_mouse_button_down(keys.MOUSE_RIGHT)):
            dx, dy = input.get_mouse_delta()
            if captured and down(keys.R) and gun is not None and gun.held is not None:
                gc.rotate = (gc.rotate[0] + dx, gc.rotate[1] + dy)
            else:
                s = self.mouse_sensitivity
                look = (look[0] + dx * s, look[1] + dy * s)
        if self.invert_y:
            look = (look[0], -look[1])
        if player is not None:
            player.look(*look)
        self.look_delta = look

    # ── Each frame ──

    def step(self, dt, viewpoint, camera, latency, active=True, before_step=None):
        """One frame of `dt` seconds (see the module's docstring): `viewpoint`
        (planet space) is the focus without a player, `camera` where the
        trunks and rocks are kept around, `latency` the probes' in seconds."""
        bubble = self.bubble
        if bubble is None:
            return
        world, physics = self.world, self.physics
        player = self.player
        if player is not None:
            here = bubble.body_relative(player.feet())
        else:
            if world.body_under(viewpoint) is not bubble.body or world.height_above_ground(viewpoint) > Bubble.RADIUS:
                self.drop_bubble()
                return
            here = bubble.body.relative(viewpoint)
        last = self._focus
        velocity = (0.0, 0.0, 0.0)
        if last is not None and dt > 0.0 and length(sub(here, last)) < JUMP_DISTANCE:
            velocity = scale(sub(here, last), 1.0 / dt)
        self._focus = here
        bubble.maybe_rebase(sub(here, bubble.origin))
        self.fine.update(sub(here, bubble.origin), velocity, latency, world.terrain_version)
        self.coarse.update(sub(here, bubble.origin), velocity, latency, world.terrain_version)
        plants = (world.terrain_version, world.vegetation, world.vegetation_density, world.rocks)
        if plants != self._plants:
            self.trunks.clear(bubble)
            if self.rocks is not None:
                self.rocks.clear(bubble)
            self._plants = plants
        trees = self.nearby_trees.read()
        if trees is not None:
            if bubble.body is world.planet:
                self.trunks.update(bubble, trees, camera, COLLIDER_RADIUS)
            if self.rocks is not None:
                self.rocks.update(bubble, trees, camera, COLLIDER_RADIUS)
        self.props.update(bubble, self.fine, self.coarse)
        self._accumulator += dt
        count = min(int(self._accumulator / STEP), MAX_STEPS)
        controls = self.controls if active else Controls()
        gun = self.gun
        if gun is not None and not active:
            gun.drop()
        for k in range(count):
            if player is not None:
                player.bob = self.head_bob
                player.step(controls, STEP, self.fine.covers(player.feet(), HOLD_MARGIN))
                if active:
                    gun.step(self.gun_controls, STEP)
            if before_step is not None:
                before_step(bubble, (k + 1) / count)
            self.props.drag(bubble)
            bubble.step(STEP)
        for body, speed, momentum in physics.take_strikes():
            if self.rocks is None:
                continue
            if self.rocks.breaks(body, speed):
                self.rocks.split(bubble, body)
            elif momentum >= self.strike_momentum:
                self.rocks.promote(bubble, body)
        self._accumulator -= count * STEP
        if count == MAX_STEPS:
            self._accumulator = 0.0

    def request_nearby(self, camera, bodies):
        """This frame's request for the trees near `camera` (planet space):
        within COLLIDER_RADIUS while there is a bubble, none otherwise;
        `bodies` the world's bodies."""
        self.nearby_trees.request(camera, COLLIDER_RADIUS if self.bubble is not None else 0.0,
                                  [body.centre() for body in sorted(bodies, key=lambda b: b.index)])

    def end_frame(self):
        """The collider probes told the frame is over (call after the world's end_frame)."""
        self.collider_probes.end_frame()

    def prop_state(self, prop):
        """(the world body it is reckoned around, position from its centre, velocity) of a prop."""
        bubble = self.bubble
        if prop.body is not None and bubble is not None:
            p = self.physics.get_transform(prop.body)[0]
            return bubble.body, bubble.body_relative(p), self.physics.get_velocity(prop.body)[0]
        return prop.on, prop.position, prop.velocity

    def forget_body(self, body):
        """The props on `body` removed, and with the bubble there what the
        gun holds dropped: for a body built again."""
        bubble = self.bubble
        if self.gun is not None and bubble is not None and bubble.body is body:
            self.gun.drop()
        for prop in list(self.props.all):
            if self.prop_state(prop)[0] is body:
                self.props.remove(bubble, prop)
