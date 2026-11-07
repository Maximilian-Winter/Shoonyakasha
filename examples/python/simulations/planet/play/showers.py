"""
Meteor showers (events.py): planning the next over the body the camera
is over, measuring its impacts' ground, the meteors drawn as glows, each
impact's flash, shake, plume and push, the meteorite and rocks left, and
which props are too hot to grab.
"""

import math

import numpy as np
from shoonyakasha.planet import world as planet_world
from shoonyakasha.planet.vec import add, cross, length, local_frame, normalize, quat_from_basis, rotate, scale, sub
from shoonyakasha.planet import resources
from shoonyakasha.planet.world import EXTRA_PROBES
from shoonyakasha.planet.rocks import density as rock_density, rock_colour

import events
from hud import bearing, distance_text
from play.core import METEOR_MARKER, PLUME_PUFFS, SMALL_PUFFS, args, engine, foot, planet, settings, state, world

SHOWER_TRIES = 4                # times a shower whose large impact falls in water is planned again
FALL_SPEED = 5.0                # m/s down a meteorite or rock flies from its impact until its ground is measured
FALL_LIFT = 2.0                 # m above the ground it starts at
METEOR_GLOW = (1.0, 0.92, 0.75)  # LDR, a large meteor's head
TRAIL_GLOW = (1.0, 0.62, 0.3)
HEAD_LIGHT, TRAIL_LIGHT = 3.0, 2.0   # times those: the sum is clamped at white, so the bright core widens
HEAD_SIZE = 25.0                # m across a large meteor's head
SMALL_HEAD = 0.5                # of that a small one's
HEAD_PIXELS = 24.0              # of the screen's height in pixels a head is across, at least
IGNITE_SECONDS = 1.5            # s from its entry a meteor brightens over
TRAIL_SECONDS = 2.0             # s of its path a trail shows
TRAIL_SEGMENTS = 8
TRAIL_WIDTH = 0.6               # of the head's size a trail is wide at the head
SMALL_LIGHT = 0.6               # a small meteor's light and an impact's flash and shake, of a large one's
FLASH_COLOUR = (1.0, 0.94, 0.82)
FLASH_ALPHA = 0.6
FLASH_REACH = 2_000.0           # m within which a large impact's flash is full
FLASH_SECONDS = 0.6
SHAKE_REACH = 3_000.0           # m within which an impact shakes the view
SHAKE_ANGLE = math.radians(1.5)  # at the impact, falling to none at SHAKE_REACH
SHAKE_SECONDS = 1.5
SHAKE_HZ = 11.0
PUSH_REACH = 3.0                # blast radii within which the props in the bubble are pushed
PUSH_SPEED = 6.0                # m/s, at the impact
SMALL_BLAST = 3.0               # m: a small impact's blast radius
PLUME_SECONDS = 20.0
PLUME_RISE = 6.0                # m/s
PLUME_COLOUR = (0.2, 0.17, 0.14)  # LDR, added to the image
PLUME_NEAR = 3.0                # puff sizes from the camera within which a puff fades out
HOT_GLOW = (1.0, 0.42, 0.12)
HOT_SIZE = 3.0                  # times a hot prop's radius its glow is across
MOON_HEAD = 0.3                 # of a meteor's head's light on the moon, where it has no trail (no air)


def shower_here():
    """The unit direction under the camera on the planet, or None at the start's menu, with neither a player
    nor the car driven, or with the camera over the moon."""
    if state.session.starting or (foot.player is None and not state.vehicle.driving):
        return None
    if world.body_under(state.view.position) is not planet:
        return None
    return normalize(planet.relative(state.view.position))


def camera_on():
    """(the world body under the camera, the unit direction to the camera from
    its centre), or None at the start's menu or with neither a player nor
    the car driven."""
    if state.session.starting or (foot.player is None and not state.vehicle.driving):
        return None
    body = world.body_under(state.view.position)
    return body, normalize(body.relative(state.view.position))


def shower_body():
    """The world body the shower falls on."""
    return state.showers.body or planet


def body_gm(body):
    return world.physics.gm if body is planet else world.physics.moon.gm


def set_shower(shower):
    """`shower` (events.Shower) the next or current one, nothing of it measured, warned of or struck yet."""
    if state.showers.asked is not None and world.extra_probes == state.showers.asked:
        world.extra_probes = []
    state.showers.shower = shower
    state.showers.impact_heights, state.showers.paths = {}, {}
    state.showers.asked, state.showers.asking = None, []
    state.showers.warned, state.showers.struck = False, 0


def probe_shower():
    """The ground under the shower's impacts not yet struck measured with
    the world's extra probes, as the points of interest are: only while
    nothing else uses them."""
    shower, asked = state.showers.shower, state.showers.asked
    if shower is None or (world.extra_probes and world.extra_probes != asked):
        return
    if asked is not None:
        if [probe for probe, _ in world.extra_heights] != asked:
            return
        for k, (_, h) in zip(state.showers.asking, world.extra_heights):
            state.showers.impact_heights[k] = h
        state.showers.asked, state.showers.asking = None, []
        world.extra_probes = []
    wanted = [k for k in range(state.showers.struck, len(shower.impacts)) if k not in state.showers.impact_heights]
    if wanted:
        asked = [(shower.impacts[k].direction, shower_body()) for k in wanted[:EXTRA_PROBES]]
        world.extra_probes = asked
        state.showers.asked, state.showers.asking = asked, wanted[:EXTRA_PROBES]


def shower_waiting():
    """Whether the shower has impacts not yet struck whose ground is not yet measured."""
    shower = state.showers.shower
    return shower is not None and any(k not in state.showers.impact_heights
                                      for k in range(state.showers.struck, len(shower.impacts)))


def on_land(body, direction, height):
    """Whether the ground `height` m up at the unit `direction` on `body` is
    above the sea and any lake or river: always on the moon, which has none."""
    if body is not planet:
        return True
    level = world.water_level(direction)
    return height > 0.0 and (level is None or height > level)


def update_showers():
    """The meteor showers (events.py): the next planned around the camera
    on the body it is over (`shower_body`; on the moon every
    events.MOON_MEAN_GAP on average)
    (at a loaded save's next_shower, at least WARNING_SECONDS on, when
    there is one), its impacts' ground measured (probe_shower) and,
    while its large impact falls in water, planned again with the same
    start up to SHOWER_TRIES times; announced WARNING_SECONDS before its
    start once that is known, with a toast and a marker on the compass;
    each meteor's path flown back from where it strikes; and each impact
    struck at its time (`strike`), also while the camera is away from its
    body. Once all have struck, the next is planned. Under --check none is planned; the check gives its own. The
    props that have cooled or are gone leave `hot`."""
    present = {id(p) for p in foot.props.rocks}
    state.showers.hot = {i: hot for i, hot in state.showers.hot.items()
                    if i in present and events.heat(world.time - hot[0], hot[1]) > 0.0}
    on, shower = camera_on(), state.showers.shower
    if args.check and shower is None:
        return
    if on is None or (shower is not None and on[0] is not shower_body()):
        if shower is None or state.session.starting:
            return
        here = shower.centre             # away from its body: a shower planned goes on
    else:
        here = on[1]
    if shower is None:
        body = on[0]
        start, state.showers.next_start = state.showers.next_start, None
        if start is not None:
            start = max(start, world.time + events.WARNING_SECONDS)
        state.showers.body = None if body is planet else body
        gap = events.MEAN_GAP if body is planet else events.MOON_MEAN_GAP
        set_shower(events.schedule(world.time, state.showers.rng, here, body.radius, start, gap))
        state.showers.tries = 0
        shower = state.showers.shower
    body = shower_body()
    probe_shower()
    heights = state.showers.impact_heights
    large = next(k for k, i in enumerate(shower.impacts) if i.large)
    known = large in heights or state.showers.struck > large
    if (large in heights and not state.showers.warned and state.showers.tries < SHOWER_TRIES
            and not on_land(body, shower.impacts[large].direction, heights[large])):
        state.showers.tries += 1
        set_shower(events.schedule(world.time, state.showers.rng, here, body.radius, shower.start))
        return
    if known and not state.showers.warned and world.time >= shower.start - events.WARNING_SECONDS:
        state.showers.warned = True
        centre = add(body.centre(), scale(shower.centre, body.radius))
        towards = bearing(here, sub(centre, state.view.position))
        state.ui.hud.toast("Meteor shower inbound: bearing %03.0f, %s"
                           % (towards or 0.0, distance_text(events.ground_distance(here, shower.centre,
                                                                                   body.radius))), METEOR_MARKER)
    for k, impact in enumerate(shower.impacts):
        if k in heights and k not in state.showers.paths:
            point = scale(impact.direction, body.radius + heights[k])
            state.showers.paths[k] = events.Path(point, events.velocity(impact, impact.direction), body_gm(body),
                                            body.radius)
    while state.showers.struck < len(shower.impacts) and shower.impacts[state.showers.struck].time <= world.time:
        strike(state.showers.struck)
        state.showers.struck += 1
    if state.showers.struck == len(shower.impacts):
        state.showers.shower = None


def meteor_rock(impact, ground, body):
    """The meteorite (an ore boulder) or the rock of `impact` on `body`, of iridium at
    its grade and mass, flying down at FALL_SPEED from FALL_LIFT above the
    ground at its point (`ground` m above the body's radius, as measured
    before it struck) until its ground is measured; its prop, noted as
    first seen there."""
    kind = "boulder" if impact.large else "rock"
    points, unit_volume = foot.rocks.kind_data(foot.physics, kind)
    s = (impact.mass / (rock_density(resources.IRIDIUM) * unit_volume)) ** (1.0 / 3.0)
    radius = float(np.linalg.norm(points * s, axis=1).max())
    up = impact.direction
    east, _ = local_frame(up)
    record = {"kind": kind, "on": body.name, "state": "flying",
              "position": list(scale(up, body.radius + ground + 0.5 * s + FALL_LIFT)),
              "rotation": list(quat_from_basis(east, up, cross(east, up))),
              "velocity": list(scale(up, -FALL_SPEED)), "spin": [0.0, 0.0, 0.0], "size": 2.0 * radius,
              "radius": radius, "rest": 0.5 * s, "mass": impact.mass, "extent": [s, s, s],
              "colour": list(rock_colour(body.index, impact.grade, resources.IRIDIUM)), "key": None,
              "ground": None, "grade": impact.grade, "resource": resources.IRIDIUM}
    prop = foot.rocks.restore(foot.physics, record, {body.name: body})
    state.economy.seen[id(prop)] = (body.name, up)
    return prop


def strike(k):
    """The shower's impact `k` strikes, at the ground measured there (the
    top of its body's terrain when it is not yet known): the large one digs its crater
    (events.keep, PlanetWorld.set_craters) and leaves its meteorite, a small
    one its rock (meteor_rock), hot (events.LARGE_COOLING or
    SMALL_COOLING); the props in the bubble within PUSH_REACH times its
    blast's radius (the crater's, or SMALL_BLAST) pushed away (`push`); a
    flash of FLASH_ALPHA, full within FLASH_REACH and falling with the
    square of the distance beyond, and within SHAKE_REACH the view shaken by
    up to SHAKE_ANGLE, both SMALL_LIGHT as strong for a small impact; and a
    plume of dust (PLUME_PUFFS, or SMALL_PUFFS) at its point. The large
    one's meteorite is marked on the compass and announced by a toast."""
    impact, body = state.showers.shower.impacts[k], shower_body()
    ground = state.showers.impact_heights.get(k, body.heights[1])
    up = impact.direction
    point = add(body.centre(), scale(up, body.radius + ground))
    on = camera_on()
    here = on[1] if on is not None and on[0] is body else up
    if impact.large:
        radius, depth, rim = impact.crater
        crater = planet_world.Crater(body.index, up, radius, depth, rim)
        state.showers.craters = events.keep(state.showers.craters, crater, world.time, here, body.radius)
        world.set_craters([c for c, _ in state.showers.craters])
    prop = meteor_rock(impact, ground, body)
    state.showers.hot[id(prop)] = (world.time, events.LARGE_COOLING if impact.large else events.SMALL_COOLING)
    blast = impact.crater[0] if impact.large else SMALL_BLAST
    push(point, PUSH_REACH * blast)
    light = 1.0 if impact.large else SMALL_LIGHT
    distance = length(sub(point, state.view.position))
    state.showers.flash = (world.time, FLASH_ALPHA * light * min(1.0, (FLASH_REACH / max(distance, 1.0)) ** 2))
    if distance < SHAKE_REACH:
        state.showers.shake = (world.time, SHAKE_ANGLE * light * (1.0 - distance / SHAKE_REACH))
    east, north = local_frame(up)
    puffs = PLUME_PUFFS if impact.large else SMALL_PUFFS
    for n in range(puffs):
        a = 2.0 * math.pi * n / puffs
        offset = scale(add(scale(east, math.cos(a)), scale(north, math.sin(a))), 0.3 * blast)
        state.showers.plumes.append((add(point, offset), up, world.time, blast, light, 0.5 + 0.5 * n / puffs))
    if impact.large:
        state.showers.meteorite = prop
        state.ui.hud.toast("Meteorite down: bearing %03.0f, %s"
                           % (bearing(here, sub(point, state.view.position)) or 0.0,
                              distance_text(events.ground_distance(here, up, body.radius))), METEOR_MARKER)


def push(point, reach):
    """The props in the bubble within `reach` m of `point` (planet space)
    pushed away from it and up, at up to PUSH_SPEED at it and none at
    `reach`."""
    bubble = foot.bubble
    if bubble is None:
        return
    for prop in foot.props.in_physics():
        p = foot.physics.get_transform(prop.body)[0]
        away = sub(bubble.to_world(p), point)
        d = length(away)
        if d >= reach:
            continue
        up = bubble.up(p)
        out = normalize(add(scale(away, 1.0 / d) if d > 1e-6 else up, up))
        foot.physics.add_impulse(prop.body, scale(out, prop.mass * PUSH_SPEED * (1.0 - d / reach)))


def too_hot(body):
    """Whether the physics body `body` is a prop still hot from its impact (the gun's TOO HOT)."""
    prop = foot.props.find(body) if body is not None else None
    hot = state.showers.hot.get(id(prop)) if prop is not None else None
    return hot is not None and events.heat(world.time - hot[0], hot[1]) > 0.0


def split_hot(boulder, chunks):
    """The chunks of a hot meteorite as hot as it was (Rocks.on_split)."""
    hot = state.showers.hot.get(id(boulder)) if boulder is not None else None
    if hot is not None:
        for chunk in chunks:
            state.showers.hot[id(chunk)] = hot
    if boulder is not None and boulder is state.showers.meteorite:
        state.showers.meteorite = None


def meteorite_point():
    """The meteorite still lying whole, planet space, or None."""
    meteorite = state.showers.meteorite
    if meteorite is None or not any(p is meteorite for p in foot.props.rocks):
        return None
    body, position, _ = foot.prop_state(meteorite)
    return add(body.centre(), position)


def flash_tint():
    """The impact's flash over the screen ((r, g, b), alpha), fading over FLASH_SECONDS, or None."""
    flash = state.showers.flash
    if flash is None or not 0.0 <= world.time - flash[0] < FLASH_SECONDS:
        return None
    return FLASH_COLOUR, flash[1] * (1.0 - (world.time - flash[0]) / FLASH_SECONDS)


def shaken(basis):
    """The view's `basis` (right, up, forward) turned by the impact's shake, fading over SHAKE_SECONDS."""
    shake = state.showers.shake
    if shake is None or not 0.0 <= world.time - shake[0] < SHAKE_SECONDS:
        return basis
    age = world.time - shake[0]
    amplitude = shake[1] * (1.0 - age / SHAKE_SECONDS)
    yaw = amplitude * math.sin(2.0 * math.pi * SHAKE_HZ * age)
    pitch = amplitude * math.sin(2.0 * math.pi * 1.3 * SHAKE_HZ * age + 1.0)
    right, up, forward = basis
    right, forward = rotate(right, up, yaw), rotate(forward, up, yaw)
    return right, rotate(up, right, pitch), rotate(forward, right, pitch)


def draw_showers(camera, basis):
    """The meteors in flight, their dust plumes and the hot props, as
    glowing quads (viewmodel.Glows) relative to `camera`, the view's
    `basis`:
    - each meteor's head a disc HEAD_SIZE across (SMALL_HEAD of that for a
      small one), at least HEAD_PIXELS of the screen, HEAD_LIGHT times
      METEOR_GLOW (SMALL_LIGHT of it for a small one) brightening over
      IGNITE_SECONDS from its entry (MOON_HEAD of that on the moon); and
      its trail (none on the moon, which has no air), TRAIL_SEGMENTS
      streaks along the last TRAIL_SECONDS of its path, TRAIL_WIDTH of the
      head wide, TRAIL_LIGHT times TRAIL_GLOW, narrowing and fading to its
      end;
    - each plume's puffs discs rising at up to PLUME_RISE, growing from
      the blast's radius to three times it and fading in PLUME_COLOUR
      over PLUME_SECONDS, and fading out within PLUME_NEAR of their size
      from the camera;
    - each hot prop a disc HOT_SIZE times its radius across in HOT_GLOW
      times its heat."""
    glows = state.ui.meteor_glows
    glows.begin(basis)
    body = shower_body()
    now, centre, airless = world.time, body.centre(), body is not planet
    pixel = math.radians(settings["fov"]) / max(1, engine.framebuffer_size[1])
    heads = 0
    shower = state.showers.shower
    for k in range(state.showers.struck, len(shower.impacts) if shower is not None else 0):
        impact, path = shower.impacts[k], state.showers.paths.get(k)
        before = impact.time - now
        if path is None or not 0.0 < before <= path.duration:
            continue
        light = (1.0 if impact.large else SMALL_LIGHT) * min(1.0, (path.duration - before) / IGNITE_SECONDS)
        if airless:
            light *= MOON_HEAD
        head = add(centre, path.at(before))
        size = max(HEAD_SIZE * (1.0 if impact.large else SMALL_HEAD), HEAD_PIXELS * pixel * length(sub(head, camera)))
        glows.disc(sub(head, camera), size, scale(METEOR_GLOW, HEAD_LIGHT * light))
        heads += 1
        last = head
        for j in range(1, TRAIL_SEGMENTS + 1 if not airless else 1):
            point = add(centre, path.at(before + TRAIL_SECONDS * j / TRAIL_SEGMENTS))
            fade = 1.0 - (j - 0.5) / TRAIL_SEGMENTS
            glows.streak(sub(last, camera), sub(point, camera), TRAIL_WIDTH * size * fade,
                         scale(TRAIL_GLOW, TRAIL_LIGHT * light * fade))
            last = point
    state.showers.plumes = [p for p in state.showers.plumes if now - p[2] < PLUME_SECONDS]
    for point, up, since, blast, light, rise in state.showers.plumes:
        age = now - since
        if age < 0.0:
            continue
        at = sub(add(point, scale(up, PLUME_RISE * rise * age + 0.5 * blast)), camera)
        size = blast * (1.0 + 2.0 * age / PLUME_SECONDS)
        fade = (1.0 - age / PLUME_SECONDS) * min(1.0, age / 0.3) * min(1.0, length(at) / (PLUME_NEAR * size))
        glows.disc(at, size, scale(PLUME_COLOUR, light * fade))
    for prop in foot.props.rocks:
        hot = state.showers.hot.get(id(prop))
        heat = events.heat(now - hot[0], hot[1]) if hot is not None else 0.0
        if heat > 0.0:
            body, position, _ = foot.prop_state(prop)
            glows.disc(sub(add(body.centre(), position), camera), HOT_SIZE * prop.radius, scale(HOT_GLOW, heat))
    glows.end()
    state.showers.heads_drawn = heads
