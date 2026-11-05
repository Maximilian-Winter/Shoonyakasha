"""
The --capture tour and the --verify and --*-check harnesses of planet.py,
each an ECS system that planet.py's on_init adds for its flag.

Imported by planet.py run as the script: the names from __main__ are
planet.py's.
"""

import json
import math
import os
import random

import numpy as np

from shoonyakasha import keys
from shoonyakasha.planet import cube_sphere
from shoonyakasha.planet import gravity_gun as gun_module
from shoonyakasha.planet import world as planet_world
from shoonyakasha.planet.character import Controls as FootControls
from shoonyakasha.planet.character import MASS as PLAYER_MASS, RADIUS as PLAYER_RADIUS, RUN as RUN_SPEED, WALK as WALK_SPEED
from shoonyakasha.planet.nearby_trees import listed_keys
from shoonyakasha.planet.on_foot import COLLIDER_RADIUS
from shoonyakasha.planet.props import yaw_rotation
from shoonyakasha.planet.resources import FERRITE, ICE
from shoonyakasha.planet.rocks import ORE_ROCKS, rock_colour
from shoonyakasha.planet.vec import (add, cross, dot, length, local_frame, normalize, quat_from_basis, quat_rotate,
                                     rotate, scale, sub)
from shoonyakasha.planet.world import (CLOUD_COVERAGE, EXTRA_PROBES, FLOATS_PER_VERTEX, MAX_LEVEL, MORPH_LEVELS,
                                       MORPH_START, QUADS, RADIUS, SKIRT_FRACTION, SLOTS, VERTICES_PER_CHUNK)

import flight
import outposts
from scanner import CONTACT
import trade

from __main__ import (
    CAPTURES, CRATE_DENSITY, CRATE_SIZE, DAY_SUN, FLIGHT_STEP, NET_HALF, ORBIT_HEIGHT, PIXEL_ANGLE,
    SET_DOWN_DEPTH, SHIP_CLEARANCE, SHIP_REACH, WARPS, bodies, change_terrain, contact_text, credits_text,
    distance_text, drop_bubble, engine, ensure_bubble, foot, get_in, get_out, go_on_foot, in_net_now, load_game,
    moon_body, moon_spot, nearby_trees, net_frame, new_seed, new_ship, on_key, outpost_here, physics, planet,
    prop_state, props, save_game, scanner, scatter_rocks, set_colour_mode, set_wireframe, ship_landed,
    ship_velocity, state, teleport, trunks, viewpoint, world)


def settled():
    """The planet's tree made, no body's chunks missing, none still to
    build again for the sun, every built chunk's heights back, and no ship
    waiting to be set down on the moon."""
    return world.settled() and state["moon_landing"] is None


def look_up(degrees):
    """The free camera's view turned `degrees` up from level, keeping its heading."""
    radial = normalize(state["position"])
    level = normalize(sub(state["forward"], scale(radial, dot(state["forward"], radial))))
    up = math.radians(degrees)
    state["forward"] = normalize(add(scale(level, math.cos(up)), scale(radial, math.sin(up))))


def overcast():
    """Clouds over 95 % of the sky."""
    world.cloud_coverage = 0.95
    world.upload_clouds()


def lift_camera(height):
    """The free camera `height` metres above the sea, where it is, looking level."""
    radial = normalize(state["position"])
    state["position"] = scale(radial, RADIUS + height)
    look_up(0.0)


def lift_ship(height):
    """The ship `height` metres above the sea, where it is, at rest in the air with assist on."""
    ship = state["ship"]
    ship.position = scale(normalize(ship.position), RADIUS + height)
    ship.velocity = (0.0, 0.0, 0.0)
    state["position"] = ship.position


def mid_reentry():
    """The ship 17 km above the blocks at 640 m/s eastwards, 8 degrees
    below level, its nose along its path: in the reentry glow."""
    _, up, east, _ = world.site_frame()
    down = math.radians(8.0)
    forward = normalize(add(scale(east, math.cos(down)), scale(up, -math.sin(down))))
    ship_up = normalize(sub(up, scale(forward, dot(up, forward))))
    position = scale(up, RADIUS + 17_000.0)
    state["ship"] = flight.Ship(position=position, velocity=scale(forward, 640.0),
                                orientation=quat_from_basis(cross(forward, ship_up), ship_up,
                                                                   scale(forward, -1.0)))
    state["controls"] = flight.Controls(assist=False)
    state["mode"] = "ship"
    state["position"] = position
    state["forward"] = forward


def look_at_moon():
    """The free camera's view turned towards the moon's centre."""
    state["forward"] = normalize(sub(moon_body().centre(), state["position"]))


def near_moon(height, sun_elevation, down=0.0):
    """The free camera `height` metres above the moon's sphere where the
    sun stands `sun_elevation` degrees up, looking level across the sun's
    direction, turned `down` degrees below level."""
    moon = moon_body()
    sun = world.sun_direction()
    across = normalize(cross(sun, (0.0, 1.0, 0.0) if abs(sun[1]) < 0.9 else (1.0, 0.0, 0.0)))
    up = rotate(sun, across, math.radians(90.0 - sun_elevation))
    state["mode"] = "camera"
    state["position"] = add(moon.centre(), scale(up, moon.radius + height))
    tilt = math.radians(down)
    state["forward"] = normalize(add(scale(across, math.cos(tilt)), scale(up, -math.sin(tilt))))


def over_ground(distance, azimuth, height, down):
    """The free camera `height` metres above the site's ground level,
    `distance` metres from the site at `azimuth` degrees from east towards
    north, looking that way and `down` degrees below level (it stays above
    the ground there)."""
    _, up, east, north = world.site_frame()
    a = math.radians(azimuth)
    level = add(scale(east, math.cos(a)), scale(north, math.sin(a)))
    spot = normalize(add(up, scale(level, distance / RADIUS)))
    spot_east, spot_north = local_frame(spot)
    ahead = normalize(add(scale(spot_east, math.cos(a)), scale(spot_north, math.sin(a))))
    state["mode"] = "camera"
    state["position"] = scale(spot, RADIUS + (world.site_height or 0.0) + height)
    state["forward"] = normalize(add(ahead, scale(spot, -math.tan(math.radians(down)))))


def clearings_ahead():
    """Two clearings on the ground in the free camera's view, each thinning
    in over 10 m: a circle of 30 m radius 110 m ahead and 35 m to the left,
    clearing everything, and a rectangle 90 by 24 m 170 m ahead and 45 m
    to the right, turned 45 degrees from the view, clearing the plants and
    the rocks but not the grass."""
    up = normalize(state["position"])
    forward = state["forward"]
    ahead = normalize(sub(forward, scale(up, dot(forward, up))))
    right = cross(ahead, up)

    def at(along, across):
        return normalize(add(up, scale(add(scale(ahead, along), scale(right, across)), 1.0 / RADIUS)))

    east, north = local_frame(up)
    bearing = math.atan2(dot(ahead, east), dot(ahead, north))
    world.set_clearings([
        planet_world.Clearing(0, at(110.0, -35.0), radius=30.0, edge=10.0),
        planet_world.Clearing(0, at(170.0, 45.0), size=(90.0, 24.0), heading=bearing + math.radians(45.0),
                              edge=10.0, clears=planet_world.CLEAR_PLANTS | planet_world.CLEAR_ROCKS),
    ])


def near_mountain(sun_elevation, sun_azimuth, top=False):
    """The free camera 2.7 km above the sphere, 600 m south-west of a peak
    60 km from the site at 67.5 degrees from east (2,460 m high with the
    default terrain), looking north-east at it and down 12 degrees; with
    `top`, 2,520 m above the sphere over the peak, looking south-west and
    down 20 degrees. The sun at `sun_elevation` and `sun_azimuth` (degrees,
    from east towards north) seen from the peak."""
    _, up, east, north = world.site_frame()
    azimuth = math.radians(67.5)
    level = add(scale(east, math.cos(azimuth)), scale(north, math.sin(azimuth)))
    peak = normalize(add(up, scale(level, 60_000.0 / RADIUS)))
    peak_east, peak_north = local_frame(peak)
    north_east = normalize(add(peak_east, peak_north))
    spot = normalize(add(peak, scale(north_east, -600.0 / RADIUS)))
    if top:
        state["position"] = scale(peak, RADIUS + 2_520.0)
        state["forward"] = normalize(add(scale(north_east, -1.0), scale(peak, -math.tan(math.radians(20.0)))))
    else:
        state["position"] = scale(spot, RADIUS + 2_700.0)
        state["forward"] = normalize(add(north_east, scale(spot, -math.tan(math.radians(12.0)))))
    elevation, azimuth = math.radians(sun_elevation), math.radians(sun_azimuth)
    sun = add(scale(peak, math.sin(elevation)),
              scale(add(scale(peak_east, math.cos(azimuth)), scale(peak_north, math.sin(azimuth))), math.cos(elevation)))
    world.sun_elevation = math.degrees(math.asin(max(-1.0, min(1.0, dot(sun, up)))))
    world.sun_azimuth = math.degrees(math.atan2(dot(sun, north), dot(sun, east))) % 360.0
    world.upload_sky()


class CaptureTour:
    """--capture: each shot in turn, a screenshot once settled (or after
    MAX_FRAMES), then exit. Waits for the site's height first."""

    MIN_FRAMES, MAX_FRAMES = 10, 600
    # (file name, preset, wireframe, colour mode, terrain change or None, sun elevation and azimuth,
    #  then any functions to call once it is set up)
    SHOTS = [("preset_%d" % p, p, False, 0, None, DAY_SUN) for p in range(1, 7)] + [
        ("preset_1_sunset", 1, False, 0, None, (3.0, 45.0)),
        ("preset_2_sunset", 2, False, 0, None, (2.0, 90.0)),
        ("preset_3_low_sun", 3, False, 0, None, (8.0, 45.0)),
        ("preset_4_terminator", 4, False, 0, None, (0.0, 200.0)),
        ("preset_7_coast", 7, False, 0, None, DAY_SUN),
        ("preset_7_sun_on_the_sea", 7, False, 0, None, (12.0, 135.0)),
        ("preset_3_ocean_sun", 3, False, 0, None, (50.0, 135.0)),
        ("preset_8_ship_on_the_pad", 8, False, 0, None, DAY_SUN),
        ("preset_9_ship_in_orbit", 9, False, 0, None, DAY_SUN),
        ("preset_8_ship_sunset", 8, False, 0, None, (6.0, 90.0)),
        ("preset_8_menu_and_throttle", 8, False, 0, None, DAY_SUN,
         lambda: state["menu"].show(True), lambda: setattr(state["controls"], "throttle", 0.6)),
        ("controls_sheet", 1, False, 0, None, DAY_SUN, lambda: state["help"].show(True)),
        ("chase_gamepad_hints", 8, False, 0, None, DAY_SUN, lambda: state.update(chase=True, input="pad")),
        ("preset_1_wireframe_levels", 1, True, 2, None, DAY_SUN, lambda: state.update(debug_text=True)),
        ("preset_2_wireframe", 2, True, 0, None, DAY_SUN),
        ("preset_2_twilight", 2, False, 0, None, (-3.0, 90.0)),
        ("orbit_from_afar", 9, False, 0, None, DAY_SUN, lambda: teleport(4)),
        ("reentry", 9, False, 0, None, DAY_SUN, mid_reentry),
        ("clouds_terminator", 3, False, 0, None, (0.0, 200.0)),
        ("clouds_below_sunset", 1, False, 0, None, (4.0, 45.0), lambda: look_up(25.0)),
        ("clouds_inside", 2, False, 0, None, DAY_SUN, overcast, lambda: lift_camera(2_500.0)),
        ("clouds_cockpit_inside", 8, False, 0, None, DAY_SUN, overcast, lambda: lift_ship(2_500.0)),
        ("clouds_from_above", 2, False, 0, None, DAY_SUN, lambda: lift_camera(6_000.0), lambda: look_up(-25.0)),
        ("clouds_level", 2, False, 0, None, DAY_SUN, lambda: lift_camera(2_500.0)),
        ("clouds_from_below", 2, False, 0, None, DAY_SUN, lambda: lift_camera(800.0), lambda: look_up(10.0)),
        ("vegetation_grassland", 1, False, 0, None, DAY_SUN, lambda: over_ground(3_000.0, 180.0, 120.0, 12.0)),
        ("vegetation_conifers", 1, False, 0, None, DAY_SUN, lambda: over_ground(6_000.0, 270.0, 1_800.0, 12.0)),
        ("vegetation_mixed_wood", 1, False, 0, None, DAY_SUN, lambda: over_ground(8_000.0, 180.0, 260.0, 10.0)),
        ("grass_meadow", 1, False, 0, None, DAY_SUN, lambda: over_ground(3_000.0, 180.0, 2.5, 6.0)),
        ("grass_wood_edge", 1, False, 0, None, DAY_SUN, lambda: over_ground(8_000.0, 180.0, 3.0, 4.0)),
        ("vegetation_wood_low", 1, False, 0, None, DAY_SUN, lambda: over_ground(8_000.0, 180.0, 0.0, 0.0),
         lambda: lift_camera(700.0), lambda: look_up(-20.0)),
        ("vegetation_clearings", 1, False, 0, None, DAY_SUN, lambda: over_ground(8_000.0, 180.0, 0.0, 0.0),
         lambda: lift_camera(700.0), lambda: look_up(-20.0), clearings_ahead),
        ("mountain_close", 7, False, 0, None, DAY_SUN, lambda: near_mountain(40.0, 160.0)),
        ("mountain_close_low_sun", 7, False, 0, None, DAY_SUN, lambda: near_mountain(10.0, 135.0)),
        ("mountain_top", 7, False, 0, None, DAY_SUN, lambda: near_mountain(15.0, 135.0, top=True)),
        ("mountain_top_no_detail", 7, False, 0, ("detail", 1, 0.0), DAY_SUN,
         lambda: near_mountain(15.0, 135.0, top=True)),
        ("preset_3_mountains_higher", 3, False, 0, ("mountains", 1, 1.5), DAY_SUN),
        ("moon_from_the_site", 1, False, 0, None, DAY_SUN, look_at_moon),
        ("moon_from_afar", 1, False, 0, None, DAY_SUN, lambda: near_moon(40_000.0, 30.0, down=45.0)),
        ("moon_surface", 1, False, 0, None, DAY_SUN, lambda: near_moon(300.0, 15.0, down=12.0)),
        ("moon_low_sun", 1, False, 0, None, DAY_SUN, lambda: near_moon(1_000.0, 5.0, down=25.0)),
        ("ship_on_the_moon", 1, False, 0, None, DAY_SUN, lambda: teleport(8, moon=True)),
        ("ship_around_the_moon", 1, False, 0, None, DAY_SUN, lambda: teleport(9, moon=True)),
        ("generated_cockpit_on_the_pad", 8, False, 0, None, DAY_SUN,
         lambda: state.update(generated_cockpit=True)),
        ("chase_on_the_pad", 8, False, 0, None, DAY_SUN, lambda: state.update(chase=True)),
        ("chase_in_orbit", 9, False, 0, None, DAY_SUN, lambda: state.update(chase=True)),
        ("chase_on_the_moon", 1, False, 0, None, DAY_SUN, lambda: teleport(8, moon=True),
         lambda: state.update(chase=True)),
    ]

    def __init__(self, directory, names=None):
        """`names`: only the shots whose names contain one of these."""
        os.makedirs(directory, exist_ok=True)
        self.directory = directory
        if names:
            self.SHOTS = [shot for shot in self.SHOTS if any(name in shot[0] for name in names)]
        self.shot = 0
        self.frames = 0
        self.captured = None

    def __call__(self, dt):
        if world.site_height is None:
            return True
        name, preset, wireframe, colour_mode, change, sun, *setup = self.SHOTS[self.shot]
        self.frames += 1
        if self.frames == 1:
            if change is not None:
                change_terrain(*change)
            world.sun_elevation, world.sun_azimuth = sun
            world.upload_sky()
            world.cloud_coverage, world.cloud_wind = CLOUD_COVERAGE, (0.0, 0.0, 0.0)
            world.upload_clouds()
            state["chase"] = False
            state["generated_cockpit"] = False
            state["debug_text"] = False
            teleport(preset)
            set_wireframe(wireframe)
            set_colour_mode(colour_mode)
            state["menu"].show(False)
            state["help"].show(False)
            state["input"] = "keys"
            world.set_clearings([])
            for action in setup:
                action()
        elif self.captured is None:
            done = self.frames >= self.MIN_FRAMES and settled()
            if done or self.frames >= self.MAX_FRAMES:
                path = os.path.join(self.directory, name + ".png")
                ok = engine.capture_screenshot(path)
                selected = sum(len(body.tree.selected) for body in bodies if body.tree is not None)
                print("[planet] %s after %d frames%s, %d of %d chunks drawn, frame %.1f ms"
                      % (path if ok else "screenshot failed", self.frames, "" if done else " (not settled)",
                         selected - world.chunks_culled, selected, state["frame_ms"]), flush=True)
                self.captured = self.frames
        elif self.frames > self.captured + 2:
            self.shot += 1
            self.frames = 0
            self.captured = None
            if self.shot == len(self.SHOTS):
                os._exit(0)        # the engine has no quit call
        return True


class Verify:
    """--verify: at preset 1, once settled, read ChunkPool back and check
    every built chunk against cube_sphere.chunk_mesh:
      - directions and face coordinates equal to its, rounded to 32-bit;
      - positions equal to direction * (radius + height - skirt) - centre
        worked out from its directions and the GPU's heights, the skirt the
        larger of the job's depth and the grid's height range, to within one
        unit in the last place (ULP) of the chunk's largest coordinate;
      - normals of unit length, on the outer side;
      - each slot's lowest and highest grid heights in ChunkHeights equal to
        those of the heights in the pool.
    Prints the largest differences and exits with status 0 when all hold,
    1 otherwise."""

    MIN_FRAMES, MAX_FRAMES = 10, 1200
    TOLERANCE_ULPS = 1.0

    def __init__(self):
        self.frames = 0
        self.requested = None

    def __call__(self, dt):
        if world.site_height is None:
            return True
        self.frames += 1
        if self.frames == 1:
            teleport(1)
        elif self.requested is None:
            if (self.frames >= self.MIN_FRAMES and settled()) or self.frames >= self.MAX_FRAMES:
                engine.request_readback("ChunkPool")
                self.requested = self.frames
        else:
            readback = engine.get_readback("ChunkPool")
            if readback is not None:
                self.check(readback)
        return True

    def check(self, readback):
        gpu = np.frombuffer(readback["data"], dtype=np.float32).reshape(SLOTS, VERTICES_PER_CHUNK,
                                                                       FLOATS_PER_VERTEX)
        heights = np.frombuffer(engine.get_readback("ChunkHeights")["data"], dtype=np.float32).reshape(SLOTS, 4)
        grid = (QUADS + 1) ** 2
        worst = {"position (ULPs)": 0.0, "direction (ULPs)": 0.0, "face uv": 0.0,
                 "normal length - 1": 0.0, "lowest/highest height (m)": 0.0}
        lowest_normal_dot = 1.0
        chunks = {body.name: 0 for body in bodies}
        built_nodes = [(body, node) for body in bodies if body.tree is not None
                       for node in body.tree.nodes() if node.chunk is not None]
        for body, node in built_nodes:
            built = gpu[node.chunk]
            # The skirt's depth as the GPU works it out, in 32-bit: the job's, or the grid's height range.
            depth = max(np.float32(SKIRT_FRACTION * node.edge), built[:grid, 11].max() - built[:grid, 11].min())
            centre, positions, directions, uvs, _ = cube_sphere.chunk_mesh(
                node.face, node.level, node.x, node.y, QUADS, body.radius, skirt_depth=float(depth))
            height = built[:, 11].astype(np.float64)
            expected = (positions + directions * height[:, None]).astype(np.float32)
            ulp = float(np.spacing(np.abs(expected).max()))
            worst["position (ULPs)"] = max(worst["position (ULPs)"],
                                           float(np.abs(built[:, :3] - expected).max()) / ulp)
            expected = directions.astype(np.float32)
            worst["direction (ULPs)"] = max(worst["direction (ULPs)"], float(
                (np.abs(built[:, 8:11] - expected) / np.spacing(np.abs(expected))).max()))
            worst["face uv"] = max(worst["face uv"],
                                   float(np.abs(built[:, [3, 7]] - uvs.astype(np.float32)).max()))
            normals = built[:, 4:7].astype(np.float64)
            worst["normal length - 1"] = max(worst["normal length - 1"],
                                             float(np.abs(np.linalg.norm(normals, axis=1) - 1.0).max()))
            lowest_normal_dot = min(lowest_normal_dot, float((normals * directions).sum(axis=1).min()))
            worst["lowest/highest height (m)"] = max(
                worst["lowest/highest height (m)"],
                abs(float(heights[node.chunk, 0]) - float(built[:grid, 11].min())),
                abs(float(heights[node.chunk, 1]) - float(built[:grid, 11].max())))
            chunks[body.name] += 1
        print("[planet] checked %s against cube_sphere.chunk_mesh:"
              % ", ".join("%d chunks of the %s" % (n, name) for name, n in chunks.items()), flush=True)
        for name, value in worst.items():
            print("[planet]   largest %s difference %.3g" % (name, value), flush=True)
        print("[planet]   smallest normal . direction %.3f" % lowest_normal_dot, flush=True)
        ok = (all(n > 0 for n in chunks.values()) and worst["position (ULPs)"] <= self.TOLERANCE_ULPS and worst["direction (ULPs)"] == 0
              and worst["face uv"] == 0 and worst["normal length - 1"] < 1e-5 and lowest_normal_dot > 0.0
              and worst["lowest/highest height (m)"] == 0)
        os._exit(0 if ok else 1)


class MorphCheck:
    """--morph-check: once every chunk is built for the view 20 km above the
    site, the free camera comes down to 100 m, moving north at a third of
    its height per second of flight, in FRAMES steps (one per frame).
    Each frame it compares
    the drawn chunks with the frame before's: a chunk that appeared because
    its parent split, or that went because it merged, swaps drawn surfaces,
    which is invisible when every vertex of the chunk was fully morphed then
    (morph.glsl). The vertices lie in the node's bounding sphere, so the
    least factor is the one at the sphere's distance from the camera.
    Swaps where the children were built only after the camera came within
    the split distance are counted apart (they appear late, closer).
    Prints the swaps, the late ones and where they happened, and the least
    factor of the others, and exits with status 0 when none were late and
    that factor is at least 0.99, 1 otherwise."""

    FRAMES = 2400
    START, END = 20_000.0, 100.0

    def __init__(self):
        self.frame = -1
        self.previous = None
        self.swaps = 0
        self.late = 0
        self.least = 1.0
        self.travelled = 0.0             # metres north of the site
        self.step_length = 0.0           # metres the camera moved in the last frame
        self.late_heights = []           # the camera's height at each late swap

    def settled_at_start(self):
        """Puts the camera where the descent starts and keeps it there until
        every chunk it needs is built."""
        _, up, _, north = world.site_frame()
        state["mode"] = "camera"
        state["position"] = scale(up, RADIUS + (world.site_height or 0.0) + self.START)
        state["forward"] = normalize(add(north, scale(up, -0.3)))
        return settled()

    def factor(self, node, camera):
        if node.level == 0:
            return 1.0
        split = planet.tree.split_factor * 2.0 * node.edge
        distance = max(0.0, length(sub(camera, node.bound_centre)) - node.bound_radius)
        t = max(0.0, min(1.0, (distance - MORPH_START * split) / ((1.0 - MORPH_START) * split)))
        return t * t * (3.0 - 2.0 * t)

    def __call__(self, dt):
        tree = planet.tree
        if tree is None or not tree.selected:
            return True
        if self.frame < 0 and not self.settled_at_start():
            return True
        camera = state["position"]          # where the drawn selection was made
        drawn = {node.key(): node for node in tree.selected}
        if self.previous is not None:
            before = self.previous
            for key in drawn.keys() - before.keys():
                parent = before.get((key[0], key[1] - 1, key[2] // 2, key[3] // 2)) if key[1] > 0 else None
                if parent is None:
                    continue                 # not a split of a drawn parent
                self.swaps += 1
                parent_distance = max(0.0, length(sub(camera, parent.bound_centre)) - parent.bound_radius)
                if parent_distance < tree.split_factor * parent.edge - 2.0 * self.step_length:
                    self.late += 1
                    self.late_heights.append(length(camera) - RADIUS)
                else:
                    self.least = min(self.least, self.factor(drawn[key], camera))
            for key in before.keys() - drawn.keys():
                if key[1] == 0 or (key[0], key[1] - 1, key[2] // 2, key[3] // 2) not in drawn:
                    continue                 # not a merge into its parent
                self.swaps += 1
                self.least = min(self.least, self.factor(before[key], camera))
        self.previous = drawn
        self.frame += 1
        if self.frame == self.FRAMES:
            print("[planet] coming down from %.0f m to %.0f m in %d frames: %d swaps, %d late (children built "
                  "after the split distance), least morph factor of the others %.4f"
                  % (self.START, self.END, self.FRAMES, self.swaps, self.late, self.least), flush=True)
            if self.late_heights:
                print("[planet]   late swaps at camera heights from %.0f m to %.0f m (median %.0f m)"
                      % (min(self.late_heights), max(self.late_heights),
                         sorted(self.late_heights)[len(self.late_heights) // 2]), flush=True)
            os._exit(0 if self.least >= 0.99 and self.late == 0 else 1)
        # The next frame's camera.
        _, up, east, north = world.site_frame()
        height = self.START * (self.END / self.START) ** (self.frame / self.FRAMES)
        self.travelled += height / 3.0 / 60.0
        spot = normalize(add(up, scale(north, self.travelled / RADIUS)))
        new_position = scale(spot, RADIUS + (world.site_height or 0.0) + height)
        self.step_length = length(sub(new_position, camera))
        state["mode"] = "camera"
        state["position"] = new_position
        state["forward"] = normalize(add(north, scale(spot, -0.3)))
        return True


def morph_factor(node, point, level=1, body=None):
    """morph.glsl's factor towards the surface of a node's ancestor `level`
    levels up, for a vertex at `point` (relative to the centre of `body`,
    the planet when None) and the camera's present position; 0 where the
    node has no such ancestor."""
    body = planet if body is None else body
    if level > min(MORPH_LEVELS, node.level):
        return 0.0
    split = planet_world.parent_split_distance(node, body.tree) * 2.0 ** (level - 1)
    distance = length(sub(point, body.relative(state["position"])))
    t = max(0.0, min(1.0, (distance - MORPH_START * split) / ((1.0 - MORPH_START) * split)))
    return t * t * (3.0 - 2.0 * t)


def morphed_point(node, built, i, j, body=None):
    """Grid vertex (i, j) of a built chunk (its ChunkPool rows `built`) of
    `body` (the planet when None) relative to the body's centre, morphed as
    morph.glsl morphs it for the camera's present position: towards its
    parent's surface, then its grandparent's, and so on."""
    row = QUADS + 1
    vertex = lambda ij: add(node.centre, tuple(float(c) for c in built[ij[1] * row + ij[0], :3]))
    own = vertex((i, j))
    point = own
    for level in range(1, MORPH_LEVELS + 1):
        f = morph_factor(node, own, level, body)
        if f <= 0.0:
            break
        surface = (0.0, 0.0, 0.0)
        for ij, w in cube_sphere.stride_weights(i, j, 1 << level, QUADS):
            surface = add(surface, scale(vertex(ij), w))
        point = add(point, scale(sub(surface, point), f))
    return point


def drawn_height(body, gpu, point):
    """(metres above `body`'s radius, level of the chunk) of its surface as
    drawn (its selected chunks, morphed for the camera's present position,
    each quad split from its lowest to its highest corner) where the line
    from its centre through `point` (relative to its centre) meets it, from
    the ChunkPool rows `gpu`; None where no selected chunk holds that
    direction."""
    d = normalize(point)
    face, u, v = cube_sphere.face_uv(d)
    for node in body.tree.selected:
        if node.face != face or node.chunk is None:
            continue
        u0, v0, size = cube_sphere.chunk_bounds(node.level, node.x, node.y)
        if not (u0 <= u <= u0 + size and v0 <= v <= v0 + size):
            continue
        x, y = (u - u0) / size * QUADS, (v - v0) / size * QUADS
        i, j = min(int(x), QUADS - 1), min(int(y), QUADS - 1)
        fi, fj = x - i, y - j
        corner = lambda di, dj: np.array(morphed_point(node, gpu[node.chunk], i + di, j + dj, body))
        a, c = corner(0, 0), corner(1, 1)
        b = corner(1, 0) if fi >= fj else corner(0, 1)
        n = np.cross(b - a, c - a)
        return float(np.dot(a, n) / np.dot(np.array(d), n)) - body.radius, node.level
    return None


class CrackCheck:
    """--crack-check: at each view in VIEWS, once settled, freezes the level
    of detail, reads ChunkPool back and, for every drawn chunk's edge whose
    neighbour is coarser by k levels, measures the gap at each of the
    chunk's edge vertices between it and the neighbour's edge, the straight
    line between the vertices the two share (every 2^k-th). A gap where the
    finer edge is higher must be covered by the finer chunk's skirt, one
    where it is lower by the coarser chunk's. Prints per view the edges with
    a coarser neighbour, the largest gap, and the largest gap as a share of
    the skirt that covers it; exits with status 0 when no share is above 1,
    1 otherwise."""

    MIN_FRAMES, MAX_FRAMES = 10, 1200
    VIEWS = [
        ("preset 1", lambda: teleport(1)),
        ("preset 2", lambda: teleport(2)),
        ("preset 6, the cube's corner", lambda: teleport(6)),
        ("beside the peak", lambda: (teleport(7), near_mountain(40.0, 160.0))),
        ("over the peak", lambda: (teleport(7), near_mountain(40.0, 160.0, top=True))),
        ("over the peak, mountains twice as high",
         lambda: (change_terrain("mountains", 1, 2.0), teleport(7), near_mountain(40.0, 160.0, top=True))),
    ]

    def __init__(self):
        self.view = 0
        self.frames = 0
        self.requested = False
        self.worst = 0.0

    def __call__(self, dt):
        if planet.tree is None:
            return True
        self.frames += 1
        if self.frames == 1:
            world.frozen = False
            self.VIEWS[self.view][1]()
        elif not self.requested:
            if (self.frames >= self.MIN_FRAMES and settled()) or self.frames >= self.MAX_FRAMES:
                world.frozen = True
                previous = engine.get_readback("ChunkPool")
                self.after = previous["frame"] if previous is not None else -1
                engine.request_readback("ChunkPool")
                self.requested = True
        else:
            readback = engine.get_readback("ChunkPool")
            if readback is not None and readback["frame"] > self.after:
                self.measure(np.frombuffer(readback["data"], dtype=np.float32).reshape(
                    SLOTS, VERTICES_PER_CHUNK, FLOATS_PER_VERTEX))
                self.view += 1
                self.frames = 0
                self.requested = False
                if self.view == len(self.VIEWS):
                    os._exit(0 if self.worst <= 1.0 else 1)
        return True

    def measure(self, gpu):
        selected = planet.tree.selected
        by_key = {(n.face, n.level, n.x, n.y): n for n in selected}
        row = QUADS + 1

        def selected_at(d):
            face, u, v = cube_sphere.face_uv(d)
            for level in range(MAX_LEVEL + 1):
                cells = 1 << level
                x = min(max(int((u + 1.0) * 0.5 * cells), 0), cells - 1)
                y = min(max(int((v + 1.0) * 0.5 * cells), 0), cells - 1)
                if (face, level, x, y) in by_key:
                    return by_key[face, level, x, y]
            return None

        edges, largest_gap, largest_share = 0, 0.0, 0.0
        largest_drawn_gap, largest_drawn_share, largest_drawn_pixels = 0.0, 0.0, 0.0
        for node in selected:
            u0, v0, size = cube_sphere.chunk_bounds(node.level, node.x, node.y)
            # Per edge: its grid indices in order, and a point just outside its middle.
            sides = [
                (list(range(row)), (u0 + 0.5 * size, v0 - 1e-4 * size)),
                ([j * row + QUADS for j in range(row)], (u0 + size + 1e-4 * size, v0 + 0.5 * size)),
                ([QUADS * row + i for i in range(row)], (u0 + 0.5 * size, v0 + size + 1e-4 * size)),
                ([j * row for j in range(row)], (u0 - 1e-4 * size, v0 + 0.5 * size)),
            ]
            built = gpu[node.chunk]
            fine_skirt = planet_world.skirt_depth(node)
            for indices, (u, v) in sides:
                # Past a face's edge the cube point leaves the cube; normalising puts it on the sphere.
                other = selected_at(normalize(cube_sphere.spherify(cube_sphere.cube_point(node.face, u, v))))
                if other is None or other.level >= node.level:
                    continue
                neighbour = other.level
                edges += 1
                step = 1 << (node.level - neighbour)
                coarse_skirt = planet_world.skirt_depth(other)
                points = [add(node.centre, tuple(float(c) for c in built[k, :3])) for k in indices]
                # As drawn: both chunks' vertices morphed (morph.glsl), the coarse ones found by position.
                drawn_points = [morphed_point(node, built, k % row, k // row) for k in indices]
                coarse = gpu[other.chunk][:row * row, :3].astype(np.float64) + np.array(other.centre)
                for i in range(row):
                    i0 = (i // step) * step
                    if i0 == i:
                        continue
                    t = (i - i0) / step
                    a, b = points[i0], points[i0 + step]
                    line = add(a, scale(sub(b, a), t))
                    gap = length(points[i]) - length(line)
                    share = gap / fine_skirt if gap > 0.0 else -gap / coarse_skirt
                    largest_gap = max(largest_gap, abs(gap))
                    if share > largest_share:
                        largest_share = share
                        where = (node.level, neighbour, gap, fine_skirt if gap > 0.0 else coarse_skirt,
                                 length(sub(points[i], state["position"])))
                    ends = []
                    for p in (a, b):
                        kc = int(np.argmin(((coarse - np.array(p)) ** 2).sum(axis=1)))
                        ends.append(morphed_point(other, gpu[other.chunk], kc % row, kc // row))
                    drawn_line = add(ends[0], scale(sub(ends[1], ends[0]), t))
                    drawn_gap = length(drawn_points[i]) - length(drawn_line)
                    distance = length(sub(drawn_points[i], state["position"]))
                    largest_drawn_share = max(largest_drawn_share,
                                              drawn_gap / fine_skirt if drawn_gap > 0.0 else -drawn_gap / coarse_skirt)
                    largest_drawn_pixels = max(largest_drawn_pixels, abs(drawn_gap) / distance / PIXEL_ANGLE)
                    if abs(drawn_gap) > largest_drawn_gap:
                        largest_drawn_gap = abs(drawn_gap)
                        drawn_where = (node.level, neighbour, morph_factor(node, points[i]),
                                       max(morph_factor(other, p) for p in (a, b)), distance)
        self.worst = max(self.worst, largest_share, largest_drawn_share)
        print("[planet] %s: %d chunks selected, %d edges with a coarser neighbour, largest gap %.3f m, "
              "largest gap / skirt %.3f" % (self.VIEWS[self.view][0], len(selected), edges, largest_gap,
                                            largest_share), flush=True)
        print("[planet]   as drawn (morphed): largest gap %.3f m, largest gap / skirt %.3f, largest gap %.2f pixels"
              % (largest_drawn_gap, largest_drawn_share, largest_drawn_pixels), flush=True)
        if largest_share > 0.0:
            print("[planet]   there: level %d beside level %d, gap %+.2f m, skirt %.2f m, %.0f m from the camera"
                  % where, flush=True)
        if largest_drawn_gap > 0.0:
            print("[planet]   largest as drawn: level %d beside level %d, the finer edge morphed %.3f, the coarser "
                  "%.3f, %.0f m from the camera" % drawn_where, flush=True)


class LandingCheck:
    """--landing-check, on the planet, and --moon-landing-check, on the
    moon: measures the ground 9 km below spots on rings around a centre
    (on the planet 10 to 60 km around the site, on the moon 2 to 12 km
    around moon_spot) with the free camera, and the slope there from
    probes 20 m east and north; of those no steeper than 10 degrees takes
    the highest on the planet and the lowest on the moon, and brings the
    ship down onto it from 500 m above with translation assist and full
    downward thrust asked for; once it touches, lets go of the controls
    for ten seconds on the planet, thirty on the moon. Prints the
    touchdown speed, the slope there, how far each gear point is below the
    ground patch's surface, how far the patch's height under the ship is
    from the probe's, each gear point's height above the terrain probed at
    its own direction, and the ship's speed. Then a ship coasts straight at
    the spot from 1.5 km up and 1.5 km away at 400 m/s and 16x warp; prints
    whether it crashed and how high its centre ended above the ground.
    Exits with status 0 when the ship touched down under half the gear's
    crash speed, at rest on its gear, uncrashed, with the patch's surface
    within 5 cm of the terrain at every gear point and, on the planet,
    every gear point within 5 cm of both; and the diving ship crashed with
    its centre at least 1 m above the ground and within reach of it; 1
    otherwise. Positions are reckoned from the body's centre and speeds
    relative to its surface."""

    MAX_SLOPE = 10.0                    # degrees: steeper spots are left out
    SLOPE_SPAN = 20.0                   # m east and north of a spot where its slope is measured
    RINGS = {"planet": (10_000.0, 20_000.0, 35_000.0, 60_000.0), "moon": (2_000.0, 4_000.0, 7_000.0, 12_000.0)}
    AZIMUTHS = 16
    FRAMES_PER_SPOT = 8                 # more than the probes' latency
    GEAR_TOLERANCE = 0.05
    REST_SECONDS = {"planet": 10.0, "moon": 30.0}
    TIMEOUT_SECONDS = 90.0
    DIVE_SPEED = 400.0
    DIVE_MIN_CENTRE_HEIGHT = 1.0        # m; half the gear's depth below the centre

    def __init__(self, body_name):
        self.body = next(body for body in bodies if body.name == body_name)
        self.rings = self.RINGS[body_name]
        self.lowest = self.body is not planet
        if self.body is planet:
            _, up, east, north = world.site_frame()
            self.around = "the site"
        else:
            up = moon_spot()
            east, north = local_frame(up)
            self.around = "the spot between the sun and the planet"
        self.spots = []
        for ring in self.rings:
            for k in range(self.AZIMUTHS):
                azimuth = 2.0 * math.pi * k / self.AZIMUTHS
                level = add(scale(east, math.cos(azimuth)), scale(north, math.sin(azimuth)))
                self.spots.append(normalize(add(up, scale(level, ring / self.body.radius))))
        self.spot = -1
        self.frames = 0
        self.best = None                # (ground height, direction)
        self.touched = None             # flight time when the ship touched
        self.diving = None              # flight time when the dive started
        self.touchdown_speed = 0.0

    def __call__(self, dt):
        if self.body.tree is None:
            return True
        if self.best is None or self.spot < len(self.spots):
            return self.search()
        if self.diving is not None:
            return self.dive()
        return self.land()

    def search(self):
        self.frames += 1
        if self.spot >= 0 and self.frames < self.FRAMES_PER_SPOT:
            return True
        if self.spot >= 0 and world.ground is not None:
            height = world.ground
            slope = self.slope()
            if slope is not None and slope <= self.MAX_SLOPE and (
                    self.best is None or (height < self.best[0] if self.lowest else height > self.best[0])):
                self.best = (height, self.spots[self.spot])
                self.best_place = (self.rings[self.spot // self.AZIMUTHS],
                                   360.0 * (self.spot % self.AZIMUTHS) / self.AZIMUTHS, slope)
        self.spot += 1
        self.frames = 0
        if self.spot < len(self.spots):
            state["mode"] = "camera"
            state["position"] = add(self.body.centre(), scale(self.spots[self.spot], self.body.radius + 9_000.0))
            world.ground = None
            world.extra_probes = self.slope_probes(self.spots[self.spot])
        else:
            world.extra_probes = []
            height, up = self.best
            east, north = local_frame(up)
            state["ship"] = flight.Ship(position=self.at(up, height + 500.0), velocity=self.body.velocity(),
                                        orientation=quat_from_basis(east, up, scale(north, -1.0)))
            state["controls"] = flight.Controls(vertical=-1.0, assist=True)
            state["scripted_controls"] = True
            state["mode"] = "ship"
            state["warp"] = 0
            world.patch = None
            self.start = world.time
            print("[planet] the %s spot found on the %s with a slope of at most %.0f degrees: %.0f m, %.0f km from %s "
                  "at %.1f degrees from east, %.1f degrees steep"
                  % ("lowest" if self.lowest else "highest", self.body.name, self.MAX_SLOPE, height,
                     self.best_place[0] / 1000.0, self.around, self.best_place[1], self.best_place[2]), flush=True)
        return True

    def slope_probes(self, direction):
        """Probes at `direction` and SLOPE_SPAN east and north of it."""
        east, north = local_frame(direction)
        step = self.SLOPE_SPAN / self.body.radius
        return [(d, self.body) for d in (direction, normalize(add(direction, scale(east, step))),
                                         normalize(add(direction, scale(north, step))))]

    def slope(self):
        """The slope (degrees) at the spot from its slope_probes once their
        heights are back, else None."""
        asked = world.extra_probes
        if not asked or [probe for probe, _ in world.extra_heights] != asked:
            return None
        surface = (lambda h: max(h, 0.0)) if self.body.ocean else (lambda h: h)
        here, east, north = (surface(h) for _, h in world.extra_heights)
        rise = math.hypot(east - here, north - here) / self.SLOPE_SPAN
        return math.degrees(math.atan(rise))

    def at(self, direction, height):
        """The point `height` above the body's radius at `direction` from its centre, now."""
        return add(self.body.centre(), scale(direction, self.body.radius + height))

    def relative_speed(self, ship):
        return length(sub(ship.velocity, self.body.velocity()))

    def land(self):
        ship = state["ship"]
        now = world.time
        if self.touched is None:
            self.touchdown_speed = max(self.touchdown_speed, state["impact_speed"])
        if self.touched is None and (ship.on_ground or ship.crashed):
            self.touched = now
            state["controls"] = flight.Controls(assist=True)
        if self.touched is not None:
            q = ship.orientation
            world.extra_probes = [(normalize(self.body.relative(add(ship.position, quat_rotate(q, arm)))),
                                      self.body) for arm in ship.params.gear]
        if self.touched is not None and now - self.touched >= self.REST_SECONDS[self.body.name] or now - self.start > self.TIMEOUT_SECONDS:
            self.report(ship)
        return True

    def report(self, ship):
        patch = world.patch[1] if world.patch is not None else None
        ok = patch is not None and not ship.crashed and ship.on_ground
        if patch is not None:
            centre = self.body.relative(ship.position)
            depth, normal = patch.query(centre)
            radial = normalize(centre)
            slope = math.degrees(math.acos(max(-1.0, min(1.0, dot(normal, radial)))))
            patch_height = length(centre) - self.body.radius + depth / dot(radial, normal)
            q = ship.orientation
            gear = [patch.query(add(centre, quat_rotate(q, arm)))[0] for arm in ship.params.gear]
            print("[planet] touched down at %.1f m/s on a %.1f degree slope at %.0f m:"
                  % (self.touchdown_speed, slope, patch_height), flush=True)
            print("[planet]   gear points below the surface: %s m" % ", ".join("%+.3f" % d for d in gear),
                  flush=True)
            print("[planet]   patch height under the ship minus the probe's: %+.3f m"
                  % (patch_height - (world.ground or 0.0)), flush=True)
            # Each gear point's height above the terrain itself, measured at its direction.
            surface = (lambda h: max(h, 0.0)) if self.body.ocean else (lambda h: h)
            terrain = [length(add(centre, quat_rotate(q, arm))) - self.body.radius - surface(h)
                       for arm, (_, h) in zip(ship.params.gear, world.extra_heights)]
            print("[planet]   gear points above the terrain at their directions: %s m"
                  % ", ".join("%+.3f" % d for d in terrain), flush=True)
            # The patch's surface against the terrain's at each gear point.
            agree = len(terrain) == len(gear) and max(abs(d + t) for d, t in zip(gear, terrain)) <= self.GEAR_TOLERANCE
            print("[planet]   gear points touching: %d of %d"
                  % (sum(1 for d in gear if d > -self.GEAR_TOLERANCE), len(gear)), flush=True)
            ok = ok and self.touchdown_speed < 0.5 * ship.params.gear_crash_speed and agree
            if self.body is planet:
                ok = (ok and max(abs(d) for d in gear) <= self.GEAR_TOLERANCE
                      and max(abs(d) for d in terrain) <= self.GEAR_TOLERANCE)
        speed = self.relative_speed(ship)
        print("[planet]   speed %.4f m/s, %s, touched %s s after the start"
              % (speed, contact_text(ship) or "in the air",
                 "never" if self.touched is None else "%.1f" % (self.touched - self.start)), flush=True)
        self.landed_ok = ok and speed < 0.05
        self.start_dive()

    def start_dive(self):
        """A ship 1.5 km above the spot and 1.5 km south of it, coasting
        straight at it at DIVE_SPEED, at the highest warp."""
        height, up = self.best
        east, north = local_frame(up)
        target = self.at(up, height)
        position = add(target, add(scale(up, 1_500.0), scale(north, -1_500.0)))
        state["ship"] = flight.Ship(position=position,
                                    velocity=add(self.body.velocity(),
                                                 scale(normalize(sub(target, position)), self.DIVE_SPEED)),
                                    orientation=quat_from_basis(east, up, scale(north, -1.0)))
        state["controls"] = flight.Controls(assist=False)
        state["warp"] = len(WARPS) - 1
        world.patch = None
        world.extra_probes = []
        self.diving = world.time
        self.crash_frames = 0

    def dive(self):
        ship = state["ship"]
        if ship.crashed:
            self.crash_frames += 1
            state["warp"] = 0
        if self.crash_frames < self.FRAMES_PER_SPOT and world.time - self.diving < self.TIMEOUT_SECONDS:
            return True
        above = world.height_above_ground(viewpoint())
        print("[planet] a dive at %.0f m/s and x%d warp: %s, the centre %.2f m above the ground, impact %.0f m/s"
              % (self.DIVE_SPEED, WARPS[-1], "crashed" if ship.crashed else "not crashed", above,
                 ship.impact_speed), flush=True)
        ok = self.landed_ok and ship.crashed and self.DIVE_MIN_CENTRE_HEIGHT < above < SHIP_REACH + 2.0
        os._exit(0 if ok else 1)


class OrbitCheck:
    """--orbit-check: the ship in preset 9's circular orbit, coasting at 16x
    for one orbital period without the moon's pull (whose tide alone moves
    the orbit by about 100 m). Prints the periapsis and apoapsis at the
    start and the end and the range of heights flown, and exits with status
    0 when the heights stayed within 100 m of the orbit's, 1 otherwise."""

    TOLERANCE = 100.0

    def __init__(self):
        self.start = None
        self.low = math.inf
        self.high = -math.inf

    def __call__(self, dt):
        if planet.tree is None:
            return True
        if self.start is None:
            teleport(9)
            world.physics.moon_pulls = False
            state["warp"] = len(WARPS) - 1
            ship = state["ship"]
            self.start = world.time
            self.period = 2.0 * math.pi * math.sqrt((RADIUS + ORBIT_HEIGHT) ** 3 / world.physics.gm)
            self.orbit0 = flight.orbit(world.physics, ship.position, ship.velocity)
            return True
        ship = state["ship"]
        height = length(ship.position) - RADIUS
        self.low, self.high = min(self.low, height), max(self.high, height)
        if world.time - self.start >= self.period:
            orbit1 = flight.orbit(world.physics, ship.position, ship.velocity)
            print("[planet] one orbit (%.0f s of flight) at %d m:" % (self.period, ORBIT_HEIGHT), flush=True)
            print("[planet]   periapsis / apoapsis at the start %.1f / %.1f m, at the end %.1f / %.1f m"
                  % (self.orbit0 + orbit1), flush=True)
            print("[planet]   heights flown %.1f to %.1f m" % (self.low, self.high), flush=True)
            ok = max(abs(self.low - ORBIT_HEIGHT), abs(self.high - ORBIT_HEIGHT)) <= self.TOLERANCE
            os._exit(0 if ok else 1)
        return True


class PropRestCheck:
    """--prop-rest-check: on the planet 3 km north of the site, then on the
    moon at moon_spot, the free camera HEIGHT metres above the ground. Once
    the bubble's terrain mesh is under it, CRATES crates (CRATE_SIZE,
    CRATE_DENSITY) and SMALL boxes (SMALL_SIZE, SMALL_DENSITY) are dropped
    DROP_HEIGHT above the mesh at points of a grid around the camera where
    the mesh is no steeper than MAX_SLOPE and no fixed rock (rocks.py) is
    within ROCK_CLEAR, each square to the mesh's
    normal there and turned about it at random.
    After SETTLE_SECONDS every one must be asleep in the physics; over the
    next HOLD_SECONDS none may move more than TOLERANCE. Then the camera
    moves AWAY metres east and back until the bubble has rebased at least
    REBASES times, and back over the boxes, after RETURN_SECONDS, none may
    have moved more than TOLERANCE from where it rested. Positions are
    reckoned from the body's centre. Prints per body the slopes under the
    boxes, how many slept, the largest movements and the rebases, and exits
    with status 0 when both bodies passed, 1 otherwise."""

    HEIGHT = 6.0
    CRATES, SMALL = 20, 10
    SMALL_SIZE, SMALL_DENSITY = 0.3, 2_600.0
    DROP_HEIGHT = 0.2
    GRID_STEP = 2.0                     # m between candidate points
    GRID_REACH = 9.0                    # m from the camera candidate points reach
    MAX_SLOPE = 30.0                    # degrees
    ROCK_CLEAR = 1.0                    # m between a box and a fixed rock's sphere at least
    SETTLE_SECONDS = 10.0
    HOLD_SECONDS = 30.0
    TOLERANCE = 0.01                    # m
    AWAY = 600.0                        # m east the camera moves to make the bubble rebase
    AWAY_HEIGHT = 50.0                  # m above the ground at home while away
    MOVE_SECONDS = 1.5
    REBASES = 5
    RETURN_SECONDS = 2.0
    PLACE_FRAMES = 10                   # frames for the ground under a new place to come back

    def __init__(self):
        self.bodies = ["planet", "moon"]
        self.results = []
        self.random = random.Random(1)
        self.start_body()

    def start_body(self):
        self.stage = "place"
        self.frames = 0
        self.clock = 0.0
        self.rebases = 0
        self.moves = 0

    def __call__(self, dt):
        if planet.tree is None:
            return True
        self.frames += 1
        self.clock += dt
        getattr(self, self.stage)()
        return True

    def body(self):
        return planet if self.bodies[0] == "planet" else moon_body()

    def direction(self):
        if self.body() is planet:
            _, up, _, north = world.site_frame()
            return normalize(add(up, scale(north, 3_000.0 / RADIUS)))
        return moon_spot()

    def put_camera(self, direction, height):
        body = self.body()
        state["mode"] = "camera"
        state["position"] = add(body.centre(), scale(direction, body.radius + height))
        state["forward"] = local_frame(direction)[1]

    def next_stage(self, stage):
        self.stage, self.frames, self.clock = stage, 0, 0.0

    def place(self):
        """The camera 5 km above the spot until the ground there is known, then HEIGHT above it; the
        props of the body before gone."""
        drop_bubble()
        props.clear(None)
        self.home = self.direction()
        self.put_camera(self.home, 5_000.0)
        self.next_stage("lower")

    def lower(self):
        if self.frames < self.PLACE_FRAMES or world.ground is None:
            return
        self.ground = world.surface_height(state["position"])
        self.put_camera(self.home, self.ground + self.HEIGHT)
        self.next_stage("wait_for_mesh")

    def wait_for_mesh(self):
        if self.frames < self.PLACE_FRAMES:
            return
        bubble = ensure_bubble()
        terrain = foot.fine
        if terrain.patch is None or not terrain.covers(bubble.to_bubble(state["position"]), self.GRID_REACH + 2.0):
            return
        self.drop(bubble, terrain.patch)
        self.next_stage("settle")

    def drop(self, bubble, patch):
        """The boxes at grid points no steeper than MAX_SLOPE, nearest the camera first."""
        here = bubble.body.relative(state["position"])
        east, north = local_frame(normalize(here))
        steps = int(self.GRID_REACH / self.GRID_STEP)
        candidates = sorted(((i * i + j * j, i, j) for i in range(-steps, steps + 1)
                             for j in range(-steps, steps + 1)))
        sizes = [(CRATE_SIZE, CRATE_DENSITY)] * self.CRATES + [(self.SMALL_SIZE, self.SMALL_DENSITY)] * self.SMALL
        self.slopes = []
        for _, i, j in candidates:
            if len(self.slopes) == len(sizes):
                break
            spot = add(here, add(scale(east, i * self.GRID_STEP), scale(north, j * self.GRID_STEP)))
            normal = patch.normal(spot)
            if normal is None:
                continue
            up = normalize(spot)
            slope = math.degrees(math.acos(max(-1.0, min(1.0, dot(normal, up)))))
            if slope > self.MAX_SLOPE:
                continue
            size, density = sizes[len(self.slopes)]
            spot_bubble = sub(spot, bubble.origin)
            if any(length(sub(physics.get_transform(r.body)[0], spot_bubble)) < r.radius + size + self.ROCK_CLEAR
                   for r in scatter_rocks.rocks.values()):
                continue
            xy = patch.offset(spot)
            mid = (patch.columns - 1) // 2
            ci = min(max(int(round(xy[0] / patch.spacing)) + mid, 0), patch.columns - 1)
            cj = min(max(int(round(xy[1] / patch.spacing)) + mid, 0), patch.columns - 1)
            ground = scale(up, bubble.body.radius + float(patch.heights[cj, ci]))
            centre = add(ground, scale(normal, self.DROP_HEIGHT + 0.5 * size))
            east_n = normalize(sub(east, scale(normal, dot(east, normal))))
            rotation = yaw_rotation(east_n, normal, cross(east_n, normal), self.random.uniform(0.0, 2.0 * math.pi))
            props.spawn_crate(bubble, sub(centre, bubble.origin), rotation, size, density)
            self.slopes.append(slope)

    def positions(self):
        return [prop_state(c)[1] for c in props.crates]

    def settle(self):
        if self.clock < self.SETTLE_SECONDS:
            return
        self.asleep = sum(1 for c in props.crates if c.body is not None and not physics.is_active(c.body))
        self.frozen = sum(1 for c in props.crates if c.body is None)
        self.rested = self.positions()
        self.next_stage("hold")

    def largest_move(self):
        return max(length(sub(a, b)) for a, b in zip(self.positions(), self.rested))

    def hold(self):
        if self.clock < self.HOLD_SECONDS:
            return
        self.held_move = self.largest_move()
        bubble = foot.bubble
        bubble.listeners.append(self.count_rebase)
        self.next_stage("move")

    def count_rebase(self, offset):
        self.rebases += 1

    def move(self):
        if self.frames == 1:
            away = self.moves % 2 == 0
            east, _ = local_frame(self.home)
            direction = normalize(add(self.home, scale(east, self.AWAY / self.body().radius))) if away else self.home
            self.put_camera(direction, self.ground + (self.AWAY_HEIGHT if away else self.HEIGHT))
            self.moves += 1
        if self.clock < self.MOVE_SECONDS:
            return
        if self.rebases >= self.REBASES and self.moves % 2 == 0:
            self.next_stage("back")
        else:
            self.next_stage("move")

    def back(self):
        if self.clock < self.RETURN_SECONDS:
            return
        moved = self.largest_move()
        count = len(props.crates)
        ok = (count == self.CRATES + self.SMALL and self.asleep == count and self.frozen == 0
              and self.held_move <= self.TOLERANCE and moved <= self.TOLERANCE and self.rebases >= self.REBASES)
        name = self.bodies[0]
        print("[planet] prop rest on the %s: %d boxes on slopes of %.1f to %.1f degrees"
              % (name, count, min(self.slopes), max(self.slopes)), flush=True)
        print("[planet]   after %.0f s: %d asleep, %d out of the physics"
              % (self.SETTLE_SECONDS, self.asleep, self.frozen), flush=True)
        print("[planet]   largest move over the next %.0f s: %.4f m" % (self.HOLD_SECONDS, self.held_move), flush=True)
        print("[planet]   after %d rebases (%d terrain meshes built): largest move %.4f m"
              % (self.rebases, foot.fine.builds, moved), flush=True)
        if not ok:
            patch = foot.fine.patch
            for k, (crate, now, rested) in enumerate(zip(props.crates, self.positions(), self.rested)):
                active = crate.body is not None and physics.is_active(crate.body)
                if length(sub(now, rested)) > self.TOLERANCE or active:
                    normal = patch.normal(now) if patch is not None else None
                    under = (math.degrees(math.acos(max(-1.0, min(1.0, dot(normal, normalize(now))))))
                             if normal is not None else math.nan)
                    where = ", awake" if active else ""
                    where += ", on the coarse mesh" if crate.body is not None and crate.far else ""
                    where += ", " + crate.state if crate.body is None else ""
                    rocks_near = [length(sub(add(foot.bubble.origin, physics.get_transform(r.body)[0]), rested))
                                  - r.radius for r in scatter_rocks.rocks.values()]
                    if rocks_near:
                        where += ", %.2f m from a fixed rock where it rested" % max(min(rocks_near), 0.0)
                    print("[planet]   box %d (%.1f m): dropped on %.1f degrees, moved %.3f m, now on %.1f degrees%s"
                          % (k, crate.size, self.slopes[k], length(sub(now, rested)), under, where), flush=True)
        print("[planet]   %s" % ("passed" if ok else "FAILED"), flush=True)
        self.results.append(ok)
        self.bodies.pop(0)
        drop_bubble()
        if not self.bodies:
            os._exit(0 if all(self.results) else 1)
        self.start_body()


def point_triangle_distance(p, a, b, c):
    """The distance from point `p` to the triangle a, b, c (numpy arrays):
    to the nearest point of its plane inside it, else of its edges."""
    n = np.cross(b - a, c - a)
    n /= np.linalg.norm(n)
    q = p - np.dot(p - a, n) * n
    if all(np.dot(np.cross(v1 - v0, q - v0), n) >= 0.0 for v0, v1 in ((a, b), (b, c), (c, a))):
        return float(np.linalg.norm(p - q))
    best = math.inf
    for v0, v1 in ((a, b), (b, c), (c, a)):
        e = v1 - v0
        t = min(1.0, max(0.0, float(np.dot(p - v0, e) / np.dot(e, e))))
        best = min(best, float(np.linalg.norm(p - (v0 + t * e))))
    return best


class WalkCheck:
    """--walk-check: on the planet 3 km north of the site, on the planet in
    the mountains 600 m south-west of near_mountain's peak, then on the moon
    at moon_spot, a player put down from the free camera walks a circle of
    LAP metres at WALK speed, then at RUN, its heading turned each frame by
    its distance along the ground over the circle's radius, and by
    STUCK_TURN more when it covered less than STUCK_DISTANCE in the last
    second (stopped by ground too steep to climb). While it stands on
    the terrain mesh (not on a rock), the gap between its capsule's lower sphere and the nearest
    triangle of the terrain mesh around its feet is measured. The feet must stay
    within TOLERANCE of the ground and never more than BELOW under it, and
    each lap must cover at least MIN_LAP_SHARE of its length. Every
    SAMPLE_EVERY metres the player stops for SAMPLE_WAIT, the level of
    detail is frozen and ChunkPool read back, and the terrain mesh's height is compared with the
    drawn surface's (drawn_height) under the feet and at SAMPLE_POINTS
    points SAMPLE_RING metres around them, which must stay within
    MATCH_TOLERANCE of each other. Prints per body and speed the
    distance, the share of the time on the ground, the gaps' range and the
    largest differences between the mesh and the drawn surface, and exits
    with status 0 when every lap passed, 1 otherwise."""

    LAP = 200.0
    TOLERANCE = 0.05
    BELOW = 0.01
    MIN_LAP_SHARE = 0.9
    START_HEIGHT = 2.0
    SETTLE_SECONDS = 2.0
    PLACE_FRAMES = 10
    SAMPLE_EVERY = 20.0
    SAMPLE_RING = 1.0
    SAMPLE_POINTS = 8
    MATCH_TOLERANCE = 0.1
    SAMPLE_WAIT = 1.0                   # s standing before the level of detail is frozen
    STUCK_DISTANCE = 1.0
    STUCK_TURN = math.radians(45.0)

    def __init__(self):
        self.bodies = ["planet", "mountains", "moon"]
        self.results = []
        self.start_body()

    def start_body(self):
        self.stage, self.frames, self.clock = "place", 0, 0.0
        self.speeds = ["walk", "run"]

    def __call__(self, dt):
        if planet.tree is None:
            return True
        self.frames += 1
        self.clock += dt
        getattr(self, self.stage)(dt)
        return True

    def body(self):
        return moon_body() if self.bodies[0] == "moon" else planet

    def next_stage(self, stage):
        self.stage, self.frames, self.clock = stage, 0, 0.0

    def home_direction(self):
        """The direction from the body's centre the player is put down at."""
        _, up, east, north = world.site_frame()
        if self.bodies[0] == "planet":
            return normalize(add(up, scale(north, 3_000.0 / RADIUS)))
        if self.bodies[0] == "mountains":
            azimuth = math.radians(67.5)
            level = add(scale(east, math.cos(azimuth)), scale(north, math.sin(azimuth)))
            peak = normalize(add(up, scale(level, 60_000.0 / RADIUS)))
            peak_east, peak_north = local_frame(peak)
            return normalize(add(peak, scale(normalize(add(peak_east, peak_north)), -600.0 / RADIUS)))
        return moon_spot()

    def place(self, dt):
        drop_bubble()
        state["scripted_controls"] = True
        body = self.body()
        self.home = self.home_direction()
        state["mode"] = "camera"
        state["position"] = add(body.centre(), scale(self.home, body.radius + 5_000.0))
        state["forward"] = local_frame(self.home)[1]
        self.next_stage("lower")

    def lower(self, dt):
        if self.frames < self.PLACE_FRAMES or world.ground is None:
            return
        body = self.body()
        ground = world.surface_height(state["position"])
        state["position"] = add(body.centre(), scale(self.home, body.radius + ground + self.START_HEIGHT))
        self.next_stage("spawn")

    def spawn(self, dt):
        if self.frames < self.PLACE_FRAMES or world.ground is None:
            return
        go_on_foot()
        if foot.player is not None:
            self.next_stage("settle")

    def settle(self, dt):
        player = foot.player
        if player.held or not player.on_ground:
            self.clock = 0.0
        if self.clock >= self.SETTLE_SECONDS:
            self.start_lap()

    def start_lap(self):
        self.next_stage("lap")
        self.distance = 0.0
        self.ground_time = self.time = 0.0
        self.low, self.high = math.inf, -math.inf
        self.last = foot.bubble.body_relative(foot.player.feet())
        self.next_sample = self.SAMPLE_EVERY
        self.mesh_above = self.mesh_below = 0.0     # m the mesh was above / below the drawn surface at most
        self.turns = 0
        self.levels = set()                         # levels of the chunks the drawn surface was measured on
        self.second = (0.0, 0.0)                    # (lap time, distance) a second ago

    def gap(self):
        """The gap between the capsule's lower sphere and the nearest of
        the mesh's triangles within two grid cells of the feet (negative
        where it sinks in), or None off the mesh."""
        player, bubble = foot.player, foot.bubble
        patch = foot.fine.patch
        feet = bubble.body_relative(player.feet())
        xy = patch.offset(feet) if patch is not None else None
        if xy is None:
            return None
        mid, last = (patch.columns - 1) // 2, patch.columns - 1
        i, j = int(math.floor(xy[0] / patch.spacing)) + mid, int(math.floor(xy[1] / patch.spacing)) + mid
        if not (2 <= i <= last - 3 and 2 <= j <= last - 3):
            return None
        centre = np.asarray(add(feet, scale(normalize(feet), PLAYER_RADIUS)), dtype=np.float64)
        p = patch.points()
        nearest = math.inf
        for jj in range(j - 2, j + 3):
            for ii in range(i - 2, i + 3):
                a, b, c, d = p[jj, ii], p[jj, ii + 1], p[jj + 1, ii], p[jj + 1, ii + 1]
                nearest = min(nearest, point_triangle_distance(centre, a, b, c),
                              point_triangle_distance(centre, b, d, c))
        return nearest - PLAYER_RADIUS

    def lap(self, dt):
        player, bubble = foot.player, foot.bubble
        controls = foot.controls
        controls.forward, controls.strafe = 1.0, 0.0
        controls.run = self.speeds[0] == "run"
        controls.crouch = controls.jet = controls.jump = False
        feet = bubble.body_relative(player.feet())
        up = normalize(feet)
        moved = sub(feet, self.last)
        along = length(sub(moved, scale(up, dot(moved, up))))
        self.last = feet
        self.distance += along
        self.time += dt
        player.heading = rotate(player.heading, up, along / (self.LAP / (2.0 * math.pi)))
        if self.time - self.second[0] >= 1.0:
            if self.distance - self.second[1] < self.STUCK_DISTANCE:
                player.heading = rotate(player.heading, up, self.STUCK_TURN)
                self.turns += 1
            self.second = (self.time, self.distance)
        if player.on_ground and not player.held:
            self.ground_time += dt
            on_terrain = physics.character_ground(player.character)[3] == foot.fine.mesh
            gap = self.gap() if on_terrain else None
            if gap is not None:
                self.low, self.high = min(self.low, gap), max(self.high, gap)
        if self.distance >= self.LAP or self.time > 4.0 * self.LAP / (RUN_SPEED if controls.run else WALK_SPEED):
            self.finish_lap()
        elif self.distance >= self.next_sample:
            self.next_sample += self.SAMPLE_EVERY
            self.stage, self.frames = "sample", 0

    def sample(self, dt):
        """Standing still with the level of detail frozen: the mesh against
        the drawn surface around the feet, once ChunkPool comes back."""
        controls = foot.controls
        controls.forward = 0.0
        if self.frames == 1:
            self.waited = 0.0
        if self.waited < self.SAMPLE_WAIT:
            self.waited += dt
            return
        if not world.frozen:
            world.frozen = True
            previous = engine.get_readback("ChunkPool")
            self.after = previous["frame"] if previous is not None else -1
            engine.request_readback("ChunkPool")
            return
        readback = engine.get_readback("ChunkPool")
        if readback is None or readback["frame"] <= self.after:
            return
        gpu = np.frombuffer(readback["data"], dtype=np.float32).reshape(SLOTS, VERTICES_PER_CHUNK,
                                                                        FLOATS_PER_VERTEX)
        bubble, patch = foot.bubble, foot.fine.patch
        feet = bubble.body_relative(foot.player.feet())
        east, north = local_frame(normalize(feet))
        points = [feet] + [add(feet, add(scale(east, self.SAMPLE_RING * math.cos(a)),
                                         scale(north, self.SAMPLE_RING * math.sin(a))))
                           for a in (2.0 * math.pi * k / self.SAMPLE_POINTS for k in range(self.SAMPLE_POINTS))]
        for p in points:
            mesh, drawn = patch.height(p), drawn_height(bubble.body, gpu, p)
            if mesh is not None and drawn is not None:
                self.mesh_above = max(self.mesh_above, mesh - drawn[0])
                self.mesh_below = max(self.mesh_below, drawn[0] - mesh)
                self.levels.add(drawn[1])
        world.frozen = False
        self.last = feet
        self.stage, self.frames = "lap", 0

    def finish_lap(self):
        name, speed = self.bodies[0], self.speeds.pop(0)
        ok = (self.distance >= self.MIN_LAP_SHARE * self.LAP and self.low >= -self.BELOW
              and self.high <= self.TOLERANCE and max(self.mesh_above, self.mesh_below) <= self.MATCH_TOLERANCE)
        print("[planet] walk on the %s, %s: %.1f m in %.1f s, %.0f %% on the ground, feet %+.3f to %+.3f m from it, "
              "the mesh up to %.3f m above and %.3f m below the drawn surface (chunks of level %s), "
              "%d turns away from steep ground: %s"
              % (name, speed, self.distance, self.time, 100.0 * self.ground_time / max(self.time, 1e-9),
                 self.low, self.high, self.mesh_above, self.mesh_below,
                 ", ".join(str(v) for v in sorted(self.levels)), self.turns, "passed" if ok else "FAILED"),
              flush=True)
        self.results.append(ok)
        if self.speeds:
            self.start_lap()
            return
        self.bodies.pop(0)
        drop_bubble()
        if not self.bodies:
            os._exit(0 if all(self.results) else 1)
        self.start_body()


class GunCheck(WalkCheck):
    """The gravity gun's checks: WalkCheck's player put down on the planet
    3 km north of the site, then on the moon, standing still, then the
    stages from `begin` on each body. Exits with status 0 when every body
    passed, 1 otherwise."""

    GRAB_TIME = 0.1                     # s the grab control is held, as a click
    GRAB_WAIT = 0.1                     # s after it a crate must be held by

    def __init__(self):
        self.bodies = ["planet", "moon"]
        self.results = []
        self.start_body()

    def start_lap(self):
        foot.controls = FootControls()
        self.begin()

    def crate_ahead(self, mass, distance=None):
        """A crate of `mass` kg on the view's line, its centre `distance`
        m from the eye (where the gun holds it when None), turned with
        the view."""
        player, gun = foot.player, foot.gun
        right, up, forward = player.basis()
        if distance is None:
            distance = gun.hold_distance + 0.5 * math.sqrt(3.0) * CRATE_SIZE
        return props.spawn_crate(foot.bubble, add(player.eye(), scale(forward, distance)),
                                 quat_from_basis(right, up, scale(forward, -1.0)), CRATE_SIZE, mass=mass)

    def grab(self):
        """The grab control held for GRAB_TIME from the stage's 3rd frame;
        True once GRAB_WAIT more has passed."""
        controls = foot.gun_controls
        if self.frames == 3:
            self.grab_clock = self.clock
        pressed = self.clock - self.grab_clock if self.frames >= 3 else -1.0
        controls.grab = 0.0 <= pressed < self.GRAB_TIME
        return pressed >= self.GRAB_TIME + self.GRAB_WAIT

    def off_hold_point(self):
        player, gun = foot.player, foot.gun
        p = physics.get_transform(gun.held)[0]
        return length(sub(p, gun.hold_point(player.eye(), player.basis()[2])))

    def clear(self):
        foot.gun.drop()
        props.clear(foot.bubble)

    def next_body(self, ok):
        self.results.append(ok)
        self.bodies.pop(0)
        drop_bubble()
        if not self.bodies:
            os._exit(0 if all(self.results) else 1)
        self.start_body()


class HoldCheck(GunCheck):
    """--hold-check: on each body the gun grabs a HOLD_MASS kg crate, holds
    it for STEADY seconds, then the player turns a full circle in
    TURN_TIME, walks WALK metres, jumps and lands, and the bubble is
    rebased under it. The crate must stay held and within
    GunParams.drop_distance of the hold point throughout, and move by less
    than REBASE_JUMP relative to the eye across the rebase. Then a
    HEAVY_MASS kg crate in front must be too heavy on the planet
    (HEAVY_MASS x 4.905 m/s^2 is more than 0.9 hold_force) and be grabbed
    on the moon. Prints per body how far off the hold point the crate got
    in each stage."""

    HOLD_MASS = 50.0
    HEAVY_MASS = 2_000.0
    STEADY = 1.0
    TURN_TIME = 2.0
    WALK = 30.0
    LAND_TIME = 0.5                     # s on the ground after the jump
    JUMP_TIMEOUT = 15.0
    REBASE_FRAMES = 30
    REBASE_JUMP = 0.01

    def begin(self):
        self.next_stage("pick")
        self.off = {}                   # stage: m off the hold point at most
        self.lost = None                # the stage the crate was dropped in
        self.rebase_jump = None
        self.heavy_status = None        # the gun's status aimed at the heavy crate
        self.heavy_held = False

    def pick(self, dt):
        if self.frames == 1:
            self.crate = self.crate_ahead(self.HOLD_MASS)
        if not self.grab():
            return
        if foot.gun.held != self.crate.body:
            self.lost = "pick"
            self.finish()
            return
        self.next_stage("steady")

    def measure(self):
        """The crate's distance from the hold point in this stage, or the
        stage noted when it was dropped; False once dropped."""
        if foot.gun.held != self.crate.body:
            self.lost = self.lost or self.stage
            return False
        self.off[self.stage] = max(self.off.get(self.stage, 0.0), self.off_hold_point())
        return True

    def steady(self, dt):
        if not self.measure():
            self.finish()
        elif self.clock >= self.STEADY:
            self.turned = 0.0
            self.next_stage("turn")

    def turn(self, dt):
        if not self.measure():
            self.finish()
            return
        player = foot.player
        angle = min(2.0 * math.pi - self.turned, 2.0 * math.pi * dt / self.TURN_TIME)
        player.heading = rotate(player.heading, player.up(), angle)
        self.turned += angle
        if self.turned >= 2.0 * math.pi - 1e-9:
            self.last = foot.bubble.body_relative(player.feet())
            self.walked = 0.0
            self.next_stage("walk")

    def walk(self, dt):
        if not self.measure():
            self.finish()
            return
        controls = foot.controls
        controls.forward = 1.0
        feet = foot.bubble.body_relative(foot.player.feet())
        moved = sub(feet, self.last)
        up = normalize(feet)
        self.walked += length(sub(moved, scale(up, dot(moved, up))))
        self.last = feet
        if self.walked >= self.WALK or self.clock > 4.0 * self.WALK / WALK_SPEED:
            controls.forward = 0.0
            self.left_ground = False
            self.landed = 0.0
            self.next_stage("jump")

    def jump(self, dt):
        if not self.measure():
            self.finish()
            return
        player = foot.player
        if self.frames == 1:
            foot.controls.jump = True
        if not player.on_ground:
            self.left_ground = True
            self.landed = 0.0
        elif self.left_ground:
            self.landed += dt
        if self.landed >= self.LAND_TIME or self.clock > self.JUMP_TIMEOUT:
            self.next_stage("rebase")

    def rebase(self, dt):
        if not self.measure():
            self.finish()
            return
        player, bubble = foot.player, foot.bubble
        offset = sub(physics.get_transform(self.crate.body)[0], player.eye())
        if self.frames == 1:
            self.before = offset
            bubble.rebase(player.feet())
        elif self.frames == 2:
            self.rebase_jump = length(sub(offset, self.before))
        elif self.frames >= self.REBASE_FRAMES:
            self.clear()
            self.next_stage("heavy")

    def heavy(self, dt):
        gun = foot.gun
        if self.frames == 1:
            self.heavy_crate = self.crate_ahead(self.HEAVY_MASS, distance=3.0)
        if self.frames == 3:
            self.heavy_status = gun.status
        if not self.grab():
            return
        self.heavy_held = gun.held == self.heavy_crate.body
        self.finish()

    def finish(self):
        name = self.bodies[0]
        limit = foot.gun.p.drop_distance
        heavy = self.heavy_status
        heavy_ok = (heavy == "too_heavy" and not self.heavy_held) if name == "planet" else \
            (heavy == "can_grab" and self.heavy_held)
        ok = (self.lost is None and all(v <= limit for v in self.off.values())
              and self.rebase_jump is not None and self.rebase_jump < self.REBASE_JUMP and heavy_ok)
        stages = ", ".join("%s %.3f" % (stage, self.off.get(stage, math.nan))
                           for stage in ("steady", "turn", "walk", "jump", "rebase"))
        print("[planet] hold on the %s: a %.0f kg crate %s, at most m off the hold point: %s; it moved %s "
              "relative to the eye across the rebase; a %.0f kg crate %s and %s: %s"
              % (name, self.HOLD_MASS, "held throughout" if self.lost is None else "DROPPED in " + self.lost,
                 stages, "%.4f m" % self.rebase_jump if self.rebase_jump is not None else "-",
                 self.HEAVY_MASS, heavy, "grabbed" if self.heavy_held else "not grabbed",
                 "passed" if ok else "FAILED"), flush=True)
        self.clear()
        self.next_body(ok)


class PuntCheck(GunCheck):
    """--punt-check: on each body the gun grabs crates of MASSES kg in turn,
    holds each for STEADY seconds and punts it (Gun.punt) with the view
    level. Right after the punt the crate's velocity along the view must
    have grown by min(punt_speed, punt_impulse / mass) and the player's
    velocity against it by min(MAX_RECOIL, recoil_factor x the impulse /
    the player's mass), each within TOLERANCE. Then the player looks
    straight down and punts the ground with the punt control: it must rise
    within JUMP_TOLERANCE of MAX_RECOIL^2 / 2g above where it stood.
    Prints per body the speeds, the recoils and the height."""

    MASSES = (5.0, 50.0, 500.0)
    STEADY = 1.0
    TOLERANCE = 0.02
    JUMP_TOLERANCE = 0.1
    JUMP_TIMEOUT = 30.0

    def begin(self):
        self.masses = list(self.MASSES)
        self.punts = []                 # (mass, speed, wanted speed, recoil, wanted recoil)
        self.next_stage("pick")

    def pick(self, dt):
        if self.frames == 1:
            self.crate = self.crate_ahead(self.masses[0])
        if not self.grab():
            return
        if foot.gun.held != self.crate.body:
            self.punts.append((self.masses[0], math.nan, 0.0, math.nan, 0.0))
            self.after_punt()
            return
        self.next_stage("steady")

    def steady(self, dt):
        if self.clock < self.STEADY:
            return
        player, gun = foot.player, foot.gun
        forward = player.basis()[2]
        body, character = self.crate.body, player.character
        v0, p0 = physics.get_velocity(body)[0], physics.character_velocity(character)
        gun.punt(body, forward)
        v1, p1 = physics.get_velocity(body)[0], physics.character_velocity(character)
        m = self.masses[0]
        speed = min(gun.p.punt_speed, gun.p.punt_impulse / m)
        recoil = min(gun_module.MAX_RECOIL, gun.p.recoil_factor * m * speed / PLAYER_MASS)
        self.punts.append((m, dot(sub(v1, v0), forward), speed, -dot(sub(p1, p0), forward), recoil))
        self.after_punt()

    def after_punt(self):
        self.clear()
        self.masses.pop(0)
        if self.masses:
            self.next_stage("pick")
        else:
            foot.player.pitch = -math.radians(89.0)
            self.next_stage("ground")

    def ground(self, dt):
        player, bubble = foot.player, foot.bubble
        height = length(bubble.body_relative(player.feet()))
        if self.frames == 10:
            self.start_height = height
            self.top = height
            self.left_ground = False
            foot.gun_controls.punt = True
        elif self.frames > 10:
            self.top = max(self.top, height)
            if not player.on_ground:
                self.left_ground = True
            elif self.left_ground or self.clock > self.JUMP_TIMEOUT:
                self.finish()

    def finish(self):
        name, gun = self.bodies[0], foot.gun
        g = length(foot.bubble.gravity(foot.player.feet()))
        wanted_height = gun_module.MAX_RECOIL ** 2 / (2.0 * g)
        rise = self.top - self.start_height
        ok = abs(rise - wanted_height) <= self.JUMP_TOLERANCE * wanted_height
        rows = []
        for m, speed, wanted, recoil, wanted_recoil in self.punts:
            ok = ok and abs(speed - wanted) <= self.TOLERANCE * wanted
            ok = ok and abs(recoil - wanted_recoil) <= self.TOLERANCE * wanted_recoil
            rows.append("%.0f kg at %.2f m/s (%.2f), recoil %.2f m/s (%.2f)" % (m, speed, wanted, recoil, wanted_recoil))
        print("[planet] punt on the %s: %s; punting the ground lifted the player %.2f m (%.2f): %s"
              % (name, "; ".join(rows), rise, wanted_height, "passed" if ok else "FAILED"), flush=True)
        self.clear()
        self.next_body(ok)


class PromotionCheck(GunCheck):
    """--promotion-check: in the mountains (WalkCheck's place) and on the
    moon, once the player has stood still with rocks around it, the gun
    grabs each of the rocks within REACH of the feet in turn (up to
    TARGETS; those too heavy are skipped): the view is turned to the rock
    and held still for STILL_FRAMES, the grab control pressed until the
    rock comes loose (Rocks.promote), the view held still STILL_FRAMES
    more, and the rock dropped. Then a STRIKE_MASS kg crate is thrown down
    at STRIKE_SPEED onto the nearest fixed rock left within REACH, which
    must come loose within STRIKE_TIMEOUT. Each frame, by the stamps of the nearby
    lists (nearby_trees: the scatter lists every rock it draws near the
    camera), the cells the scatter listed and the cells of the loose
    rocks' entities are kept: for WINDOW frames before a promotion's frame
    its rock must be listed and have no entity, and from that frame on for
    WINDOW frames have an entity and not be listed, so it is drawn exactly
    once. Prints per place the rocks promoted, and exits with status 0 when
    every place promoted at least one rock and every promotion passed, 1
    otherwise."""

    REACH = 20.0
    TARGETS = 12
    STILL_FRAMES = 12
    WINDOW = 3
    GRAB_TIMEOUT = 4.0

    def __init__(self):
        self.bodies = ["mountains", "moon"]
        self.results = []
        self.start_body()
        promote = scatter_rocks.promote

        def recorded(bubble, body):
            key = scatter_rocks.bodies.get(body)
            done = promote(bubble, body)
            if done:
                self.promotions.append((nearby_trees.stamp + 1, key))
            return done
        scatter_rocks.promote = recorded

    def begin(self):
        self.listed, self.entities, self.promotions = {}, {}, []
        self.skipped = 0
        self.struck = False
        self.next_stage("find")

    def count(self):
        """This tick's cells: the loose rocks' after the last frame (its
        nearby stamp), and the scatter's of whichever list came back."""
        self.entities[nearby_trees.stamp] = {p.key for p in props.rocks}
        readback = engine.get_readback("NearbyTrees")
        if readback is not None:
            stamp, keys = listed_keys(readback["data"])
            self.listed[stamp] = keys

    def __call__(self, dt):
        if planet.tree is not None and self.stage in ("find", "aim", "grab", "after", "strike", "strike_after"):
            self.count()
        return super().__call__(dt)

    def find(self, dt):
        if not scatter_rocks.rocks or self.clock < self.STEADY:
            if self.clock > 30.0:
                self.finish()
            return
        player, bubble = foot.player, foot.bubble
        feet = player.feet()
        near = sorted((length(sub(physics.get_transform(r.body)[0], feet)), r.body)
                      for r in scatter_rocks.rocks.values())
        self.targets = [body for d, body in near if d <= self.REACH][:self.TARGETS]
        self.next_target()

    STEADY = 1.0
    STRIKE_MASS = 50.0
    STRIKE_SPEED = 8.0
    STRIKE_TIMEOUT = 2.0

    def strike(self, dt):
        """A crate of STRIKE_MASS thrown down at STRIKE_SPEED onto the
        nearest fixed rock within REACH, in view: it must come loose."""
        player = foot.player
        if self.frames == 1:
            feet = player.feet()
            near = sorted((length(sub(physics.get_transform(r.body)[0], feet)), r.body)
                          for r in scatter_rocks.rocks.values())
            near = [body for d, body in near if 2.0 < d <= self.REACH]
            self.rock = near[0] if near else None
            if self.rock is None:
                self.finish()
                return
            self.aim_at()
        if self.frames == self.STILL_FRAMES:
            bubble = foot.bubble
            p = physics.get_transform(self.rock)[0]
            up = bubble.up(p)
            right, _, forward = player.basis()
            self.strike_crate = props.spawn_crate(bubble, add(p, scale(up, 2.0)), quat_from_basis(
                right, up, cross(right, up)), 0.3, mass=self.STRIKE_MASS)
            physics.set_velocity(self.strike_crate.body, scale(up, -self.STRIKE_SPEED))
            self.strike_promotions = len(self.promotions)
        if self.frames > self.STILL_FRAMES:
            self.struck = len(self.promotions) > self.strike_promotions
            if self.struck or self.clock > self.STRIKE_TIMEOUT + 0.5:
                self.next_stage("strike_after")

    def strike_after(self, dt):
        if self.frames >= self.STILL_FRAMES:
            self.finish()

    def next_target(self):
        if not self.targets:
            self.next_stage("strike")
            return
        self.rock = self.targets.pop(0)
        if not scatter_rocks.is_static_rock(self.rock):
            self.next_target()
            return
        self.next_stage("aim")

    def aim_at(self):
        player = foot.player
        to = normalize(sub(physics.get_transform(self.rock)[0], player.eye()))
        up = player.up()
        player.heading = player.level(to, up)
        player.pitch = math.asin(max(-1.0, min(1.0, dot(to, up))))

    def aim(self, dt):
        if self.frames == 1:
            self.aim_at()
        if self.frames < self.STILL_FRAMES:
            return
        gun = foot.gun
        if gun.status == "too_heavy" or gun.target is None or gun.target.body != self.rock:
            self.skipped += 1
            self.next_target()
            return
        self.grab_clock = 0.0
        self.next_stage("grab")

    def grab(self, dt):
        controls = foot.gun_controls
        controls.grab = self.clock < 0.1 or (foot.gun.held is None and self.clock < self.GRAB_TIMEOUT)
        if scatter_rocks.is_static_rock(self.rock) and self.clock < self.GRAB_TIMEOUT:
            return
        controls.grab = False
        self.next_stage("after")

    def after(self, dt):
        if self.frames < self.STILL_FRAMES:
            return
        controls = foot.gun_controls
        controls.grab = self.frames < self.STILL_FRAMES + 6
        if controls.grab:
            return
        foot.gun.drop()
        self.next_target()

    def finish(self):
        foot.gun_controls.grab = False
        name = self.bodies[0]
        rows, ok = [], bool(self.promotions)
        for p, key in self.promotions:
            stamps = range(p - self.WINDOW, p + self.WINDOW)
            if not all(s in self.listed and s in self.entities for s in stamps):
                rows.append("frame %d: lists missing" % p)
                ok = False
                continue
            # Per frame: (drawn by the scatter, drawn as an entity).
            seen = [(key in self.listed[s], key in self.entities[s]) for s in stamps]
            good = all(drawn == ((True, False) if s < p else (False, True)) for s, drawn in zip(stamps, seen))
            ok = ok and good
            rows.append("ok" if good else "frame %d: %s" % (p, seen))
        ok = ok and self.struck
        print("[planet] promotion in the %s: %d rocks promoted, %d too heavy; the thrown crate %s its rock; "
              "drawn once around each: %s: %s"
              % (name, len(self.promotions), self.skipped, "knocked loose" if self.struck else "did NOT loosen",
                 ", ".join(rows) or "-", "passed" if ok else "FAILED"), flush=True)
        self.clear()
        self.next_body(ok)


class OrbitCrateCheck(GunCheck):
    """--orbit-crate-check: on the moon (WalkCheck's place), a MASS kg crate
    is put HEIGHT above the player's feet (at least CLEAR above the moon's
    radius) moving level along the view at the circular speed there, and
    tracked, with the time warp at WARP. It must leave the physics bubble
    and fly; one period of its circular orbit after it was put there, its
    position (taken back to that moment along its velocity) must be within
    TOLERANCE of where it started; and coming round it must come back into
    the physics within TIMEOUT more seconds of the world's clock. Prints
    the period, how far the bubble's origin it got, the error after one
    period and its state at the end, and exits with status 0 when all of
    these held, 1 otherwise."""

    MASS = 10.0
    SIZE = 0.4
    HEIGHT = 1_800.0
    CLEAR = 1_700.0
    WARP = 4                            # index into WARPS: 16x
    TOLERANCE = 50.0
    TIMEOUT = 120.0

    def __init__(self):
        self.bodies = ["moon"]
        self.results = []
        self.start_body()

    def begin(self):
        player, bubble = foot.player, foot.bubble
        feet = bubble.body_relative(player.feet())
        up = normalize(feet)
        r = max(length(feet) + self.HEIGHT, bubble.body.radius + self.CLEAR)
        self.start = scale(up, r)
        gm = world.physics.moon.gm
        speed = math.sqrt(gm / r)
        heading = player.level(player.basis()[2], up)
        self.period = 2.0 * math.pi * math.sqrt(r ** 3 / gm)
        right = normalize(cross(heading, up))
        crate = props.spawn_crate(bubble, sub(self.start, bubble.origin),
                                  quat_from_basis(right, up, scale(heading, -1.0)), self.SIZE, mass=self.MASS)
        physics.set_velocity(crate.body, scale(heading, speed))
        self.crate = crate
        state["tracked"] = crate
        state["warp"] = self.WARP
        self.t0 = world.time
        self.farthest = 0.0
        self.flew = False
        self.back_in = False
        self.error = None
        self.last = None
        self.next_stage("orbit")

    def orbit(self, dt):
        crate, bubble = self.crate, foot.bubble
        body, position, velocity = prop_state(crate)
        self.farthest = max(self.farthest, length(sub(position, bubble.origin)))
        if crate.state == "flying":
            self.flew = True
        t = world.time - self.t0
        if self.error is None and t >= self.period and self.last is not None:
            back = scale(velocity, t - self.period)
            self.error = length(sub(sub(position, back), self.start))
        self.last = t
        self.back_in = self.back_in or (self.flew and crate.body is not None)
        if (self.back_in and self.error is not None) or t > self.period + self.TIMEOUT:
            self.finish(self.back_in)

    def finish(self, back_in):
        state["warp"] = 0
        ok = self.flew and back_in and self.error is not None and self.error <= self.TOLERANCE
        print("[planet] orbit crate on the moon: period %.0f s, %s from the bubble's origin at most, %s after one "
              "period, %s: %s" % (self.period, distance_text(self.farthest),
                                  "%.1f m off" % self.error if self.error is not None else "no period",
                                  "back in the physics" if back_in else "NOT back (%s)" % self.crate.state,
                                  "passed" if ok else "FAILED"), flush=True)
        self.clear()
        self.next_body(ok)


class NearbyLists:
    """The lists of nearby trees and rocks that come back (nearby_trees.read,
    which this wraps): `trees` the last, `count` how many came back."""

    def __init__(self):
        self.trees, self.count = None, 0
        read = nearby_trees.read

        def kept():
            trees = read()
            if trees is not None:
                self.trees, self.count = trees, self.count + 1
            return trees
        nearby_trees.read = kept

    def rocks(self, radius, around=None):
        """The rocks of the last list on the bubble's body within `radius` of
        `around` (relative to its centre; the player's feet when None)."""
        bubble = foot.bubble
        if around is None:
            around = bubble.body_relative(foot.player.feet())
        return [t for t in self.trees or [] if t.rock and t.body == bubble.body.index
                and length(sub(t.base, around)) <= radius]


class SaveCheck(GunCheck):
    """--save-check: in the mountains (WalkCheck's place), once the player
    has stood still and a list of the nearby rocks came back, the cells of
    the rocks listed within INNER (and OUTER) of the feet are kept; up to
    ROCKS fixed rocks nearest the feet within REACH come loose and are
    thrown up and away (HOP, AWAY), and a CRATE_MASS kg crate is thrown at
    THROW m/s 45 degrees up along the view. Once the crate is out of the
    physics and the rocks are asleep (or after WAIT seconds), the game is
    saved to PATH, a new seed builds the planet again (which must remove
    its props and put its rocks back), and the save is loaded: the taken
    rocks and the props' records must then equal the saved ones. Once the
    planet is built again and SETTLE seconds have passed, a new list of the
    rocks must hold every kept cell within INNER that is not taken, none
    that is taken, and none within INNER that was not kept within OUTER; and
    the loose rocks back in the physics must lie within MOVED of their
    records. Then the save is made one of version 1 and loaded
    (version_1). Prints what it found and exits with status 0 when all of
    these held, 1 otherwise."""

    ROCKS = 10
    REACH = 20.0
    STEADY = 1.0                        # s standing before the rocks are listed
    INNER, OUTER = 18.0, 24.0
    HOP, AWAY = 3.0, 2.0                # m/s
    CRATE_MASS = 1_000.0
    THROW = 150.0                       # m/s
    WAIT = 40.0                         # s
    SETTLE = 3.0                        # s
    MOVED = 0.01                        # m
    PATH = os.path.join(CAPTURES, "save_check.json")

    def __init__(self):
        self.bodies = ["mountains"]
        self.results = []
        self.start_body()
        self.nearby = NearbyLists()

    def rock_cells(self, radius):
        """The cells of the rocks in the last list within `radius` of the feet."""
        return {t.key for t in self.nearby.rocks(radius)}

    def begin(self):
        self.listed_at = self.nearby.count
        self.next_stage("look")

    def look(self, dt):
        if self.clock < self.STEADY or not world.settled() or self.nearby.count < self.listed_at + 2:
            return
        self.inner, self.outer = self.rock_cells(self.INNER), self.rock_cells(self.OUTER)
        player, bubble = foot.player, foot.bubble
        feet = player.feet()
        near = sorted((length(sub(physics.get_transform(r.body)[0], feet)), r.body)
                      for r in scatter_rocks.rocks.values())
        self.moved = 0
        for d, body in near[:self.ROCKS]:
            if d > self.REACH or not scatter_rocks.promote(bubble, body):
                continue
            p = physics.get_transform(body)[0]
            up = bubble.up(p)
            away = sub(p, feet)
            away = sub(away, scale(up, dot(away, up)))
            away = normalize(away) if length(away) > 1e-6 else local_frame(up)[0]
            physics.set_velocity(body, add(scale(up, self.HOP), scale(away, self.AWAY)))
            self.moved += 1
        right, up, forward = player.basis()
        level = player.level(forward, up)
        right = normalize(cross(level, up))
        direction = normalize(add(level, up))
        self.crate = props.spawn_crate(bubble, add(player.eye(), add(scale(level, 3.0), scale(up, 1.0))),
                                       quat_from_basis(right, up, scale(level, -1.0)), CRATE_SIZE,
                                       mass=self.CRATE_MASS)
        physics.set_velocity(self.crate.body, scale(direction, self.THROW))
        self.next_stage("wait")

    def wait(self, dt):
        if (self.crate.body is not None or props.awake(foot.bubble)) and self.clock < self.WAIT:
            return
        os.makedirs(CAPTURES, exist_ok=True)
        self.saved = save_game(self.PATH)
        self.taken = set(world.taken_rocks.keys)
        self.crate_record = props.record(foot.bubble, self.crate)
        new_seed()
        self.next_stage("regenerate")

    def regenerate(self, dt):
        if self.frames < 3:
            return
        self.cleared = (not any(prop_state(p)[0] is planet for p in props.all)
                        and not any(key[0] == planet.index for key in world.taken_rocks.keys))
        if self.clock < 2.0 or not world.settled():
            return
        load_game(self.PATH)
        self.taken_match = set(world.taken_rocks.keys) == self.taken
        self.records_match = props.records(foot.bubble) == self.saved["props"]
        self.listed_at = self.nearby.count
        self.next_stage("after")

    def after(self, dt):
        if not world.settled() or self.clock < self.SETTLE or self.nearby.count < self.listed_at + 2:
            return
        taken = world.taken_rocks.keys
        inner, outer = self.rock_cells(self.INNER), self.rock_cells(self.OUTER)
        missing = {k for k in self.inner if k not in taken and k not in outer}
        shown = outer & taken
        extra = inner - self.outer
        bubble = foot.bubble
        records = {tuple(r["key"]): r for r in self.saved["props"] if r["key"] is not None}
        back, worst = 0, 0.0
        for rock in props.rocks:
            record = records.get(rock.key)
            if rock.body is None or record is None or record["state"] != "resting":
                continue
            p = bubble.body_relative(physics.get_transform(rock.body)[0])
            worst = max(worst, length(sub(p, tuple(record["position"]))))
            back += 1
        ok = (self.moved > 0 and self.cleared and self.taken_match and self.records_match
              and not missing and not shown and not extra and worst <= self.MOVED)
        print("[planet] save in the mountains: %d rocks moved, %d taken, %d props saved (the crate %s, %s away); "
              "a new seed %s; after loading the taken rocks %s and the records %s; of %d rocks listed within "
              "%.0f m before, %d missing, %d taken still listed, %d new; %d loose rocks back in the physics, "
              "%.4f m from their records at most: %s"
              % (self.moved, len(self.taken), len(self.saved["props"]), self.crate_record["state"],
                 distance_text(length(sub(tuple(self.crate_record["position"]), bubble.origin))),
                 "cleared the planet's props and rocks" if self.cleared else "did NOT clear them",
                 "match" if self.taken_match else "DIFFER", "match" if self.records_match else "DIFFER",
                 len(self.inner), self.INNER, len(missing), len(shown), len(extra), back, worst,
                 "passed" if ok else "FAILED"), flush=True)
        ok = self.version_1() and ok
        if os.path.exists(self.PATH):
            os.remove(self.PATH)
        self.clear()
        self.next_body(ok)

    def version_1(self):
        """The save made version 1 (no resources on the props' records, a
        ship ORBIT_HEIGHT over the site where it has none, its cargo one item
        of ore without a resource, no upgrades) and loaded: the
        records must come back as saved but with ferrite for each ore rock
        and 0 for the rest, the cargo item ferrite, and no upgrades owned."""
        old = json.loads(json.dumps(self.saved))
        old["version"] = 1
        old.pop("upgrades", None)
        expected = []
        for record in old["props"]:
            record.pop("resource", None)
            expected.append(dict(record, resource=FERRITE if record.get("grade", 0.0) > 0.0 else 0))
        if old["ship"] is None:
            old["ship"] = {"position": list(scale(world.site_frame()[1], RADIUS + ORBIT_HEIGHT)),
                           "velocity": [0.0, 0.0, 0.0], "orientation": [1.0, 0.0, 0.0, 0.0],
                           "angular_velocity": [0.0, 0.0, 0.0], "on_ground": False, "resting": False,
                           "crashed": False, "fuel": 1_000.0}
        old["ship"]["cargo"] = [{"body": "planet", "direction": [0.0, 0.0, 1.0], "mass": 100.0, "grade": 0.5}]
        with open(self.PATH, "w", encoding="utf-8") as f:
            json.dump(old, f)
        state["owned"] = {"hold_1"}
        loaded = load_game(self.PATH)
        records_ok = loaded is not None and props.records(foot.bubble) == expected
        ship = state["ship"]
        cargo_ok = ship is not None and [c.get("resource") for c in ship.cargo] == [FERRITE]
        owned_ok = state["owned"] == set()
        ok = records_ok and cargo_ok and owned_ok
        print("[planet]   a save of version 1 loaded: %d records %s, ore rocks ferrite; the cargo %s; upgrades %s: %s"
              % (len(expected), "match" if records_ok else "DIFFER", "ferrite" if cargo_ok else "NOT ferrite",
                 "none" if owned_ok else "STILL OWNED", "passed" if ok else "FAILED"), flush=True)
        return ok


class ProbeRounds:
    """Directions measured with the world's extra probes, a round of at
    most EXTRA_PROBES at a time, for a check: `ask` while `idle`, then
    `take` gives ((direction, body), height, ore) of each once they are
    back (else None)."""

    def __init__(self):
        self.asked = None

    @property
    def idle(self):
        return self.asked is None and not world.extra_probes

    def ask(self, probes):
        self.asked = list(probes[:EXTRA_PROBES])
        world.extra_probes = self.asked

    def take(self):
        if self.asked is None or [p for p, _ in world.extra_heights] != self.asked:
            return None
        results = [(probe, height, ore) for (probe, height), (_, ore) in zip(world.extra_heights, world.extra_ores)]
        world.extra_probes = []
        self.asked = None
        return results


class ScanCheck(GunCheck):
    """--scan-check: on the planet (preset 9's orbit) and on the moon
    (Shift+9's), the ship's right display on SCAN and the time warp at
    WARP, until the scanner (scanner.py) has CONTACTS contacts on the body
    or TIMEOUT seconds have passed. Each contact is measured again with the
    extra probes: its richness must come back the same (within 0.001), at
    least scanner.CONTACT. Then a player is put down at the richest (as
    WalkCheck does) and, once it has stood still and a list of the nearby
    rocks came back, the rocks listed within NEAR of the contact must be
    there and all be ore rocks (a grade above shoonyakasha.planet.rocks'
    ORE_ROCKS). Prints per body the contacts, the grades found and exits
    with status 0 when every body passed, 1 otherwise."""

    CONTACTS = 4
    WARP = 4                            # index into WARPS: 16x
    TIMEOUT = 240.0                     # s
    NEAR = 10.0                         # m
    STEADY = 1.0                        # s

    def __init__(self):
        self.bodies = ["planet", "moon"]
        self.results = []
        self.nearby = NearbyLists()
        self.start_body()

    def start_body(self):
        WalkCheck.start_body(self)
        self.next_stage("fly")

    def home_direction(self):
        return self.best.direction

    def on_body(self):
        return [c for c in scanner.contacts if c.body is self.body()]

    def fly(self, dt):
        if self.frames == 1:
            drop_bubble()
            state["scripted_controls"] = True
            teleport(9, moon=self.bodies[0] == "moon")
            scanner.contacts = []
            state["page"] = "scan"
            state["warp"] = self.WARP
            return
        if len(self.on_body()) >= self.CONTACTS or self.clock > self.TIMEOUT:
            state["warp"] = 0
            state["page"] = "orbit"
            self.contacts = self.on_body()
            self.rounds = ProbeRounds()
            self.queue = list(self.contacts)
            self.again = []                 # (contact, richness measured again)
            self.next_stage("verify")

    def verify(self, dt):
        results = self.rounds.take()
        if results is not None:
            self.again += list(zip(self.batch, [ore for _, _, ore in results]))
        if not self.rounds.idle:
            return
        if self.queue:
            self.batch, self.queue = self.queue[:EXTRA_PROBES], self.queue[EXTRA_PROBES:]
            self.rounds.ask([(c.direction, c.body) for c in self.batch])
            return
        if not self.contacts:
            self.finish([])
            return
        self.best = max(self.contacts, key=lambda c: c.richness)
        self.next_stage("place")

    def begin(self):
        self.listed_at = self.nearby.count
        self.next_stage("look")

    def look(self, dt):
        if self.clock < self.STEADY or not world.settled() or self.nearby.count < self.listed_at + 2:
            return
        best = self.best
        self.finish(self.nearby.rocks(self.NEAR, scale(best.direction, best.body.radius + best.height)))

    def finish(self, near):
        name = self.bodies[0]
        same = all(abs(c.richness - ore) <= 1e-3 and ore >= CONTACT for c, ore in self.again)
        ore_rocks = [t for t in near if t.grade > ORE_ROCKS]
        ok = (len(self.contacts) >= self.CONTACTS and len(self.again) == len(self.contacts) and same and near
              and len(ore_rocks) == len(near))
        grades = ", ".join("%.2f" % t.grade for t in near) or "-"
        print("[planet] scan on the %s: %d contacts (%s) after %d points; measured again: %s; at the richest "
              "(%.2f), %d of %d rocks within %.0f m are ore, grades %s: %s"
              % (name, len(self.contacts), ", ".join("%.2f" % c.richness for c in self.contacts),
                 scanner.scanned, "the same" if same else "DIFFERENT",
                 self.best.richness if self.contacts else 0.0, len(ore_rocks), len(near), self.NEAR, grades,
                 "passed" if ok else "FAILED"), flush=True)
        self.next_body(ok)


class BreakCheck(GunCheck):
    """--break-check: in the mountains (WalkCheck's place) and on the moon
    at the Shift+8 spot, the extra probes search a spiral (SPACING apart)
    around the place for ore richness of at least RICH, and the player is
    put down there. Once it has stood still and the fixed rocks are made,
    a STRIKER_MASS kg crate of STRIKER_SIZE is thrown at STRIKE_SPEED into
    the nearest fixed ore boulder within REACH, from STRIKE_FROM beyond its
    surface towards the player. The boulder must split (rocks.split) within
    TIMEOUT: into 3 to 6 chunks whose masses add up to the boulder's
    (within 0.1 %), each with the boulder's grade, its cell taken; after
    SETTLE more seconds every chunk must still be a prop. Prints per body
    what it found and exits with status 0 when every body passed, 1
    otherwise."""

    SPACING = 40.0                      # m
    SEARCH = 4_000                      # points at most
    RICH = 0.7
    REACH = 30.0                        # m
    STRIKER_MASS, STRIKER_SIZE = 10.0, 0.3
    STRIKE_SPEED = 25.0                 # m/s
    STRIKE_FROM = 1.5                   # m
    TIMEOUT = 2.0                       # s
    SETTLE = 1.0                        # s
    STEADY = 1.0                        # s

    def __init__(self):
        self.bodies = ["mountains", "moon"]
        self.results = []
        self.start_body()
        split = scatter_rocks.split

        def recorded(bubble, body):
            chunks = split(bubble, body)
            if chunks:
                self.chunks = chunks
            return chunks
        scatter_rocks.split = recorded

    def start_body(self):
        WalkCheck.start_body(self)
        self.found = None
        self.chunks = []
        self.next_stage("search")

    def home_direction(self):
        return self.found

    def search(self, dt):
        if self.frames == 1:
            drop_bubble()
            state["scripted_controls"] = True
            self.base = WalkCheck.home_direction(self)
            self.rounds = ProbeRounds()
            self.k = 0
        results = self.rounds.take()
        for (direction, _), height, ore in results or []:
            if ore >= self.RICH and (self.body() is not planet or height > 0.0) and self.found is None:
                self.found = direction
        if self.found is not None:
            self.next_stage("place")
            return
        if self.k >= self.SEARCH:
            self.finish("no ground of richness %.1f within %d points" % (self.RICH, self.k), False)
            return
        if not self.rounds.idle:
            return
        body = self.body()
        east, north = local_frame(self.base)
        probes = []
        for _ in range(EXTRA_PROBES):
            r, a = self.SPACING * math.sqrt(self.k), self.k * 2.39996323
            offset = add(scale(east, r * math.cos(a)), scale(north, r * math.sin(a)))
            probes.append((normalize(add(self.base, scale(offset, 1.0 / body.radius))), body))
            self.k += 1
        self.rounds.ask(probes)

    def begin(self):
        self.next_stage("aim")

    def aim(self, dt):
        if self.clock < self.STEADY:
            return
        feet = foot.player.feet()
        ore = sorted((length(sub(physics.get_transform(r.body)[0], feet)), key)
                     for key, r in scatter_rocks.rocks.items() if r.kind == "boulder" and r.grade > 0.0)
        ore = [(d, key) for d, key in ore if d <= self.REACH]
        if not ore:
            if self.clock > 10.0:
                self.finish("no fixed ore boulder within %.0f m" % self.REACH, False)
            return
        self.key = ore[0][1]
        self.boulder = scatter_rocks.rocks[self.key]
        bubble = foot.bubble
        centre = physics.get_transform(self.boulder.body)[0]
        up = bubble.up(centre)
        towards = sub(feet, centre)
        towards = sub(towards, scale(up, dot(towards, up)))
        towards = normalize(add(normalize(towards) if length(towards) > 1e-6 else local_frame(up)[0],
                                scale(up, 0.3)))
        start = add(centre, scale(towards, self.boulder.radius + self.STRIKE_FROM))
        right = normalize(cross(towards, up))
        crate = props.spawn_crate(bubble, start, quat_from_basis(right, up, cross(right, up)),
                                  self.STRIKER_SIZE, mass=self.STRIKER_MASS)
        physics.set_velocity(crate.body, scale(towards, -self.STRIKE_SPEED))
        self.next_stage("strike")

    def strike(self, dt):
        if self.chunks:
            self.next_stage("settle_chunks")
        elif self.clock > self.TIMEOUT:
            self.finish("the %.0f kg boulder (grade %.2f) did NOT split" % (self.boulder.mass, self.boulder.grade),
                        False)

    def settle_chunks(self, dt):
        if self.clock < self.SETTLE:
            return
        chunks, boulder = self.chunks, self.boulder
        mass = sum(c.mass for c in chunks)
        kept = all(c in props.rocks for c in chunks)
        ok = (3 <= len(chunks) <= 6 and abs(mass - boulder.mass) <= 1e-3 * boulder.mass and kept
              and all(c.grade == boulder.grade for c in chunks) and self.key in world.taken_rocks
              and self.key not in scatter_rocks.rocks)
        self.finish("a %.0f kg ore boulder (grade %.2f) split into %d chunks of %s kg (%.1f kg in all), grades %s, "
                    "its cell %s, the chunks %s"
                    % (boulder.mass, boulder.grade, len(chunks), ", ".join("%.0f" % c.mass for c in chunks), mass,
                       "the same" if all(c.grade == boulder.grade for c in chunks) else "DIFFERENT",
                       "taken" if self.key in world.taken_rocks else "NOT taken",
                       "kept" if kept else "NOT all kept"), ok)

    def finish(self, text, ok):
        print("[planet] break on the %s: %s: %s" % (self.bodies[0], text, "passed" if ok else "FAILED"), flush=True)
        if foot.player is not None:
            self.clear()
        self.next_body(ok)


class FuelCheck:
    """--fuel-check: the ship in preset 9's circular orbit, translation
    assist off, burns its main engine at full throttle along its velocity
    until its tanks are empty, then coasts DRY_TIME with the throttle still
    full, then pitches up for TURN_TIME. Each flight step is measured: its
    velocity's change less gravity's over the step, the speed the thrust
    (and the thin air) gave it. The time the engine burnt must match the
    tanks over the main engine's flow (main_thrust / (isp_main g0)) within
    a step; the speed gained must match the rocket equation for the
    ship's full and dry masses, isp_main g0 ln(m0 / m1), within
    DV_TOLERANCE; with the tanks empty the speed gained must stay below
    DRY_TOLERANCE, and the ship must still turn (its pitch rate above
    TURN_RATE). Then a new ship in preset 9's orbit with empty tanks and
    CALL_CREDITS calls for fuel (Space), and
    the time warp goes to WARP: within CALL_TIMEOUT the fuel must arrive,
    the tanks holding what the credits left after the call's fee buy at
    its price (at most their capacity) and the credits less that and the
    fee. Prints the figures and exits with status 0 when all hold, 1
    otherwise."""

    DV_TOLERANCE = 0.005                # of the rocket equation's
    DRY_TIME = 2.0                      # s
    DRY_TOLERANCE = 0.01                # m/s
    TURN_TIME = 1.0                     # s
    TURN_RATE = 0.3                     # rad/s
    CALL_CREDITS = 10_000.0
    WARP = 4                            # index into WARPS: 16x
    CALL_TIMEOUT = 120.0                # s

    def __init__(self):
        self.stage = "start"
        self.gained = 0.0                # m/s from the thrust while there was fuel
        self.dry_gained = 0.0            # m/s after
        self.burn_time = 0.0
        self.clock = 0.0

    def __call__(self, dt):
        if planet.tree is None:
            return True
        if self.stage == "start":
            teleport(9)
            state["scripted_controls"] = True
            state["controls"] = flight.Controls(throttle=1.0, assist=False)
            ship = state["ship"]
            self.full, self.dry = ship.mass(), ship.params.dry_mass
            original = ship.step

            def measured(physics_planet, controls, step, ground=None):
                gravity = physics_planet.gravity(ship.position)
                velocity, fuel = ship.velocity, ship.fuel
                original(physics_planet, controls, step, ground)
                gained = length(sub(sub(ship.velocity, velocity), scale(gravity, step)))
                if fuel > 0.0:
                    self.gained += gained
                    self.burn_time += step * min(ship.fuel_used / (ship.fuel_flow((0.0, 0.0, -ship.params.main_thrust))
                                                                   * step), 1.0)
                else:
                    self.dry_gained += gained
            ship.step = measured
            self.stage = "burn"
            return True
        ship = state["ship"]
        self.clock += dt
        if self.stage == "burn":
            ship.orientation = self.along(ship, ship_velocity())
            ship.angular_velocity = (0.0, 0.0, 0.0)
            if ship.fuel <= 0.0:
                self.stage, self.clock = "dry", 0.0
        elif self.stage == "dry" and self.clock >= self.DRY_TIME:
            state["controls"] = flight.Controls(pitch=1.0, assist=False)
            self.stage, self.clock = "turn", 0.0
        elif self.stage == "turn" and self.clock >= self.TURN_TIME:
            self.measure(ship)
            teleport(9)
            ship = state["ship"]
            ship.fuel = 0.0
            state["controls"] = flight.Controls(assist=False)
            state["market"].credits = self.CALL_CREDITS
            on_key(keys.SPACE)
            self.call = state["fuel_call"]
            if self.call is None:
                self.finish(None)
            else:
                state["warp"] = self.WARP
                self.stage, self.clock = "call", 0.0
        elif self.stage == "call" and (state["fuel_call"] is None or self.clock >= self.CALL_TIMEOUT):
            state["warp"] = 0
            self.finish(ship)
        return True

    @staticmethod
    def along(ship, velocity):
        """The ship's orientation with its nose along `velocity`, its up as near its own as square to it allows."""
        forward = normalize(velocity)
        up = ship.axes()[1]
        up = normalize(sub(up, scale(forward, dot(up, forward))))
        right = cross(forward, up)
        return quat_from_basis(right, up, scale(forward, -1.0))

    def measure(self, ship):
        """The burn's, the dry coast's and the turn's figures, printed; whether they hold."""
        p = ship.params
        flow = p.main_thrust / (p.isp_main * flight.G0)
        expected_time = p.fuel_capacity / flow
        expected_dv = p.isp_main * flight.G0 * math.log(self.full / self.dry)
        rate = abs(ship.angular_velocity[0])
        time_ok = abs(self.burn_time - expected_time) <= FLIGHT_STEP
        dv_ok = abs(self.gained - expected_dv) <= self.DV_TOLERANCE * expected_dv
        dry_ok = self.dry_gained <= self.DRY_TOLERANCE
        turn_ok = rate >= self.TURN_RATE
        print("[planet] fuel: burnt for %.3f s (the tanks over the main engine's flow: %.3f s): %s"
              % (self.burn_time, expected_time, "ok" if time_ok else "FAILED"), flush=True)
        print("[planet]   speed gained %.1f m/s (the rocket equation from %.0f to %.0f kg: %.1f m/s): %s"
              % (self.gained, self.full, self.dry, expected_dv, "ok" if dv_ok else "FAILED"), flush=True)
        print("[planet]   with the tanks empty: %.4f m/s gained in %.0f s at full throttle: %s"
              % (self.dry_gained, self.DRY_TIME, "ok" if dry_ok else "FAILED"), flush=True)
        print("[planet]   pitching up with no fuel: %.2f rad/s after %.0f s: %s"
              % (rate, self.TURN_TIME, "ok" if turn_ok else "FAILED"), flush=True)
        self.flight_ok = time_ok and dv_ok and dry_ok and turn_ok

    def finish(self, ship):
        """The fuel call's figures printed (`ship` None when it could not be
        called), and the check's result."""
        call_ok = False
        if ship is None:
            print("[planet]   calling for fuel in orbit: NO CALL (%s)" % state["trade_note"], flush=True)
        else:
            call = self.call
            kg = min(ship.params.fuel_capacity, (self.CALL_CREDITS - call["fee"]) / call["price"])
            cost = call["fee"] + kg * call["price"]
            delivered = state["fuel_call"] is None
            call_ok = (delivered and abs(ship.fuel - kg) <= 1e-6
                       and abs(state["market"].credits - (self.CALL_CREDITS - cost)) <= 1e-6)
            print("[planet]   calling for fuel in orbit from %s: %.2f cr/kg and a %s fee, %s; %.0f kg in the tanks "
                  "(expected %.0f), %s left (expected %s): %s"
                  % (call["outpost"].name, call["price"], credits_text(call["fee"]),
                     "delivered" if delivered else "NOT DELIVERED", ship.fuel, kg,
                     credits_text(state["market"].credits), credits_text(self.CALL_CREDITS - cost),
                     "ok" if call_ok else "FAILED"), flush=True)
        ok = self.flight_ok and call_ok
        print("[planet]   %s" % ("passed" if ok else "FAILED"), flush=True)
        os._exit(0 if ok else 1)


def ore_rock(bubble, point, extent, grade):
    """A loose ore rock (rocks.py's "rock" kind scaled by `extent`, of
    `grade`) at `point` (in the bubble), level with the ground; its prop."""
    up = bubble.up(point)
    east, _ = local_frame(up)
    points, _ = scatter_rocks.kind_data(physics, "rock")
    hull = (points * np.array(extent)).astype(np.float32)
    body = physics.create_convex_hull(hull, point, quat_from_basis(east, up, cross(east, up)),
                                      motion="dynamic", density=2_600.0, friction=0.8, restitution=0.05)
    colour = rock_colour(bubble.body.index, grade)
    entity, glow = scatter_rocks.entity("rock", extent, colour, grade)
    radius = float(np.linalg.norm(hull, axis=1).max())
    return props.add_rock(body, entity, 2.0 * radius, radius, None, ("hull", hull, physics.mass(body)),
                          0.5 * extent[1], bubble.body, "rock", tuple(extent), colour, grade, glow)


class CargoCheck:
    """--cargo-check: the ship at rest beside the blocks (preset 8) with
    PRELOAD kg of ice already in its cargo (most of the hold's volume), and
    the player out of it at its boarding point. Loose ore rocks of GRADE
    are put LIFT above the ground: one of each EXTENTS in the cargo net, one
    OUTSIDE metres beyond the net's edge, and a crate in the net. Once they
    have rested SETTLE seconds the player gets in (F). The rocks loaded
    must be gone from the props and in the cargo with their masses and
    grade, never past the hold's mass or volume; a rock in the net left
    out must not fit in what was left (flight.Ship.can_take); the rock outside and the crate must still be props; and the
    ship's mass must have risen by what was loaded and its delta-v be
    isp_main g0 ln(m / (m - fuel)) for its new mass. Then one more ore rock
    is put in the net and the ship climbs with translation assist: once it
    is no longer landed that rock must be in the cargo. Prints what was
    loaded and exits with status 0 when all hold, 1 otherwise."""

    PRELOAD = 2_000.0                   # kg of ice
    GRADE = 0.7
    EXTENTS = ((0.35, 0.25, 0.35), (0.45, 0.3, 0.45), (0.6, 0.45, 0.6))   # m: about 250, 500 and 1,300 kg
    LIFT = 1.0                          # m
    OUTSIDE = 3.0                       # m
    SETTLE = 3.0                        # s
    TIMEOUT = 20.0                      # s

    def __init__(self):
        self.stage, self.frames, self.clock = "start", 0, 0.0
        self.lines, self.ok = [], True

    def __call__(self, dt):
        if planet.tree is None or state["survey"] is not None:
            return True
        self.frames += 1
        self.clock += dt
        getattr(self, self.stage)()
        return True

    def next_stage(self, stage):
        self.stage, self.frames, self.clock = stage, 0, 0.0

    def start(self):
        teleport(8)
        state["scripted_controls"] = True
        self.next_stage("get_out")

    def get_out(self):
        if not ship_landed(state["ship"]) or self.clock < 1.0:
            return
        get_out()
        if foot.player is not None:
            self.next_stage("place")
        elif self.clock > self.TIMEOUT:
            self.finish("the player could not get out", False)

    def net_point(self, right=0.0, back=0.0):
        """A point LIFT above the gear's feet in the net, `right` and `back` metres from its centre, in the bubble."""
        centre, r, u, b = net_frame(state["ship"])
        return foot.bubble.to_bubble(add(centre, add(add(scale(r, right), scale(b, back)), scale(u, self.LIFT))))

    def place(self):
        if self.clock < 1.0:
            return
        ship, bubble = state["ship"], foot.bubble
        ship.cargo = [{"body": "planet", "direction": world.site_frame()[1], "mass": self.PRELOAD, "grade": 0.5,
                       "resource": ICE}]
        self.inside = [ore_rock(bubble, self.net_point(-1.8 + 1.8 * k, -1.5 + 1.5 * k), extent, self.GRADE)
                       for k, extent in enumerate(self.EXTENTS)]
        self.outside = ore_rock(bubble, self.net_point(NET_HALF[0] + self.OUTSIDE, 0.0), self.EXTENTS[0], self.GRADE)
        right, up, forward = ship.axes()
        self.crate = props.spawn_crate(bubble, self.net_point(0.0, 2.5),
                                       quat_from_basis(right, up, scale(forward, -1.0)), CRATE_SIZE)
        self.next_stage("board")

    def board(self):
        if self.clock < self.SETTLE:
            return
        ship = state["ship"]
        self.mass_before, self.cargo_before = ship.mass(), ship.cargo_mass
        in_net = [prop for prop, _ in in_net_now()]
        get_in()
        if state["mode"] != "ship":
            self.finish("the player could not get in", False)
            return
        loaded = [p for p in self.inside if p not in props.rocks]
        left = [p for p in self.inside if p in props.rocks]
        cargo = ship.cargo[1:]
        masses_ok = sorted(round(c["mass"], 3) for c in cargo) == sorted(round(p.mass, 3) for p in loaded)
        grades_ok = all(c["grade"] == self.GRADE for c in cargo)
        capacity, volume = ship.params.cargo_capacity, ship.params.hold_m3
        fits = ship.cargo_mass <= capacity + 1e-6 and ship.cargo_volume <= volume + 1e-9
        left_ok = all(p in in_net and not ship.can_take(p.mass, p.resource) for p in left)
        kept = self.outside in props.rocks and self.crate in props.crates
        m = ship.mass()
        risen = abs(m - self.mass_before - sum(p.mass for p in loaded)) <= 1e-6
        dv_ok = abs(ship.delta_v() - ship.params.isp_main * flight.G0 * math.log(m / (m - ship.fuel))) <= 1e-6
        self.ok = loaded and masses_ok and grades_ok and fits and left_ok and kept and risen and dv_ok
        self.lines.append("boarding loaded %d of %d rocks in the net (%s kg) onto %.0f kg of ice: %s; %s; the cargo "
                          "%.0f of %.0f kg, %.2f of %.2f m3; the rock outside and the crate %s; the ship's mass %s, "
                          "its delta-v %.0f m/s %s"
                          % (len(loaded), len(self.inside), ", ".join("%.0f" % p.mass for p in loaded),
                             self.PRELOAD, "masses and grades match" if masses_ok and grades_ok else "MISMATCH",
                             ("left out %s kg, not fitting" % ", ".join("%.0f" % p.mass for p in left))
                             if left and left_ok else ("NOT ALL LOADED" if left else "none left out"),
                             ship.cargo_mass, capacity, ship.cargo_volume, volume,
                             "kept" if kept else "NOT kept", "risen by it" if risen else "WRONG", ship.delta_v(),
                             "as the rocket equation" if dv_ok else "WRONG"))
        ship.cargo = ship.cargo[:1]
        self.next_stage("place_more")

    def place_more(self):
        if self.clock < 0.5:
            return
        bubble = foot.bubble
        if bubble is None:
            self.finish("no bubble around the landed ship", False)
            return
        self.last = ore_rock(bubble, self.net_point(0.0, 0.0), self.EXTENTS[0], self.GRADE)
        self.next_stage("lift")

    def lift(self):
        if self.clock < self.SETTLE:
            return
        state["controls"] = flight.Controls(vertical=1.0, assist=True)
        self.next_stage("climb")

    def climb(self):
        ship = state["ship"]
        if ship_landed(ship) and self.clock < self.TIMEOUT:
            return
        loaded = self.last not in props.rocks and any(abs(c["mass"] - self.last.mass) < 1e-3 for c in ship.cargo[1:])
        self.lines.append("taking off loaded the rock put in the net: %s" % ("yes" if loaded else "NO"))
        self.finish(None, self.ok and loaded)

    def finish(self, text, ok):
        if text is not None:
            self.lines.append(text)
        for k, line in enumerate(self.lines):
            print("[planet] %s%s" % ("cargo: " if k == 0 else "  ", line), flush=True)
        print("[planet]   %s" % ("passed" if ok else "FAILED"), flush=True)
        os._exit(0 if ok else 1)


class TradeCheck:
    """--trade-check: once the outposts are found, for the first outpost
    on the planet and then the first on the moon: the terrain is measured
    at RING points FLAT_SHARE of its flat radius around it, which must lie
    within FLAT_TOLERANCE of its height; the ship is put down at rest on
    its pad (its gear SET_DOWN_DEPTH into the ground) and, once it rests,
    must count as there (outpost_here) with the trade card shown. Its
    cargo is set to CARGO kg of each body's ore and its fuel to FUEL kg,
    then the cargo is sold (Enter) and the tanks filled (Space). The
    credits must rise by trade.sale's sum for the cargo, the outpost's
    price for each body's ore must have fallen by 1 + kg sold /
    SUPPLY_HALF, and the fuel must have risen by as much as the credits
    buy, up to full, the credits falling by its price. Prints per outpost
    the figures and exits with status 0 when both passed, 1 otherwise."""

    RING = 8
    FLAT_SHARE = 0.8
    FLAT_TOLERANCE = 0.05               # m
    CARGO = 1_500.0                     # kg of each body's ore
    FUEL = 2_000.0                      # kg
    TIMEOUT = 30.0                      # s

    def __init__(self):
        self.bodies = ["planet", "moon"]
        self.results = []
        self.stage, self.frames, self.clock = "measure", 0, 0.0

    def __call__(self, dt):
        if planet.tree is None or state["survey"] is not None:
            return True
        self.frames += 1
        self.clock += dt
        getattr(self, self.stage)()
        return True

    def next_stage(self, stage):
        self.stage, self.frames, self.clock = stage, 0, 0.0

    def outpost(self):
        return next(o for o in state["outposts"][1:] if o.body.name == self.bodies[0])

    def measure(self):
        if self.frames == 1:
            state["scripted_controls"] = True
            o = self.outpost()
            self.ring = [(d, o.body) for d in outposts.ring(o.direction, o.body.radius,
                                                            self.FLAT_SHARE * o.flat_radius, self.RING)]
            self.heights = []
            self.rounds = ProbeRounds()
        results = self.rounds.take()
        if results:
            self.heights += [height for _, height, _ in results]
        if len(self.heights) >= self.RING:
            self.next_stage("land")
        elif self.rounds.idle:
            self.rounds.ask(self.ring[len(self.heights):])

    def land(self):
        if self.frames == 1:
            o = self.outpost()
            up = o.direction
            new_ship(add(o.point(), scale(up, SHIP_CLEARANCE - SET_DOWN_DEPTH)), o.body.velocity(),
                     local_frame(up)[1], up, True)
            state["controls"] = flight.Controls(assist=True)
            return
        ship = state["ship"]
        if (ship.resting and outpost_here() is not None) or self.clock > self.TIMEOUT:
            self.next_stage("trade")

    def trade(self):
        if self.frames < 3:
            return                       # the cards updated with the ship there
        o, ship, market = self.outpost(), state["ship"], state["market"]
        lines = []
        flat = max(abs(h - o.height) for h in self.heights)
        flat_ok = flat <= self.FLAT_TOLERANCE
        lines.append("the ground %.0f m around it within %.3f m of its height: %s"
                     % (self.FLAT_SHARE * o.flat_radius, flat, "ok" if flat_ok else "FAILED"))
        here = outpost_here()
        there = here is o and ship.resting and state["trade_card"].visible
        lines.append("the ship %s on its pad%s" % ("rests" if ship.resting else "does NOT rest",
                                                   ", the trade card shown" if there else ": NOT counted as there"))
        if not there:
            self.finish(o, lines, False)
            return
        far = normalize(add(o.direction, scale(local_frame(o.direction)[0], 0.3)))
        ship.cargo = [{"body": b.name, "direction": far if b is o.body else local_frame(o.direction)[1],
                       "mass": self.CARGO, "grade": 0.8} for b in bodies]
        ship.fuel = self.FUEL
        samples = [{"body": b.name, "direction": o.direction, "mass": 1.0, "grade": 1.0} for b in bodies]
        before = [trade.price(o, s, world.time) for s in samples]
        credits0 = market.credits
        expected = trade.sale(o, ship.cargo, world.time)[0]
        on_key(keys.ENTER)
        gained = market.credits - credits0
        after = [trade.price(o, s, world.time) for s in samples]
        dropped = [a * (1.0 + self.CARGO / trade.SUPPLY_HALF) / b for a, b in zip(after, before)]
        sold_ok = not ship.cargo and abs(gained - expected) <= 1e-6 * expected
        drop_ok = all(abs(d - 1.0) <= 1e-3 for d in dropped)
        lines.append("sold %.0f kg of each body's ore for %s (expected %s): %s; the prices fell %s: %s"
                     % (self.CARGO, credits_text(gained), credits_text(expected), "ok" if sold_ok else "FAILED",
                        " and ".join("%.3f to %.3f cr/kg" % ba for ba in zip(before, after)),
                        "ok" if drop_ok else "FAILED"))
        credits1 = market.credits
        on_key(keys.SPACE)
        bought = ship.fuel - self.FUEL
        wanted = min(ship.params.fuel_capacity - self.FUEL, credits1 / o.fuel_price)
        paid = credits1 - market.credits
        fuel_ok = abs(bought - wanted) <= 1e-6 and abs(paid - bought * o.fuel_price) <= 1e-6
        lines.append("filled %.0f kg of fuel at %.2f cr/kg for %s, %.0f kg in the tanks, %s left: %s"
                     % (bought, o.fuel_price, credits_text(paid), ship.fuel, credits_text(market.credits),
                        "ok" if fuel_ok else "FAILED"))
        self.finish(o, lines, flat_ok and sold_ok and drop_ok and fuel_ok)

    def finish(self, outpost, lines, ok):
        print("[planet] trade at %s on the %s:" % (outpost.name, outpost.body.name), flush=True)
        for line in lines:
            print("[planet]   %s" % line, flush=True)
        print("[planet]   %s" % ("passed" if ok else "FAILED"), flush=True)
        self.results.append(ok)
        self.bodies.pop(0)
        if not self.bodies:
            os._exit(0 if all(self.results) else 1)
        self.next_stage("measure")


class UpgradeCheck:
    """--upgrade-check: the ship on Base's pad (preset 8) with CREDITS and
    no upgrades. Through the outpost card's pages (Page Down) and Enter it
    tries Main engine tuning first, which must be refused for want of the
    Auxiliary tank, the credits unchanged; then buys the Auxiliary tank,
    Main engine tuning, Cargo racks and Field emitter I. After each the
    credits must have fallen by its cost and the ship's parameters be the
    base ones times the effects bought. The player then gets out and the
    gun must hold the Field emitter's force and range. The game is saved to
    PATH, the upgrades cleared and the save loaded: the upgrades owned, the
    ship's and the gun's parameters and the credits must come back. Prints
    each step and exits with status 0 when all hold, 1 otherwise."""

    CREDITS = 20_000.0
    BUY = ("engine_1", "tank_1", "engine_1", "hold_1", "gun_force_1")
    PATH = os.path.join(CAPTURES, "upgrade_check.json")
    TIMEOUT = 30.0                      # s

    def __init__(self):
        self.stage, self.frames, self.clock = "start", 0, 0.0
        self.lines, self.ok = [], True

    def __call__(self, dt):
        if planet.tree is None or state["survey"] is not None:
            return True
        self.frames += 1
        self.clock += dt
        getattr(self, self.stage)()
        return True

    def next_stage(self, stage):
        self.stage, self.frames, self.clock = stage, 0, 0.0

    def note(self, ok, text):
        self.ok = self.ok and ok
        self.lines.append("%s: %s" % (text, "ok" if ok else "FAILED"))

    def start(self):
        teleport(8)
        state["scripted_controls"] = True
        self.next_stage("buy")

    def buy(self):
        if outpost_here() is None or self.clock < 1.0:
            if self.clock > self.TIMEOUT:
                self.finish("the ship never rested on Base's pad")
            return
        from __main__ import apply_upgrades
        import upgrades
        apply_upgrades(set())
        state["market"].credits = self.CREDITS
        base = flight.ShipParams()
        for key in self.BUY:
            upgrade = upgrades.BY_KEY[key]
            page = upgrades.UPGRADES.index(upgrade) + 1
            for _ in range(len(upgrades.UPGRADES) + 1):
                if state["outpost_page"] == page:
                    break
                on_key(keys.PAGE_DOWN)
            credits, owned = state["market"].credits, set(state["owned"])
            on_key(keys.ENTER)
            refused = upgrades.refusal(upgrade, owned, credits)
            if refused is not None:
                self.note(state["owned"] == owned and state["market"].credits == credits,
                          "%s refused (%s), nothing bought" % (upgrade.name, refused))
                continue
            expected = upgrades.effective({"ship": base, "gun": gun_module.GunParams()}, owned | {key})
            p = state["ship"].params
            self.note(key in state["owned"] and abs(state["market"].credits - (credits - upgrade.cost)) < 1e-6
                      and p == expected["ship"],
                      "%s bought for %.0f cr: %s left, hold %.1f t / %.1f m3, tanks %.0f kg, dry %.1f t, isp %.1f s"
                      % (upgrade.name, upgrade.cost, credits_text(state["market"].credits),
                         p.cargo_capacity / 1_000.0, p.hold_m3, p.fuel_capacity, p.dry_mass / 1_000.0, p.isp_main))
        self.expected = expected
        get_out()
        self.next_stage("gun")

    def gun(self):
        if foot.gun is None:
            if self.clock > self.TIMEOUT:
                self.finish("the player could not get out")
            return
        p = foot.gun.p
        self.note(p == self.expected["gun"], "out of the ship, the gun holds %.1f kN and pulls from %.1f m"
                  % (p.hold_force / 1_000.0, p.pull_range))
        self.credits, self.owned = state["market"].credits, set(state["owned"])
        os.makedirs(CAPTURES, exist_ok=True)
        save_game(self.PATH)
        from __main__ import apply_upgrades
        apply_upgrades(set())
        state["market"].credits = 0.0
        load_game(self.PATH)
        self.next_stage("loaded")

    def loaded(self):
        if foot.gun is None:
            if self.clock > self.TIMEOUT:
                self.finish("no player after loading")
            return
        ok = (state["owned"] == self.owned and state["market"].credits == self.credits
              and state["ship"].params == self.expected["ship"] and foot.gun.p == self.expected["gun"])
        self.note(ok, "saved, cleared and loaded: %s owned, %s, the ship's and the gun's parameters back"
                  % (", ".join(sorted(state["owned"])), credits_text(state["market"].credits)))
        self.finish(None)

    def finish(self, text):
        if text is not None:
            self.lines.append(text)
            self.ok = False
        if os.path.exists(self.PATH):
            os.remove(self.PATH)
        for k, line in enumerate(self.lines):
            print("[planet] %s%s" % ("upgrades: " if k == 0 else "  ", line), flush=True)
        print("[planet]   %s" % ("passed" if self.ok else "FAILED"), flush=True)
        os._exit(0 if self.ok else 1)


class TrunkCheck:
    """--trunk-check: a player put down from preset 1 (5 m above the
    blocks) waits until it has stood on the ground for SETTLE seconds with
    at least one trunk in the bubble, then walks at each of the TARGETS
    trunks nearest it within REACH: put down START metres from the trunk's
    axis on the side it stood on, lifted LIFT, facing the trunk, it stands
    for SETTLE seconds and walks towards it for WALK seconds. Its feet must
    stop TOLERANCE from the trunk's radius plus its own from the axis.
    Prints each trunk's kind, radius and where the feet stopped, and exits
    with status 0 when every walk passed, 1 otherwise."""

    TARGETS = 5
    REACH = 15.0
    START = 3.0
    LIFT = 1.5
    SETTLE = 1.0
    WALK = 2.5
    TOLERANCE = 0.05
    TIMEOUT = 60.0

    def __init__(self):
        self.stage, self.clock, self.frames = "place", 0.0, 0
        self.results = []

    def __call__(self, dt):
        if planet.tree is None:
            return True
        self.frames += 1
        self.clock += dt
        getattr(self, self.stage)(dt)
        return True

    def next_stage(self, stage):
        self.stage, self.clock, self.frames = stage, 0.0, 0

    def place(self, dt):
        if not settled():
            return
        state["scripted_controls"] = True
        teleport(1)
        self.next_stage("spawn")

    def spawn(self, dt):
        if self.frames < 10 or not settled():
            return
        go_on_foot()
        if foot.player is not None:
            self.next_stage("wait")

    def wait(self, dt):
        player = foot.player
        if not player.on_ground or not trunks.trunks:
            self.clock = 0.0
            if self.frames > 60 * self.TIMEOUT:
                print("[planet] trunks: none came within %.0f m: FAILED" % COLLIDER_RADIUS, flush=True)
                os._exit(1)
            return
        if self.clock < self.SETTLE:
            return
        bubble = foot.bubble
        feet = player.feet()
        near = []
        for key, trunk in trunks.trunks.items():
            base = self.base(trunk)
            d = self.across(sub(base, feet), bubble.up(base))
            if d <= self.REACH:
                near.append((d, key))
        self.targets = [key for _, key in sorted(near)[:self.TARGETS]]
        print("[planet] trunks: %d in the bubble, walking at the %d nearest" % (len(trunks.trunks), len(self.targets)),
              flush=True)
        self.next_target()

    @staticmethod
    def base(trunk):
        bubble = foot.bubble
        return bubble.to_bubble(add(bubble.body.centre(), trunk.base))

    @staticmethod
    def across(v, up):
        """`v` without its part along `up`: its length."""
        return length(sub(v, scale(up, dot(v, up))))

    def next_target(self):
        if not self.targets:
            os._exit(0 if self.results and all(self.results) else 1)
        self.key = self.targets.pop(0)
        trunk = trunks.trunks.get(self.key)
        if trunk is None:
            print("[planet]   a trunk left the bubble before its walk: FAILED", flush=True)
            self.results.append(False)
            self.next_target()
            return
        player, bubble = foot.player, foot.bubble
        base = self.base(trunk)
        up = bubble.up(base)
        away = sub(player.feet(), base)
        away = sub(away, scale(up, dot(away, up)))
        away = normalize(away) if length(away) > 1e-3 else local_frame(up)[0]
        physics.character_set_position(player.character, add(base, add(scale(away, self.START), scale(up, self.LIFT))))
        physics.character_set_velocity(player.character, (0.0, 0.0, 0.0))
        player.heading = player.level(scale(away, -1.0), up)
        player.pitch = 0.0
        foot.controls = FootControls()
        self.closest = math.inf
        self.next_stage("approach")

    def approach(self, dt):
        trunk = trunks.trunks.get(self.key)
        player, bubble = foot.player, foot.bubble
        if trunk is None:
            print("[planet]   a trunk left the bubble during its walk: FAILED", flush=True)
            self.results.append(False)
            self.next_target()
            return
        if self.clock < self.SETTLE:
            return
        foot.controls.forward = 1.0
        base = self.base(trunk)
        self.closest = min(self.closest, self.across(sub(player.feet(), base), bubble.up(base)))
        if self.clock < self.SETTLE + self.WALK:
            return
        foot.controls.forward = 0.0
        radius = trunk.radius
        wanted = radius + PLAYER_RADIUS
        ok = abs(self.closest - wanted) <= self.TOLERANCE
        self.results.append(ok)
        print("[planet]   %s, trunk radius %.3f m: the feet stopped %.3f m from its axis (%.3f): %s"
              % (trunk.kind, radius, self.closest, wanted, "passed" if ok else "FAILED"), flush=True)
        self.next_target()
