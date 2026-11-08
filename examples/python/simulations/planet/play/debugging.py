"""
The walker's debug tools (walker.py --debug): the Slate's Debug app, and
the meteor aim.

Teleports (`teleport`): the player taken off where it is (out of the car
and out of space mode), put down above a point of a body and brought
back once the ground there is known (walking.maybe_spawn, as a loaded
save's player is), the car parked CAR_BESIDE east of it when the point's
height is known; to Base, to each outpost on the planet and the moon, to
the found points of interest in turn from the nearest (`next_poi`), and
into a circular orbit ORBIT_HEIGHT above the planet in space mode, in
the car, its translation assist off (`to_orbit`).

Events: a meteor shower on the body under the player, its impacts from
SHOWER_LEAD on (events.schedule); a storm over the player (STORM_RADIUS,
a wind.Storm for STORM_SECONDS), or one coming from upwind as the
storms' own come (events.schedule_storm), starting now; the day's hour
set (`set_hour`).

The player's: CREDITS more credits; the car's tank, the suit's reserve
and health full; PARTS more of each upgrade part; the pinned contract
completed; the car brought beside the player (`car_to_me`).

The meteor aim (`aiming`): while it is on and the player is on foot, the
gun is held back and drawn away (walking.place_viewmodel), the ground the
view meets within AIM_REACH (a ray in the physics, through the player)
is marked by a disc AIM_PIXELS of the view across (`update_aim`), and a
left click with the mouse captured calls a meteor there (`call_meteor`):
an impact METEOR_LEAD on, a meteorite of METEORITE_MASS with its crater
or a rock of ROCK_MASS (`state.debug.size`), as a shower of its one
impact on the body the player stands on.
"""

import math

from shoonyakasha import keys
from shoonyakasha.planet import wind as wind_field
from shoonyakasha.planet.gravity_gun import GunControls
from shoonyakasha.planet.vec import add, cross, length, local_frame, normalize, quat_from_basis, scale, sub

import clock
import events
import fieldslate as fs
import flight
import hovercar
import suit
import slate
from play.core import GOOD_TOAST, SHIP_HALF, engine, foot, planet, settings, state, world
from play import contracting, showers, spaceflight, vehicle

CAR_BESIDE = 12.0                # m east of a teleport's point the car is parked
ORBIT_HEIGHT = 40_000.0          # m above the planet's radius
SHOWER_LEAD = 20.0               # s
STORM_RADIUS = 6_000.0           # m
STORM_SECONDS = 30 * 60.0        # s of the world's clock
CREDITS = 10_000.0
PARTS = 5
AIM_REACH = 1_800.0              # m
AIM_PIXELS = 0.012               # of the view's distance: the marker's size
AIM_COLOUR = (4.0, 1.2, 0.3)
METEOR_LEAD = 10.0               # s from the click to the impact
METEORITE_MASS = 3_000.0         # kg
ROCK_MASS = 40.0                 # kg
METEOR_SPEED = 4_000.0           # m/s as it strikes
METEOR_ANGLE = 60.0              # degrees below the horizon
SIZES = ("Meteorite", "Rock")
SIZE_NAMES = ("Large", "Small")  # as the Debug app shows them
HOURS = (("Dawn", 6.0), ("Noon", 12.0), ("Dusk", 18.3), ("Midnight", 0.0))


def toast(text):
    state.ui.hud.toast(text, GOOD_TOAST)
    state.debug.note = text
    print("[walker] debug: %s" % text, flush=True)


def here():
    """(body, unit up) under the player's feet or the car, or None."""
    if state.vehicle.driving and state.vehicle.car is not None:
        body = spaceflight.car_body()
        return body, normalize(body.relative(vehicle.car_position()))
    if foot.player is None or foot.bubble is None:
        return None
    return foot.bubble.body, normalize(foot.bubble.body.relative(foot.bubble.to_world(foot.player.feet())))


def leave_vehicle():
    """Out of space mode and out of the car, which stays where it is."""
    if state.space.ship is not None:
        state.space.ship, state.space.warp, state.space.hold = None, 0, None
    if state.vehicle.driving:
        state.vehicle.driving = False
        state.vehicle.controls = hovercar.CarControls()
        vehicle.set_fov(settings["fov"])


def park_car(body, direction, height):
    """The car parked over the ground `height` m above `body`'s radius at the unit `direction`."""
    up = normalize(direction)
    position = scale(up, body.radius + height + hovercar.HOVER_MIN + SHIP_HALF[1])
    state.vehicle.car = hovercar.Car(position, local_frame(up)[1], params=state.economy.params["car"],
                                     fuel=state.vehicle.car.fuel if state.vehicle.car is not None else None)
    state.vehicle.body = None if body is planet else body
    vehicle.update_ship()


def teleport(body, direction, height, name, car=True):
    """The player put down on `body` at the unit `direction`, `height` m
    above its radius there (the ground's, when known), and brought back
    once the ground is known; the car parked beside it with `car`."""
    leave_vehicle()
    foot.drop_bubble()
    up = normalize(direction)
    state.view.position = add(body.centre(), scale(up, body.radius + height + 2.0))
    state.view.forward = local_frame(up)[1]
    state.session.loading = {"on": body.name, "pitch": 0.0, "toast": "Teleported to %s" % name}
    world.ground = None
    if car:
        park_car(body, events.along_ground(up, 90.0, CAR_BESIDE, body.radius), height)
    toast("teleporting to %s" % name)


def to_outpost(outpost):
    teleport(outpost.body, outpost.direction, outpost.height, outpost.name)


def next_poi():
    """The player taken to the next found point of interest, from the nearest on."""
    found = state.exploring.pois.found()
    if not found:
        toast("no points of interest found yet")
        return
    at = here()
    if at is not None and at[0] is planet:
        found = sorted(found, key=lambda p: -sum(a * b for a, b in zip(p.direction, at[1])))
    poi = found[state.debug.poi % len(found)]
    state.debug.poi += 1
    teleport(planet, poi.direction, poi.height, "a %s" % (poi.kind or "point of interest"), car=False)


def to_orbit():
    """The player in the car, in space mode, in a circular orbit ORBIT_HEIGHT above the planet over where it is."""
    at = here()
    up = at[1] if at is not None and at[0] is planet else state.site.home.direction
    if state.vehicle.car is None:
        park_car(planet, up, 0.0)
    if not state.vehicle.driving:
        vehicle.enter_car(anywhere=True)
    state.space.ship = None
    r = planet.radius + ORBIT_HEIGHT
    east, north = local_frame(up)
    speed = math.sqrt(world.physics.gm / r)
    car = state.vehicle.car
    state.space.ship = flight.Ship(add(planet.centre(), scale(up, r)), add(planet.velocity(), scale(east, speed)),
                                   quat_from_basis(north, up, scale(east, -1.0)),
                                   params=hovercar.space_params(state.economy.params["car"], SHIP_HALF), fuel=car.fuel,
                                   cargo=car.cargo)
    state.space.controls = flight.Controls(assist=False)
    state.space.time, state.space.warp = world.time, 0
    toast("in orbit %.0f km above the planet" % (ORBIT_HEIGHT / 1000.0))


def meteor_shower():
    at = here()
    if at is None:
        return
    body, up = at
    state.showers.body = None if body is planet else body
    showers.set_shower(events.schedule(world.time, state.showers.rng, up, body.radius, world.time + SHOWER_LEAD))
    toast("a meteor shower in %.0f s" % SHOWER_LEAD)


def storm_here():
    at = here()
    if at is None or at[0] is not planet:
        toast("storms are on the planet only")
        return
    up = at[1]
    axis = normalize(cross(up, local_frame(up)[1]))
    world.storm = wind_field.Storm(up, axis, 0.0, STORM_RADIUS, world.time - wind_field.FADE,
                                   world.time + STORM_SECONDS, planet.radius)
    toast("a storm overhead")


def storm_coming():
    at = here()
    if at is None or at[0] is not planet:
        toast("storms are on the planet only")
        return
    world.storm = events.schedule_storm(world.time, state.storms.rng, at[1], planet.radius,
                                        world.wind_at(planet, planet.relative(add(planet.centre(), at[1])), 10.0),
                                        world.time)
    toast("a storm coming from upwind")


def set_hour(name, hour):
    state.session.day_offset = clock.time_of(hour, world.time) - world.time
    toast(name.lower())


def more_credits():
    state.economy.market.credits += CREDITS
    toast("%s more credits" % fs.credits(CREDITS))


def refill():
    car = state.vehicle.car
    if car is not None:
        car.fuel = car.params.fuel_capacity
    state.suit.state = suit.SuitState(reserve=state.suit.params.reserve)
    toast("tank, suit and health full")


def more_parts():
    for part in state.economy.parts:
        state.economy.parts[part] += PARTS
    toast("%d more of each part" % PARTS)


def finish_contract():
    c = contracting.pinned_contract()
    if c is None:
        toast("no contract pinned")
        return
    contracting.complete_contract(c)
    toast("the pinned contract completed")


def car_to_me():
    if state.vehicle.driving or foot.player is None:
        toast("on foot only")
        return
    body = foot.bubble.body
    feet = foot.bubble.to_world(foot.player.feet())
    up = normalize(body.relative(feet))
    height = length(body.relative(feet)) - body.radius
    park_car(body, events.along_ground(up, 90.0, CAR_BESIDE * 0.5, body.radius), height)
    toast("the car beside you")


# ── The meteor aim ─────────────────────────────────────────────────────────

def aiming():
    return state.debug.aim and foot.player is not None and not state.vehicle.driving


def target():
    """The point (planet space) on the ground the view meets within AIM_REACH, or None."""
    player, bubble, view = foot.player, foot.bubble, foot.view
    if not aiming() or bubble is None or view is None:
        return None
    hit = foot.physics.cast_ray(player.eye(), view[2], AIM_REACH, skip_character=player.character)
    return bubble.to_world(hit[1]) if hit is not None else None


def call_meteor(point):
    body = foot.bubble.body
    up = normalize(body.relative(point))
    meteorite = state.debug.size == SIZES[0]
    mass = METEORITE_MASS if meteorite else ROCK_MASS
    now = world.time + METEOR_LEAD
    impact = events.Impact(now, up, mass, 0.6, METEOR_SPEED, METEOR_ANGLE, state.showers.rng.uniform(0.0, 360.0),
                           events.crater_of(mass) if meteorite else None)
    state.showers.body = None if body is planet else body
    showers.set_shower(events.Shower(now, up, [impact]))
    toast("a %s strikes in %.0f s" % (state.debug.size.lower(), METEOR_LEAD))


def hold_gun():
    """While aiming, the gun's controls this frame none (call after the player's controls are read)."""
    if aiming():
        foot.gun_controls = GunControls()


def update_aim(camera, basis):
    """Each frame: the aimed ground marked, and a meteor called there by a
    new left click (the Slate closed)."""
    glows = state.ui.aim_glows
    if glows is None:
        return
    glows.begin(basis)
    clicked = engine.input.is_mouse_button_down(keys.MOUSE_LEFT) and engine.input.is_mouse_captured()
    if aiming():
        point = target()
        if point is not None:
            d = length(sub(point, camera))
            glows.disc(sub(point, camera), max(0.3, AIM_PIXELS * d), AIM_COLOUR)
            if clicked and not state.debug.clicked and not state.ui.slate.visible:
                call_meteor(point)
    state.debug.clicked = clicked
    glows.end()


# ── The Slate's app ────────────────────────────────────────────────────────

class Debug:
    name, icon = "Debug", "debug"

    def label(self):
        return "Debug · teleports, events, the meteor aim"

    def title(self):
        return "Debug tools"

    def corner(self, surface, right, top):
        return

    def hints(self):
        return []

    def note(self):
        return state.debug.note

    def teleports(self):
        items = [{"heading": "Teleport"},
                 {"button": "Base", "do": lambda: to_outpost(state.site.home), "hint": "On Base's pad, the car beside."}]
        for o in state.site.outposts[1:]:
            items.append({"button": "%s (%s)" % (o.name, o.body.name), "do": lambda o=o: to_outpost(o),
                          "hint": "On %s's pad, the car beside." % o.name})
        items.append({"button": "Next point of interest", "do": next_poi,
                      "hint": "The found points of interest in turn, from the nearest."})
        items.append({"button": "Low orbit", "do": to_orbit,
                      "hint": "In the car, in space mode, %.0f km up in a circular orbit." % (ORBIT_HEIGHT / 1000.0)})
        return items

    def events(self):
        items = [{"heading": "Events"},
                 {"button": "Meteor shower", "do": meteor_shower,
                  "hint": "A shower on the body under you in %.0f s." % SHOWER_LEAD},
                 {"button": "Storm overhead", "do": storm_here, "hint": "A storm centred on you, now."},
                 {"button": "Storm coming", "do": storm_coming, "hint": "A storm starting upwind, now."}]
        for name, hour in HOURS:
            items.append({"button": name, "do": lambda name=name, hour=hour: set_hour(name, hour),
                          "hint": "The day's clock set to %02d:%02d." % (int(hour), int(hour * 60) % 60)})
        return items

    def player(self):
        return [{"heading": "Player"},
                {"button": "Credits", "do": more_credits, "hint": "%s more credits." % fs.credits(CREDITS)},
                {"button": "Refill", "do": refill, "hint": "The car's tank, the suit's reserve and health full."},
                {"button": "Parts", "do": more_parts, "hint": "%d more of each upgrade part." % PARTS},
                {"button": "Finish contract", "do": finish_contract, "hint": "The pinned contract completed."},
                {"button": "Car to me", "do": car_to_me, "hint": "The car parked beside you."},
                {"toggle": "Meteor aim", "get": lambda: state.debug.aim,
                 "set": lambda on: setattr(state.debug, "aim", on),
                 "hint": "The gun held back; a left click calls a meteor where you look."},
                {"choice": "Meteor size", "options": list(zip(SIZES, SIZE_NAMES)), "get": lambda: state.debug.size,
                 "set": lambda v: setattr(state.debug, "size", v),
                 "hint": "A meteorite of %.0f kg with its crater, or a rock of %.0f kg." % (METEORITE_MASS,
                                                                                           ROCK_MASS)}]

    def draw(self, surface, left, top, width, height):
        slate.draw_items(surface, ("columns", [self.teleports(), self.events(), self.player()]), left, top + 10.0,
                         width)
