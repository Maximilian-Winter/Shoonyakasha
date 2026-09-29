"""
Shoonyakasha Alley, a tech demo on the default pipeline

A back alley between an apartment block and an old factory, built from Poly
Haven's CC0 models: two building fronts assembled from their modular kits, a
fire escape, street lamps, a burning barrel stove, a covered car, a
chain-link gate, and the usual clutter. A fox wanders through.

It shows what the default pipeline does with a real scene:
- Late afternoon: a low sun through the gate, whose chain-link casts
  alpha-tested shadows down the alley. Ray traced where the GPU has ray
  queries, with soft penumbrae; cascaded otherwise.
- Night: street lamps, a security light and the stove's fire, each with its
  own spot or point shadow in the atlas. Bloom around the bulbs, and
  exposure adapting as the lights change.
- Ambient occlusion in the corners, TAA with motion vectors on the fox,
  image-based light from the sky.

Keys:
    N       day or night
    R       ray-traced or cascaded sun shadows (where the GPU has ray queries)
    1-3     quality preset low / medium / high
    SPACE   stop or resume the camera's flight; while stopped, WASD/Q/E and
            the right mouse button fly it
    V       cycle debug views (cascades, shadow mask, normals, occlusion,
            lights per cluster, motion vectors)
    P       save a screenshot to alley_screenshot.png

Usage:
    python tools/fetch_assets.py alley     # once: ~120 MB of Poly Haven models
    python alley.py

Models and textures: Poly Haven (https://polyhaven.com), CC0. The fox:
Khronos glTF-Sample-Assets, CC0. See assets/README.md.
"""

import json
import math
import os
import random
import sys

import shoonyakasha as sk
from shoonyakasha import keys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import kit  # noqa: E402

PH = "polyhaven"
REQUIRED = ["modular_urban_apartments_facade", "modular_factory_facade", "modular_chainlink_fence",
            "asphalt_02"]
root = sk.assets.root()
if root is None or not all((root / PH / name).is_dir() for name in REQUIRED):
    print("The alley's models are not in the asset root. Fetch them once with\n"
          "    python tools/fetch_assets.py alley")
    sys.exit(1)

engine = sk.Engine(
    title="Shoonyakasha - Alley (default pipeline)",
    width=1600, height=900,
    hdr_environment_path="env/farm_sunset_1k.hdr",
)

# ── Layout ───────────────────────────────────────────────────────────────
# The alley runs along -z from the camera's end (z = 6) to a chain-link gate
# (z = GATE_Z). The apartments line its left side (x = -HALF), facing +x; the
# factory its right side (x = +HALF), facing -x. Modules are 3 m wide and a
# floor is 3 m tall.
HALF = 4.0
COLUMNS = 12
START_Z = 6.0
GATE_Z = START_Z - 3.0 * COLUMNS + 6.0
FLOORS = 4

SUN_DAY = (0.5, -0.55, 0.67)         # low over the apartments' roof, raking across the factory's brick
SUN_NIGHT = (0.35, -0.8, 0.45)       # the moon, high and cold

SUN = 6.0
MOON = 0.4
DUSK_SECONDS = 6.0   # N switches between day and night over this long

# Light intensities at night.
LANTERN = 10.0
SECURITY = 11.0
STREET = 7.0
FIRE = 10.0
WINDOW = 2.5      # emission of a lit window
DAY_FIRE = 0.1    # the stove's light by day, relative to night


def ph(name):
    """A Poly Haven model's path in the asset root."""
    return "%s/%s/%s.gltf" % (PH, name, name)


def compose_apartments():
    c = kit.Composer(str(root / PH / "modular_urban_apartments_facade" / "modular_urban_apartments_facade.gltf"))
    # There is nothing behind the windows: keep their glass opaque.
    c.override_material("modular_urban_apartments_facade_glass", alphaMode="OPAQUE")
    ground = {2: "door_centered_large_01", 5: "door_centered_small_01", 9: "door_offset_small_01"}
    upper = ["window_centered_large", "window_centered_double", "window_centered_small",
             "window_centered_large", "window_offset_small", "window_centered_double"]
    for k in range(COLUMNS):
        # yaw 90: a module's local x (-3..0) runs along +z, its front faces +x.
        at = lambda y: (-HALF, y, START_Z - 3.0 * (k + 1))
        c.add("base_standard_01", at(0.0), 90)
        door = ground.get(k)
        if door:
            c.add("wall_" + door, at(0.0), 90)
            c.add(door, at(0.0), 90)
        else:
            c.add("wall_standard_standard_01", at(0.0), 90)
        c.add("cornice_standard_standard_01", at(3.0), 90)
        for floor in range(1, FLOORS):
            window = upper[(k + floor) % len(upper)] + "_%02d" % min(floor, 3)
            c.add("wall_" + window, at(3.0 * floor), 90)
            c.add(window, at(3.0 * floor), 90)
        c.add("crown_standard_standard_01", at(3.0 * FLOORS), 90)
    return c.write("alley_apartments")


def compose_factory():
    c = kit.Composer(str(root / PH / "modular_factory_facade" / "modular_factory_facade.gltf"))
    c.override_material("modular_factory_facade_windows_glass", alphaMode="OPAQUE")
    # Ground floor: (first column, module, columns wide).
    ground = [(1, "garage_door_01", 2), (6, "recessed_large_01", 1), (8, "garage_centered_01", 2)]
    taken = {k for first, _, width in ground for k in range(first, first + width)}
    for k in range(COLUMNS):
        # yaw -90: local x (-3..0) runs along -z, the front faces -x.
        at = lambda y, k=k: (HALF, y, START_Z - 3.0 * k)
        c.add("base_standard_standard_01", at(0.0), -90)
        if k not in taken:
            c.add("wall_standard_standard_01", at(0.0), -90)
        c.add("cornice01_standard_standard_01", at(3.0), -90)
        for floor in range(1, FLOORS):
            if k % 3 == 1:
                window = "window_tall_large_%02d" % floor
            else:
                window = ("window_centered_large_%02d" if k % 3 == 0 else "window_centered_medium_%02d") % floor
            c.add("wall_" + window, at(3.0 * floor), -90)
            c.add(window, at(3.0 * floor), -90)
        c.add("crown_standard_standard_01", at(3.0 * FLOORS), -90)
    for first, door, width in ground:
        # A module `width` columns wide spans local x -3*width..0, so from
        # its origin it covers those columns towards -z.
        pos = (HALF, 0.0, START_Z - 3.0 * first)
        c.add("wall_door_" + door, pos, -90)
        c.add("door_" + door, pos, -90)
    return c.write("alley_factory")


def compose_backdrop():
    """A lower factory far beyond the gate, closing the view without
    shading the alley from the low sun."""
    c = kit.Composer(str(root / PH / "modular_factory_facade" / "modular_factory_facade.gltf"))
    c.override_material("modular_factory_facade_windows_glass", alphaMode="OPAQUE")
    z = GATE_Z - 30.0
    floors = 2
    for k in range(-6, 7):
        at = lambda y, k=k: (3.0 * k + 1.5, y, z)
        c.add("base_standard_standard_01", at(0.0))
        c.add("wall_standard_standard_01", at(0.0))
        for floor in range(1, floors + 1):
            window = "window_centered_large_%02d" % floor
            c.add("wall_" + window, at(3.0 * floor))
            c.add(window, at(3.0 * floor))
        c.add("crown_standard_standard_01", at(3.0 * (floors + 1)))
    return c.write("alley_backdrop")


def compose_gate():
    c = kit.Composer(str(root / PH / "modular_chainlink_fence" / "modular_chainlink_fence.gltf"))
    c.bake_texture_transforms()
    # Alpha-tested wire casts shadows; a blended one would not.
    c.override_material("modular_chainlink_fence_wire", alphaMode="MASK", alphaCutoff=0.5)
    for x in (-HALF, -2.0, 2.0, HALF):
        c.add("modular_chainlink_fence_post", (x, 0.0, GATE_Z))
    for x in (-2.0, 0.0, HALF):
        c.add("modular_chainlink_fence_double", (x, 0.0, GATE_Z))
    c.add("modular_chainlink_fence_door_frame", (0.0, 0.0, GATE_Z))
    c.add("modular_chainlink_fence_door_gate", (0.55, 1.08, GATE_Z))
    return c.write("alley_gate")


def compose_ground():
    return kit.ground_plane(str(root / PH / "asphalt_02"), "asphalt_02", "alley_ground",
                            (-40.0, 40.0), (START_Z + 30.0, GATE_Z - 40.0), tile=3.0)


# ── Scene ────────────────────────────────────────────────────────────────

def root_of(entity):
    parent = engine.scene.get_parent(entity)
    while parent != sk.NULL_ENTITY:
        entity, parent = parent, engine.scene.get_parent(parent)
    return entity


class Prop:
    """A loaded model: its entities, its roots, and material names by entity."""

    def __init__(self, name, position, yaw=0.0, scale=1.0, hide=(), path=None):
        path = path or ph(name)
        self.result = engine.load_gltf_scene(path)
        self.entities = list(self.result.entities)
        self.roots = sorted({root_of(e) for e in self.entities})
        with open(str(root / path), encoding="utf-8") as f:
            self.gltf = json.load(f)
        for r in self.roots:
            px, py, pz = engine.scene.get_position(r)
            # Turn the model's own layout about its origin, then place it.
            x, z = kit.rotate_yaw(px * scale, pz * scale, yaw)
            engine.scene.set_position(r, (position[0] + x, position[1] + py * scale, position[2] + z))
            rx, ry, rz = engine.scene.get_rotation(r)
            engine.scene.set_rotation(r, (rx, ry + math.radians(yaw), rz))
            if scale != 1.0:
                sx, sy, sz = engine.scene.get_scale(r)
                engine.scene.set_scale(r, (sx * scale, sy * scale, sz * scale))
        for e in self.entities:
            if any(h in engine.scene.get_name(e) for h in hide):
                engine.scene.set_visible(e, False)

    def with_material(self, material):
        """Entities drawn with the named glTF material."""
        out = []
        names = {}
        for node in self.gltf["nodes"]:
            if "mesh" in node:
                prims = self.gltf["meshes"][node["mesh"]]["primitives"]
                names[node.get("name")] = [self.gltf["materials"][p["material"]]["name"] if "material" in p else ""
                                           for p in prims]
        for e in self.entities:
            full = engine.scene.get_name(e)
            for node, materials in names.items():
                for i, m in enumerate(materials):
                    suffix = "_%s_prim%d" % (node, i) if len(materials) > 1 else "_%s" % node
                    if m == material and full.endswith(suffix):
                        out.append(e)
        return out


class Lamp:
    """A light, the bulbs that glow with it, and the glass around them."""

    def __init__(self, light, color, glow, intensity, prop=None, bulb=None, glass=None):
        self.light, self.color, self.glow, self.intensity = light, color, glow, intensity
        self.bulbs = prop.with_material(bulb) if prop and bulb else []
        self.glass = prop.with_material(glass) if prop and glass else []

    def set(self, level):
        """0 off, 1 fully on."""
        engine.scene.set_light_intensity(self.light, self.intensity * level)
        k = self.glow * level
        glow = (self.color[0] * k, self.color[1] * k, self.color[2] * k, 0.0)
        for b in self.bulbs:
            engine.scene.set_material_vec4(b, "emissiveFactor", glow)
        for g in self.glass:
            engine.scene.set_material_vec4(g, "emissiveFactor", tuple(v * 0.25 for v in glow))


def spot(position, pitch, yaw, color, intensity, rng):
    light = engine.create_point_light(pos=position, color=color, intensity=intensity, range=rng)
    engine.scene.set_light_type(light, sk.LIGHT_SPOT)
    engine.scene.set_rotation(light, (pitch, yaw, 0.0))
    engine.scene.set_light_cast_shadows(light, True)
    return light


def point(position, color, intensity, rng):
    light = engine.create_point_light(pos=position, color=color, intensity=intensity, range=rng)
    engine.scene.set_light_cast_shadows(light, True)
    return light


class Scene:
    def __init__(self):
        self.lamps = []
        self.fire = None
        self.windows = []
        self.sun = sk.NULL_ENTITY
        self.fox = []
        self.night = False    # where the dusk is heading
        self.dusk = 0.0       # 0 day, 1 night
        self.applied = None

    def build(self):
        rel = lambda path: os.path.relpath(path, str(root)).replace(os.sep, "/")
        apartments = Prop(None, (0.0, 0.0, 0.0), path=rel(compose_apartments()))
        factory = Prop(None, (0.0, 0.0, 0.0), path=rel(compose_factory()))
        backdrop = Prop(None, (0.0, 0.0, 0.0), path=rel(compose_backdrop()))
        for path in (compose_gate(), compose_ground()):
            engine.load_gltf_scene(rel(path))

        # Some windows are lit at night, most in warm lamplight, a few in the
        # blue of a television.
        pick = random.Random(7)
        self.windows = []
        # The factory's windows are large panes of frosted glass: fewer lit,
        # and dimmer, or they glare.
        for building, glass, share, level in ((apartments, "modular_urban_apartments_facade_glass", 0.35, 1.0),
                                              (factory, "modular_factory_facade_windows_glass", 0.2, 0.3),
                                              (backdrop, "modular_factory_facade_windows_glass", 0.3, 0.4)):
            for entity in building.with_material(glass):
                if pick.random() < share:
                    tv = pick.random() < 0.2
                    color = (0.35, 0.5, 1.0) if tv else (1.0, 0.62 + 0.15 * pick.random(), 0.3)
                    k = WINDOW * level * (0.5 + pick.random())
                    # Each switches on at its own point of the dusk.
                    self.windows.append((entity, color, k, 0.2 + 0.6 * pick.random()))

        Prop("modular_fire_escape", (-HALF + 0.75, 3.0 + 3.65, START_Z - 12.0), yaw=90)
        Prop("covered_car", (HALF - 1.4, 0.0, START_Z - 9.0), yaw=8)
        Prop("metal_trash_can", (-HALF + 0.45, 0.0, START_Z - 4.0), yaw=90)
        Prop("fire_hydrant", (HALF - 0.4, 0.0, START_Z - 2.0), yaw=-90, hide=("_aged",))
        Prop("concrete_road_barrier", (1.2, 0.0, START_Z - 0.5), yaw=15)
        Prop("utility_box_01", (HALF - 0.25, 0.0, START_Z - 16.5), yaw=-90)
        Prop("exterior_aircon_unit", (HALF - 0.25, 4.2, START_Z - 20.0), yaw=-90, hide=("_rusted",))
        Prop("rollershutter_door", (HALF - 0.05, 0.0, START_Z - 25.5), yaw=-90, hide=("rollershutter_door_rollershutter_door",))
        Prop("water_manhole_cover", (0.3, 0.0, START_Z - 13.0))
        Prop("old_tyre", (-HALF + 0.5, 0.3, START_Z - 17.0), yaw=70)
        Prop("wooden_crate_01", (-HALF + 0.6, 0.0, START_Z - 21.5), yaw=85)
        Prop("cardboard_box_01", (-HALF + 0.5, 0.0, START_Z - 22.4), yaw=100)
        Prop("cardboard_box_01", (-HALF + 0.55, 0.34, START_Z - 22.3), yaw=80)
        Prop("Barrel_02", (HALF - 0.5, 0.0, START_Z - 14.2))
        Prop("plastic_monobloc_chair_01", (-1.6, 0.0, START_Z - 15.6), yaw=40)
        Prop("potted_plant_02", (-HALF + 0.55, 0.0, START_Z - 7.4))
        for i in range(6):
            Prop("weed_plant_02", (-HALF + 0.3 + 0.2 * (i % 2), 0.0, START_Z - 3.0 - 4.7 * i), yaw=90 + 30 * i)
            Prop("weed_plant_02", (HALF - 0.3, 0.0, START_Z - 5.0 - 4.1 * i), yaw=-90 + 25 * i)

        # Lights. A wall lantern on the apartments, a security light over the
        # garage, a street lamp at the gate, the stove's fire.
        warm = (1.0, 0.72, 0.42)
        lantern = Prop("street_lamp_02", (-HALF + 0.05, 3.3, START_Z - 6.5), yaw=90)
        self.lamps.append(Lamp(spot((-HALF + 0.75, 3.1, START_Z - 6.5), -1.35, math.radians(-90), warm, LANTERN, 11.0),
                               warm, 40.0, LANTERN, lantern, "street_lamp_02_bulb", "street_lamp_02_glass"))
        lantern2 = Prop("street_lamp_02", (-HALF + 0.05, 3.3, START_Z - 24.5), yaw=90)
        self.lamps.append(Lamp(spot((-HALF + 0.75, 3.1, START_Z - 24.5), -1.35, math.radians(-90), warm, LANTERN, 11.0),
                               warm, 40.0, LANTERN, lantern2, "street_lamp_02_bulb", "street_lamp_02_glass"))
        cold = (0.75, 0.85, 1.0)
        security = Prop("security_light", (HALF - 0.05, 3.4, START_Z - 6.0), yaw=-90)
        self.lamps.append(Lamp(spot((HALF - 0.4, 3.3, START_Z - 6.0), -0.9, math.radians(90), cold, SECURITY, 14.0),
                               cold, 60.0, SECURITY, security, "security_light_bulb", "security_light_glass"))
        street = Prop("street_lamp_01", (-2.6, 0.0, GATE_Z + 1.2))
        self.lamps.append(Lamp(point((-2.6, 3.55, GATE_Z + 1.2), warm, STREET, 10.0),
                               warm, 40.0, STREET, street, "street_lamp_01_bulb", "street_lamp_01_glass"))

        Prop("barrel_stove", (-1.9, 0.0, START_Z - 15.0))
        fire = (1.0, 0.45, 0.15)
        self.fire = Lamp(point((-1.9, 1.15, START_Z - 15.0), fire, FIRE, 8.0), fire, 0.0, FIRE)

        # The sun is the first directional light with its shadow flag set.
        self.sun = engine.create_directional_light(direction=SUN_DAY, color=(1.0, 0.74, 0.48), intensity=SUN)
        engine.scene.set_light_cast_shadows(self.sun, True)
        engine.set_sun_shadows(cascades=4, max_distance=45.0, split_lambda=0.8)

        fox = engine.load_gltf_scene("models/Fox.glb", load_skins=True, load_animations=True)
        self.fox_root = sorted({root_of(e) for e in fox.entities})[0]
        engine.scene.set_scale(self.fox_root, (0.011, 0.011, 0.011))
        for e in fox.entities:
            if engine.scene.get_animation_clip_count(e) > 0:
                engine.scene.set_animation_looping(e, True)
                engine.scene.play_animation(e, 1)     # walk
                self.fox.append(e)
        self.fox_t = 0.0

    def set_night(self, night, instant=False):
        """Head for night or day, over DUSK_SECONDS unless `instant`."""
        self.night = night
        if instant:
            self.dusk = 1.0 if night else 0.0
            self.apply()

    def apply(self):
        """Light the scene for the current point of the dusk."""
        m = self.dusk
        if self.applied == m:
            return
        self.applied = m
        # The same directional light is the sun, sinking and reddening until
        # halfway, and then the moon.
        if m < 0.5:
            f = m / 0.5
            x, y, z = SUN_DAY
            direction = (x, lerp(y, -0.04, f), z)
            engine.scene.set_light_intensity(self.sun, SUN * (1.0 - f) ** 1.5)
            engine.scene.set_light_color(self.sun, (1.0, lerp(0.74, 0.45, f), lerp(0.48, 0.25, f)))
        else:
            f = (m - 0.5) / 0.5
            direction = SUN_NIGHT
            engine.scene.set_light_intensity(self.sun, MOON * f)
            engine.scene.set_light_color(self.sun, (0.55, 0.65, 1.0))
        engine.scene.set_rotation(self.sun, sk_rotation_facing(direction))
        engine.set_custom_float("default.iblIntensity", lerp(0.5, 0.05, smooth(m)))
        # Exposure still adapts at night, but no further than a lit street:
        # darker than that stays dark instead of being brightened to day.
        engine.set_custom_float("default.exposureMinEV", lerp(-4.0, 1.0, m))
        lamps = smooth(min(max((m - 0.35) / 0.3, 0.0), 1.0))
        for lamp in self.lamps:
            lamp.set(lamps)
        for entity, color, k, threshold in self.windows:
            k = k if m > threshold else 0.0
            engine.scene.set_material_vec4(entity, "emissiveFactor", (color[0] * k, color[1] * k, color[2] * k, 0.0))

    def update(self, dt, t):
        target = 1.0 if self.night else 0.0
        step = dt / DUSK_SECONDS
        self.dusk = min(self.dusk + step, target) if self.dusk < target else max(self.dusk - step, target)
        self.apply()
        # The fire flickers: two slow waves and a little noise. By day it is
        # faint against the daylight.
        flicker = 0.8 + 0.12 * math.sin(t * 7.3) + 0.08 * math.sin(t * 13.1 + 1.0) + 0.05 * random.random()
        self.fire.set(flicker * lerp(DAY_FIRE, 1.0, self.dusk))
        # The fox walks down the alley and back, at walking pace.
        self.fox_t += dt
        length = 22.0
        s = (self.fox_t * 0.9) % (2.0 * length)
        forward = s < length
        z = START_Z - 1.0 - (s if forward else 2.0 * length - s)
        x = 0.8 * math.sin(z * 0.35)
        engine.scene.set_position(self.fox_root, (x, 0.0, z))
        # The fox faces +z.
        engine.scene.set_rotation(self.fox_root, (0.0, math.pi if forward else 0.0, 0.0))


def lerp(a, b, t):
    return a + (b - a) * t


def smooth(t):
    return t * t * (3.0 - 2.0 * t)


def sk_rotation_facing(direction):
    """Euler rotation (pitch, yaw, 0) whose forward is `direction`."""
    x, y, z = direction
    length = math.sqrt(x * x + y * y + z * z)
    return (math.asin(y / length), math.atan2(-x, -z), 0.0)


# ── Camera ───────────────────────────────────────────────────────────────

class Flight:
    """A slow camera path: down the alley and back up, looking about."""

    def __init__(self):
        self.t = 0.0
        self.running = True

    def pose(self):
        u = 0.5 - 0.5 * math.cos(self.t * 0.08)          # 0..1..0
        z = START_Z + 3.0 - u * 24.0
        x = 1.2 * math.sin(self.t * 0.13)
        y = 1.7 + 0.5 * math.sin(self.t * 0.05)
        eye = (x, y, z)
        target = (0.6 * math.sin(self.t * 0.07), 2.2 + 1.2 * math.sin(self.t * 0.06), z - 8.0)
        return eye, target


def look_rotation(eye, target):
    dx, dy, dz = (t - e for t, e in zip(target, eye))
    length = math.sqrt(dx * dx + dy * dy + dz * dz)
    return (math.asin(dy / length), math.atan2(-dx, -dz), 0.0)


scene = Scene()
flight = Flight()
clock = [0.0]


def on_init():
    # A little under the automatic exposure: shade stays shade.
    engine.set_custom_float("default.exposure", 0.75)
    engine.set_custom_float("default.skyBlur", 1.0)
    engine.set_custom_float("default.aoRadius", 0.8)
    engine.set_custom_float("default.contactShadowLength", 0.2)
    eye, target = flight.pose()
    camera = engine.create_camera(pos=eye, fov=62.0, speed=3.0, near_plane=0.05, far_plane=300.0)
    engine.scene.set_rotation(camera, look_rotation(eye, target))
    scene.build()
    scene.set_night(False, instant=True)
    controls.ray_traced = engine.ray_query_supported()
    if controls.ray_traced:
        engine.apply_pipeline_preset("raytraced")
    print("sun shadows:", "ray traced" if controls.ray_traced else "cascaded")

    def animate(dt):
        clock[0] += dt
        if flight.running:
            flight.t += dt
            eye, target = flight.pose()
            engine.scene.set_position(engine.camera_entity, eye)
            engine.scene.set_rotation(engine.camera_entity, look_rotation(eye, target))
        scene.update(dt, clock[0])
        return True

    # Before the transform system (priority 0), so this frame's poses render.
    engine.ecs.add_system("Alley", animate, priority=-5)


DEBUG_VIEWS = ("final image", "shadow cascades", "shadow mask", "normals", "ambient occlusion",
               "lights per cluster", "motion vectors")


class Controls:
    def __init__(self):
        self._was_down = {}
        self.ray_traced = False
        self.debug_view = 0

    def pressed(self, key):
        down = engine.input.is_key_down(key)
        was = self._was_down.get(key, False)
        self._was_down[key] = down
        return down and not was

    def update(self, dt):
        if self.pressed(keys.N):
            scene.set_night(not scene.night)
            print("night" if scene.night else "day")
        if self.pressed(keys.R):
            if engine.ray_query_supported():
                self.ray_traced = not self.ray_traced
                engine.apply_pipeline_preset("raytraced" if self.ray_traced else "high")
                print("sun shadows:", "ray traced" if self.ray_traced else "cascaded")
            else:
                print("this GPU has no ray queries; the shadows stay cascaded")
        for key, preset in ((keys.NUM_1, "low"), (keys.NUM_2, "medium"), (keys.NUM_3, "high")):
            if self.pressed(key):
                engine.apply_pipeline_preset(preset)
                self.ray_traced = False
                print("preset", preset)
        if self.pressed(keys.SPACE):
            flight.running = not flight.running
        if self.pressed(keys.V):
            self.debug_view = (self.debug_view + 1) % len(DEBUG_VIEWS)
            engine.set_custom_uint("default.debugView", self.debug_view)
            print("debug view:", DEBUG_VIEWS[self.debug_view])
        if self.pressed(keys.P):
            path = "alley_screenshot.png"
            print("screenshot ->", path if engine.capture_screenshot(path) else "failed")


controls = Controls()
engine.set_on_init(on_init)
engine.set_on_update(controls.update)

if __name__ == "__main__":
    engine.run()
