"""
Shoonyakasha Bistro, a tech demo on the default pipeline

Amazon Lumberyard's Bistro: a Paris street corner of cafés, a bakery, a
bookshop and a round plaza, 2.8 million triangles. It opens at dusk: the
last sun leaves the rooftops, then about a hundred lights come on (street
lamps, wall lanterns, and strings of coloured bulbs over the café
terraces) and the street runs on their light alone.

It shows the default pipeline carrying a production-sized scene:
- Clustered lighting with every one of the scene's 96 point lights live.
- The shadow atlas choosing, each frame, which lamps near the camera get
  point shadows.
- Ray-traced sun shadows where the GPU has ray queries, cascades otherwise.
- Automatic exposure following the dusk, bloom around the bulbs, ambient
  occlusion under the tables and awnings, TAA.

Keys:
    N       day or night
    R       ray-traced or cascaded sun shadows (where the GPU has ray queries)
    1-3     quality preset low / medium / high
    SPACE   stop or resume the camera's walk; while stopped, WASD/Q/E and
            the right mouse button fly it
    V       cycle debug views (cascades, shadow mask, normals, occlusion,
            lights per cluster, motion vectors)
    P       save a screenshot to bistro_screenshot.png

Usage:
    python tools/fetch_assets.py bistro    # once: ~2 GB download, then converted
    python bistro.py

Amazon Lumberyard Bistro, Open Research Content Archive (ORCA),
https://developer.nvidia.com/orca/amazon-lumberyard-bistro, CC BY 4.0.
glTF edit by NVIDIA (RTXDI assets, MIT). See assets/README.md.
"""

import json
import math
import os
import sys

import shoonyakasha as sk
from shoonyakasha import keys
from shoonyakasha.mathutil import lerp, look_rotation, rotation_facing, smoothstep

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gltf_lights  # noqa: E402

SCENE = "bistro/bistro.gltf"
root = sk.assets.root()
if root is None or not (root / SCENE).is_file():
    print("The Bistro is not in the asset root. Fetch and convert it once with\n"
          "    python tools/fetch_assets.py bistro")
    sys.exit(1)

engine = sk.Engine(
    title="Shoonyakasha - Bistro (default pipeline)",
    width=1600, height=900,
    hdr_environment_path="env/farm_sunset_1k.hdr",
)

SUN_DAY = (-0.45, -0.7, -0.55)       # afternoon, from over the plaza
SUN_NIGHT = (0.3, -0.8, 0.4)         # the moon
SUN = 10.0
MOON = 0.25
DUSK_SECONDS = 8.0

# The file's lights are in candela; this brings them to the engine's scale.
CANDELA = 0.03
# Its street lamps are pure white; incandescent reads better.
LAMP_WHITE = (1.0, 0.78, 0.52)
# Lights at least this strong (candela) may get a shadow in the atlas.
SHADOW_CANDELA = 150.0
# The file's emission is strong enough for its own renderer; scaled to ours.
GLOW = 0.5


# ── The scene ────────────────────────────────────────────────────────────

class Light:
    def __init__(self, entity, intensity, threshold):
        self.entity = entity
        self.intensity = intensity
        self.threshold = threshold      # where in the dusk it switches on


class Glow:
    """The entities drawn with one emissive material, and its emission."""

    def __init__(self, entities, factor, threshold):
        self.entities = entities
        self.factor = factor
        self.threshold = threshold


def switch(m, threshold):
    """0 before a light's point in the dusk, rising to 1 just after."""
    return smoothstep((m - threshold) / 0.08)


class Scene:
    def __init__(self):
        self.lights = []
        self.glows = []
        self.sun = sk.NULL_ENTITY
        self.night = False      # where the dusk is heading
        self.dusk = 0.0         # 0 day, 1 night
        self.applied = None

    def build(self):
        result = engine.load_gltf_scene(SCENE, max_texture_size=2048)
        print("[bistro] %d entities, %d vertices" % (len(result.entities), result.total_vertices))

        # Entities are named after their node, and the converter names mesh
        # nodes "<material>#<index>": group them by material.
        by_material = {}
        prefix = "bistro_"
        for entity in result.entities:
            name = engine.scene.get_name(entity)
            if name.startswith(prefix) and "#" in name:
                by_material.setdefault(name[len(prefix):].rsplit("#", 1)[0], []).append(entity)

        path = str(root / SCENE)
        with open(path, encoding="utf-8") as f:
            materials = json.load(f)["materials"]
        # Emissive materials glow at night. They come on over the middle of
        # the dusk, each at its own moment, like the lights.
        for i, material in enumerate(materials):
            factor = material.get("emissiveFactor")
            entities = by_material.get(material.get("name"), [])
            if factor and max(factor) > 0.0 and entities:
                self.glows.append(Glow(entities, [v * GLOW for v in factor], 0.45 + 0.25 * _hash(i)))

        for i, light in enumerate(gltf_lights.lights(path)):
            if light.kind == "directional":
                continue          # the demo keeps its own sun
            color = LAMP_WHITE if light.color == (1.0, 1.0, 1.0) else light.color
            intensity = light.intensity * CANDELA
            entity = engine.create_point_light(pos=light.position, color=color, intensity=intensity,
                                               range=light.range or 8.0)
            if light.intensity >= SHADOW_CANDELA:
                engine.scene.set_light_cast_shadows(entity, True)
            self.lights.append(Light(entity, intensity, 0.45 + 0.25 * _hash(i + 1000)))
        print("[bistro] %d lights, %d glowing materials" % (len(self.lights), len(self.glows)))

        # The sun is the first directional light with its shadow flag set.
        self.sun = engine.create_directional_light(direction=SUN_DAY, color=(1.0, 0.74, 0.48), intensity=SUN)
        engine.scene.set_light_cast_shadows(self.sun, True)
        engine.set_sun_shadows(cascades=4, max_distance=70.0, split_lambda=0.8)

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
        engine.scene.set_rotation(self.sun, rotation_facing(direction))
        engine.set_custom_float("default.iblIntensity", lerp(0.3, 0.04, smoothstep(m)))
        # Exposure still adapts at night, but no further than a lit street:
        # darker than that stays dark instead of being brightened to day.
        engine.set_custom_float("default.exposureMinEV", lerp(-4.0, 0.5, m))
        for light in self.lights:
            engine.scene.set_light_intensity(light.entity, light.intensity * switch(m, light.threshold))
        for glow in self.glows:
            k = switch(m, glow.threshold)
            value = (glow.factor[0] * k, glow.factor[1] * k, glow.factor[2] * k, 0.0)
            for entity in glow.entities:
                engine.scene.set_material_vec4(entity, "emissiveFactor", value)

    def update(self, dt):
        target = 1.0 if self.night else 0.0
        step = dt / DUSK_SECONDS
        self.dusk = min(self.dusk + step, target) if self.dusk < target else max(self.dusk - step, target)
        self.apply()


def _hash(i):
    """A fixed pseudo-random number in [0, 1) for index i."""
    return ((i * 2654435761) % 4294967296) / 4294967296.0


# ── Camera ───────────────────────────────────────────────────────────────

# A walk down the lane from the north end, round the café corner, and out
# into the plaza, at eye height. (x, z) in metres.
WALK = [(2.4, -21.1), (-0.5, -14.5), (-6.2, -9.3), (-10.9, -3.1), (-16.0, 4.5),
        (-15.5, 12.0), (-8.1, 19.6), (-0.5, 22.5), (9.0, 25.3), (18.5, 29.1), (26.0, 32.0)]
EYE_HEIGHT = 1.7
WALK_SPEED = 1.1          # metres per second


def catmull_rom(points, s):
    """The point at s (0..len-1) on a Catmull-Rom spline through points."""
    n = len(points)
    i = min(int(s), n - 2)
    t = s - i
    p0, p1, p2, p3 = (points[max(0, min(n - 1, j))] for j in (i - 1, i, i + 1, i + 2))
    return tuple(0.5 * (2 * b + (-a + c) * t + (2 * a - 5 * b + 4 * c - d) * t * t + (-a + 3 * b - 3 * c + d) * t ** 3)
                 for a, b, c, d in zip(p0, p1, p2, p3))


class Walk:
    """Down the street and back, looking a little ahead and about."""

    def __init__(self):
        self.t = 0.0
        self.running = True
        # Spline parameter per metre, from the length of each segment.
        self.lengths = [math.dist(WALK[i], WALK[i + 1]) for i in range(len(WALK) - 1)]
        self.total = sum(self.lengths)

    def _param(self, distance):
        distance = min(max(distance, 0.0), self.total)
        for i, length in enumerate(self.lengths):
            if distance <= length:
                return i + distance / length
            distance -= length
        return len(self.lengths)

    def pose(self):
        d = (self.t * WALK_SPEED) % (2.0 * self.total)
        forward = d < self.total
        d = d if forward else 2.0 * self.total - d
        ahead = d + (6.0 if forward else -6.0)
        x, z = catmull_rom(WALK, self._param(d))
        tx, tz = catmull_rom(WALK, self._param(ahead))
        # Glance up at the facades now and then.
        eye = (x, EYE_HEIGHT + 0.15 * math.sin(self.t * 0.3), z)
        target = (tx + 1.5 * math.sin(self.t * 0.11), EYE_HEIGHT + 1.2 + 1.3 * math.sin(self.t * 0.07), tz)
        return eye, target


scene = Scene()
walk = Walk()


def on_init():
    engine.set_custom_float("default.exposure", 0.8)
    engine.set_custom_float("default.skyBlur", 1.0)
    engine.set_custom_float("default.aoRadius", 0.8)
    engine.set_custom_float("default.contactShadowLength", 0.2)
    eye, target = walk.pose()
    camera = engine.create_camera(pos=eye, fov=62.0, speed=4.0, near_plane=0.05, far_plane=400.0)
    engine.scene.set_rotation(camera, look_rotation(eye, target))
    scene.build()
    # Open at the start of the dusk and let it fall.
    scene.set_night(False, instant=True)
    scene.set_night(True)
    controls.ray_traced = engine.ray_query_supported()
    if controls.ray_traced:
        engine.apply_pipeline_preset("raytraced")
    print("sun shadows:", "ray traced" if controls.ray_traced else "cascaded")

    def animate(dt):
        if walk.running:
            walk.t += dt
            eye, target = walk.pose()
            engine.scene.set_position(engine.camera_entity, eye)
            engine.scene.set_rotation(engine.camera_entity, look_rotation(eye, target))
        scene.update(dt)
        return True

    # Before the transform system (priority 0), so this frame's poses render.
    engine.ecs.add_system("Bistro", animate, priority=-5)


DEBUG_VIEWS = ("final image", "shadow cascades", "shadow mask", "normals", "ambient occlusion",
               "lights per cluster", "motion vectors")


class Controls:
    def __init__(self):
        self.pressed = keys.KeyEdges(engine.input).pressed
        self.ray_traced = False
        self.debug_view = 0

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
            walk.running = not walk.running
        if self.pressed(keys.V):
            self.debug_view = (self.debug_view + 1) % len(DEBUG_VIEWS)
            engine.set_custom_uint("default.debugView", self.debug_view)
            print("debug view:", DEBUG_VIEWS[self.debug_view])
        if self.pressed(keys.P):
            path = "bistro_screenshot.png"
            print("screenshot ->", path if engine.capture_screenshot(path) else "failed")


controls = Controls()
engine.set_on_init(on_init)
engine.set_on_update(controls.update)

if __name__ == "__main__":
    engine.run()
