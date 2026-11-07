"""
The world around Base: the Base blocks, the outposts' and the lakes'
survey, the rivers, the parked ship's pose and model, and where the
player is put down.
"""

import math
import os

from shoonyakasha.planet.vec import add, dot, euler_from_basis, normalize, scale, sub
from shoonyakasha.planet import rivers
from shoonyakasha.planet.lakes import LakeSurvey
from shoonyakasha.planet.world import SITE_BLEND_RADIUS

from outposts import Survey
from play.core import HERE, RIVER_CHECK_WIDTH, SHIP_CLEARANCE, blocks, bodies, box, engine, pads, planet, state, world
from play import saving, spaceflight, vehicle, walking

EAST_OF_SHIP = 4.5               # m east of the ship's centre the player starts
SHIP_SOUTH = 16.0                # m south of the blocks the ship stands
# The ship is a hover car (models/cyber_vehicle, "Cyber Vehicle" by Alt, CC-BY-4.0), its model in
# metres with its nose along +Z and up +Y: its centre (its half sizes about it are core.SHIP_HALF).
SHIP_MODEL = os.path.join(HERE, "models", "cyber_vehicle", "scene.gltf")
SHIP_MODEL_CENTRE = (0.0, 0.586, -1.402)
HOVER_BOB, HOVER_PERIOD = 0.04, 3.2   # m up and down, s, while slow


def build_site():
    """The Base blocks around the site, in metres east, up and north of it."""
    _, up, east, north = world.site_frame()
    rotation = euler_from_basis(east, up, scale(north, -1.0))
    positions, normals = box()
    pieces = [
        ((0.0, 0.075, 0.0), (20.0, 0.05, 20.0), (0.35, 0.35, 0.38)),    # pad, 5 cm above the ground
        ((0.0, 0.6, 0.0), (1.0, 1.0, 1.0), (0.85, 0.85, 0.85)),
        ((2.0, 0.35, 1.0), (0.5, 0.5, 0.5), (0.85, 0.25, 0.20)),
        ((-6.0, 2.1, 4.0), (4.0, 4.0, 4.0), (0.25, 0.40, 0.85)),
        ((8.0, 15.1, -8.0), (0.5, 30.0, 0.5), (0.90, 0.30, 0.15)),      # mast
    ]
    for (e, u, n), size, colour in pieces:
        entity = engine.create_mesh(positions, normals=normals, name="Block")
        engine.scene.set_rotation(entity, rotation)
        engine.scene.set_scale(entity, size)
        engine.scene.set_material_vec4(entity, "baseColorFactor", colour + (1.0,))
        engine.scene.set_material_float(entity, "gridStrength", 0.0)
        engine.scene.set_render_layer_mask(entity, 1)
        blocks.append((entity, add(scale(east, e), add(scale(up, u), scale(north, n)))))


def begin_survey():
    """The outposts' survey and the lakes' around the site, measured together."""
    seed = tuple(planet.terrain["seed"])
    survey = Survey(bodies, seed)
    lake_survey = LakeSurvey(planet, world.site_frame()[1], seed)
    state.site.survey = (survey, lake_survey)
    world.start_survey(survey.points + lake_survey.points)


def update_outposts():
    """The home outpost at the site's height; once the survey's heights are
    back, the outposts chosen, their sites flattened and their pads drawn,
    the lakes chosen clear of the outposts' sites and the site's pad, and
    the height grid around the site asked for (update_rivers)."""
    home = state.site.home
    home.height = world.site_height or 0.0
    surveys = state.site.survey
    if surveys is not None and surveys[0] == "rivers":
        update_rivers()
        return
    heights = world.surveyed() if surveys is not None else None
    if heights is None:
        return
    survey, lake_survey = surveys
    count = len(survey.points)
    found = survey.choose(heights[:count], taken=[], home=home)
    state.site.outposts = [home] + sorted(found, key=lambda o: (o.body.index, o.name))
    world.set_sites([o.site() for o in state.site.outposts if o.flat_radius > 0.0])
    avoid = [(home.direction, SITE_BLEND_RADIUS)] + [(o.direction, o.blend_radius) for o in found
                                                     if o.body is planet]
    lakes = lake_survey.choose(heights[count:], avoid=avoid)
    world.set_lakes(lakes)
    if lakes:
        nearest = min(planet.radius * math.acos(max(-1.0, min(1.0, dot(home.direction, normalize(k.direction)))))
                      for k in lakes)
        print("[walker] lakes: %d, %.0f to %.0f m across, the nearest %.1f km from Base"
              % (len(lakes), 2.0 * min(k.radius for k in lakes), 2.0 * max(k.radius for k in lakes),
                 nearest / 1000.0), flush=True)
    frame = rivers.GridFrame(home.direction, planet.radius)
    world.request_height_grid(frame)
    state.site.survey = ("rivers", frame, avoid)
    pads.build(state.site.outposts)


def update_rivers():
    """Once the height grid around the site is back: the rivers traced over
    it (shoonyakasha.planet.rivers), round the outposts' sites and the
    site's pad and out of the lakes, set, and the survey ended."""
    _, frame, avoid = state.site.survey
    heights = world.height_grid()
    if heights is None:
        return
    found = rivers.trace(heights, frame, sea=planet.ocean, avoid=avoid, wet=world.lake_level)
    world.set_rivers(found, frame)
    print("[walker] rivers: %d, %d segments, %.0f km in all"
          % (len(found), sum(len(r.points) - 1 for r in found), river_length(found) / 1000.0), flush=True)
    world.end_survey()
    state.site.survey = None
    saving.restore_market()


def river_spot():
    """(river, k): the point k of a river at least RIVER_CHECK_WIDTH wide
    there, nearest the site, with a point after it; (None, None) without one."""
    up = world.site_frame()[1]
    best, spot = None, (None, None)
    for river in world.rivers:
        for k in range(len(river.points) - 1):
            if river.widths[k] < RIVER_CHECK_WIDTH:
                continue
            gap = -dot(up, river.points[k])
            if best is None or gap < best:
                best, spot = gap, (river, k)
    return spot


def river_length(found):
    """Metres along the ground of all the rivers in `found`."""
    total = 0.0
    for river in found:
        for a, b in zip(river.points, river.points[1:]):
            total += planet.radius * math.acos(max(-1.0, min(1.0, dot(a, b))))
    return total


def ship_pose():
    """The parked ship: SHIP_SOUTH south of the blocks, hovering
    HOVER_HEIGHT above the ground, facing north."""
    _, up, east, north = world.site_frame()
    position = add(world.site_ground(), add(scale(up, SHIP_CLEARANCE), scale(north, -SHIP_SOUTH)))
    return position, east, up, north


def spawn_point():
    """The player's start: EAST_OF_SHIP east of the ship, on the sphere's
    radius of the site's height (the ground there comes from the probe)."""
    position, east, up, _ = ship_pose()
    return add(position, add(scale(east, EAST_OF_SHIP), scale(up, -SHIP_CLEARANCE)))


def build_ship_model():
    """The ship's model, its meshes parented to one entity, its centre
    (SHIP_MODEL_CENTRE) at the entity's origin; hidden until the ship's
    pose is known."""
    result = engine.load_gltf_scene(SHIP_MODEL, flatten_hierarchy=True)
    if not result.success:
        print("[walker] the ship's model did not load:", result.error, flush=True)
        return
    root = engine.scene.create_entity("ShipModel")
    for entity in result.entities:
        engine.scene.set_parent(entity, root)
        engine.scene.set_position(entity, scale(SHIP_MODEL_CENTRE, -1.0))
        engine.scene.set_material_float(entity, "gridStrength", 0.0)
        engine.scene.set_render_layer_mask(entity, 1)
        engine.scene.set_visible(entity, False)
    state.site.ship_model = (root, result.entities)


def place_ship(camera):
    """The ship's model relative to `camera`: the car's pose, banked and
    pitched (hovercar.Car.attitude), rising and falling HOVER_BOB once
    every HOVER_PERIOD while slow; and the engines' glow."""
    car = state.vehicle.car
    if car is None:
        return
    centre = vehicle.car_position()
    right, up, forward = spaceflight.car_attitude()
    if state.site.ship_model is not None:
        root, meshes = state.site.ship_model
        for entity in meshes:
            engine.scene.set_visible(entity, True)
        still = max(0.0, 1.0 - car.speed() / 10.0)
        bob = still * HOVER_BOB * math.sin(2.0 * math.pi * world.time / HOVER_PERIOD)
        engine.scene.set_position(root, sub(add(centre, scale(up, bob)), camera))
        engine.scene.set_rotation(root, euler_from_basis(scale(right, -1.0), up, scale(forward, -1.0)))
    controls = state.vehicle.controls
    throttle = controls.throttle if state.vehicle.driving else 0.0
    if state.space.ship is not None:
        throttle = state.space.controls.throttle
    state.ui.car_glows.update(walking.view_basis(), sub(centre, camera), (right, up, forward), throttle,
                              controls.boost and state.vehicle.driving and state.space.ship is None, car.speed())
