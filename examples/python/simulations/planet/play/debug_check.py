"""
The walker's --debug-check (walker.py): the debug tools (play.debugging)
run through in place of the player's controls, a line printed for each,
exiting with 0 when all passed and 1 otherwise.

Once on foot at Base: a teleport to the first outpost on the planet and
then to the first on the moon, each passing when the player stands
within ARRIVE of its pad's centre within ARRIVE_SECONDS, the car parked
within CAR_NEAR of it; on the moon, the meteor aim on and the view
turned AIM_DOWN degrees down, a meteor called where the view meets the
ground, passing when its crater is dug within METEOR_NEAR of that point
within METEOR_SECONDS; a storm overhead, passing when the storm covers the
player; the day set to noon and then midnight, passing when the hour is
within a minute of each; a refill, passing when the tank and the suit
are full; low orbit, passing when the ship is within ORBIT_SLACK of its
height and its speed of the circular speed there; then back to Base,
passing as the teleports do; then the Slate opened on the Debug app
for APP_SECONDS (with --shot, a screenshot of it), passing when it is
shown.
"""

import math
import os

from shoonyakasha.planet.vec import dot, length, normalize, sub

import clock
from play.core import args, foot, planet, state, world
from play import debugging, survival, vehicle, walking

ARRIVE = 30.0                    # m along the ground
ARRIVE_SECONDS = 30.0
CAR_NEAR = 40.0                  # m
AIM_DOWN = 25.0                  # degrees
METEOR_NEAR = 30.0               # m
METEOR_SECONDS = 30.0
ORBIT_SLACK = 0.02               # of its height and its speed
STEPS = ("planet", "moon", "meteor", "storm", "hours", "refill", "orbit", "base", "app")
APP_SECONDS = 1.0                # s the Debug app is open before its screenshot


def report(check, ok, line):
    print("[walker] debug check: %s: %s" % (line, "passed" if ok else "FAILED"), flush=True)
    check["ok"] = check["ok"] and ok
    check["step"] += 1
    check["t0"] = check["time"]


def arrived(check, outpost, name):
    """True once the player stands on `outpost`'s pad (or the time is up), reported."""
    player = foot.player
    t = check["time"] - check["t0"]
    if player is not None and foot.bubble is not None and foot.bubble.body is outpost.body and player.on_ground:
        feet = foot.bubble.to_world(player.feet())
        along = outpost.body.radius * math.acos(max(-1.0, min(1.0, dot(normalize(outpost.body.relative(feet)),
                                                                        outpost.direction))))
        car = length(sub(vehicle.car_position(), feet)) if state.vehicle.car is not None else 1e9
        report(check, along <= ARRIVE and car <= CAR_NEAR,
               "teleported to %s: %.1f m from its pad's centre, the car %.1f m away" % (name, along, car))
        return True
    if t > ARRIVE_SECONDS:
        report(check, False, "teleported to %s: not on the ground in %.0f s" % (name, ARRIVE_SECONDS))
        return True
    return False


def run_debug_check(dt):
    check = state.session.check
    if check is None:
        if foot.player is None:
            return
        check = state.session.check = {"step": 0, "time": 0.0, "t0": 0.0, "ok": True, "started": False}
    check["time"] += dt
    t = check["time"] - check["t0"]
    if check["step"] >= len(STEPS):
        print("[walker] debug check %s" % ("passed" if check["ok"] else "FAILED"), flush=True)
        os._exit(0 if check["ok"] else 1)
    step = STEPS[check["step"]]
    first = check.get("began") != step
    check["began"] = step
    planet_outpost = next(o for o in state.site.outposts[1:] if o.body is planet)
    moon_outpost = next(o for o in state.site.outposts[1:] if o.body is not planet)
    if step == "planet":
        if first:
            debugging.to_outpost(planet_outpost)
        arrived(check, planet_outpost, planet_outpost.name)
    elif step == "moon":
        if first:
            debugging.to_outpost(moon_outpost)
        arrived(check, moon_outpost, moon_outpost.name)
    elif step == "meteor":
        if first:
            state.debug.aim = True
            foot.player.pitch = -math.radians(AIM_DOWN)
            check["aimed"] = None
            return
        if check["aimed"] is None:
            point = debugging.target()
            if point is None:
                if t > 5.0:
                    report(check, False, "the meteor aim found no ground")
                return
            debugging.call_meteor(point)
            check["aimed"] = normalize(foot.bubble.body.relative(point))     # the body moves: kept relative to it
            check["craters"] = list(world.craters)
            return
        new = [c for c in world.craters if c not in check["craters"]]
        if new:
            body = foot.bubble.body
            aimed = check["aimed"]
            off = min(body.radius * math.acos(max(-1.0, min(1.0, dot(aimed, normalize(c.direction))))) for c in new)
            report(check, off <= METEOR_NEAR, "a meteor called by the aim struck, its crater %.1f m from the aimed "
                                              "point" % off)
            state.debug.aim = False
        elif t > METEOR_SECONDS:
            report(check, False, "the aimed meteor did not strike in %.0f s" % METEOR_SECONDS)
            state.debug.aim = False
    elif step == "storm":
        if first:
            debugging.to_outpost(planet_outpost)
            check["stormed"] = False
            return
        if foot.player is None or foot.bubble.body is not planet:
            return
        if not check["stormed"]:
            debugging.storm_here()
            check["stormed"] = True
            return
        here = normalize(planet.relative(foot.bubble.to_world(foot.player.feet())))
        report(check, world.storm is not None and world.storm.at(here, world.time) > 0.0,
               "a storm overhead covers the player (%.2f)" % (world.storm.at(here, world.time)
                                                               if world.storm is not None else 0.0))
    elif step == "hours":
        hours = []
        for name, hour in (("Noon", 12.0), ("Midnight", 0.0)):
            debugging.set_hour(name, hour)
            hours.append(clock.hour(survival.day_time()))
        ok = abs(hours[0] - 12.0) < 1.0 / 60.0 and min(abs(hours[1]), abs(hours[1] - 24.0)) < 1.0 / 60.0
        report(check, ok, "the day set to noon (%.3f) and midnight (%.3f)" % tuple(hours))
    elif step == "refill":
        state.vehicle.car.fuel = 0.0
        state.suit.state.reserve = 0.0
        debugging.refill()
        ok = (state.vehicle.car.fuel == state.vehicle.car.params.fuel_capacity
              and state.suit.state.reserve == state.suit.params.reserve)
        report(check, ok, "a refill fills the tank and the suit")
    elif step == "orbit":
        if first:
            debugging.to_orbit()
            return
        ship = state.space.ship
        if ship is None:
            report(check, False, "low orbit: no ship in space mode")
            return
        r = length(planet.relative(ship.position))
        speed = length(sub(ship.velocity, planet.velocity()))
        circular = math.sqrt(world.physics.gm / r)
        height = r - planet.radius
        ok = (abs(height - debugging.ORBIT_HEIGHT) <= ORBIT_SLACK * debugging.ORBIT_HEIGHT
              and abs(speed - circular) <= ORBIT_SLACK * circular)
        if t >= 2.0:
            report(check, ok, "low orbit: %.1f km up at %.0f m/s (circular %.0f)" % (height / 1000.0, speed,
                                                                                    circular))
    elif step == "base":
        if first:
            debugging.to_outpost(state.site.home)
        arrived(check, state.site.home, "Base")
    elif step == "app":
        if first:
            state.ui.slate.open("Debug")
            return
        if t >= APP_SECONDS:
            if args.shot:
                walking.screenshot(args.shot)
            report(check, state.ui.slate.visible, "the Slate's Debug app open")
            state.ui.slate.close()
