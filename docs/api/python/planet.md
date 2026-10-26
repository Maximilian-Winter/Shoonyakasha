# Planets: `shoonyakasha.planet`

A planet and its moon with procedural terrain on the planet pipeline (`shoonyakasha/pipelines/planet/`): a cube-sphere with quadtree level of detail whose chunks a compute pass builds, an atmosphere, clouds, an ocean, sun shadows and ground textures. The [planet example](../../../examples/python/simulations/planet/README.md) flies a ship over it; the [planet explorer](../../../examples/python/simulations/planet_explorer/README.md) looks at it like a globe or a planetarium. Needs numpy, and a GPU with 64-bit float shaders (`shaderFloat64`).

| Module | |
|---|---|
| `world` | `PlanetWorld`: the bodies, the probes, the sky, and what the pipeline is handed each frame. `PIPELINE`, `compile_shaders()`, and the constants below |
| `cameras` | `GlobeCamera`, `FocusCamera`, `cursor_ray`, `ray_sphere` |
| `settings` | Settings-menu items for a `PlanetWorld` (for `toolkit.overlay.SettingsMenu`), and `TerrainEdit` |
| `orbits` | `Planet` (gravity and air), `Moon` (a circular orbit on rails), `reference`, `orbit`, `orbit_path`, `circular_speed` |
| `vec` | Vectors as tuples: `add`, `sub`, `scale`, `dot`, `cross`, `length`, `normalize`, `rotate`, `local_frame`, `euler_from_basis` |
| `cube_sphere`, `quadtree`, `shadows` | The cube-sphere's faces and chunk meshes, the quadtree in Python (the level of detail `sk.PlanetLod` does in C++), the shadow cascades' fitting |
| `ground_textures` | The ground's textures: download (`python -m shoonyakasha.planet.ground_textures`) and `load(engine)` |
| `vegetation` | The vegetation's low-poly meshes (`meshes()`) and their draws (`draw_template`) |

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
| `setup()` | From `on_init`: the camera entity, the sky, clouds and terrain settings, the chunks' index buffer, the ground's textures (when downloaded) and, with `moon`, the moon |
| `begin_frame(viewpoint=None)` | The chunks' and probes' heights read back, with `viewpoint` where the ground is measured; the terrain started once the site's height is known. Returns True in the frame it starts |
| `end_frame(position, basis, dt, warp=1.0, viewpoint=None, patch=None)` | The level of detail and the probes for the camera at `position` with `basis` (right, up, forward), the drawn chunks, the camera entity's rotation, the shadow cascades and the ground textures' offsets, and the clouds blown on by `dt` times `warp` seconds. `patch` is (body, patch) to probe: an object with `directions` (up to `PATCH_PROBES`) and `set_heights(heights)` |
| `set_orbit_path(points)` | A line through up to `ORBIT_POINTS` points (planet space) over the scene, depth-tested; none when empty |
| `upload_sky()`, `upload_clouds()`, `upload_terrain()` | Hand changed settings to the pipeline |
| `set_ocean(on)`, `set_wireframe(on)` | Those passes on or off |
| `regenerate()` | The planet built again with `planet.terrain`: every chunk released and the site measured again |
| `change_terrain(name, index, factor)`, `new_seed(seed)` | One terrain parameter times `factor`, or a new noise offset (three numbers), and the planet built again |
| `settled()` | The planet's tree made, no chunk missing, none still to build again for the sun, every built chunk's heights back |
| `body_under(point)` | The body `point` is reckoned around: the moon within its sphere of influence, else the planet |
| `altitude(point)`, `height_above_ground(point)`, `surface_height(point)`, `datum_name(point)` | Heights above the body's sphere (sea level on the planet) and above the ground measured under the viewpoint (the sea where that is below sea level) |
| `keep_above_ground(point, clearance)` | `point` raised to `clearance` above that ground when it is below |
| `site_frame()`, `site_ground()`, `sun_direction()` | The site on the +Z face (its point, up, east and north), its point on the ground, and the sun's direction from its elevation and azimuth seen there |
| `in_view(centre, radius)` | Whether a sphere is roughly in the camera's view |

| Attribute | |
|---|---|
| `planet`, `moon`, `bodies` | The bodies (`Body`: `index`, `name`, `radius`, `terrain` (the planet's parameters), `orbit`, `ocean`, `tree`, `centre()`, `velocity()`, `relative(point)`). `tree` is a `TreeView`, or None until the body's tree is made: `selected` (the nodes drawn), `missing` ((distance, node), nearest first) and `nodes()`, as `NodeInfo` copies (`face`, `level`, `x`, `y`, `chunk` (its slot or None), `centre`, `edge`, `low`, `high`, `measured`, `bound_centre`, `bound_radius`, `key()`), and `split_factor`, which can be set |
| `time` | The world's clock in seconds; it places the moon |
| `physics` | The `orbits.Planet` with the moon, at `time`: gravity, air density (scaled by `density`) and orbits |
| `sun_elevation`, `sun_azimuth`, `density`, `ozone`, `multiple_scattering`, `exposure` | The sky (`upload_sky`) |
| `clouds`, `cloud_coverage`, `cloud_density`, `wind_speed` | The clouds (`upload_clouds`; the wind is read each frame) |
| `vegetation`, `vegetation_density`, `vegetation_range` | Read each frame: the trees and bushes on or off, their chances' scale (0 to 1) and their range in metres (2,500 at the start) |
| `shadows`, `texture_strength`, `texture_relief`, `colour_mode`, `frozen` | Read each frame: the shadow cascades on or off, the ground textures' strength and normal maps, the colour mode (an index into `COLOUR_MODES`), and the level of detail kept as it is |
| `ground`, `site_height`, `patch`, `extra_probes`, `extra_heights`, `probe_latency` | The probes: the ground under the viewpoint (kept only while the viewpoint is within 1 km of where it was measured), the site's height, the last patch back, the up to `EXTRA_PROBES` (direction, body) pairs to measure and their heights, and the frames they take |
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
| `set_face_colours(colours)`, `split_factor(body)`, `set_split_factor(body, value)` | Colour mode 1's six colours; a tree's split factor |
| `selected(body)`, `nodes(body)`, `missing_nodes(body)`, `missing(body)`, `selected_count(body)` | The trees' state, nodes as dicts |
| `instances()`, `draws("chunk" / "ocean" / "shadow")`, `culled`, `slots`, `used`, `pending`, `rebuild_queued` | The buffers as bytes, and counts |

## Vegetation

Low-poly conifers, broadleaf trees and bushes grow on the planet within `vegetation_range` of the camera, placed on the GPU each frame by `VegetationScatter` where the ground's layers are forest, grass or dry ground, and drawn with sun and cloud shadows; the [planet example's README](../../../examples/python/simulations/planet/README.md) has how they are placed. Each of the five kinds (conifer, conifer far, broadleaf, broadleaf far, bush) holds up to `VEGETATION_CAPACITY` plants in view at a time.

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

`sky_items(world, keys=None, site="the site")`, `cloud_items`, `ground_items` (with the vegetation's on, density and range) and `view_items(world, keys=None)` give items for `toolkit.overlay.SettingsMenu` that read and change the world's settings; `planet_items(world, edit, new_seed, keys=None)` gives a new seed (`new_seed()`, at once) and the terrain's parameters, held in `edit` (a `TerrainEdit(world)`) until its Apply builds the planet again (`TerrainEdit.apply`; `reset()` takes the planet's parameters again). `keys` maps an item's name to key names shown beside it.

## Ground textures

`python -m shoonyakasha.planet.ground_textures` downloads an albedo and a normal map at 1K for each of the terrain's layers (sand, dry ground, grass, forest floor, rock, snow and the moon's ground), CC0 scans from Poly Haven and ambientCG, with a `CREDITS.md`. They are kept in the `SHOONYAKASHA_GROUND_TEXTURES` directory, or else `shoonyakasha/ground_textures` in the user's cache (`%LOCALAPPDATA%` on Windows, `$XDG_CACHE_HOME` or `~/.cache` elsewhere). `PlanetWorld.setup` hands them to the pipeline with mipmaps when all are there, read with Pillow; without them the ground keeps its procedural colours.
