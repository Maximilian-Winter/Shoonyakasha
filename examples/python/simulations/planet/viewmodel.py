"""
The gravity gun's viewmodel: the gun's model (models/scifi_weapon, "Scifi
Weapon NO.2" by No.cccccc, CC-BY-4.0) in the camera's space, drawn at the
lower right of the view, with a glowing tip at its muzzle, and its effects
drawn as glowing quads. The model is scaled to GUN_LENGTH metres; MUZZLE
is its muzzle in the model's units (its barrel along +Z, up +Y), and the
gun's space puts the muzzle at TIP.

The gun sits at BASE (metres, camera space: forward -Z, up +Y, right +X),
moved by:
- **sway:** the view's turning, low-passed over SWAY_SMOOTHING seconds,
  SWAY_PER_PIXEL metres per pixel of mouse movement a frame, at most
  SWAY_MAX each way, so the gun lags behind the turn;
- **bob:** the player's head bob (shoonyakasha.planet.character), BOB_SHARE of its height,
  BOB_PHASE radians behind it, and swaying sideways once a stride;
- **recoil:** after a punt, back by RECOIL_BACK and turned up by
  RECOIL_PITCH, both easing back to rest over RECOIL_TIME.

The tip is drawn unlit (the Screens pass, render layer 2, which shows a
white emissive texture, white_texture, times the material's
emissiveFactor) with GLOW's strength for the gun's status
(gun.Gun.status), pulsing at PULSE_HZ while holding, and while a punt
charges brighter (up to 1 + CHARGE_GLOW times) and whiter with the
charge.

The effects are `Glows`: camera-facing quads added to the image after the
atmosphere (the Glow pass, render layer 4, glow.frag), each a disc or a
streak:
- **flare** FLARE_BACK from the tip's centre towards the gun (in front of
  the tip seen from the eye), FLARE_SIZE across and FLARE_LIGHT strong for
  the gun's status, red and flickering when the target is too heavy,
  growing by up to CHARGE_FLARE times with a charge;
- while holding, a **beam** from the tip to the held body's centre: a thin
  bright core (CORE_WIDTH) inside a wide dim sheath (SHEATH_WIDTH); MOTES
  **motes** flowing along it, MOTE_RATE lengths a second (up to
  1 + CHARGE_RATE times faster with a charge), spiralling around it at up
  to MOTE_SPIRAL, fading in and out at its ends and kept MOTE_ANGLE
  radians across at any distance; and a **halo** behind the held body,
  HALO_SCALE times its diameter across, so that its rim glows;
- after a punt, a **flash** at the tip over FLASH_TIME, and SPARKS
  **sparks** thrown forward from the tip at SPARK_SPEED with SPARK_SPREAD
  sideways, each a streak along its velocity that slows by SPARK_DRAG and
  fades over its life of SPARK_LIFE.
The quads' light is LDR (see glow.frag): GLOW_COLOUR and CORE_COLOUR are
the colours at full strength.
"""

import math
import os
import random
import struct
import tempfile
import zlib

from shoonyakasha.planet.character import BOB_AMPLITUDE, BOB_STRIDE
from shoonyakasha.planet.vec import add, cross, euler_from_basis, length, normalize, scale, sub

GUN_MODEL = os.path.join(os.path.dirname(os.path.abspath(__file__)), "models", "scifi_weapon", "scene.gltf")
MODEL_LENGTH = 77.39                 # the model's units from its stock to its muzzle
MUZZLE = (-11.2, 150.6, 90.97)       # the muzzle's centre in the model's units
GUN_LENGTH = 0.5                     # m
GUN_SCALE = GUN_LENGTH / MODEL_LENGTH
BASE = (0.22, -0.2, -0.45)
SWAY_SMOOTHING = 0.08                # s
SWAY_PER_PIXEL = 0.0006              # m
SWAY_MAX = 0.03                      # m
BOB_SHARE, BOB_PHASE = 0.6, 0.6
RECOIL_BACK = 0.06                   # m
RECOIL_PITCH = math.radians(3.0)
RECOIL_TIME = 0.25                   # s
TIP = (0.0, 0.0, -0.25)              # the tip's centre in the gun's space (the muzzle)
TIP_SIZE = 0.018                     # m
TIP_COLOUR = (1.0, 0.55, 0.18)       # HDR, times GLOW
HEAVY_COLOUR = (1.0, 0.16, 0.1)
GLOW = {"idle": 1.0, "can_grab": 1.8, "too_heavy": 1.5, "holding": 2.8}
PULSE_HZ = 2.0
CHARGE_GLOW = 3.0                    # the tip's glow at full charge, times more
WHITE_PNG = "shoonyakasha_planet_white.png"

GLOW_LAYER = 4
GLOW_COLOUR = (1.0, 0.52, 0.16)      # LDR, added to the image
CORE_COLOUR = (1.0, 0.86, 0.62)
HEAVY_GLOW = (1.0, 0.18, 0.1)
FLARE_SIZE = {"idle": 0.05, "can_grab": 0.09, "too_heavy": 0.08, "holding": 0.11}   # m across
FLARE_LIGHT = {"idle": 0.3, "can_grab": 0.8, "too_heavy": 0.8, "holding": 1.0}
FLARE_BACK = 0.02                    # m from the tip's centre back towards the gun
CHARGE_FLARE = 1.5
CORE_WIDTH = 0.012                   # m
SHEATH_WIDTH = 0.06                  # m
CORE_LIGHT, SHEATH_LIGHT = 0.9, 0.6
MOTES = 18
MOTE_RATE = 0.8                      # beam lengths a second
CHARGE_RATE = 2.5
MOTE_SPIRAL = 0.05                   # m from the beam's line, at most
MOTE_ANGLE = 0.022                   # radians across
MOTE_LIGHT = 1.0
HALO_SCALE = 1.6                     # the halo across, times the held body's diameter
HALO_LIGHT = 0.9
FLASH_TIME = 0.16                    # s
FLASH_SIZE = 0.3                     # m across at the punt
SPARKS = 20
SPARK_SPEED = (4.0, 10.0)            # m/s, forward
SPARK_SPREAD = 2.5                   # m/s, sideways at most
SPARK_DRAG = 4.0                     # 1/s
SPARK_LIFE = (0.2, 0.4)              # s
SPARK_STREAK = 0.05                  # s of its velocity a spark's streak is long
SPARK_WIDTH = 0.012                  # m
DISCS = MOTES + 2                    # the motes, the flare and the halo
STREAKS = 2 + SPARKS                 # the core, the sheath and the sparks


def white_texture():
    """The path of WHITE_PNG, a one-pixel white PNG in the temporary
    directory, written there if it is not."""
    path = os.path.join(tempfile.gettempdir(), WHITE_PNG)
    if not os.path.exists(path):
        def chunk(kind, data):
            return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))
        with open(path, "wb") as f:
            f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", 1, 1, 8, 2, 0, 0, 0))
                    + chunk(b"IDAT", zlib.compress(b"\x00\xff\xff\xff")) + chunk(b"IEND", b""))
    return path


def quad(along):
    """(positions, uvs, indices) of a unit quad in the XY plane: from 0 to 1
    along X when `along` (a streak), else centred on the origin (a disc);
    its uvs run 0 to 1 across it."""
    x0 = 0.0 if along else -0.5
    positions = [(x0, -0.5, 0.0), (x0 + 1.0, -0.5, 0.0), (x0 + 1.0, 0.5, 0.0), (x0, 0.5, 0.0)]
    uvs = [(0.0, 0.0), (1.0, 0.0), (1.0, 1.0), (0.0, 1.0)]
    return positions, uvs, [0, 1, 2, 0, 2, 3]


def perpendicular(axis):
    """A unit vector perpendicular to the unit `axis`."""
    helper = (0.0, 1.0, 0.0) if abs(axis[1]) < 0.9 else (1.0, 0.0, 0.0)
    return normalize(cross(axis, helper))


class Glows:
    """Pools of glowing discs and streaks (the Glow pass), placed each
    frame between `begin` and `end`; positions relative to the camera.
    Those not placed since `begin` are hidden by `end`."""

    def __init__(self, engine, discs, streaks):
        self.engine = engine
        self.discs = [self._quad(False, "GlowDisc") for _ in range(discs)]
        self.streaks = [self._quad(True, "GlowStreak") for _ in range(streaks)]
        self.shown = {}               # entity: whether it is visible
        self.used = [0, 0]            # discs, streaks placed since `begin`
        self.view = None

    def _quad(self, along, name):
        positions, uvs, indices = quad(along)
        entity = self.engine.create_mesh(positions, indices=indices, normals=[(0.0, 0.0, 1.0)] * 4, uvs=uvs,
                                         colors=[(1.0, 1.0, 1.0)] * 4, name=name)
        self.engine.scene.set_render_layer_mask(entity, GLOW_LAYER)
        self.engine.scene.set_visible(entity, False)
        return entity

    def _show(self, entity, on):
        if self.shown.get(entity) != on:
            self.engine.scene.set_visible(entity, on)
            self.shown[entity] = on

    def begin(self, view):
        """`view`: (right, up, forward) of the camera."""
        self.view = view
        self.used = [0, 0]

    def disc(self, centre, size, colour):
        """A disc `size` metres across at `centre`, facing the camera, of `colour` (rgb)."""
        if self.used[0] >= len(self.discs) or size <= 0.0:
            return
        entity = self.discs[self.used[0]]
        self.used[0] += 1
        scene = self.engine.scene
        right, up, forward = self.view
        scene.set_position(entity, centre)
        scene.set_rotation(entity, euler_from_basis(right, up, forward))
        scene.set_scale(entity, (size, size, 1.0))
        scene.set_material_vec4(entity, "emissiveFactor", tuple(colour) + (0.0,))
        self._show(entity, True)

    def streak(self, start, end, width, colour):
        """A streak `width` metres across from `start` to `end`, turned about
        its line to face the camera, of `colour` (rgb)."""
        along = sub(end, start)
        d = length(along)
        if self.used[1] >= len(self.streaks) or d < 1e-4:
            return
        entity = self.streaks[self.used[1]]
        self.used[1] += 1
        axis = scale(along, 1.0 / d)
        to_camera = scale(add(start, end), -0.5)
        side = cross(to_camera, axis)
        side = normalize(side) if length(side) > 1e-9 else perpendicular(axis)
        scene = self.engine.scene
        scene.set_position(entity, start)
        scene.set_rotation(entity, euler_from_basis(axis, side, cross(side, axis)))
        scene.set_scale(entity, (d, width, 1.0))
        scene.set_material_vec4(entity, "emissiveFactor", tuple(colour) + (1.0,))
        self._show(entity, True)

    def end(self):
        for entity in self.discs[self.used[0]:] + self.streaks[self.used[1]:]:
            self._show(entity, False)


def shade(colour, light):
    return tuple(c * light for c in colour)


def whiten(colour, t):
    return tuple(c + (1.0 - c) * t for c in colour)


class Viewmodel:
    def __init__(self, engine, cube):
        """`cube`: (positions, normals) of a unit cube centred on the origin."""
        self.engine = engine
        scene = engine.scene
        self.gun = scene.create_entity("GravityGun")
        scene.set_scale(self.gun, (GUN_SCALE,) * 3)
        self.meshes = []
        result = engine.load_gltf_scene(GUN_MODEL, flatten_hierarchy=True)
        if result.success:
            anchor = (MUZZLE[0] + TIP[0] / GUN_SCALE, MUZZLE[1] - TIP[1] / GUN_SCALE, MUZZLE[2] + TIP[2] / GUN_SCALE)
            for entity in result.entities:
                scene.set_parent(entity, self.gun)
                scene.set_position(entity, scale(anchor, -1.0))
                scene.set_material_float(entity, "gridStrength", 0.0)
                scene.set_render_layer_mask(entity, 1)
                self.meshes.append(entity)
        else:
            print("[viewmodel] the gun's model did not load:", result.error, flush=True)
        self.tip = self.glowing(*cube, "GravityGunTip")
        scene.set_scale(self.tip, (TIP_SIZE, TIP_SIZE, TIP_SIZE))
        self.glows = Glows(engine, DISCS, STREAKS)
        self.random = random.Random(7)
        self.sparks = []              # [position (planet space), velocity, age, life]
        self.since_punt = math.inf
        self.sway = (0.0, 0.0)
        self.time = 0.0
        self.visible = None
        self.show(False)

    def glowing(self, positions, normals, name):
        """A mesh drawn unlit: in the Screens pass, its emissiveMap white."""
        entity = self.engine.create_mesh(positions, normals=normals, name=name)
        self.engine.scene.set_render_layer_mask(entity, 2)
        self.engine.scene.set_material_texture(entity, "emissiveMap", white_texture())
        return entity

    def show(self, visible):
        if visible == self.visible:
            return
        self.visible = visible
        for entity in self.meshes + [self.tip]:
            self.engine.scene.set_visible(entity, visible)
        if not visible:
            self.sparks = []
            self.glows.begin(None)
            self.glows.end()

    def update(self, dt, basis, look, player, gun, held=None, camera=(0.0, 0.0, 0.0)):
        """The gun placed for this frame: `basis` the view's (right, up,
        forward), `look` the mouse movement (pixels) turning it this frame,
        `held` the held body's centre relative to the camera, or None,
        `camera` the camera's position in planet space (for the sparks)."""
        scene = self.engine.scene
        self.time += dt
        right, up, forward = basis
        blend = 1.0 - math.exp(-dt / SWAY_SMOOTHING) if dt > 0.0 else 0.0
        wanted = (max(-SWAY_MAX, min(SWAY_MAX, -look[0] * SWAY_PER_PIXEL)),
                  max(-SWAY_MAX, min(SWAY_MAX, look[1] * SWAY_PER_PIXEL)))
        self.sway = tuple(s + (w - s) * blend for s, w in zip(self.sway, wanted))
        x, y, z = BASE
        x, y = x + self.sway[0], y + self.sway[1]
        if player.bob:
            phase = 2.0 * math.pi * player.bob_distance / BOB_STRIDE
            y += BOB_SHARE * BOB_AMPLITUDE * player.bob_weight * math.sin(phase - BOB_PHASE)
            x += 0.5 * BOB_AMPLITUDE * player.bob_weight * math.cos(0.5 * phase)
        kick = max(0.0, 1.0 - gun.since_punt / RECOIL_TIME) ** 2
        z += RECOIL_BACK * kick
        pitch = RECOIL_PITCH * kick
        g_up = add(scale(up, math.cos(pitch)), scale(forward, -math.sin(pitch)))
        g_forward = add(scale(forward, math.cos(pitch)), scale(up, math.sin(pitch)))

        def to_view(origin, p):
            return add(origin, add(add(scale(right, p[0]), scale(g_up, p[1])), scale(g_forward, -p[2])))

        origin = add(add(scale(right, x), scale(up, y)), scale(forward, -z))
        scene.set_position(self.gun, origin)
        scene.set_rotation(self.gun, euler_from_basis(scale(right, -1.0), g_up, scale(g_forward, -1.0)))
        tip = to_view(origin, TIP)
        scene.set_position(self.tip, tip)
        scene.set_rotation(self.tip, euler_from_basis(right, g_up, g_forward))

        status = gun.status
        strength = GLOW[status]
        if status == "holding":
            strength *= 1.0 + 0.25 * math.sin(2.0 * math.pi * PULSE_HZ * self.time)
        colour = HEAVY_COLOUR if status == "too_heavy" else TIP_COLOUR
        charge = gun.charge
        if charge > 0.0:
            strength *= 1.0 + CHARGE_GLOW * charge
            colour = whiten(colour, charge)
        scene.set_material_vec4(self.tip, "emissiveFactor", shade(colour, strength) + (0.0,))

        punted = gun.since_punt < self.since_punt
        self.since_punt = gun.since_punt
        if punted:
            self.throw_sparks(add(camera, tip), g_forward, right, g_up)
        self.glows.begin(basis)
        self.draw_flare(sub(tip, scale(g_forward, FLARE_BACK)), status, charge)
        if held is not None:
            self.draw_beam(tip, held, gun.held_radius, charge)
        self.draw_sparks(dt, camera)
        self.glows.end()

    def draw_flare(self, tip, status, charge):
        """The tip's flare, and the flash after a punt."""
        size, light = FLARE_SIZE[status], FLARE_LIGHT[status]
        colour = GLOW_COLOUR
        if status == "too_heavy":
            colour = HEAVY_GLOW
            light *= 0.7 + 0.3 * math.sin(31.0 * self.time) * math.sin(17.0 * self.time)
        elif status == "can_grab":
            light *= 0.85 + 0.15 * math.sin(2.0 * math.pi * 1.2 * self.time)
        if charge > 0.0:
            size *= 1.0 + CHARGE_FLARE * charge
            light = min(1.0, light * (1.0 + charge))
            colour = whiten(colour, 0.6 * charge)
        flash = max(0.0, 1.0 - self.since_punt / FLASH_TIME)
        if flash > 0.0:
            size = max(size, FLASH_SIZE * flash)
            light = max(light, flash)
            colour = whiten(colour, 0.5 * flash)
        self.glows.disc(tip, size, shade(colour, light))

    def draw_beam(self, start, end, radius, charge):
        """The beam from `start` (the tip) to `end` (the held body's centre),
        its motes and the held body's halo."""
        along = sub(end, start)
        d = length(along)
        if d < 1e-3:
            return
        pulse = 0.85 + 0.15 * math.sin(2.0 * math.pi * PULSE_HZ * self.time)
        core = whiten(CORE_COLOUR, 0.7 * charge)
        self.glows.streak(start, end, SHEATH_WIDTH, shade(GLOW_COLOUR, SHEATH_LIGHT * pulse * (1.0 + charge)))
        self.glows.streak(start, end, CORE_WIDTH * (1.0 + charge), shade(core, min(1.0, CORE_LIGHT * pulse + charge)))
        axis = scale(along, 1.0 / d)
        side = perpendicular(axis)
        lift = cross(side, axis)
        rate = MOTE_RATE * (1.0 + CHARGE_RATE * charge)
        for k in range(MOTES):
            t = (k / MOTES + self.time * rate) % 1.0
            envelope = math.sin(math.pi * t)
            a = 2.0 * math.pi * (0.618 * k) + 5.0 * self.time + 6.0 * t
            r = MOTE_SPIRAL * envelope * (0.55 + 0.45 * math.sin(1.7 * k))
            p = add(add(start, scale(along, t)), add(scale(side, r * math.cos(a)), scale(lift, r * math.sin(a))))
            self.glows.disc(p, MOTE_ANGLE * length(p) * (0.6 + 0.6 * envelope),
                            shade(whiten(GLOW_COLOUR, 0.4 + 0.4 * charge), MOTE_LIGHT * envelope))
        self.glows.disc(end, HALO_SCALE * 2.0 * max(radius, 0.1),
                        shade(GLOW_COLOUR, HALO_LIGHT * pulse * (1.0 + charge)))

    def throw_sparks(self, tip, forward, right, up):
        """SPARKS sparks from `tip` (planet space), thrown along `forward`."""
        rnd = self.random
        for _ in range(SPARKS):
            a = rnd.uniform(0.0, 2.0 * math.pi)
            s = SPARK_SPREAD * math.sqrt(rnd.random())
            v = add(scale(forward, rnd.uniform(*SPARK_SPEED)),
                    add(scale(right, s * math.cos(a)), scale(up, s * math.sin(a))))
            self.sparks.append([tip, v, 0.0, rnd.uniform(*SPARK_LIFE)])
        del self.sparks[:-SPARKS]

    def draw_sparks(self, dt, camera):
        alive = []
        drag = math.exp(-SPARK_DRAG * dt)
        for spark in self.sparks:
            position, velocity, age, life = spark
            age += dt
            if age >= life:
                continue
            position = add(position, scale(velocity, dt))
            velocity = scale(velocity, drag)
            alive.append([position, velocity, age, life])
            fade = 1.0 - age / life
            head = sub(position, camera)
            self.glows.streak(sub(head, scale(velocity, SPARK_STREAK)), head, SPARK_WIDTH,
                              shade(whiten(GLOW_COLOUR, 0.6), fade))
        self.sparks = alive
