"""
The day and the suit: the day's time and hour, the air's temperature
where the camera is, the suit's reserve and the player's health, waking
at an outpost, the suit's tint and slowed walk, and the lights at night.
"""

from shoonyakasha.planet.vec import add, dot, length, local_frame, normalize, scale, sub

import clock
import lamps
import suit
import outposts
from play.core import RECOVERY_FEE, foot, planet, state, world
from play import economy, spaceflight, vehicle, walking

START_HOUR = 9.0                # the site's hour (clock.py) when a new game starts
CAR_HEADLIGHTS = ((-0.95, 0.0, -3.0), (0.95, 0.0, -3.0))   # m from the car's centre (right, up, back)
CAR_HEADLIGHT_DIP = 4.0         # degrees below its nose
CHILL_HEIGHT = 10.0             # m above the ground the wind's chill is reckoned at


def day_time():
    """The day's time (clock.py): the world's clock plus the day's offset,
    set for START_HOUR the first time it is asked for."""
    if state["day_offset"] is None:
        state["day_offset"] = clock.time_of(START_HOUR, world.time) - world.time
    return world.time + state["day_offset"]


def hour_text():
    hour = clock.hour(day_time())
    return "%02d:%02d" % (int(hour) % 24, int(hour * 60.0) % 60)


def here_temperature():
    """The air's temperature (degrees C) where the camera is: on the planet
    suit.local_temperature of the climate under it (with its height) at
    the local hour and the speed of the wind there CHILL_HEIGHT above the
    ground (PlanetWorld.wind_at); on the moon suit.moon_temperature
    of the sun's elevation there; None while the ground is not known."""
    camera = state["position"]
    body = world.body_under(camera)
    radial = normalize(body.relative(camera))
    if body is not planet:
        return suit.moon_temperature(dot(world.sun_direction(), radial))
    climate = world.ground_climate
    if climate is None:
        return None
    _, up, east, north = world.site_frame()
    hour = clock.local_hour(radial, day_time(), up, east, north)
    wind = length(world.wind_at(planet, body.relative(camera), CHILL_HEIGHT))
    return suit.local_temperature(climate[0], climate[1], hour, wind)


def update_suit(dt):
    """The suit stepped by `dt` seconds (suit.step_suit) at the temperature
    here, sheltered in the car or on an outpost's pad; with its health
    out, the player wakes at the nearest outpost (`wake_at_outpost`)."""
    if foot.player is None and not state["driving"]:
        return
    temperature = here_temperature()
    state["temperature"] = temperature
    if temperature is None:
        return
    outpost = None if state["driving"] else economy.at_outpost()
    sheltered = state["driving"] or outpost is not None
    if suit.step_suit(state["suit"], state["suit_params"], temperature, sheltered, outpost is not None, dt):
        wake_at_outpost()


def wake_at_outpost():
    """The player, its health gone, woken on the pad of the outpost
    nearest it on its body for RECOVERY_FEE (as far as the credits go):
    what the gun held dropped where it fell, the suit full again; it comes
    back once the ground there is known (maybe_spawn)."""
    feet = foot.bubble.to_world(foot.player.feet())
    body = foot.bubble.body
    outpost = outposts.nearest([o for o in state["outposts"] if o.body is body], feet)
    if outpost is None:
        return
    fee = min(RECOVERY_FEE, state["market"].credits)
    state["market"].credits -= fee
    foot.drop_bubble()
    up = normalize(sub(outpost.point(), body.centre()))
    state["position"] = add(outpost.point(), scale(up, 2.0))
    state["forward"] = local_frame(up)[1]
    state["loading"] = {"on": body.name, "pitch": 0.0,
                        "toast": "You came to at %s; the outpost's medics charged %s cr"
                                 % (outpost.name, format(int(round(fee)), ","))}
    world.ground = None
    state["suit"] = suit.SuitState(reserve=state["suit_params"].reserve)
    state["woken"] += 1
    print("[walker] the suit gave out; woke at %s for %.0f cr" % (outpost.name, fee), flush=True)


def suit_effects():
    """suit.effects at the temperature here: (the tint, the movement's factor); none while it is not known."""
    if state["temperature"] is None:
        return None, 1.0
    return suit.effects(state["suit"], state["suit_params"], state["temperature"])


def slow_down():
    """The walk scaled by the suit's movement factor, no running below it."""
    factor = suit_effects()[1]
    if factor < 1.0:
        controls = foot.controls
        controls.forward *= factor
        controls.strafe *= factor
        controls.run = False


def suit_rows():
    """The objective card's rows of the suit and the temperature here."""
    s, temperature = state["suit"], state["temperature"]
    rows = [("Suit", "%d %%" % round(100.0 * s.reserve / state["suit_params"].reserve)
             + ("" if s.health >= 0.995 else ", health %d %%" % round(100.0 * s.health)))]
    if temperature is not None:
        rows.append(("Temp", "%.0f C" % temperature))
    return rows


def update_lights(camera):
    """The sun where clock.py has it at the day's time, the exposure for
    it seen from `camera` (clock.set_sun), and while lamps.lights_on the
    local lights: each outpost's, the car's headlights (CAR_HEADLIGHTS,
    CAR_HEADLIGHT_DIP down) and on foot the suit lamp."""
    clock.set_sun(world, day_time(), camera)
    lights = []
    if lamps.lights_on(world, camera):
        lights += lamps.outpost_lights(state["outposts"])
        if state["car"] is not None:
            right, up, forward = spaceflight.car_attitude()
            lights += lamps.headlights(vehicle.car_position(), right, up, forward, CAR_HEADLIGHTS, CAR_HEADLIGHT_DIP)
        if not state["driving"] and foot.player is not None:
            lights.append(lamps.suit_lamp(camera, *walking.view_basis()))
    world.lights = lights
