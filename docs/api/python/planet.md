# Planets: `shoonyakasha.planet`

A planet and its moon with procedural terrain on the planet pipeline (`shoonyakasha/pipelines/planet/`): a cube-sphere with quadtree level of detail whose chunks a compute pass builds, an atmosphere, clouds, an ocean, sun shadows and ground textures. The [planet example](../../../examples/python/simulations/planet/README.md) flies a ship over it; the [planet explorer](../../../examples/python/simulations/planet_explorer/README.md) looks at it like a globe or a planetarium. Needs numpy, Pillow (which bakes the vegetation's textures), and a GPU with 64-bit float shaders (`shaderFloat64`).

| Module | |
|---|---|
| `world` | `PlanetWorld`: the bodies, the probes, the sky, and what the pipeline is handed each frame. `PIPELINE`, `compile_shaders()`, and the constants below |
| `cameras` | `GlobeCamera`, `FocusCamera`, `cursor_ray`, `ray_sphere` |
| `settings` | Settings-menu items for a `PlanetWorld` (for `toolkit.overlay.SettingsMenu`), `TerrainEdit` and `next_palette` |
| `biomes` | The biomes as the shaders classify them: `NAMES`, `weights(temperature, moisture)`, `dominant`, `snowy`, `card_rows(climate)`, and the `PALETTES` |
| `orbits` | `Planet` (gravity and air), `Moon` (a circular orbit on rails), `reference`, `orbit`, `orbit_path`, `circular_speed` |
| `vec` | Vectors as tuples: `add`, `sub`, `scale`, `dot`, `cross`, `length`, `normalize`, `rotate`, `local_frame`, `euler_from_basis` |
| `bubble` | `Bubble`: a frame for rigid bodies (`sk.PlanetPhysics`) near a point on the planet or the moon, with gravity towards the body's centre and rebasing |
| `collider_probes` | The terrain under a bubble as a mesh in its physics: `ColliderPatch`, `ColliderProbes` (the `ColliderProbes` pass's requests and results), `TerrainCollider` |
| `cube_sphere`, `quadtree`, `shadows` | The cube-sphere's faces and chunk meshes, the quadtree in Python (the level of detail `sk.PlanetLod` does in C++), the shadow cascades' fitting |
| `ground_textures` | The ground's textures: download (`python -m shoonyakasha.planet.ground_textures`) and `load(engine)` |
| `vegetation` | The vegetation's meshes of bark and foliage cards, impostor quads, grass and rocks (`meshes(impostor_rects)`, `trees()`, `impostor_width`, `rock_points(name)`, `rock_triangles(name)`), and their draws (`draw_template(ranges, firsts)`) |
| `rocks` | The rocks the scatter leaves out (`TakenRocks`), and the rocks' keys, colours and density |
| `nearby_trees` | The trees, cacti and rocks the scatter places near the camera, read back (`NearbyTrees`) |
| `vegetation_textures` | The foliage atlas and bark tile, baked from CC0 scans or drawn: download (`python -m shoonyakasha.planet.vegetation_textures`), `atlas()`, `card_rect(name)`, `bark()` |
| `vegetation_impostors` | The trees' impostors drawn from their meshes: `bake(trees, atlas, bark)` |

Importing these modules does not require the engine's extension; `PlanetWorld` takes the engine as an argument.

## A program on a `PlanetWorld`

```python
import shoonyakasha as sk
from shoonyakasha.planet import cameras, world as planet_world

planet_world.compile_shaders()
engine = sk.Engine(width=1600, height=900, pipeline_json_path=str(planet_world.PIPELINE))
world = planet_world.PlanetWorld(engine)
camera = None


def frame(dt):
    world.begin_frame(camera.position)
    world.time += dt
    camera.update(dt, active=not engine.ui.pointer_over_ui)
    world.end_frame(camera.position, camera.basis, dt)
    return True


def on_init():
    global camera
    world.setup()
    camera = cameras.GlobeCamera(world, range_=300_000.0)
    engine.ecs.add_system("Planet", frame, priority=-1)


engine.set_on_init(on_init)
engine.input.set_on_mouse_scroll(lambda x, y: camera.scroll(y))
engine.run()
```

`compile_shaders()` compiles the pipeline's shaders that changed since their SPIR-V was built; without glslc it leaves the SPIR-V shipped with the package.

## `PlanetWorld(engine, fov=70.0, near=0.05, terrain=None, moon=True, pad=True)`

Positions are Python floats in metres, with the planet's centre at the origin. The engine's camera (`camera`, made by `setup`) stays at its own origin and only turns; every chunk is placed at its position less the camera's, in double. The projection is reverse-Z with an infinite far plane.

| Call | |
|---|---|
| `setup()` | From `on_init`: the camera entity, the sky, clouds and terrain settings, the chunks' index buffer, the ground's textures (when downloaded), the vegetation's textures, impostors and meshes (the rocks' too) and, with `moon`, the moon |
| `begin_frame(viewpoint=None)` | The chunks' and probes' heights read back, with `viewpoint` where the ground is measured; the terrain started once the site's height is known. Returns True in the frame it starts |
| `end_frame(position, basis, dt, warp=1.0, viewpoint=None, patch=None)` | The level of detail and the probes for the camera at `position` with `basis` (right, up, forward), the drawn chunks, the camera entity's rotation, the shadow cascades and the ground textures' offsets, the climate settings, the temporal anti-aliasing's jitter and the camera's motion since the frame before, and the clouds blown on by `dt` times `warp` seconds. `patch` is (body, patch) to probe: an object with `directions` (up to `PATCH_PROBES`) and `set_heights(heights)` |
| `set_orbit_path(points)` | A line through up to `ORBIT_POINTS` points (planet space) over the scene, depth-tested; none when empty |
| `upload_sky()`, `upload_clouds()`, `upload_terrain()` | Hand changed settings to the pipeline |
| `set_ocean(on)`, `set_wireframe(on)` | Those passes on or off |
| `regenerate()` | The planet built again with `planet.terrain`: every chunk released and the site measured again; counted in `regenerations` |
| `change_terrain(name, index, factor)`, `new_seed(seed)` | One terrain parameter times `factor`, or a new noise offset (three numbers), and the planet built again |
| `settled()` | The planet's tree made, no chunk missing, none still to build again for the sun, every built chunk's heights back |
| `body_under(point)` | The body `point` is reckoned around: the moon within its sphere of influence, else the planet |
| `altitude(point)`, `height_above_ground(point)`, `surface_height(point)`, `datum_name(point)` | Heights above the body's sphere (sea level on the planet) and above the ground measured under the viewpoint (the sea where that is below sea level) |
| `keep_above_ground(point, clearance)` | `point` raised to `clearance` above that ground when it is below |
| `site_frame()`, `site_ground()`, `sun_direction()` | The site on the +Z face (its point, up, east and north), its point on the ground, and the sun's direction from its elevation and azimuth seen there |
| `in_view(centre, radius)` | Whether a sphere is roughly in the camera's view |
| `with_climate_settings(climate)` | A probe's (temperature, moisture) with the climate settings' offsets, the moisture clamped to 0 to 1; None stays None |

| Attribute | |
|---|---|
| `planet`, `moon`, `bodies` | The bodies (`Body`: `index`, `name`, `radius`, `terrain` (the planet's parameters), `orbit`, `ocean`, `tree`, `centre()`, `velocity()`, `relative(point)`). `tree` is a `TreeView`, or None until the body's tree is made: `selected` (the nodes drawn), `missing` ((distance, node), nearest first) and `nodes()`, as `NodeInfo` copies (`face`, `level`, `x`, `y`, `chunk` (its slot or None), `centre`, `edge`, `low`, `high`, `measured`, `bound_centre`, `bound_radius`, `key()`), and `split_factor` and `max_level` (the deepest level it splits to, `MAX_LEVEL` at the start), which can be set |
| `time` | The world's clock in seconds; it places the moon |
| `physics` | The `orbits.Planet` with the moon, at `time`: gravity, air density (scaled by `density`) and orbits |
| `sun_elevation`, `sun_azimuth`, `density`, `ozone`, `multiple_scattering`, `exposure` | The sky (`upload_sky`) |
| `clouds`, `cloud_coverage`, `cloud_density`, `wind_speed` | The clouds (`upload_clouds`; the wind is read each frame) |
| `vegetation`, `vegetation_density`, `vegetation_range` | Read each frame: the vegetation on or off, its chances' scale (0 to 1) and the trees' and bushes' range in metres (2,500 at the start) |
| `rocks`, `taken_rocks` | Read each frame: the rocks on or off, and the cells whose rocks the scatter leaves out (`rocks.TakenRocks`; `regenerate` puts the planet's back) |
| `grass`, `grass_range` | Read each frame: the grass on or off and its range in metres (150 at the start) |
| `temperature_offset`, `moisture_offset`, `flora_hue`, `soil_hue` | Read each frame: degrees C added to the climate's temperature, added to its moisture, and the plants' and the soils' colours turned around the colour wheel in degrees (see Biomes) |
| `shadows`, `texture_strength`, `texture_relief`, `colour_mode`, `taa`, `frozen` | Read each frame: the shadow cascades on or off, the ground textures' strength and normal maps, the colour mode (an index into `COLOUR_MODES`: terrain, face, level, biome), temporal anti-aliasing on or off (its jitter is `taa_jitter(frame, size)`; the history is dropped when the camera moves more than `TAA_RESET_DISTANCE` in a frame), and the level of detail kept as it is |
| `ground`, `ground_climate`, `site_height`, `patch`, `extra_probes`, `extra_heights`, `extra_climates`, `probe_latency` | The probes: the ground under the viewpoint (kept only while the viewpoint is within 1 km of where it was measured) and on the planet its (temperature, moisture) with the climate settings' offsets, the site's height, the last patch back, the up to `EXTRA_PROBES` (direction, body) pairs to measure, their heights and their (temperature, moisture) without the offsets (None off the planet), and the frames they take |
| `lod`, `pool`, `chunks_culled`, `ocean_chunks`, `frame` | The level of detail (`sk.PlanetLod`), the chunk pool's `slots`, `used` and `pending` (chunks waiting for their heights), and this frame's counts |

The terrain's parameters, the atmosphere, the clouds, the shadow cascades and the pool's sizes are the module's constants (`TERRAIN`, `RADIUS`, `MAX_LEVEL`, `SPLIT_FACTOR`, `CLOUD_LAYER`, `SHADOW_SPLITS`, `SLOTS`, `MAX_JOBS`, `PROBES` and the others); those the pipeline's buffers and shaders depend on match `pipeline.json` and the shaders. With `pad`, the terrain is flat within 150 m of the site, at its natural height, easing back by 600 m.

## `sk.PlanetLod`

The level of detail in C++ (`include/Planet/PlanetLod.h`): per body a quadtree over the six cube faces, the pool's slots and the jobs that build chunks into them (each with a stamp the heights read back carry), the chunks waiting for their heights, the chunks to build again for a new sun, and the draw lists with each slot's instance values. `PlanetWorld` drives it; it does what `quadtree.py` and the pool did in Python, the same to the bit (`tests/python/test_planet_native_lod.py`).

| Call | |
|---|---|
| `PlanetLod(slots, max_jobs, vertices_per_chunk, skirt_fraction, evict_frames=120, merge_ratio=1.25)` | An empty pool of `slots` |
| `create_tree(body, radius, low, high, max_level, split_factor, height_pad)`, `drop_tree(body)`, `has_tree(body)` | A body's tree, its six roots given slots and jobs; dropped with its chunks released |
| `update(view, bodies, frame, frozen=False, requeue=False)` | Unless `frozen`, each tree updated for the view, its missing chunks built (in view first, nearest first) and unused subtrees released; with `requeue` every built chunk queued to be built again, then the queue worked through with the jobs left. A view is (position, forward, fov degrees, aspect); bodies are (index, centre, the radius their terrain stays above, ocean) |
| `jobs()`, `clear_jobs()` | This frame's jobs, ((face, level, x, y), (slot, skirt depth, stamp, body)) each |
| `take_heights(data)` | `ChunkHeights` read back: (lowest, highest, stamp, unused) float32 per slot |
| `write_draws(view, bodies, colour_mode, shadow_reach, index_count)`, `upload(engine)` | The draw lists and instances for the view; handed to `ChunkInstances`, `ChunkDraws`, `OceanDraws` and `ShadowDraws` |
| `set_face_colours(colours)`, `split_factor(body)`, `set_split_factor(body, value)`, `max_level(body)`, `set_max_level(body, level)` | Colour mode 1's six colours; a tree's split factor and deepest level (lowered, the deeper chunks go as unused ones do) |
| `selected(body)`, `nodes(body)`, `missing_nodes(body)`, `missing(body)`, `selected_count(body)` | The trees' state, nodes as dicts |
| `instances()`, `draws("chunk" / "ocean" / "shadow")`, `culled`, `slots`, `used`, `pending`, `rebuild_queued` | The buffers as bytes, and counts |

## Physics

`sk.PlanetPhysics(max_bodies=4096, threads=-1)` holds rigid bodies on Jolt Physics in single precision (`include/Planet/PlanetPhysics.h`), stepped on `threads` worker threads (-1: up to four, one fewer than the hardware has). It has no gravity of its own; moving bodies lose 0.05 of their velocity a second (Jolt's linear damping) until `set_linear_damping` changes it. Positions are metres in the caller's frame, rotations unit quaternions (w, x, y, z), motions `"static"`, `"kinematic"` or `"dynamic"`. Static bodies, ground meshes among them, collide with moving ones only. Ground meshes are fine or coarse, and a moving body near (the start) or far (`set_far`): near bodies collide with the fine ground, far ones with the coarse. Bodies are int ids; a call with an id that is not in the world raises `ValueError`.

Characters are capsules moved by their velocity (Jolt's `CharacterVirtual`), each with its own up, set by the caller. A character's position is the bottom of its capsule; it stands on what it touches below the middle of its lower cap. Each carries a kinematic inner body that moving bodies collide with. Characters have int ids of their own.

| Call | |
|---|---|
| `create_box(half_extents, position, rotation=(1, 0, 0, 0), motion="dynamic", density=500.0, mass=0.0, friction=0.7, restitution=0.1)` | A box; its mass `mass` kg when above 0, else from `density` (kg/m³) |
| `create_sphere(radius, position, motion="dynamic", density=500.0, mass=0.0, friction=0.7, restitution=0.1)` | A sphere |
| `create_capsule(radius, half_height, position, rotation=(1, 0, 0, 0), motion="dynamic", density=500.0, mass=0.0, friction=0.7, restitution=0.1)` | A capsule along its Y axis: a cylinder `half_height` either side of its centre, capped by half spheres of `radius` |
| `create_grid_mesh(points, columns, rows, position, friction=0.8, coarse=False)` | A static triangle mesh from a grid of float32 points (x, y, z, row by row) relative to `position`; each cell is two triangles facing the side cross(next column − point, next row − point) points to. Fine ground, or with `coarse` coarse ground |
| `set_linear_damping(body, damping)` | The share of a moving body's velocity it loses a second |
| `set_far(body, far=True)`, `is_far(body)` | Whether a moving body is far (on the coarse ground) or near (on the fine); static bodies stay as they are, and `set_motion` makes a moving body near |
| `create_convex_hull(points, position, rotation=(1, 0, 0, 0), motion="dynamic", density=500.0, mass=0.0, friction=0.7, restitution=0.1)` | The convex hull of float32 points (x, y, z each) relative to `position` |
| `remove(body)`, `contains(body)` | |
| `move_kinematic(body, position, rotation, dt)` | A kinematic body moved there over the next step of `dt` seconds |
| `get_transform(body)`, `set_transform(body, position, rotation, activate=True)` | (position, rotation) |
| `get_velocity(body)`, `set_velocity(body, linear, angular=(0, 0, 0))` | (linear m/s, angular rad/s) |
| `add_force(body, force)`, `add_impulse(body, impulse)` | A force (N) for the next step, or an impulse (N s), through the centre of mass; both wake the body |
| `set_motion(body, motion)`, `motion(body)` | The motion, with the collisions of the new motion; turned kinematic or dynamic, the body wakes and is near |
| `set_pass_through(body, character=None)` | The body passes through the character's capsule and inner body, and they through it; None: through no character. A body passes through one character at a time |
| `mass(body)`, `is_active(body)`, `activate(body)` | Mass in kg (0 when static); whether it is awake |
| `get_transforms(bodies)` | The bodies' positions and rotations as bytes, 7 float64 each (x, y, z, w, qx, qy, qz), in their order |
| `cast_ray(origin, direction, max_distance, skip_character=None)` | (body, point, normal, fraction) of the nearest surface a near body would hit (not the coarse ground) along the unit `direction`, or None; with `skip_character`, through that character and the bodies passing through it |
| `watch_strikes(body, watched=True)`, `set_strike_momentum(momentum)`, `take_struck()` | Whether a dynamic body striking `body` is reported; the momentum (kg m/s, along the contact's normal, 300 at first) a strike needs; the watched bodies struck since the last call, each once |
| `apply_central_gravity(centre, gm)` | For the next step, each awake dynamic body pulled towards `centre` with gm / r² |
| `step(dt)`, `shift_origin(offset)` | One step; every body moved by −`offset` without waking any |
| `body_count`, `active_count` | Bodies, and awake bodies |
| `create_character(position, up, radius=0.3, height=1.8, mass=80.0, max_slope_deg=50.0)`, `remove_character(character)` | A character with its feet at `position`; it pushes bodies as a `mass` kg body and walks on slopes up to `max_slope_deg` |
| `character_set_up(character, up)` | Its up, and its capsule turned along it |
| `character_position(character)`, `character_set_position(character, position)`, `character_velocity(character)`, `character_set_velocity(character, velocity)` | Its feet and velocity |
| `character_update(character, dt, gravity)` | Moved by its velocity over `dt` seconds, sliding along what it touches, stepping up onto steps up to 0.4 m and kept on the floor down to 0.5 m below it while it was on ground (both along its up); `gravity` is how it pushes what it stands on |
| `character_ground(character)` | (state, normal, ground velocity, body or None): state `"on_ground"`, `"on_steep_ground"`, `"not_supported"` or `"in_air"` |

`bubble.Bubble(physics, body, gm, origin)` is a frame for those bodies on `body` (a `PlanetWorld` body) whose GM is `gm`. Bubble coordinates are positions relative to the body's centre minus `origin` (body-relative); the axes are the body's, so on the moon the frame moves with it. `to_bubble(point)` and `to_world(p)` convert from and to planet space, `body_relative(p)`, `up(p)` and `gravity(p)` give the point from the body's centre, the way up there and gravity's acceleration. `step(dt)` applies gravity towards the body's centre and steps the physics. `maybe_rebase(focus)` moves the origin to `focus` (bubble coordinates) once it is `REBASE_DISTANCE` (500 m) or more away: `rebase(offset)` shifts every body by −`offset` and calls each of `listeners` with it. `anchor`, `east` and `north` are the frame of the plane the terrain collider's grid lies in; the anchor moves to the origin once the origin is more than `ANCHOR_REACH` (1 km) from it. `RADIUS` is the bubble's reach, 2 km from the origin.

`collider_probes` builds the terrain under a bubble as a mesh body. `ColliderPatch(body, anchor, east, north, cell, columns=65, spacing=0.5)` is a grid of directions in the plane tangent to the body's sphere at `anchor`, centred on grid cell `cell` of that plane, so patches of one anchor share their directions where they overlap; `set_heights(heights)` (below sea level counts as sea level on a body with a sea), `points()`, `offset(point)`, `covers(point, margin=0.0)`, `triangle(point)` (a corner and the unit normal of the grid triangle under it), `normal(point)` and `height(point)` (where the line from the body's centre through it meets that triangle) take points relative to the body's centre. `ColliderProbes(engine)` hands a patch's directions (up to 4,225) to the pipeline's `ColliderProbes` pass with a stamp (`request(patch, version)`; one a frame: a second request in a frame returns False and asks nothing), takes the heights back by stamp some frames later (`read(version)`, the same patch to every caller in a frame, dropping those asked for under another terrain version), and tells the pass to measure nothing in frames without a request (`end_frame()`). `TerrainCollider(bubble, probes, columns=65, spacing=0.5, friction=0.8, coarse=False)` keeps one patch as a static mesh body (coarse ground with `coarse`); several can share the probes: `update(focus, velocity, latency, version)` builds a patch whose heights came back and asks for a new one, centred ahead of `focus` by `velocity` times `latency` seconds, when that point is more than a quarter of the patch's width from its centre or the terrain version changed; the mesh in use stays until then. `covers(p, margin=0.0)` takes bubble coordinates; `drop()` removes the mesh.

`nearby_trees.NearbyTrees(engine)` lists the trees, cacti and rocks near the camera, which the `VegetationScatter` pass appends to the `NearbyTrees` buffer as it places them (only on the chunks drawn that frame, up to `CAPACITY`, 1,024). `request(camera, radius, centres)` asks for those within `radius` metres of `camera` (planet space) this frame, with a stamp, `centres` being the bodies' centres (planet space, by index) that frame; `read()` gives the list that came back some frames later, or None. Each `NearbyTree` has its `key` ((body, grid, cube face, cell u, cell v), the same every frame), `kind` (a name of `vegetation.KINDS`), `base` (from its body's centre as it was that frame, on the drawn surface), `height`, `width` (the factor `vegetation.glsl` widens it by), `turn` (cosine and sine about its up) and `tint`; `body` is its body's index, `rock` whether it is a rock, and `trunk` (radius, height) of a plant's trunk in metres, from `TRUNKS`, the share of the plant's height its mesh's trunk has. `listed_keys(data)` gives (stamp, keys) of a readback's bytes.

Rocks: with `rocks` on, the scatter also places small rocks (on their own grid, within `ROCK_RANGE`) and boulders (on the trees' grid where no tree or bush grows, within the bushes' range) on the planet, by their biomes and slope, and on the moon, by slope. `rocks.TakenRocks(engine)` holds the keys of rocks the scatter leaves out: `add(key)` (False once `MAX_TAKEN` are held), `discard(key)`, `clear()`, `clear_body(index)`, `key in taken`, `len(taken)`; `upload()` sends its table to the `TakenRocks` buffer when it changed and gives its count. `rock_grid(kind)` is a rock kind's grid (1 for "rock", 0 for "boulder"); `ROCK_COLOURS` (by body index), `tinted(colour, tint)` and `DENSITY` (2,600 kg/m³) give a rock's colour as drawn and its density. `vegetation.rock_points(name)` gives a rock kind's points for a convex hull and `rock_triangles(name)` its triangles (positions, normals, shades), one unit tall above its base and reaching `ROCK_SINK` below it.

## Biomes

Each of the planet's chunk vertices carries its climate (`climate.glsl`): a temperature that falls from 30 degrees C at the equator to -14 at the poles and by 8.5 degrees per kilometre of height, a moisture that is high at the equator and near 60 degrees of latitude, low near 30 degrees and at the poles and lower inland, and a cover noise about 400 m across; each with noise. The shaders (`biome.glsl`) add `temperature_offset` and `moisture_offset` and weigh eight biomes by temperature and moisture: desert, savanna, tropical forest, steppe, grassland, deciduous forest, taiga and tundra, blending at the edges between them. The biomes colour the ground and decide what grows: snow lies below -6 degrees, sand on beaches, rock on steep slopes. `biomes.weights` and `biomes.dominant` classify as the shaders do; `biomes.card_rows(world.ground_climate)` names the biome under the viewpoint.

`flora_hue` turns the plants' colours and the forests' and grass's on the ground around the colour wheel, `soil_hue` the bare ground's, keeping their brightness; `biomes.PALETTES` are named pairs of them (Earth, Crimson, Violet, Amber, Teal). The colour mode "biome" shows each biome in a flat colour.

## Vegetation

Trees (spruces, pines, two broadleaf trees, acacias), bushes, shrubs and cacti grow on the planet within `vegetation_range` of the camera, placed on the GPU each frame by `VegetationScatter` by their biomes' chances and forms, coloured by their biomes, and drawn with sun and cloud shadows; the [planet example's README](../../../examples/python/simulations/planet/README.md) has how they are placed and drawn. They are built of bark and alpha-tested foliage cards showing an atlas that `setup` bakes (`vegetation_textures`): from the CC0 scans that `python -m shoonyakasha.planet.vegetation_textures` downloads into `SHOONYAKASHA_VEGETATION_TEXTURES` or `shoonyakasha/vegetation_textures` in the user's cache (`vegetation_scans` is True when they were used), else from leaves drawn in the module. Beyond `VEGETATION_NEAR` a tree is its impostor (`vegetation_impostors`), one quad facing the camera. Each kind of `vegetation.KINDS` but grass holds up to `VEGETATION_CAPACITY` plants in view at a time.

Tufts of grass grow within `grass_range` of the camera, placed by `GrassScatter` on a finer grid by their biomes' grass chances, and drawn by the same `Vegetation` pass as the last kind, crossed cards in their biomes' grass colour, swaying in the wind; they cast no shadows. Up to `GRASS_CAPACITY` tufts are in view at a time. `vegetation` and `vegetation_density` apply to the grass too.

## Cameras

Both have `update(dt, active=True)` once a frame (`active` False while the pointer is over the user interface), `scroll(steps)` for the mouse wheel, `position` and `basis` for `end_frame`, and `body`.

`GlobeCamera(world, body=None, target=None, range_=20000.0, heading=0.0, tilt=0.0)` looks at a target point on a body's ground (`target`, a direction from its centre) from `range` metres, turned `heading` radians from north and tilted `tilt` from straight down:

| Input | |
|---|---|
| Left button, drag | Grabs the ground: the view turns about the body's centre so the grabbed point stays under the pointer. Let go while moving and it coasts, slowing down |
| Wheel | Zooms by 20 % a step towards (or away from) the point under the pointer |
| Right or middle button, drag | Heading (sideways) and tilt (up and down, 0 to 85 degrees) |
| Double-click | Flies to the point under the pointer, 70 % closer |
| W, A, S, D; Q, E | Move the target over the ground at the range a second; turn the heading |

`look_at(body, target, range_, heading, tilt)` sets the view at once, `fly_to(target, range_, heading, tilt, seconds)` over time; `north_up()` and `straight_down()`. The target's height comes from the world's extra probe, which the camera asks for each frame; it keeps 2 m above the ground under it. The range runs from 5 m to 40 planet radii.

`FocusCamera(world, body=None, distance=None, yaw=0.0, pitch=20 degrees)` circles a body's centre at `distance` (3 radii by default), `yaw` around and `pitch` above the moon's orbital plane:

| Input | |
|---|---|
| Left button, drag | Circles the body; let go while moving and it coasts |
| Wheel | Zooms by 15 % a step, from 50 m above the ground to 40 planet radii |
| Click (pressed and let go within 5 pixels) | Focuses the body under the pointer: the view moves to it over 1.5 seconds, ending 3 of its radii from its centre |
| Q, E; R, F | Yaw; pitch |

`focus(body)` does the same as a click; `pick(cursor)` gives the body under a window point. `cursor_ray(cursor, size, basis, fov)` is the direction through a window point, `ray_sphere(origin, direction, centre, radius)` where a ray first meets a sphere.

## Settings

`sky_items(world, keys=None, site="the site")`, `cloud_items`, `ground_items` (with the vegetation's on, density and range, and the grass's on and range), `climate_items` (the temperature and moisture offsets, the flora's and soil's hues, and a button that sets the next palette, `next_palette(world)`) and `view_items(world, keys=None)` give items for `toolkit.overlay.SettingsMenu` that read and change the world's settings; `planet_items(world, edit, new_seed, keys=None)` gives a new seed (`new_seed()`, at once) and the terrain's parameters, held in `edit` (a `TerrainEdit(world)`) until its Apply builds the planet again (`TerrainEdit.apply`; `reset()` takes the planet's parameters again). `keys` maps an item's name to key names shown beside it.

## Ground textures

`python -m shoonyakasha.planet.ground_textures` downloads an albedo and a normal map at 1K for each of the terrain's layers (sand, dry ground, grass, forest floor, rock, snow and the moon's ground), CC0 scans from Poly Haven and ambientCG, with a `CREDITS.md`. They are kept in the `SHOONYAKASHA_GROUND_TEXTURES` directory, or else `shoonyakasha/ground_textures` in the user's cache (`%LOCALAPPDATA%` on Windows, `$XDG_CACHE_HOME` or `~/.cache` elsewhere). `PlanetWorld.setup` hands them to the pipeline with mipmaps when all are there, read with Pillow; without them the ground keeps its procedural colours.
