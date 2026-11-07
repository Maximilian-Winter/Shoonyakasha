"""
The points of interest in play (pois.py): looking for the candidates
around the player and measuring them, discovering them, drawing them and
making their props, using their spots (E), selling pods' crates on the
pads, and their markers on the compass and the map.
"""

import math

from shoonyakasha import keys
from shoonyakasha.planet import world as planet_world
from shoonyakasha.planet.vec import (add, dot, euler_from_basis, length, local_frame, normalize, quat_from_basis, scale,
                                     sub)
from shoonyakasha.planet import resources
from shoonyakasha.planet.world import EXTRA_PROBES

import contracts
import fieldslate as fs
import pois
from hud import distance_text
from objectives import REST_SPEED
from play.core import CHECK_ORE_EXTENT, GOOD_TOAST, POI_REACH, box, engine, foot, planet, state, world
from play import contracting, showers, spaceflight, storms, vehicle, walking

# Points of interest (pois.py), on the planet: candidates within POI_REACH measured (again once the player
# has moved POI_WANT_MOVE); discovered within SCAN_RANGE, or within SEEN_RANGE in the view's cone
# (SEEN_COS); undiscovered ones within POI_REACH shown as signals; drawn within DRAW_RANGE; a pod's and a
# geode field's props made within PROP_RANGE; their spots looked for within USE_LOOK.
POI_WANT_MOVE = 500.0           # m
SCAN_RANGE, SEEN_RANGE, SEEN_COS = 600.0, 1_500.0, 0.85
DRAW_RANGE = 2_000.0            # m
PROP_RANGE = 120.0              # m
USE_LOOK = 60.0                 # m
CELL_SURVEY = 0.5               # credits of survey data per map cell explored
POI_MARKER, LOOTED_MARKER, SIGNAL_MARKER = fs.AMBER, fs.with_alpha(fs.MIST, 0.7), fs.with_alpha(fs.MIST, 0.6)


def poi_seed():
    return tuple(planet.terrain["seed"])


def poi_point(poi):
    """A point of interest's point on the ground, planet space."""
    return add(planet.centre(), scale(poi.direction, planet.radius + poi.height))


def poi_at(poi, offset):
    """A point `offset` (east, up, north, metres) from a point of interest's point, planet space."""
    up = poi.direction
    east, north = local_frame(up)
    return add(poi_point(poi), add(add(scale(east, offset[0]), scale(up, offset[1])), scale(north, offset[2])))


def player_here():
    """The player's feet (or the car), planet space, on the planet; None elsewhere or without either."""
    if state.vehicle.driving:
        return vehicle.car_position() if spaceflight.car_body() is planet else None
    if foot.player is None or foot.bubble.body is not planet:
        return None
    return foot.bubble.to_world(foot.player.feet())


def poi_distance(poi, here):
    """Metres along the ground from `here` (planet space) to a point of interest."""
    return planet.radius * math.acos(max(-1.0, min(1.0, dot(poi.direction, normalize(planet.relative(here))))))


def probe_pois():
    """The candidates waiting to be measured (pois.Pois.to_measure) measured
    with the world's extra probes, as the explored map does: only while
    nothing else uses them and no meteor shower's impact waits for its
    ground (shower_waiting); each with its water (PlanetWorld.water_level)."""
    p, asked = state.exploring.pois, state.exploring.asked
    if world.extra_probes and world.extra_probes != asked:
        return
    if asked is not None:
        if [probe for probe, _ in world.extra_heights] != asked:
            return
        for poi, (_, h), (_, climate), (_, ore), (_, resource) in zip(state.exploring.asking, world.extra_heights,
                                                                       world.extra_climates, world.extra_ores,
                                                                       world.extra_resources):
            p.measured(poi.key, h, climate, ore, resource, world.water_level(poi.direction))
        state.exploring.asked, state.exploring.asking = None, []
        world.extra_probes = []
    if showers.shower_waiting():
        return
    batch = p.to_measure(EXTRA_PROBES)
    if batch:
        asked = [(poi.direction, planet) for poi in batch]
        world.extra_probes = asked
        state.exploring.asked, state.exploring.asking = asked, batch


def update_pois(dt):
    """The candidates within POI_REACH looked for once the player has moved
    POI_WANT_MOVE; the found ones discovered within SCAN_RANGE, or within
    SEEN_RANGE in the view's cone; their scenes and props; the use key; and
    the explored map's newly walked cells paid as survey data."""
    explored = state.ui.map
    if state.exploring.walked_counted is None:
        state.exploring.walked_counted = explored.walked
    if explored.walked > state.exploring.walked_counted:
        state.economy.survey_data += CELL_SURVEY * (explored.walked - state.exploring.walked_counted)
        state.exploring.walked_counted = explored.walked
    here = player_here()
    if here is None:
        update_poi_scenes(None)
        state.exploring.use_spot = None
        return
    p = state.exploring.pois
    direction = normalize(planet.relative(here))
    last = state.exploring.want_at
    if last is None or planet.radius * math.acos(max(-1.0, min(1.0, dot(last, direction)))) > POI_WANT_MOVE:
        p.want(planet.index, direction, POI_REACH, planet.radius, poi_seed())
        state.exploring.want_at = direction
    view = walking.view_basis()[2]
    for poi in p.found():
        if poi.discovered:
            continue
        d = poi_distance(poi, here)
        towards = normalize(sub(poi_point(poi), state.view.position))
        if d <= SCAN_RANGE or (d <= SEEN_RANGE and dot(towards, view) >= SEEN_COS):
            poi.discovered = True
            state.ui.hud.toast("Found: %s, %s" % (poi.name, distance_text(d)), POI_MARKER, kind="finds")
    update_poi_scenes(here)
    make_poi_props(here)
    update_use(dt)


def update_poi_scenes(here):
    """The boxes of the found points within DRAW_RANGE of `here` made
    (pois.LAYOUTS), those beyond removed; the plants and rocks cleared
    around each (pois.CLEAR_RADIUS, PlanetWorld.set_clearings) as the set
    changes."""
    scenes = state.exploring.scenes
    near = [] if here is None else [poi for poi in state.exploring.pois.found() if poi_distance(poi, here) <= DRAW_RANGE]
    keys = sorted(poi.key for poi in near)
    if keys != state.exploring.cleared:
        state.exploring.cleared = keys
        nearest = sorted(near, key=lambda poi: poi_distance(poi, here))[:planet_world.MAX_CLEARINGS]
        world.set_clearings([planet_world.Clearing(planet.index, poi.direction, radius=pois.CLEAR_RADIUS[poi.kind],
                                                   edge=pois.CLEAR_EDGE,
                                                   clears=planet_world.CLEAR_PLANTS | planet_world.CLEAR_ROCKS)
                             for poi in nearest])
    wanted = {poi.key: poi for poi in near if pois.LAYOUTS.get(poi.kind)}
    for key in [k for k in scenes if k not in wanted]:
        for entity, _ in scenes.pop(key):
            engine.scene.destroy_entity(entity)
    positions, normals = box()
    for key, poi in wanted.items():
        if key in scenes:
            continue
        up = poi.direction
        east, north = local_frame(up)
        rotation = euler_from_basis(east, up, scale(north, -1.0))
        pieces = []
        for offset, size, colour in pois.LAYOUTS[poi.kind]:
            entity = engine.create_mesh(positions, normals=normals, name="Poi")
            engine.scene.set_rotation(entity, rotation)
            engine.scene.set_scale(entity, size)
            engine.scene.set_material_vec4(entity, "baseColorFactor", colour + (1.0,))
            engine.scene.set_material_float(entity, "gridStrength", 0.0)
            engine.scene.set_render_layer_mask(entity, 1)
            pieces.append((entity, poi_at(poi, (offset[0], offset[1] - pois.SINK, offset[2]))))
        scenes[key] = pieces


def draw_poi_scenes(camera):
    for pieces in state.exploring.scenes.values():
        for entity, point in pieces:
            engine.scene.set_position(entity, sub(point, camera))


def make_poi_props(here):
    """A pod's crate (a recovery's of its contract's size), and a geode
    field's rocks, made once the player's bubble is within PROP_RANGE of
    their point (each once: `done`)."""
    bubble = foot.bubble
    if bubble is None or bubble.body is not planet:
        return
    for poi in state.exploring.pois.found():
        if poi.kind not in ("pod", "geode") or poi.done or poi.looted or poi_distance(poi, here) > PROP_RANGE:
            continue
        up = poi.direction
        east, north = local_frame(up)
        if poi.kind == "pod":
            size, colour, mass = ((contracts.POD_SIZE, contracts.POD_COLOUR, contracts.POD_MASS) if poi.fixed_kind
                                  else (pois.POD_SIZE, pois.POD_COLOUR, pois.POD_MASS))
            spot = bubble.to_bubble(poi_at(poi, (0.0, 0.5 * size + 0.3, 0.0)))
            crate = foot.props.spawn_crate(bubble, spot, quat_from_basis(east, up, scale(north, -1.0)), size,
                                           colour=colour, mass=mass)
            state.exploring.pods[id(crate)] = poi.key
        else:
            resource = poi.resource or resources.FERRITE
            for k in range(pois.GEODE_ROCKS):
                angle = 2.0 * math.pi * k / pois.GEODE_ROCKS
                r = pois.GEODE_SPREAD * (0.4 + 0.6 * ((k * 7) % 5) / 4.0)
                rock = walking.ore_rock(poi_at(poi, (r * math.cos(angle), 0.6, r * math.sin(angle))),
                                CHECK_ORE_EXTENT, pois.GEODE_GRADE, resource)
                state.economy.seen[id(rock)] = (planet.name, poi.direction)
            poi.looted = True
        poi.done = [0]


def usable_spot():
    """(poi, spot index, distance) of the nearest spot (pois.SPOTS) not used
    yet within its reach of the player's feet, on foot; or None."""
    if foot.player is None or foot.bubble.body is not planet:
        return None
    feet = foot.bubble.to_world(foot.player.feet())
    best = None
    for poi in state.exploring.pois.found():
        if poi.looted or poi.kind not in pois.SPOTS or poi_distance(poi, feet) > USE_LOOK:
            continue
        for k, (offset, reach, _) in enumerate(pois.SPOTS[poi.kind]):
            if k in poi.done:
                continue
            d = length(sub(poi_at(poi, offset), feet))
            if d <= reach and (best is None or d < best[2]):
                best = (poi, k, d)
    return best


def update_use(dt):
    """The use key (E while the HUD shows, or --check's) held on the usable spot for its time
    (pois.SPOTS) uses it (use_spot); let go between uses."""
    spot = state.exploring.use_spot = usable_spot()
    held = (engine.input.is_key_down(keys.E) and walking.hud_shown()) or state.session.check_use
    if not held:
        state.exploring.use_latch = False
    if spot is None or not held or state.exploring.use_latch:
        state.exploring.use_progress = 0.0
        return
    poi, k, _ = spot
    state.exploring.use_progress += dt
    if state.exploring.use_progress >= pois.SPOTS[poi.kind][k][2]:
        state.exploring.use_progress = 0.0
        state.exploring.use_latch = True
        use_spot(poi, k)


def use_spot(poi, k):
    """Spot `k` of `poi` used: a beacon reveals the map around it
    (ExploredMap.reveal) and gives survey data; a wreck's spot a part; a
    rig's crate RIG_ROCKS ore rocks and a part; an arch survey data. The
    point is looted once every spot is used."""
    poi.done.append(k)
    poi.discovered = True
    hud = state.ui.hud
    if poi.kind in ("beacon", "arch"):
        value = pois.survey_value(poi)
        state.economy.survey_data += value
        if poi.kind == "beacon":
            cells = state.ui.map.reveal(planet, poi.direction, pois.REVEAL)
            hud.toast("Beacon activated: the map within %s revealed (%d cells), survey data worth %.0f cr"
                      % (distance_text(pois.REVEAL), cells, value), GOOD_TOAST, kind="finds")
        else:
            hud.toast("Arch scanned: survey data worth %.0f cr" % value, GOOD_TOAST, kind="finds")
    else:
        part = pois.parts_for(poi, k)
        state.economy.parts[part] = state.economy.parts.get(part, 0) + 1
        text = "%s: a %s" % (poi.name, pois.PART_NAMES[part].lower())
        if poi.kind == "rig":
            offset = pois.SPOTS["rig"][k][0]
            for n in range(pois.RIG_ROCKS):
                rock = walking.ore_rock(poi_at(poi, (offset[0] + 0.6 * n, 1.6, offset[2])), CHECK_ORE_EXTENT,
                                pois.RIG_GRADE, poi.resource or resources.FERRITE)
                state.economy.seen[id(rock)] = (planet.name, poi.direction)
            text += " and %d ore rocks" % pois.RIG_ROCKS
        hud.toast(text, GOOD_TOAST, kind="finds")
    if len(poi.done) == len(pois.SPOTS[poi.kind]):
        poi.looted = True


def pad_under(crate, held):
    """The open outpost on whose pad `crate` has settled (slower than
    objectives.REST_SPEED), not `held`; or None."""
    if crate is held:
        return None
    body, position, velocity = foot.prop_state(crate)
    if not walking.settled(crate) or length(velocity) >= REST_SPEED:
        return None
    return next((o for o in state.site.outposts
                 if o.body is body and not storms.closed(o) and o.at(add(body.centre(), position))), None)


def sell_pods(held):
    """Each cargo pod's crate settled on an outpost's pad (pad_under), not
    `held`, sold for pois.POD_VALUE, its point looted; a recovery's pod
    only on its contract's destination's pad, which completes it."""
    for crate in list(foot.props.crates):
        key = state.exploring.pods.get(id(crate))
        if key is None:
            continue
        outpost = pad_under(crate, held)
        if outpost is None:
            continue
        c = contracting.crate_contract(crate)
        if c is not None:
            if outpost.name == c.destination:
                contracting.forget_crate(crate)
                foot.props.remove(foot.bubble, crate, release=False)
                contracting.complete_contract(c)
            continue
        state.economy.market.credits += pois.POD_VALUE
        state.economy.earned += pois.POD_VALUE
        state.exploring.pods_sold += 1
        del state.exploring.pods[id(crate)]
        foot.props.remove(foot.bubble, crate, release=False)
        poi = state.exploring.pois.pois.get(key)
        if poi is not None:
            poi.looted = True
        state.ui.hud.toast("Cargo pod delivered to %s: +%s cr" % (outpost.name, format(int(pois.POD_VALUE), ",")),
                           GOOD_TOAST, kind="finds")


def poi_markers(body):
    """The compass's and the map's markers of the points of interest on
    `body` (the planet): each discovered by its name (greyed once looted),
    each found but not discovered within POI_REACH as a signal."""
    here = player_here()
    if body is not planet or here is None:
        return []
    found = []
    for poi in state.exploring.pois.found():
        if poi.discovered:
            found.append((poi.name, poi_point(poi), LOOTED_MARKER if poi.looted else POI_MARKER))
        elif poi_distance(poi, here) <= POI_REACH:
            found.append(("Signal", poi_point(poi), SIGNAL_MARKER))
    return found
