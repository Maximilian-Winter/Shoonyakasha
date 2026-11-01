"""
The gravity gun's viewmodel: a gun built from boxes (cockpit.py's
oriented_box and beam) in the camera's space, drawn at the lower right of
the view, its tip glowing, and while it holds something a beam from the
tip to the held body.

The gun sits at BASE (metres, camera space: forward -Z, up +Y, right +X),
moved by:
- **sway:** the view's turning, low-passed over SWAY_SMOOTHING seconds,
  SWAY_PER_PIXEL metres per pixel of mouse movement a frame, at most
  SWAY_MAX each way, so the gun lags behind the turn;
- **bob:** the player's head bob (player.py), BOB_SHARE of its height,
  BOB_PHASE radians behind it, and swaying sideways once a stride;
- **recoil:** after a punt, back by RECOIL_BACK and turned up by
  RECOIL_PITCH, both easing back to rest over RECOIL_TIME.

The tip and the beam are drawn unlit (the Screens pass, render layer 2,
which shows a white emissive texture, white_texture, times the material's
emissiveFactor) with GLOW's strength for the gun's status (gun.Gun.status), pulsing at
PULSE_HZ while holding, and while a punt charges brighter (up to
1 + CHARGE_GLOW times) and whiter with the charge. The beam runs through BEAM_SEGMENTS straight
pieces, bowed sideways by up to BEAM_WOBBLE in the middle and wobbling
over time.
"""

import math
import os
import struct
import tempfile
import zlib

from shoonyakasha.planet.vec import add, cross, euler_from_basis, length, normalize, scale, sub

from cockpit import beam, oriented_box
from player import BOB_AMPLITUDE, BOB_STRIDE

BASE = (0.22, -0.2, -0.45)
SWAY_SMOOTHING = 0.08                # s
SWAY_PER_PIXEL = 0.0006              # m
SWAY_MAX = 0.03                      # m
BOB_SHARE, BOB_PHASE = 0.6, 0.6
RECOIL_BACK = 0.06                   # m
RECOIL_PITCH = math.radians(3.0)
RECOIL_TIME = 0.25                   # s
TIP = (0.0, 0.004, -0.215)           # the tip's centre in the gun's space
TIP_SIZE = 0.022                     # m
GLOW_COLOUR = (1.0, 0.55, 0.18)
HEAVY_COLOUR = (1.0, 0.16, 0.1)
GLOW = {"idle": 0.4, "can_grab": 1.5, "too_heavy": 1.5, "holding": 2.5}
PULSE_HZ = 2.0
CHARGE_GLOW = 3.0                    # the tip's glow at full charge, times more
BEAM_SEGMENTS = 8
BEAM_WIDTH = 0.008                   # m
BEAM_WOBBLE = 0.04                   # m
BEAM_GLOW = 2.0
WHITE_PNG = "shoonyakasha_planet_white.png"

BODY = (0.11, 0.115, 0.125)
TRIM = (0.2, 0.21, 0.23)
CLAW = (0.55, 0.33, 0.1)


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


def gun_mesh():
    """Triangles (positions, normals, colours) of the gun, its barrel along -Z."""
    out = ([], [], [])
    x, y = (1.0, 0.0, 0.0), (0.0, 1.0, 0.0)
    oriented_box((0.0, 0.0, 0.06), x, y, (0.075, 0.085, 0.24), BODY, out)          # body
    oriented_box((0.0, 0.05, 0.08), x, y, (0.05, 0.02, 0.16), TRIM, out)           # top rail
    grip_up = normalize((0.0, 1.0, 0.35))
    oriented_box((0.0, -0.085, 0.13), x, grip_up, (0.04, 0.11, 0.05), BODY, out)   # grip
    oriented_box((0.0, 0.004, -0.11), x, y, (0.052, 0.052, 0.1), TRIM, out)        # barrel
    for k in range(3):
        a = math.radians(90.0 + 120.0 * k)
        c, s = math.cos(a), math.sin(a)
        start = (0.026 * c, 0.004 + 0.026 * s, -0.15)
        bend = (0.045 * c, 0.004 + 0.045 * s, -0.2)
        end = (0.03 * c, 0.004 + 0.03 * s, -0.25)
        beam(start, bend, 0.012, CLAW, out)
        beam(bend, end, 0.01, CLAW, out)
    return out


class Viewmodel:
    def __init__(self, engine, cube):
        """`cube`: (positions, normals) of a unit cube centred on the origin."""
        self.engine = engine
        scene = engine.scene
        positions, normals, colours = gun_mesh()
        self.gun = engine.create_mesh(positions, normals=normals, colors=colours, name="GravityGun")
        scene.set_material_vec4(self.gun, "baseColorFactor", (1.0, 1.0, 1.0, 1.0))
        scene.set_material_float(self.gun, "gridStrength", 0.0)
        scene.set_render_layer_mask(self.gun, 1)
        self.tip = self.glowing(cube, "GravityGunTip")
        scene.set_scale(self.tip, (TIP_SIZE, TIP_SIZE, TIP_SIZE))
        self.beam = [self.glowing(cube, "GravityGunBeam") for _ in range(BEAM_SEGMENTS)]
        self.sway = (0.0, 0.0)
        self.time = 0.0
        self.visible = None
        self.beam_visible = None
        self.show(False)

    def glowing(self, cube, name):
        """A cube drawn unlit: in the Screens pass, its emissiveMap white."""
        positions, normals = cube
        entity = self.engine.create_mesh(positions, normals=normals, name=name)
        self.engine.scene.set_render_layer_mask(entity, 2)
        self.engine.scene.set_material_texture(entity, "emissiveMap", white_texture())
        return entity

    def show(self, visible):
        if visible == self.visible:
            return
        self.visible = visible
        for entity in (self.gun, self.tip):
            self.engine.scene.set_visible(entity, visible)
        if not visible:
            self.show_beam(False)

    def show_beam(self, visible):
        if visible == self.beam_visible:
            return
        self.beam_visible = visible
        for entity in self.beam:
            self.engine.scene.set_visible(entity, visible)

    def update(self, dt, basis, look, player, gun, held=None):
        """The gun placed for this frame: `basis` the view's (right, up,
        forward), `look` the mouse movement (pixels) turning it this frame,
        `held` the held body's centre relative to the camera, or None."""
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
        rotation = euler_from_basis(right, g_up, g_forward)
        scene.set_position(self.gun, origin)
        scene.set_rotation(self.gun, rotation)
        tip = to_view(origin, TIP)
        scene.set_position(self.tip, tip)
        scene.set_rotation(self.tip, rotation)

        status = gun.status
        strength = GLOW[status]
        if status == "holding":
            strength *= 1.0 + 0.25 * math.sin(2.0 * math.pi * PULSE_HZ * self.time)
        colour = HEAVY_COLOUR if status == "too_heavy" else GLOW_COLOUR
        charge = gun.charge
        if charge > 0.0:
            strength *= 1.0 + CHARGE_GLOW * charge
            colour = tuple(c + (1.0 - c) * charge for c in colour)
        scene.set_material_vec4(self.tip, "emissiveFactor", tuple(c * strength for c in colour) + (0.0,))

        self.show_beam(held is not None)
        if held is not None:
            self.place_beam(tip, held)

    def place_beam(self, start, end):
        scene = self.engine.scene
        along = sub(end, start)
        d = length(along)
        if d < 1e-3:
            return
        axis = scale(along, 1.0 / d)
        helper = (0.0, 1.0, 0.0) if abs(axis[1]) < 0.9 else (1.0, 0.0, 0.0)
        side = normalize(cross(axis, helper))
        lift = cross(side, axis)
        points = []
        for k in range(BEAM_SEGMENTS + 1):
            t = k / BEAM_SEGMENTS
            bow = BEAM_WOBBLE * math.sin(math.pi * t)
            a = bow * math.sin(9.0 * self.time + 5.0 * t)
            b = bow * math.cos(7.0 * self.time - 4.0 * t)
            points.append(add(add(start, scale(along, t)), add(scale(side, a), scale(lift, b))))
        glow = tuple(c * BEAM_GLOW for c in GLOW_COLOUR) + (0.0,)
        for entity, p, q in zip(self.beam, points, points[1:]):
            piece = sub(q, p)
            n = length(piece)
            direction = scale(piece, 1.0 / n)
            helper = (0.0, 1.0, 0.0) if abs(direction[1]) < 0.9 else (1.0, 0.0, 0.0)
            s = normalize(cross(direction, helper))
            u = cross(s, direction)
            scene.set_position(entity, scale(add(p, q), 0.5))
            scene.set_rotation(entity, euler_from_basis(s, u, direction))
            scene.set_scale(entity, (BEAM_WIDTH, BEAM_WIDTH, n))
            scene.set_material_vec4(entity, "emissiveFactor", glow)
