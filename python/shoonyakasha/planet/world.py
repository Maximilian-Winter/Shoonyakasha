"""
A planet and its moon drawn with procedural terrain on the planet pipeline
(PIPELINE): cube-sphere chunks built on the GPU with quadtree level of
detail, an atmosphere, clouds, an ocean, sun shadows and ground textures.

    engine = sk.Engine(..., pipeline_json_path=str(world.PIPELINE))
    planet = world.PlanetWorld(engine)

    def on_init():
        planet.setup()

    def frame(dt):
        planet.begin_frame()                 # heights read back; the terrain started once the site is known
        ...                                  # move the camera, advance planet.time
        planet.end_frame(position, (right, up, forward), dt)

Positions are Python floats (64-bit) with the planet's centre at the origin.
The engine's camera (`PlanetWorld.camera`) stays at its own origin and only
turns: every frame each chunk is placed at its position minus the camera's,
so the GPU sees only small numbers. The projection is reverse-Z with an
infinite far plane.

The planet is a cube-sphere: six cube faces mapped onto the sphere
(cube_sphere.py), each the root of a quadtree. Each frame a body's tree
picks the chunks to draw, finer near the camera, and up to MAX_JOBS
missing chunks, those in view first and nearest first, are built on the
GPU into slots of a buffer pool (chunk_build.comp). Each chunk's lowest
and highest heights come back and tighten its node's bounds. All the drawn
chunks are one indirect draw call. The trees, the pool, culling and the
draw lists are sk.PlanetLod's (include/Planet/PlanetLod.h, in C++), as
quadtree.py does them in Python; `Body.tree` (TreeView) and `pool`
(PoolView) show their state.

A site on the +Z face anchors the sun's direction (by its elevation and
azimuth seen from the site), the moon's orbit and, with `pad`, a flat pad
in the terrain at the site's natural height, which a probe measures before
the planet is built.

Probes: each frame BuildChunks also evaluates the terrain at PROBES
directions: the ground under the viewpoint, the site, the directions of a
ground patch the caller asks for (an object with `directions` and
`set_heights(heights)`, up to PATCH_PROBES), and EXTRA_PROBES directions in
`extra_probes`. Their heights come back some frames later
(`probe_latency`): `ground`, `site_height`, `patch` and `extra_heights`.
"""

import math
from pathlib import Path

import numpy as np

from . import cube_sphere, ground_textures, orbits, shadows
from .vec import add, dot, euler_from_basis, length, local_frame, normalize, scale, sub

#: The planet pipeline shipped with the package.
PIPELINE = Path(__file__).resolve().parent.parent / "pipelines" / "planet" / "pipeline.json"

RADIUS = 100_000.0               # metres
QUADS = 16                       # quads along each side of a chunk
MAX_LEVEL = 12                   # a level-12 chunk is 38 m across, its quads 2.4 m
SPLIT_FACTOR = 1.25              # split when nearer than this many chunk edges
MORPH_START = 0.8                # morphing starts at this share of the parent's split distance; matches morph.glsl
MORPH_LEVELS = 4                 # ancestors a vertex morphs towards at most; matches morph.glsl
# The sun's shadow cascades (shadows.py): where they split the view (m), the map's size per side,
# and how far towards the sun beyond a cascade casters count (m).
SHADOW_SPLITS = (0.05, 12.0, 150.0, 2_000.0, 20_000.0)
SHADOW_RESOLUTION = 2048         # matches sunShadowMap in pipeline.json
SHADOW_REACH = 5_000.0
# The cloud layer (clouds.glsl): bottom and top (m); the coverage noise's frequency per unit of
# direction (cells of about 2.5 km), the detail noise's scale (m) and how much it erodes.
CLOUD_LAYER = (1_500.0, 3_500.0)
CLOUD_SHAPE = (40.0, 900.0, 0.8)
CLOUD_COVERAGE = 0.45            # share of the sky the clouds cover at the start
# The ground's textures (ground_textures.py, ground.glsl): metres per tile at the near and far scale.
GROUND_SCALES = (5.0, 47.0)
SKIRT_FRACTION = 0.05            # skirt depth, as a fraction of the chunk's edge
HEIGHT_PAD = 0.05                # added to a node's height range, as a fraction of its edge
ROOT_HEIGHTS = (-2000.0, 6000.0)  # the roots' height range until their chunks are measured
# The GPU chunk pool; these match pipeline.json and chunk_build.comp.
SLOTS = 2048                     # chunks the pool holds
MAX_JOBS = 16                    # chunks BuildChunks builds per frame at most
PROBES = 176                     # directions BuildChunks evaluates the terrain at
EXTRA_PROBES = 4                 # the last probes, for directions a caller asks for
PATCH_PROBES = PROBES - EXTRA_PROBES - 2   # after the ground under the viewpoint and the site
VERTICES_PER_CHUNK = (QUADS + 1) ** 2 + 4 * (QUADS + 1)
FLOATS_PER_VERTEX = 16           # position and u, normal and v, direction and height, horizon and padding
HORIZON_SUN_STEP = math.radians(0.5)   # the sun's move after which the chunks' horizons are measured again
DRAWS_OFFSET = 16                # bytes before the draw commands in ChunkDraws: the count, then padding
ORBIT_POINTS = 256               # points of the drawn orbit path; matches OrbitPoints in pipeline.json
# The terrain's parameters (terrain.glsl), the defaults in pipeline.json.
TERRAIN = {
    "continent": [1.5, 0.6, 0.05, 800.0],   # frequency, warp, sea level bias, height scale (m)
    "mountains": [6.0, 3000.0, 2.0, 8.0],   # frequency, height (m), mask frequency, octaves
    "hills": [40.0, 150.0, 1500.0, 6.0],    # frequency, height (m), ocean depth (m), octaves
    "seed": [0.0, 0.0, 0.0, 0.0],           # offset added to the noise input
    "detail": [80.0, 15.0, 4.0, 0.5],       # wavelength (m), height (m), octaves, erosion
}
SITE_FACE = 4                    # +Z
SITE_UV = 1.0 / 64.0             # along u and v from the face's centre: a vertex of every chunk from level 3 down
SITE_FLAT_RADIUS = 150.0         # metres of flat pad around the site
SITE_BLEND_RADIUS = 600.0        # metres from the site where the terrain is its own again
TERRAIN_COLOUR = (-1.0, 0.0, 0.0)  # tells terrain.frag to colour by height, slope and latitude
FACE_COLOURS = ((0.62, 0.30, 0.25), (0.25, 0.50, 0.55), (0.55, 0.55, 0.30),
                (0.40, 0.30, 0.60), (0.30, 0.55, 0.30), (0.60, 0.42, 0.25))
COLOUR_MODES = ("terrain", "face", "level")
SUN_ELEVATION, SUN_AZIMUTH = 40.0, 200.0   # degrees, seen from the site; azimuth from east towards north
# The atmosphere (atmosphere.glsl), the defaults in pipeline.json: a top 20 km up, scale heights of
# 2.5 km (air) and 400 m (haze), and Earth's coefficients times 3.2, which keeps Earth's optical
# depth straight up for the shorter scale heights.
ATMOSPHERE_TOP = 20_000.0
RAYLEIGH_SCALE_HEIGHT, MIE_SCALE_HEIGHT = 2_500.0, 400.0
RAYLEIGH = (1.857e-5, 4.339e-5, 1.0592e-4)          # scattering per metre at height 0
MIE_SCATTERING, MIE_ABSORPTION, MIE_G = 1.279e-5, 1.408e-5, 0.8
# Ozone: Earth's absorption at its peak times 3.2, peaking at 25 km / 3.2, a tent 15 km / 3.2 to each side.
OZONE_ABSORPTION = (2.08e-6, 6.02e-6, 2.72e-7)
OZONE_PEAK, OZONE_HALF_WIDTH = 7_800.0, 4_700.0
SUN_ILLUMINANCE = 20.0
EXPOSURE = 0.4
NEAR_PLANE = 0.05
FOV = 70.0
# The moon (orbits.Moon): radius, surface gravity (a fifth of the planet's), distance from the
# planet's centre, and where it starts, in radians east of the site's zenith along its orbit.
MOON_RADIUS = 25_000.0
MOON_GRAVITY = 0.981
MOON_DISTANCE = 600_000.0
MOON_PHASE = 0.6
MOON_HEIGHTS = (-2000.0, 1500.0)  # the moon's roots' height range until their chunks are measured
MOON_FLOOR = -2000.0             # metres above the moon's radius that its terrain (moon.glsl) stays above


def compile_shaders():
    """Compile the planet pipeline's shaders that changed since their SPIR-V
    was built; without glslc, the SPIR-V shipped with the package is used."""
    from .. import shaders
    try:
        shaders.compile_dir(PIPELINE.parent / "shaders")
    except shaders.GlslcNotFound:
        pass


class NodeInfo:
    """A copy of a quadtree node's state: its face, level and (x, y) on the
    face, its chunk's slot (`chunk`, None while it has none), its centre on
    the sphere, its edge length, its height range (`measured` once its
    chunk's heights came back) and its bounding sphere."""

    __slots__ = ("face", "level", "x", "y", "chunk", "centre", "edge", "low", "high", "measured",
                 "bound_centre", "bound_radius")

    def __init__(self, values):
        self.face, self.level, self.x, self.y = values["face"], values["level"], values["x"], values["y"]
        self.chunk = values["slot"]
        self.centre, self.edge = values["centre"], values["edge"]
        self.low, self.high, self.measured = values["low"], values["high"], values["measured"]
        self.bound_centre, self.bound_radius = values["bound_centre"], values["bound_radius"]

    def key(self):
        return (self.face, self.level, self.x, self.y)


class TreeView:
    """A body's quadtree in the world's level of detail (sk.PlanetLod): the
    nodes drawn (`selected`, in the order selected), the missing ones
    (`missing`, (distance, node) nearest first) and every node (`nodes()`),
    as NodeInfo copies taken again after anything changed; and its
    `split_factor`, which can be set."""

    def __init__(self, world, body):
        self.world = world
        self.body = body
        self.cache = {}

    def _cached(self, name, make):
        version = self.world.lod_version
        hit = self.cache.get(name)
        if hit is None or hit[0] != version:
            hit = (version, make())
            self.cache[name] = hit
        return hit[1]

    @property
    def selected(self):
        return self._cached("selected", lambda: [NodeInfo(n) for n in self.world.lod.selected(self.body)])

    @property
    def missing(self):
        return self._cached("missing", lambda: [(d, NodeInfo(n)) for d, n in self.world.lod.missing_nodes(self.body)])

    def nodes(self):
        return iter(self._cached("nodes", lambda: [NodeInfo(n) for n in self.world.lod.nodes(self.body)]))

    @property
    def split_factor(self):
        return self.world.lod.split_factor(self.body)

    @split_factor.setter
    def split_factor(self, value):
        self.world.lod.set_split_factor(self.body, value)
        self.world.lod_version += 1


class PoolView:
    """The chunk pool: `slots`, `used`, and `pending`, the chunks waiting for their heights."""

    def __init__(self, lod):
        self.lod = lod

    @property
    def slots(self):
        return self.lod.slots

    @property
    def used(self):
        return self.lod.used

    @property
    def pending(self):
        return self.lod.pending


class Body:
    """A body drawn with terrain from the chunk pool: its index (its chunk
    jobs carry it, and BuildChunks reads its radius by it), its radius, its
    terrain's parameters (the planet's; None for the moon, whose terrain
    has fixed parameters in moon.glsl), its orbit (an object with
    position(time) and velocity(time) around the planet's centre, or None
    for the planet), whether the ocean is drawn on it, metres above its
    radius its terrain stays above (`floor`, or None to work it out from
    `terrain`), its roots' height range until measured, and its quadtree
    (`tree`, a TreeView, or None until made). Its tree's chunks are relative
    to its centre. The world's `physics.time` places it on its orbit."""

    def __init__(self, index, name, radius, terrain, world, orbit=None, ocean=False, floor=None,
                 heights=ROOT_HEIGHTS):
        self.index, self.name, self.radius = index, name, radius
        self.terrain = terrain
        self.floor = floor
        self.heights = heights
        self.orbit = orbit
        self.ocean = ocean
        self.world = world
        self.view = TreeView(world, index)

    @property
    def tree(self):
        return self.view if self.world.lod.has_tree(self.index) else None

    def centre(self):
        """Its centre now, from the planet's centre."""
        return self.orbit.position(self.world.physics.time) if self.orbit is not None else (0.0, 0.0, 0.0)

    def velocity(self):
        """Its velocity now."""
        return self.orbit.velocity(self.world.physics.time) if self.orbit is not None else (0.0, 0.0, 0.0)

    def relative(self, point):
        """`point` (from the planet's centre) from its centre."""
        return sub(point, self.centre())


def skirt_depth(node):
    """How far a built chunk's skirts reach below its edges, as
    chunk_build.comp makes them: SKIRT_FRACTION of its edge, or its grid's
    height range."""
    return max(SKIRT_FRACTION * node.edge, node.high - node.low)


def parent_split_distance(node, tree):
    """The distance from its bounding sphere at which a node's parent in
    `tree` splits (the parent's edge is twice the node's); 0 for the
    roots."""
    return tree.split_factor * 2.0 * node.edge if node.level > 0 else 0.0


def terrain_floor(body):
    """Metres above `body`'s radius that no terrain goes below: its
    `floor`, or from its terrain parameters the seabed's depth, or on land
    the continent's edge plus the hills' and the detail's heights, both
    with 10 % to spare (terrain.glsl's noise stays within [-1, 1])."""
    if body.floor is not None:
        return body.floor
    terrain = body.terrain
    return -1.1 * max(terrain["hills"][2], 0.05 * terrain["continent"][3] + terrain["hills"][1] + terrain["detail"][1])


class PlanetWorld:
    """The planet, its moon and everything the planet pipeline draws of them.

    Settings, read when uploaded: `sun_elevation`, `sun_azimuth` (degrees,
    seen from the site), `density` (times the atmosphere's coefficients),
    `ozone`, `multiple_scattering`, `exposure` (upload_sky); `clouds`,
    `cloud_coverage`, `cloud_density` (upload_clouds); `wind_speed` (m/s, the
    clouds' drift); `shadows`; `texture_strength`, `texture_relief` (the
    ground's textures); `colour_mode` (an index into COLOUR_MODES);
    `frozen` (the level of detail kept as it is). `set_ocean` and
    `set_wireframe` switch those passes. The planet's terrain parameters
    are `planet.terrain`; `regenerate` builds it again with them.

    `time` is the world's clock in seconds, which places the moon;
    `physics` (orbits.Planet) has the planet's and the moon's gravity and
    air at that time."""

    def __init__(self, engine, fov=FOV, near=NEAR_PLANE, terrain=None, moon=True, pad=True):
        self.engine = engine
        self.fov = fov
        self.near = near
        self.pad = pad
        self._lod = None
        self.lod_version = 0                # counts changes to the level of detail, for TreeView's copies
        self.physics = orbits.Planet(radius=RADIUS, scale_height=RAYLEIGH_SCALE_HEIGHT)
        terrain = terrain or TERRAIN
        self.planet = Body(0, "planet", RADIUS, {name: list(value) for name, value in terrain.items()}, self,
                           ocean=True)
        self.bodies = [self.planet]
        self.with_moon = moon
        self.camera = None
        # The view, as end_frame was last given it.
        self.position = (0.0, 0.0, RADIUS + 5.0)
        self.forward = (0.0, 0.0, 1.0)
        self.viewpoint = self.position
        # Settings.
        self.sun_elevation, self.sun_azimuth = SUN_ELEVATION, SUN_AZIMUTH
        self.density = 1.0
        self.ozone = True
        self.multiple_scattering = True
        self.exposure = EXPOSURE
        self.shadows = True
        self.clouds = True
        self.cloud_coverage = CLOUD_COVERAGE
        self.cloud_density = 0.04
        self.cloud_wind = (0.0, 0.0, 0.0)   # the coverage noise's offset, in its units (clouds.glsl)
        self.wind_speed = 15.0
        self.ground_textures = False        # whether the ground's textures were loaded
        self.texture_strength = 1.0
        self.texture_relief = 1.0
        self.ocean = True
        self.wireframe = False
        self.colour_mode = 0
        self.frozen = False
        # State.
        self.frame = 0
        self.terrain_version = 0           # counts terrain changes; probe results carry the version they were made with
        self.ground = None                 # terrain height under the viewpoint on the body under it, from the probes
        self.site_height = None            # natural terrain height at the site, from the probes
        self.patch = None                  # (body, patch) whose heights came back last
        self.probe_requests = {}           # stamp: (terrain version, body under the viewpoint, patch, extra probes)
        self.extra_probes = []             # (direction, body) to measure the terrain's height at
        self.extra_heights = []            # ((direction, body), height) for the extra probes last read back
        self.probe_latency = 3.0           # frames from asking for probes to their heights coming back
        self.horizon_sun = None            # the sun's direction the chunks' horizons are measured towards
        self.chunks_culled = 0             # selected chunks not drawn: out of view or behind the horizon
        self.ocean_chunks = 0              # chunks the ocean draws
        self.index_count = 0

    # ── Clock and bodies ──────────────────────────────────────────────────

    @property
    def lod(self):
        """The level of detail (sk.PlanetLod), made when first asked for."""
        if self._lod is None:
            import shoonyakasha
            self._lod = shoonyakasha.PlanetLod(SLOTS, MAX_JOBS, VERTICES_PER_CHUNK, SKIRT_FRACTION)
            self._lod.set_face_colours(FACE_COLOURS)
        return self._lod

    @property
    def pool(self):
        return PoolView(self.lod)

    @property
    def time(self):
        return self.physics.time

    @time.setter
    def time(self, value):
        self.physics.time = value

    @property
    def moon(self):
        return next((body for body in self.bodies if body.name == "moon"), None)

    def body_under(self, point):
        """The body `point` is reckoned around, as orbits.reference reckons
        orbits: the body with an orbit whose sphere of influence holds it,
        else the planet."""
        for body in self.bodies:
            if body.orbit is not None and length(body.relative(point)) < body.orbit.influence:
                return body
        return self.planet

    def radial_up(self, point):
        """Up at `point`: away from the centre of the body under it."""
        return normalize(self.body_under(point).relative(point))

    def altitude(self, point):
        """Height of `point` above the sphere of the body under it (on the
        planet, sea level)."""
        body = self.body_under(point)
        return length(body.relative(point)) - body.radius

    def surface_height(self, point):
        """The height of what is under the viewpoint (`ground`), for `point`
        on the body the viewpoint is over: the ground, or the sea where the
        ground is below sea level on a body with an ocean."""
        ground = self.ground or 0.0
        return max(ground, 0.0) if self.body_under(point).ocean else ground

    def height_above_ground(self, point):
        """Height of `point` above what is under the viewpoint."""
        return self.altitude(point) - self.surface_height(point)

    def datum_name(self, point):
        """What altitude() is measured from: sea level, or on a body without
        a sea, its radius."""
        return "sea level" if self.body_under(point).ocean else "the radius"

    def keep_above_ground(self, point, clearance):
        """`point`, raised to `clearance` above what is under the viewpoint
        when it is below that."""
        body = self.body_under(point)
        floor = body.radius + self.surface_height(point) + clearance
        offset = body.relative(point)
        if length(offset) < floor:
            return add(body.centre(), scale(normalize(offset), floor))
        return point

    # ── The site and the sun ──────────────────────────────────────────────

    def site_frame(self):
        """The site's point on the sphere (height 0), its up, east and north."""
        up = cube_sphere.direction(SITE_FACE, SITE_UV, SITE_UV)
        east, north = local_frame(up)
        return scale(up, RADIUS), up, east, north

    def site_ground(self):
        """The site's point on the ground, once its height is known (else on the sphere)."""
        origin, up, _, _ = self.site_frame()
        return add(origin, scale(up, self.site_height or 0.0))

    def sun_direction(self):
        """Towards the sun, from the sun's elevation and azimuth seen from the site."""
        _, up, east, north = self.site_frame()
        elevation, azimuth = math.radians(self.sun_elevation), math.radians(self.sun_azimuth)
        level = add(scale(east, math.cos(azimuth)), scale(north, math.sin(azimuth)))
        return normalize(add(scale(up, math.sin(elevation)), scale(level, math.cos(elevation))))

    # ── Set up ────────────────────────────────────────────────────────────

    def setup(self):
        """The camera, the pipeline's settings and buffers, the ground's
        textures and the moon; call from the engine's on_init."""
        engine = self.engine
        camera = engine.scene.create_entity("Camera")
        engine.scene.add_component(camera, "Camera")
        engine.scene.set_camera_main(camera, True)
        engine.scene.set_camera_fov(camera, self.fov)
        engine.scene.set_camera_near(camera, self.near)
        engine.scene.set_camera_reverse_z(camera, True)
        self.camera = camera
        self.upload_sky()
        self.upload_clouds()
        self.load_ground_textures()
        self.upload_terrain()
        self.upload_chunk_indices()
        engine.set_buffer("OrbitIndices", np.arange(ORBIT_POINTS, dtype=np.uint32))
        engine.set_buffer("OrbitDraws", np.uint32([0]))
        if self.with_moon:
            self.build_moon()
        radii = [body.radius for body in self.bodies]
        engine.set_custom_vec4("planet.radii", tuple(radii + [0.0] * (4 - len(radii))))

    def build_moon(self):
        """The moon (orbits.Moon) circling in the plane of the site's up and
        east, so that it passes over the site moving east, starting
        MOON_PHASE radians east of the site's zenith; drawn as body 1 with
        its cratered terrain (moon.glsl); its tree is made with the planet's
        (start_terrain)."""
        _, up, east, _ = self.site_frame()
        self.physics.moon = orbits.Moon(radius=MOON_RADIUS, surface_gravity=MOON_GRAVITY, distance=MOON_DISTANCE,
                                        phase=MOON_PHASE, u=up, v=east, primary_gm=self.physics.gm)
        self.bodies.append(Body(1, "moon", MOON_RADIUS, None, self, orbit=self.physics.moon,
                                floor=MOON_FLOOR, heights=MOON_HEIGHTS))

    def load_ground_textures(self):
        """Hand the ground's textures to the pipeline (ground_textures.py) and
        their mean albedos to GroundParams; without them the ground keeps its
        procedural colours."""
        means = ground_textures.load(self.engine)
        self.ground_textures = means is not None
        if means is None:
            print("ground textures: none in %s; run `python -m shoonyakasha.planet.ground_textures` to download them"
                  % ground_textures.DIRECTORY, flush=True)
            return
        self.engine.set_custom_vec4_array("planet.ground.means", [m + (0.0,) for m in means])

    # ── Uploads ───────────────────────────────────────────────────────────

    def upload_chunk_indices(self):
        """The index list every chunk shares (grid, then skirts drawn from
        both sides), as cube_sphere.chunk_mesh makes it."""
        _, _, _, _, indices = cube_sphere.chunk_mesh(4, 0, 0, 0, QUADS, RADIUS, skirt_depth=1.0)
        self.index_count = len(indices)
        self.engine.set_buffer("ChunkIndices", indices)

    def upload_terrain(self):
        """The terrain's parameters, and the pad around the site once its height is known."""
        engine = self.engine
        for name, value in self.planet.terrain.items():
            engine.set_custom_vec4("planet.terrain." + name, tuple(value))
        _, up, _, _ = self.site_frame()
        known = self.site_height is not None
        engine.set_custom_vec4("planet.terrain.site", up + (self.site_height if known else 0.0,))
        engine.set_custom_vec4("planet.terrain.siteShape",
                               (SITE_FLAT_RADIUS, SITE_BLEND_RADIUS, RADIUS, 1.0 if known and self.pad else 0.0))

    def upload_sky(self):
        """The sun's direction and the atmosphere's parameters."""
        engine = self.engine
        engine.set_custom_vec4("planet.sunDirection", self.sun_direction() + (0.0,))
        density = self.density
        engine.set_custom_vec4("planet.atmosphere.radii",
                               (RADIUS, RADIUS + ATMOSPHERE_TOP, RAYLEIGH_SCALE_HEIGHT, MIE_SCALE_HEIGHT))
        engine.set_custom_vec4("planet.atmosphere.rayleigh", scale(RAYLEIGH, density) + (MIE_G,))
        engine.set_custom_vec4("planet.atmosphere.mie", (MIE_SCATTERING * density, MIE_ABSORPTION * density,
                                                         SUN_ILLUMINANCE, self.exposure))
        ozone = density if self.ozone else 0.0
        engine.set_custom_vec4("planet.atmosphere.ozone", scale(OZONE_ABSORPTION, ozone) + (OZONE_PEAK,))
        engine.set_custom_vec4("planet.atmosphere.settings",
                               (OZONE_HALF_WIDTH, 1.0 if self.multiple_scattering else 0.0, 0.0, 0.0))
        self.physics.density_factor = density

    def upload_clouds(self):
        """The cloud layer's parameters (CloudParams, clouds.glsl)."""
        engine = self.engine
        engine.set_custom_vec4("planet.clouds.layer", CLOUD_LAYER + (self.cloud_coverage, self.cloud_density))
        engine.set_custom_vec4("planet.clouds.shape", CLOUD_SHAPE + (1.0 if self.clouds else 0.0,))
        engine.set_custom_vec4("planet.clouds.wind", self.cloud_wind + (0.0,))

    def set_ocean(self, on):
        self.ocean = on
        self.engine.set_pass_enabled("Ocean", on)

    def set_wireframe(self, on):
        self.wireframe = on
        self.engine.set_pass_enabled("Wireframe", on)

    def set_orbit_path(self, points):
        """Draw a line through `points` (planet space, at most ORBIT_POINTS)
        over the scene, depth-tested (OrbitPath); none when empty. Placed
        relative to the camera's position as end_frame last had it."""
        engine = self.engine
        if points:
            relative = np.zeros((len(points), 4), dtype=np.float32)
            relative[:, :3] = [sub(p, self.position) for p in points]
            engine.set_buffer("OrbitPoints", relative)
            # count, padding, then indexCount, instanceCount, firstIndex, vertexOffset, firstInstance
            engine.set_buffer("OrbitDraws", np.uint32([1, 0, 0, 0, len(points), 1, 0, 0, 0]))
        else:
            engine.set_buffer("OrbitDraws", np.uint32([0]))

    # ── The terrain ───────────────────────────────────────────────────────

    def start_terrain(self):
        """Once the site's natural height is known: the pad at that height, a
        tree to build the planet with it, and a tree for each other body that
        has none yet."""
        self.upload_terrain()
        self.terrain_version += 1
        self.ground = None
        self.patch = None
        for body in self.bodies:
            if body is self.planet or not self.lod.has_tree(body.index):
                self.lod.create_tree(body.index, body.radius, body.heights[0], body.heights[1], MAX_LEVEL,
                                     SPLIT_FACTOR, HEIGHT_PAD)
        self.lod_version += 1

    def regenerate(self):
        """Apply changed terrain parameters: every chunk is released, the
        site's height is measured again, and the planet is built again."""
        self.lod.drop_tree(self.planet.index)
        self.lod_version += 1
        self.site_height = None
        self.ground = None
        self.patch = None
        self.terrain_version += 1
        self.upload_terrain()

    def change_terrain(self, name, index, factor):
        """One terrain parameter times `factor`, and the planet built again."""
        self.planet.terrain[name][index] *= factor
        self.regenerate()

    def new_seed(self, seed):
        """The terrain's noise offset (three numbers), and the planet built again."""
        self.planet.terrain["seed"] = list(seed) + [0.0]
        self.regenerate()

    def settled(self):
        """The planet's tree made, no body's chunks missing, none still to
        build again for the sun, and every built chunk's heights back."""
        lod = self.lod
        return (lod.has_tree(self.planet.index)
                and not any(lod.missing(body.index) for body in self.bodies if lod.has_tree(body.index))
                and not lod.rebuild_queued and not lod.pending)

    # ── Per frame ─────────────────────────────────────────────────────────

    def begin_frame(self, viewpoint=None):
        """Read back the chunks' and the probes' heights, the ground's for
        `viewpoint` (where end_frame last had it without one), and start the
        terrain once the site's height is known. Returns True in the frame
        it starts."""
        if viewpoint is not None:
            self.viewpoint = viewpoint
        self.read_back_heights()
        if not self.lod.has_tree(self.planet.index) and self.site_height is not None:
            self.start_terrain()
            return True
        return False

    def end_frame(self, position, basis, dt, warp=1.0, viewpoint=None, patch=None):
        """The level of detail and the probes for the camera at `position`
        looking along `basis` (right, up, forward), the drawn chunks, the
        camera's turn, the shadow cascades and the ground textures' offsets
        for it, and the clouds blown on by `dt` seconds times `warp`.
        `viewpoint` is where the ground is probed (the camera's position
        without one); `patch` is (body, patch) to probe, or None."""
        right, up, forward = basis
        self.position = position
        self.forward = forward
        self.viewpoint = viewpoint if viewpoint is not None else position
        self.update_level_of_detail(patch)
        self.engine.set_custom_vec4("planet.cameraFromCentre", position + (0.0,))
        self.upload_drawn_chunks()
        self.engine.scene.set_rotation(self.camera, euler_from_basis(right, up, forward))
        self.upload_shadow_cascades(forward)
        self.upload_ground()
        self.blow_clouds(dt * warp)

    def read_back_heights(self):
        """Chunk heights into the trees' nodes, and the probes' heights when
        they were made with the current terrain version: the ground under the
        viewpoint (when it is still over the body it was measured on), the
        site's natural height, and the patch. Each probe result carries the
        stamp of the frame that asked for it, which also gives the probes'
        latency."""
        engine = self.engine
        readback = engine.get_readback("ChunkHeights")
        if readback is not None:
            self.lod.take_heights(readback["data"])
            self.lod_version += 1
        readback = engine.get_readback("ProbeHeights")
        if readback is None:
            return
        probes = np.frombuffer(readback["data"], dtype=np.float32).reshape(PROBES, 4)
        stamp = float(probes[0, 1])
        requests = self.probe_requests
        request = requests.get(stamp)
        for old in [s for s in requests if s <= stamp]:
            del requests[old]
        if request is None or request[0] != self.terrain_version:
            return
        self.probe_latency = self.frame + 1 - stamp
        _, ground_body, patch, extra = request
        self.ground = float(probes[0, 0]) if ground_body is self.body_under(self.viewpoint) else None
        if self.site_height is None:
            self.site_height = float(probes[1, 0])
        if patch is not None:
            patch[1].set_heights(probes[2:2 + len(patch[1].directions), 0])
            self.patch = patch
        first = PROBES - EXTRA_PROBES
        self.extra_heights = [(d, float(h)) for d, h in zip(extra, probes[first:first + len(extra), 0])]

    def in_view(self, centre, radius):
        """Whether a sphere around `centre` (planet space) is roughly in view: a
        cone around the camera's forward direction that holds the screen."""
        to_centre = sub(centre, self.position)
        distance = length(to_centre)
        if distance <= radius:
            return True
        width, height = self.engine.framebuffer_size
        aspect = width / height if width > 0 and height > 0 else 16.0 / 9.0
        half_diagonal = math.atan(math.tan(math.radians(self.fov) * 0.5) * math.sqrt(1.0 + aspect * aspect))
        angle = math.acos(max(-1.0, min(1.0, dot(to_centre, self.forward) / distance)))
        return angle - math.asin(min(1.0, radius / distance)) < half_diagonal

    def lod_view(self):
        """The view for sk.PlanetLod: (position, forward, fov, aspect)."""
        width, height = self.engine.framebuffer_size
        aspect = width / height if width > 0 and height > 0 else 16.0 / 9.0
        return (self.position, self.forward, self.fov, aspect)

    def lod_bodies(self):
        """The bodies for sk.PlanetLod: (index, centre, the radius their terrain stays above, ocean)."""
        return [(body.index, body.centre(), body.radius + terrain_floor(body), body.ocean) for body in self.bodies]

    def sun_moved(self):
        """Whether the sun has moved more than HORIZON_SUN_STEP from the
        direction the chunks' horizons were measured towards
        (chunk_build.comp); if so the new direction is handed over to measure
        towards."""
        sun = self.sun_direction()
        if self.horizon_sun is None or dot(sun, self.horizon_sun) < math.cos(HORIZON_SUN_STEP):
            self.horizon_sun = sun
            self.engine.set_custom_vec4("planet.horizonSun", sun + (0.0,))
            return True
        return False

    def update_level_of_detail(self, patch=None):
        """Each body's tree updated for the camera, its missing chunks built,
        those in view first, and its unused ones released (sk.PlanetLod);
        when the sun has moved, every built chunk built again, nearest first,
        in the jobs new chunks leave over; then this frame's jobs and probes:
        the ground under the viewpoint on the body under it, the site, the
        patch's directions and the extra probes."""
        self.frame += 1
        lod = self.lod
        requeue = False
        if any(lod.has_tree(body.index) for body in self.bodies) and not self.frozen:
            requeue = self.sun_moved()
        lod.update(self.lod_view(), self.lod_bodies(), self.frame, self.frozen, requeue)
        self.lod_version += 1
        _, site_up, _, _ = self.site_frame()
        ground_body = self.body_under(self.viewpoint)
        probes = [(normalize(ground_body.relative(self.viewpoint)), ground_body), (site_up, self.planet)]
        probes += [(d, patch[0]) for d in patch[1].directions[:PATCH_PROBES]] if patch is not None else []
        extra = list(self.extra_probes[:EXTRA_PROBES])
        unused = ((0.0, 1.0, 0.0), self.planet)
        probes += [unused] * (PROBES - EXTRA_PROBES - len(probes))
        probes += extra + [unused] * (EXTRA_PROBES - len(extra))
        stamp = float(self.frame)
        self.probe_requests[stamp] = (self.terrain_version, ground_body, patch, extra)
        self.submit_jobs([tuple(d) + (float(body.index),) for d, body in probes], stamp)

    def submit_jobs(self, probes, stamp):
        """Hand this frame's jobs, the probes (direction and body index) and
        the stamp their results carry back to BuildChunks, and start the next
        frame's list."""
        engine = self.engine
        jobs = self.lod.jobs()
        padded = jobs + [((0.0,) * 4, (0.0,) * 4)] * (MAX_JOBS - len(jobs))
        engine.set_custom_vec4_array("planet.jobA", [a for a, _ in padded])
        engine.set_custom_vec4_array("planet.jobB", [b for _, b in padded])
        engine.set_custom_uint("planet.jobCount", len(jobs))
        engine.set_custom_vec4_array("planet.probe", probes)
        engine.set_custom_float("planet.probeStamp", stamp)
        self.lod.clear_jobs()

    def upload_drawn_chunks(self):
        """One indexed indirect draw per chunk the bodies' trees selected that
        is in view and not behind its body's horizon, and each drawn slot's
        centre relative to the camera (its body's centre plus its own, less the
        camera, in double), colour, and its parent's split distance and the
        levels above it, for morph.glsl. The shadow cascades draw every
        selected chunk within their reach, in view or not (sk.PlanetLod)."""
        lod = self.lod
        lod.write_draws(self.lod_view(), self.lod_bodies(), self.colour_mode, SHADOW_SPLITS[-1] + SHADOW_REACH,
                        self.index_count)
        lod.upload(self.engine)
        self.chunks_culled = lod.culled
        self.ocean_chunks = int(np.frombuffer(lod.draws("ocean"), dtype=np.uint32, count=1)[0])

    def upload_shadow_cascades(self, forward):
        """The sun's shadow cascades (shadows.py) for the view along `forward`,
        switched off with `shadows` or while the sun is below the horizon at
        the camera."""
        engine = self.engine
        sun = self.sun_direction()
        on = self.shadows and dot(sun, normalize(self.position)) > -0.1
        width, height = engine.framebuffer_size
        aspect = width / height if width > 0 and height > 0 else 16.0 / 9.0
        cascades = shadows.fit_cascades(self.position, forward, sun, self.fov, aspect, SHADOW_SPLITS,
                                        SHADOW_RESOLUTION, SHADOW_REACH)
        engine.set_custom_mat4_array("planet.shadow.viewProj", [c.matrix for c in cascades])
        engine.set_custom_vec4("planet.shadow.splits", tuple(c.far for c in cascades))
        engine.set_custom_vec4("planet.shadow.texel", tuple(c.texel for c in cascades))
        engine.set_custom_vec4("planet.shadow.settings", (1.0 if on else 0.0, float(SHADOW_RESOLUTION), 0.0, 0.0))

    def upload_ground(self):
        """GroundParams' tiling offsets for the camera's position relative to
        each body's centre (in double, less their whole tiles) and its
        settings."""
        offsets = []
        for body in self.bodies:
            relative = sub(self.position, body.centre())
            for size in GROUND_SCALES:
                offsets.append(tuple(v / size - math.floor(v / size) for v in relative) + (0.0,))
        self.engine.set_custom_vec4_array("planet.ground.offsets", offsets + [(0.0,) * 4] * (4 - len(offsets)))
        strength = self.texture_strength if self.ground_textures else 0.0
        self.engine.set_custom_vec4("planet.ground.settings", (strength, self.texture_relief) + GROUND_SCALES)

    def blow_clouds(self, seconds):
        """Move the clouds' weather with the wind for `seconds`: `wind_speed`
        metres a second along the coverage noise's x, in its units (a unit is
        RADIUS / its frequency metres)."""
        step = self.wind_speed * seconds * CLOUD_SHAPE[0] / RADIUS
        wind = self.cloud_wind
        self.cloud_wind = (wind[0] + step, wind[1], wind[2])
        self.engine.set_custom_vec4("planet.clouds.wind", self.cloud_wind + (0.0,))
