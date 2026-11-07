"""
Storms (events.schedule_storm): planning them upwind, the outposts they
close and the toasts announcing it, buffeting the car, and the rain.
"""

import zlib

from shoonyakasha.planet.vec import add, dot, length, local_frame, normalize, scale, sub
from shoonyakasha.planet import wind as wind_field

import events
import hovercar
import hud as hud_module
from play.core import GOOD_TOAST, RAIN_DROPS, STORM_WARNING, args, planet, state, world
from play import showers, spaceflight

BUFFET_WIND = 20.0              # m/s of wind at the car beyond which it is buffeted
BUFFET = 1.5                    # m/s^2 per m/s of wind beyond BUFFET_WIND, at most
BUFFET_RATE = 0.7               # of the noise's cells a second
RAIN_BOX = 16.0                 # m
RAIN_SPEED = 9.0                # m/s
RAIN_STREAK = 0.06              # s
RAIN_WIDTH = 0.012              # m
RAIN_COLOUR = (0.32, 0.35, 0.4)  # LDR, added to the image


def downwind(here):
    """The wind's prevailing direction along the ground at the unit `here`, or None where it is straight up."""
    d = wind_field.prevailing(world.time)
    t = sub(d, scale(here, dot(d, here)))
    return normalize(t) if length(t) > 1e-6 else None


def closed(outpost):
    """Whether a storm covers `outpost`: its pads closed, it buys, sells, fuels and offers nothing."""
    storm = world.storm
    return storm is not None and outpost.body is planet and storm.at(outpost.direction, world.time) > 0.0


def closed_note(outpost):
    return "%s is closed: a storm is over it" % outpost.name


def update_storms(dt):
    """The storms (events.schedule_storm, PlanetWorld.storm): the next
    planned around the camera once the last has ended (at a loaded save's
    storm when there is one), none under --check, which gives its own; a
    toast for each outpost on the planet STORM_WARNING before the storm
    covers it, and another once it has passed or the storm has ended; one
    on going into and out of it; and while the camera is in it, each active contract's deadline
    held back by `dt` (contracts.Contract.paused)."""
    here = showers.shower_here()
    if here is None:
        return
    storm, hud = world.storm, state["hud"]
    if storm is None or world.time >= storm.end:
        for name in sorted(state["storm_closed"]):
            hud.toast("The storm has passed %s: its pads are open" % name, GOOD_TOAST)
        if state["in_storm"]:
            hud.toast("Out of the storm", GOOD_TOAST)
        state["storm_warned"], state["storm_closed"], state["in_storm"] = set(), set(), False
        if args.check:
            world.storm = None
            return
        world.storm = events.schedule_storm(world.time, state["storm_rng"], here, planet.radius, downwind(here))
        storm = world.storm
    for o in [o for o in state["outposts"] if o.body is planet]:
        if o.name not in state["storm_warned"] and (closed(o) or storm.at(o.direction, world.time + STORM_WARNING) > 0.0):
            state["storm_warned"].add(o.name)
            hud.toast("Storm approaching %s: landing pads closed" % o.name, hud_module.WARNING)
        if closed(o):
            state["storm_closed"].add(o.name)
        elif o.name in state["storm_closed"]:
            state["storm_closed"].discard(o.name)
            state["storm_warned"].discard(o.name)
            hud.toast("The storm has passed %s: its pads are open" % o.name, GOOD_TOAST)
    inside = storm.at(here, world.time) > 0.0
    if inside != state["in_storm"]:
        hud.toast("In a storm: contracts' deadlines held" if inside else "Out of the storm",
                  hud_module.WARNING if inside else GOOD_TOAST)
        state["in_storm"] = inside
    if inside:
        for c in state["contracts"]:
            c.paused += dt


def buffet_car(dt):
    """The car pushed sideways while the wind at it is over BUFFET_WIND: by
    BUFFET a second per m/s beyond, times noise of the world's clock
    (wind.noise3 at BUFFET_RATE) and the share of the way it is from its
    cushion to hovercar.AIR_HEIGHT, so not on its cushion."""
    car = state["car"]
    if car is None or car.height is None:
        return
    speed = length(world.wind_at(spaceflight.car_body(), car.position, car.height))
    air = min(1.0, max(0.0, (car.height - hovercar.HOVER_MIN) / (hovercar.AIR_HEIGHT - hovercar.HOVER_MIN)))
    if speed <= BUFFET_WIND or air <= 0.0:
        return
    push = BUFFET * (speed - BUFFET_WIND) * air * wind_field.noise3(world.time * BUFFET_RATE, 0.5, 0.5)
    car.velocity = add(car.velocity, scale(car.right(), push * dt))


def draw_rain(camera, basis):
    """While the camera is in a storm, up to RAIN_DROPS streaks falling at
    RAIN_SPEED with the wind at the camera through a box RAIN_BOX metres
    across around it, each RAIN_STREAK seconds of its fall long, in
    RAIN_COLOUR, as many and as bright as how much of the storm is there
    (viewmodel.Glows)."""
    glows = state["rain_glows"]
    glows.begin(basis)
    storm = world.storm
    body = world.body_under(camera)
    s = storm.at(normalize(planet.relative(camera)), world.time) if storm is not None and body is planet else 0.0
    count = int(RAIN_DROPS * s)
    if count > 0:
        up = normalize(planet.relative(camera))
        east, north = local_frame(up)
        wind = world.wind_at(planet, planet.relative(camera), 10.0)
        fall = sub(wind, scale(up, RAIN_SPEED))
        t = world.time
        for k in range(count):
            h = zlib.crc32(b"rain%d" % k)
            fx, fy, fz = (h & 1023) / 1024.0, ((h >> 10) & 1023) / 1024.0, ((h >> 20) & 1023) / 1024.0
            # Each drop's place in the box, carried by its fall and wrapped.
            x = ((fx * RAIN_BOX + dot(fall, east) * t) % RAIN_BOX) - 0.5 * RAIN_BOX
            y = ((fy * RAIN_BOX + dot(fall, north) * t) % RAIN_BOX) - 0.5 * RAIN_BOX
            z = ((fz * RAIN_BOX + dot(fall, up) * t) % RAIN_BOX) - 0.5 * RAIN_BOX
            at = add(add(scale(east, x), scale(north, y)), scale(up, z))
            glows.streak(at, add(at, scale(fall, -RAIN_STREAK)), RAIN_WIDTH, scale(RAIN_COLOUR, s))
    glows.end()
    state["rain_drawn"] = count
