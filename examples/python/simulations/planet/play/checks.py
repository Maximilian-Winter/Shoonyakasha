"""
The walker's --check (walker.py): `run_check`, called each frame in place
of the player's controls, plays the game through its stages, prints a
line for each and exits with 1 at the first that fails. --shot takes its
screenshots on the way.
"""

import math
import os
import random
import tempfile
import time
from collections import Counter

from shoonyakasha.planet.gravity_gun import GunParams
from shoonyakasha.planet.vec import add, cross, dot, length, local_frame, normalize, quat_from_basis, scale, sub
from shoonyakasha.planet import resources
from shoonyakasha.planet import wind as wind_field
from shoonyakasha.planet.rocks import SCAN_ROCKS

import acoustics
import clock
import contracts
import events
import flight
import hovercar
import pois
import suit
import outposts
import fieldslate as fs
import trade
import upgrades
from hud import bearing, distance_text
from play.core import (CHECK_ORE_EXTENT, CHECK_SAVE_BASE, CRATE_SIZE, HOVER_BACK, PLUME_PUFFS, POI_REACH, RECOVERY_FEE,
                       RIVER_CHECK_WIDTH, SHIP_CLEARANCE, SHIP_HALF, SPACE_HEIGHT, args, engine, foot, planet, state,
                       settings, world)
from play import (apps_map, contracting, economy, menus, saving, showers, site, sounds, spaceflight, survival,
                  vehicle, walking)

CHECK_CAPTURES = os.path.join(tempfile.gettempdir(), "walker_check_captures")
LOAD_SECONDS = 15.0              # s --check waits for the loaded player
CHECK_SECONDS = 10.0
SHIP_SHOT_WALK = 2.0             # s --shot walks away from the ship before its screenshot of it
DRIVE_SECONDS = 5.0              # s --check drives the car flat out, turning a little
CLIMB_SECONDS = 3.0              # s it climbs at full climb
LAND_SECONDS = 25.0              # s it waits at most for the car to come down and slow
LAKE_SECONDS = 6.0               # s --check hovers the car over a lake
LAKE_CHECK_DROP = 2.0            # m above the lake's water --check puts the car
LAKE_CLIMB_SECONDS = 8.0         # s --check --shot climbs the car over the lake
RIVER_CLIMB_SECONDS = 3.0        # s --check --shot climbs the car over the river
SHOT_SECONDS = 1.0               # s of walking before --shot's screenshot
CHECK_CRATE = 20.0               # kg
CHECK_AHEAD = 2.5                # m ahead of the feet the crate is dropped
GRAB_SECONDS = 1.5               # s the check holds the grab
CHARGE_SECONDS = 1.0             # s --check charges a punt of the held crate
PUNT_SHOT_SECONDS = 0.05         # s after the punt --shot's screenshot of it
MAP_SECONDS = 2.0               # s --check keeps the full map open
MAP_WAYPOINT = (170.0, 220.0)    # m east and north of the player --check sets the waypoint
MAP_CELLS = 200                 # cells --check wants explored by then
SELL_SECONDS = 6.0              # s --check waits for the ore it drops on the pad to sell
CHECK_ORE_GRADE = 0.6
PAUSE_SECONDS = 1.0             # s --check keeps the pause menu open
MIX_SECONDS = 0.5               # s --check keeps the slate open before measuring the mix
VOICE_HITS = 40                 # hits --check plays in one frame on a ring around the camera
VOICE_RING = 13.0               # m, the ring's radius
VOICE_LIFT = 8.0                # m above the camera the ring is
VOICE_SAME = 20                 # hits it plays at one point in the same frame
VOICE_BUDGET_MS = 10.0          # ms all of them may take
SCAN_ROCK_MOISTURE = -0.8       # the climate's moisture offset --check turns the ground around the player desert with
SCAN_ROCK_SECONDS = 10.0        # s it waits at most for a scanned rock's collider
NET_SETTLE = 3.0                # s --check lets the ore it drops into the net settle
NET_CHECK_AWAY = 60.0           # m along its nose --check moves the car off Base's pad for the net
SUIT_CHECK_TEMPERATURE = -60.0  # degrees C --check stands in for the air's while the suit drains
SUIT_CHECK_RESERVE = 0.25       # the reserve --check leaves the suit with
SUIT_CHECK_HEALTH = 0.3         # and the health
SUIT_SECONDS = 30.0             # s --check waits at most for the suit to give out
POI_MEASURE_SECONDS = 20.0      # s --check waits at most for the candidates near Base to be measured
USE_CHECK_SECONDS = 2.0         # s past a spot's hold --check waits for it to be used
POD_CHECK_SECONDS = 10.0        # s --check waits at most for the pod to sell
UPGRADE_CREDITS = 10_000.0      # credits --check is given to buy UPGRADE_BUYS
UPGRADE_BUYS = ("tank_1", "hold_1", "gun_force_1")
CHECK_NET_EXTENTS = ((0.35, 0.25, 0.35), (0.45, 0.3, 0.45))   # m: the ore rocks --check drops into the net
DRY_SECONDS = 1.0               # s --check drives the dry car at full throttle
FUEL_WAIT = 2.0                 # s past the call's delay --check waits for the fuel at most
NIGHT_CHECK_HOUR = 0.5          # the hour --check sets at its end
NIGHT_SECONDS = 1.0             # s --check waits at night
NEW_GAME_NEAR = 3.0             # m from site.spawn_point a new game's player must stand, at most
VACUUM_NEAR, VACUUM_FAR = 2.0, 20.0   # m from the camera --check strikes something on the moon
VACUUM_GROUND = 10.0            # m, a strike there heard through the ground
VACUUM_STAND = 2.0              # s it waits at most for the player to stand on the moon before striking
SOUNDS_HEARD = ("grab", "punt_charged", "hit", "footstep", "land", "slate_open", "toast", "wind", "car_hum",
                "gun_hum", "breath", "meteor_boom_large", "contact", "rain", "pads_closed", "music_explore",
                "music_orbit", "music_menu")   # sounds.SOUNDS entries the check must have played
CONTRACT_CHECK_SECONDS = 30.0   # s --check waits at most for each contract to end
CONTRACT_CHECK_DROP = 12.0      # m/s down --check throws the fragile crate at
SHOWER_CHECK_LEAD = 12.0        # s from --check's shower being planned to its large impact
SHOWER_CHECK_CENTRE = 400.0     # m east of the feet its large impact strikes
SHOWER_CHECK_OFF_PAD = 4.0      # m beyond Base's pad's reach its small one strikes
SHOWER_CHECK_NEAR = 5.0         # m from its rock --check walks the player to
SHOWER_CHECK_MASS = 3_000.0     # kg, its meteorite
SHOWER_CHECK_ROCK = 40.0        # kg, its rock
SHOWER_LAND_SECONDS = 6.0       # s after its last impact --check lets its meteorite and rock land
STORM_CHECK_RADIUS = 10_000.0   # m: --check's storm, centred on the feet
STORM_CHECK_SECONDS = 6.0       # s it lasts
STORM_CHECK_LIGHT, STORM_CHECK_HEAVY = 2.0, 500.0   # kg: the crates beside the player
STORM_CHECK_APART = 2.0         # m from the feet each side
STORM_CHECK_MOVED = 1.0         # m the light crate must move, at least
SPACE_CHECK_SECONDS = 40.0      # s --check waits at most for the car to climb to SPACE_HEIGHT or come down
SPACE_RISE_SECONDS = 5.0        # s it climbs on the assist in space mode
SPACE_RISE_MIN = 100.0          # m it must rise in that time, at least
SPACE_CHECK_DROP = 600.0        # m above a moon outpost's pad it puts the car in space mode
SPACE_LAND_SECONDS = 60.0       # s it waits at most for the car to land there
MOON_SHOWER_CHECK_AWAY = 300.0  # m east of the feet on the moon --check's meteorite strikes
MOON_SHOWER_CHECK_LEAD = 10.0   # s from its planning to its impact
REFINE_CHECK_ICE = 60.0         # kg of ice --check puts in the car's hold at the refinery
REFINE_CHECK_ROOM = 10.0        # kg of room it leaves in the tank


def run_check(dt):
    """--check, once on foot (with --shot, first walking away from the ship
    for SHIP_SHOT_WALK, turned to it and a second later a screenshot of it,
    FILE with _ship added, then turned north): walk north for
    CHECK_SECONDS, half way taking
    a screenshot into CHECK_CAPTURES as P does; then a crate of
    CHECK_CRATE kg dropped CHECK_AHEAD ahead, and once it lies still the
    view turned to it and the grab held for GRAB_SECONDS; then a punt of
    it charged for CHARGE_SECONDS and let go; PUNT_SHOT_SECONDS later the
    full map open for MAP_SECONDS and a waypoint set on it by a click, and
    the map shown half a second more; then the map closed, the
    temperature here taken, and with SUIT_CHECK_TEMPERATURE standing in for
    it and no outpost counted, the suit left SUIT_CHECK_RESERVE and the
    health SUIT_CHECK_HEALTH, waiting up to SUIT_SECONDS for the suit to
    give out: passing when the temperature had been here_temperature's,
    the card had its SUIT and TEMP rows, the suit ran low with the walk
    slowed and a tint, and the player came to at Base charged
    RECOVERY_FEE with the suit full; then an ore rock
    (CHECK_ORE_EXTENT, CHECK_ORE_GRADE) dropped 1 m above the Base pad,
    waiting up to SELL_SECONDS for it to sell and a second more; then the
    car moved NET_CHECK_AWAY along its nose off Base's pad (where ore at
    rest sells by itself), an ore rock of each CHECK_NET_EXTENTS dropped
    1 m over its net and NET_SETTLE later the car got into, the rocks gone
    from the props into its cargo with their masses, the car put back on
    the pad and its cargo sold there for trade.sale's credits, and out of
    the car; then, given UPGRADE_CREDITS, Main engine tuning tried first
    (refused for want of the Auxiliary tank) and UPGRADE_BUYS bought, the
    credits falling by their costs and the car's tank and hold and the
    gun's force 1.5 times their own; then the points of interest
    (run_poi_check): the candidates within POI_REACH measured (at most
    POI_MEASURE_SECONDS); a beacon, a wreck, a rig and an arch put by the
    player (check_poi) and used with the use key held (the beacon's survey
    data and map cells queued, a part from the wreck after its hold and
    from the rig with its ore rocks, the arch's survey data); a geode
    field's six rocks of quartz at its grade made; a pod made on Base's pad
    and sold once settled; the survey data sold for trade.survey_sale's
    credits; and Capacitor bank bought if a coil was found (else refused)
    and the Thermal liner lowering the suit's cold limit; then the
    contracts (run_contract_check): Base's offers the same when asked
    twice; a delivery taking an ore rock dropped on the pad in place of
    selling it; a courier's crate put in the car's hold on the pad and
    delivered there; with the car moved NET_CHECK_AWAY off, a courier's
    crate beside the player thrown down at CONTRACT_CHECK_DROP, breaking
    and failing; a recovery's pod made on the pad and delivered there; a
    survey around the feet completed; a contract past its deadline
    expired; a pinned one abandoned; and a survey 2 km off kept active and
    pinned, on the card and the compass, the car put back; each paying its
    reward or none and moving Base's reputation, +1 at the end; then a
    meteor shower (run_shower_check): a large impact SHOWER_CHECK_CENTRE
    east of the feet SHOWER_CHECK_LEAD on, with a meteorite of
    SHOWER_CHECK_MASS, and a small one a second later SHOWER_CHECK_OFF_PAD
    beyond Base's pad's reach (outposts.PAD_REACH) on the line from its
    centre through the feet, with a rock of SHOWER_CHECK_ROCK, both at
    4 km/s 60 degrees down travelling north; SHOWER_LAND_SECONDS after the
    last, passing when one crater was dug of the large one's size where it
    struck, its meteorite of iridium
    and its mass lies hot and at rest within 1 m of the crater's floor (the
    ground measured there before, less its depth), the rock of iridium and
    its mass is hot, the shower was announced and the meteorite told of, a
    meteor's head was drawn, and there was a flash, a shake and the large
    impact's plume; then, walked to within SHOWER_CHECK_NEAR of the rock,
    the view on it and the grab held, the gun refuses it as too hot, and
    with it cooled grabs it; then a storm (run_storm_check): crates of
    STORM_CHECK_LIGHT and STORM_CHECK_HEAVY kg put down STORM_CHECK_APART
    either side of the feet and let settle, then a storm of
    STORM_CHECK_RADIUS centred on the feet at its full strength for
    STORM_CHECK_SECONDS, an ore rock dropped 1 m over Base's pad and the
    tank filled there, passing when the wind 10 m up became 1.6 to 3.1
    times what it was, the light crate moved more than STORM_CHECK_MOVED
    and the heavy one less than 5 cm and asleep, Base was announced as
    closed, the ore stayed unsold and the tank was refused, the pinned
    contract's deadline was held back by the time passed, and rain was
    drawn; then, the storm gone, Base announced open and the ore sold
    within SELL_SECONDS; then, with sound, the voices (run_voice_check);
    then a scanned rock's collider, lifted loose (run_scan_rock_check);
    then the menu shown for PAUSE_SECONDS; then the game saved to CHECK_SAVE_BASE,
    the credits, the ore sold and the explored map changed, and the save
    loaded, waiting up to LOAD_SECONDS for the player to come back and a
    second more. Passes when the player covered 80 % of walking speed's
    distance and stands on the ground, the screenshot was written and its
    toast shown, the gun holds the crate, the punt charged and threw it,
    at least MAP_CELLS cells are explored, the waypoint is set, the ore sold for credits, the car's cargo
    loaded and sold, the world's clock stood
    still while paused, and the load brought back the credits, the ore
    sold, the cells, the props, the contracts, the pin, the reputations,
    the craters, the hot props and the player within 2 m of where it was saved; then into the car, driven flat out turning a little for
    DRIVE_SECONDS, climbing for CLIMB_SECONDS and flying on 2 s more,
    saved and loaded there, braking and coming down until it can be left
    (at most LAND_SECONDS), and out of it, a second later passing when the
    load put the car back where it was and still driven, it went faster
    than 30 m/s and higher than 80 % of the climb and the player stands
    beside it; then into the car again, put LAKE_CHECK_DROP above the
    water of the lake nearest it at its centre and left for LAKE_SECONDS,
    passing when it hovers HOVER_MIN (within 25 cm) above the water and
    the ground under the camera lies at least 50 cm under the water; then
    put LAKE_CHECK_DROP above the water of the point nearest the site of a
    river at least RIVER_CHECK_WIDTH wide there, facing down it, and left
    for LAKE_SECONDS, passing when it hovers HOVER_MIN (within 25 cm)
    above the water there; then, the car down on its cushion and slower
    than 0.3 m/s (at most LAND_SECONDS), its tank emptied
    and DRY_SECONDS at full throttle, passing when it moved less than 1 m,
    fuel called (call_fuel) and, by FUEL_WAIT after it is due, delivered:
    the tank holding what the credits less the fee buy at its price (at
    most full) and the credits less that; then space mode
    (run_space_check): the car climbed past SPACE_HEIGHT and into space
    mode, risen on the assist for SPACE_RISE_SECONDS (at least
    SPACE_RISE_MIN, burning fuel), the space readout showing the transfer
    planner's lines, saved and loaded there (back within
    1 cm), brought down within HOVER_BACK and back to hovering over the
    planet; then put in space mode SPACE_CHECK_DROP above a moon outpost's
    pad at the moon's velocity, coming down on the assist, passing when it
    lands within SPACE_LAND_SECONDS, the hover car again over the moon,
    within 50 m of the pad and not hard, and the player got out stands on
    the moon at the moon's temperature; with sound, the world bus is
    silent there and of a strike VACUUM_NEAR and one VACUUM_FAR from the
    camera only the near one is heard, through the suit, the far one when
    the gun holds what struck, and (once the player stands, waiting up to
    VACUUM_STAND) one VACUUM_GROUND off through the
    ground standing on it; then a meteorite of
    SHOWER_CHECK_MASS strikes the moon MOON_SHOWER_CHECK_AWAY east of the
    feet MOON_SHOWER_CHECK_LEAD on, passing when its crater is dug on the
    moon and the meteorite lies within 1 m of its floor; then the car on
    the refinery's pad with REFINE_CHECK_ICE kg of ice in its hold and
    REFINE_CHECK_ROOM kg of room in its tank, refining (refine_ice),
    passing when the tank is full and the ice less outposts.REFINE_ICE
    times that room is left; then the car put back at Base;
    then the day's hour set to NIGHT_CHECK_HOUR and
    NIGHT_SECONDS later passing with the night's exposure
    (clock.NIGHT_SCALE) and at least the car's two headlights drawn; then
    Quit to main menu, passing half a second on with the start's screen up,
    the HUD hidden and the world's clock still, and New game, passing
    SHOT_SECONDS after the player comes back with the credits at
    trade.START_CREDITS, the first objective, no contracts or upgrades, the
    six crates, no craters or storm, less than half the explored ground and
    the player within NEW_GAME_NEAR of site.spawn_point; and each sound of
    SOUNDS_HEARD played during the check and the world bus at Base playing at the
    gain its air gives, above 0.5 (with sound). With sound, the pause has
    the world's and the suit's sounds paused, the slate opened after it for
    MIX_SECONDS the mix of sounds.MIX["slate"], and the start's screen
    the world paused and its music. The settings are
    the defaults and are not written. With --shot,
    screenshots while walking (SHOT_SECONDS in), while holding (FILE with
    _hold added), while charging (with _charge added), just after the punt
    (with _punt added), of the map (with _map added), with the suit low (with _suit added), after the sale (with
    _sell added), of the slate's Workshop after buying (with _upgrades added), of the shower's large meteor
    (with _meteor added), of its impact (with _impact added) and its plume 4 s on (with _plume added), in the
    storm (with _storm added), of the System screen (with _pause added), of the slate's Vehicle (with _car
    added), Market (with _outpost added), Contracts (with _contracts added), Overview (with _overview added)
    and Log (with _log added), of the System screen's Settings (with _settings added) and Controls (with
    _controls added),
    after the load (with _load added), a second
    before the end of the drive (with _drive added), after the climb (with
    _fly added), out of the car (with _out added), over the lake (with
    _lake added), after climbing for LAKE_CLIMB_SECONDS and 2 s more,
    above it (with _lake_above added), over the river (with _river added)
    and, after climbing for RIVER_CLIMB_SECONDS and 2 s more, above it
    (with _river_above added), in space mode (with _space added), on the moon (with _moon added), at
    night (with _night added), of the start's screen after Quit to main menu (with _menu added) and of the
    new game (with _new_game added)."""
    player = foot.player
    check = state.session.check
    if player is None and (check is None or check["stage"] not in ("reload", "drive", "fly", "land", "out", "lake", "lake_above", "to_river", "river", "river_above", "fuel", "space", "night", "net_out",
                                                                  "menu", "new game")):
        return
    if check is None:
        check = state.session.check = {"stage": "ship" if args.shot else "walk", "time": 0.0,
                                  "start": foot.bubble.to_world(player.feet())}
    check["time"] += dt
    stage, t = check["stage"], check["time"]
    if stage == "ship":
        to_ship = sub(foot.bubble.to_bubble(state.site.ship[0]), player.feet())
        if t < SHIP_SHOT_WALK:
            player.heading = player.level(scale(to_ship, -1.0), player.up())
            foot.controls.forward = 1.0
        else:
            foot.controls.forward = 0.0
            player.heading = player.level(to_ship, player.up())
            player.pitch = -0.08
        if t >= SHIP_SHOT_WALK + 1.0:
            base, ext = os.path.splitext(args.shot)
            walking.screenshot(base + "_ship" + ext)
            player.heading = state.site.ship[3]
            player.pitch = 0.0
            check["stage"], check["time"] = "walk", 0.0
            check["start"] = foot.bubble.to_world(player.feet())
    elif stage == "walk":
        foot.controls.forward = 1.0
        if args.shot and t >= SHOT_SECONDS and not state.session.shot:
            state.session.shot = True
            walking.screenshot(args.shot)
        if t >= 0.5 * CHECK_SECONDS and "capture" not in check:
            check["capture"] = walking.take_capture(CHECK_CAPTURES)
        if any(toast[0].startswith("Screenshot saved") for toast in state.ui.hud.toasts):
            check["capture_toast"] = True
        if t >= CHECK_SECONDS:
            foot.controls.forward = 0.0
            moved = length(sub(foot.bubble.to_world(player.feet()), check["start"]))
            check["walk_ok"] = moved > 0.8 * CHECK_SECONDS * 4.0 and player.on_ground
            print("[walker] walked %.1f m in %.0f s, on the ground: %s: %s"
                  % (moved, t, player.on_ground, "passed" if check["walk_ok"] else "FAILED"), flush=True)
            capture = check["capture"]
            check["capture_ok"] = (capture is not None and os.path.exists(capture) and state.view.capture_toast is None
                                   and check.get("capture_toast", False))
            print("[walker] a screenshot taken as P takes one, and its toast shown: %s"
                  % ("passed" if check["capture_ok"] else "FAILED"), flush=True)
            right, up, forward = player.basis()
            level = normalize(sub(forward, scale(up, dot(forward, up))))
            spot = add(player.feet(), add(scale(level, CHECK_AHEAD), scale(up, 1.0)))
            check["crate"] = foot.props.spawn_crate(foot.bubble, spot, quat_from_basis(right, up, scale(level, -1.0)),
                                                    mass=CHECK_CRATE)
            check["stage"], check["time"] = "settle", 0.0
    elif stage == "settle":
        crate = check["crate"]
        if (crate.body is not None and not foot.physics.is_active(crate.body)) or t > 5.0:
            check["stage"], check["time"] = "grab", 0.0
    elif stage == "grab":
        crate = check["crate"]
        to = sub(foot.physics.get_transform(crate.body)[0], player.eye())
        up = player.up()
        player.pitch = math.asin(max(-1.0, min(1.0, dot(normalize(to), up))))
        foot.gun_controls.grab = True
        if t >= GRAB_SECONDS:
            held = foot.gun.held == crate.body
            if args.shot:
                base, ext = os.path.splitext(args.shot)
                walking.screenshot(base + "_hold" + ext)
            print("[walker] grabbed the %.0f kg crate %.1f m ahead: %s" % (CHECK_CRATE, CHECK_AHEAD,
                  "passed" if held else "FAILED"), flush=True)
            check["held_ok"] = held
            foot.gun_controls.grab = False
            foot.gun_controls.punt = foot.gun_controls.punt_held = True
            check["stage"], check["time"] = "charge", 0.0
    elif stage == "charge":
        if t >= CHARGE_SECONDS:
            check["charge"] = foot.gun.charge
            if args.shot:
                base, ext = os.path.splitext(args.shot)
                walking.screenshot(base + "_charge" + ext)
            foot.gun_controls.punt_held = False
            check["stage"], check["time"] = "punt", 0.0
    elif stage == "punt":
        if t >= PUNT_SHOT_SECONDS:
            if args.shot:
                base, ext = os.path.splitext(args.shot)
                walking.screenshot(base + "_punt" + ext)
            last = foot.gun.last_punt
            punt_ok = (check["charge"] > 0.0 and foot.gun.held is None and last is not None
                       and last[0] == check["crate"].body)
            print("[walker] charged to %.0f %% and threw the crate at %.0f m/s: %s"
                  % (100.0 * check["charge"], last[1] if last is not None and last[1] else 0.0,
                     "passed" if punt_ok else "FAILED"), flush=True)
            check["punt_ok"] = punt_ok
            state.ui.slate.open("Map")
            check["stage"], check["time"] = "map", 0.0
    elif stage == "map":
        if t >= MAP_SECONDS:
            apps_map.waypoint_offset(MAP_WAYPOINT[0], MAP_WAYPOINT[1])
            body = foot.bubble.body
            cells = state.ui.map.grids[body.name].measured
            waypoint = state.progress.waypoint
            map_ok = cells >= MAP_CELLS and waypoint is not None and waypoint[0] is body
            check["stage"], check["time"] = "map shown", 0.0
            check["map_ok"] = map_ok
            print("[walker] %d cells explored, a waypoint set on the map: %s"
                  % (cells, "passed" if map_ok else "FAILED"), flush=True)
    elif stage == "map shown":
        if t >= 0.5:
            if args.shot:
                base, ext = os.path.splitext(args.shot)
                walking.screenshot(base + "_map" + ext)
            state.ui.slate.close(at_once=True)
            check["stage"], check["time"] = "suit", 0.0
    elif stage == "suit":
        if "suit" not in check:
            here = survival.here_temperature()
            check["suit"] = {"temperature": state.suit.temperature, "here": here, "rows": [r[0] for r in survival.suit_rows()],
                             "credits": state.economy.market.credits, "woken": state.suit.woken,
                             "functions": (survival.here_temperature, economy.at_outpost)}
            survival.here_temperature = lambda: SUIT_CHECK_TEMPERATURE
            economy.at_outpost = lambda: None
            state.suit.state.reserve, state.suit.state.health = SUIT_CHECK_RESERVE, SUIT_CHECK_HEALTH
            return
        s = check["suit"]
        if state.suit.woken == s["woken"]:
            if t > SUIT_SECONDS:
                print("[walker] the suit did not give out in %.0f s at %.0f C: FAILED" % (t, SUIT_CHECK_TEMPERATURE),
                      flush=True)
                os._exit(1)
            if "slow" not in s and state.suit.state.reserve < suit.SLOW_BELOW and state.suit.state.reserve > 0.0:
                s["slow"] = survival.suit_effects()
            if args.shot and "shot" not in s and 0.0 < state.suit.state.reserve < 0.1:
                s["shot"] = True
                base, ext = os.path.splitext(args.shot)
                walking.screenshot(base + "_suit" + ext)
            return
        survival.here_temperature, economy.at_outpost = s["functions"]
        if foot.player is None:
            if t > SUIT_SECONDS + LOAD_SECONDS:
                print("[walker] the player did not come to at an outpost: FAILED", flush=True)
                os._exit(1)
            return
        outpost = economy.at_outpost()
        fee = s["credits"] - state.economy.market.credits
        tint, factor = s.get("slow", (None, 1.0))
        suit_ok = (s["temperature"] is not None and abs(s["temperature"] - s["here"]) < 1e-6
                   and s["rows"] == ["Suit", "Temp"] and outpost is state.site.home
                   and abs(fee - min(RECOVERY_FEE, s["credits"])) < 1e-6
                   and state.suit.state.reserve == state.suit.params.reserve and state.suit.state.health == 1.0
                   and factor == suit.SLOW_FACTOR and tint is not None and tint[1] > 0.0)
        print("[walker] %.1f C here; at %.0f C the suit ran low (the walk x%.1f, a tint of %.2f), gave out, and the "
              "player came to at %s for %.0f cr, the suit full: %s"
              % (s["here"] if s["here"] is not None else float("nan"), SUIT_CHECK_TEMPERATURE, factor,
                 tint[1] if tint else 0.0, outpost.name if outpost is not None else "no outpost", fee,
                 "passed" if suit_ok else "FAILED"), flush=True)
        check["suit_ok"] = suit_ok
        _, up, _, _ = world.site_frame()
        check["credits"] = state.economy.market.credits
        check["ore"] = walking.ore_rock(add(world.site_ground(), scale(up, 1.0)), CHECK_ORE_EXTENT, CHECK_ORE_GRADE)
        check["stage"], check["time"] = "sell", 0.0
    elif stage == "sell":
        sold = not any(prop is check["ore"] for prop in foot.props.rocks)
        if sold and "sold_at" not in check:
            check["sold_at"] = t
        if t >= check.get("sold_at", SELL_SECONDS) + 1.0 or t >= SELL_SECONDS + 1.0:   # the toast drawn first
            gained = state.economy.market.credits - check["credits"]
            sell_ok = sold and gained > 0.0 and state.economy.sold_kg > 0.0
            print("[walker] ore dropped on the Base pad sold: %.0f kg for %.0f cr: %s"
                  % (state.economy.sold_kg, gained, "passed" if sell_ok else "FAILED"), flush=True)
            if args.shot:
                base, ext = os.path.splitext(args.shot)
                walking.screenshot(base + "_sell" + ext)
            check["sell_ok"] = sell_ok
            check["stage"], check["time"] = "net", 0.0
    elif stage == "net":
        if "net_rocks" not in check:
            car = state.vehicle.car
            check["parked_at"] = car.position          # on Base's pad, where ore resting sells by itself
            car.position = add(car.position, scale(car.heading, NET_CHECK_AWAY))
            vehicle.update_ship()
            centre, right, up, back = vehicle.net_frame()
            check["net_rocks"] = [walking.ore_rock(add(add(centre, scale(right, side)), scale(up, 1.0)), extent,
                                           CHECK_ORE_GRADE)
                                  for side, extent in zip((-0.8, 0.8), CHECK_NET_EXTENTS)]
            return
        if t >= NET_SETTLE:
            rocks = check["net_rocks"]
            masses = sorted(round(r.mass, 3) for r in rocks)
            credits = state.economy.market.credits
            vehicle.enter_car(anywhere=True)
            car = state.vehicle.car
            car.position = check["parked_at"]
            vehicle.update_ship()
            outpost = economy.car_outpost()
            loaded = (not any(r in foot.props.rocks for r in rocks)
                      and sorted(round(c["mass"], 3) for c in car.cargo) == masses)
            expected = trade.sale(outpost, car.cargo, world.time)[0] if outpost is not None and car.cargo else 0.0
            economy.sell_car_cargo()
            gained = state.economy.market.credits - credits
            net_ok = loaded and outpost is not None and not car.cargo and expected > 0.0 and abs(gained - expected) < 1e-6
            print("[walker] %d ore rocks in the car's net loaded on getting in (%s kg) and sold at %s for %.0f cr "
                  "(expected %.0f): %s" % (len(rocks), ", ".join("%.0f" % m for m in masses),
                                           outpost.name if outpost is not None else "no pad", gained, expected,
                                           "passed" if net_ok else "FAILED"), flush=True)
            check["sell_ok"] = check["sell_ok"] and net_ok
            check["stage"], check["time"] = "net_out", 0.0
    elif stage == "net_out":
        state.vehicle.controls = hovercar.CarControls()
        if foot.player is None and not vehicle.leave_car():
            if t > LAND_SECONDS:
                print("[walker] could not get out of the car back on Base's pad: FAILED", flush=True)
                os._exit(1)
            return
        check["stage"], check["time"] = "upgrades", 0.0
    elif stage == "upgrades":
        if "bought" not in check:
            state.economy.market.credits += UPGRADE_CREDITS
            credits = state.economy.market.credits
            engine = upgrades.BY_KEY["engine_1"]
            economy.buy_upgrade(engine)
            refused = "engine_1" not in state.economy.owned and state.economy.market.credits == credits
            for key in UPGRADE_BUYS:
                economy.buy_upgrade(upgrades.BY_KEY[key])
            spent = credits - state.economy.market.credits
            base_car, base_gun = hovercar.CarParams(), GunParams()
            car, gun = state.vehicle.car.params, foot.gun.p
            check["bought"] = (refused and state.economy.owned == set(UPGRADE_BUYS)
                               and abs(spent - sum(upgrades.BY_KEY[k].cost for k in UPGRADE_BUYS)) < 1e-6
                               and abs(car.fuel_capacity - 1.5 * base_car.fuel_capacity) < 1e-9
                               and abs(car.cargo_capacity - 1.5 * base_car.cargo_capacity) < 1e-9
                               and abs(gun.hold_force - 1.5 * base_gun.hold_force) < 1e-9
                               and foot.gun_params is state.economy.params["gun"])
            print("[walker] at %s: Main engine tuning refused first (%s); bought %s for %.0f cr: the car's tank "
                  "%.0f kg and hold %.2f t, the gun %.1f kN: %s"
                  % (economy.at_outpost().name if economy.at_outpost() is not None else "no outpost",
                     "yes" if refused else "NO", ", ".join(upgrades.BY_KEY[k].name for k in UPGRADE_BUYS), spent,
                     car.fuel_capacity, car.cargo_capacity / 1000.0, gun.hold_force / 1000.0,
                     "passed" if check["bought"] else "FAILED"), flush=True)
            check["sell_ok"] = check["sell_ok"] and check["bought"]
            if args.shot:
                state.ui.slate.open("Workshop")
            return
        if t >= 0.5 or not args.shot:
            if args.shot:
                base, ext = os.path.splitext(args.shot)
                walking.screenshot(base + "_upgrades" + ext)
                state.ui.slate.close(at_once=True)
            check["stage"], check["time"] = "pois", 0.0
    elif stage == "pois":
        if run_poi_check(check, t):
            check["stage"], check["time"] = "contracts", 0.0
    elif stage == "contracts":
        if run_contract_check(check, t):
            check["stage"], check["time"] = "showers", 0.0
    elif stage == "showers":
        if run_shower_check(check, t):
            check["stage"], check["time"] = "storms", 0.0
    elif stage == "storms":
        if run_storm_check(check, t):
            check["stage"], check["time"] = "voices", 0.0
    elif stage == "voices":
        if run_voice_check(check, t):
            check["stage"], check["time"] = "scan rocks", 0.0
    elif stage == "scan rocks":
        if run_scan_rock_check(check, t):
            menus.set_paused(True)
            check["stage"], check["time"] = "pause", 0.0
    elif stage == "pause":
        if "clock" not in check:
            check["clock"] = world.time      # this frame is the first that stands still
        if t >= PAUSE_SECONDS:
            audio = state.sound.audio
            held = audio is None or (audio.bus_paused("world") and audio.bus_paused("suit"))
            pause_ok = world.time == check["clock"] and not walking.hud_shown() and held
            print("[walker] paused for %.0f s, the world's clock still, the world's and the suit's sounds paused "
                  "(%s): %s" % (PAUSE_SECONDS, held, "passed" if pause_ok else "FAILED"), flush=True)
            if args.shot:
                base, ext = os.path.splitext(args.shot)
                walking.screenshot(base + "_pause" + ext)
            menus.set_paused(False)
            state.ui.slate.open("Vehicle")
            check["pause_ok"] = pause_ok
            check["stage"], check["time"] = "slate mix", 0.0
    elif stage == "slate mix":
        if t >= MIX_SECONDS:
            audio = state.sound.audio
            mix_ok, line = True, "no sound"
            if audio is not None:
                world_level, suit_level, _, cap, _ = sounds.MIX["slate"]
                gain = acoustics.air_audio(sounds.air_share(state.view.position))[0] * world_level
                mix_ok = (not audio.bus_paused("world") and abs(audio.bus_volume("world") - gain) < 0.02
                          and audio.world_lowpass <= cap + 1.0 and abs(audio.bus_volume("suit") - suit_level) < 0.01)
                line = "the world at %.2f (%.2f wanted), its low-pass %.0f Hz, the suit at %.2f" % (
                    audio.bus_volume("world"), gain, audio.world_lowpass, audio.bus_volume("suit"))
            print("[walker] the slate open: %s: %s" % (line, "passed" if mix_ok else "FAILED"), flush=True)
            check["pause_ok"] = check["pause_ok"] and mix_ok
            if not args.shot:
                state.ui.slate.close(at_once=True)
                menus.set_paused(True)
            check["stage"], check["time"] = "car_tab", 0.0
    elif stage == "car_tab":
        if t >= 0.5 or not args.shot:
            if args.shot:
                base, ext = os.path.splitext(args.shot)
                walking.screenshot(base + "_car" + ext)
                state.ui.slate.open("Market")
            check["stage"], check["time"] = "outpost_tab", 0.0
    elif stage == "outpost_tab":
        if t >= 0.5 or not args.shot:
            if args.shot:
                base, ext = os.path.splitext(args.shot)
                walking.screenshot(base + "_outpost" + ext)
                state.ui.slate.open("Contracts")
            check["stage"], check["time"] = "contracts_tab", 0.0
    elif stage == "contracts_tab":
        if t >= 0.5 or not args.shot:
            if args.shot:
                base, ext = os.path.splitext(args.shot)
                walking.screenshot(base + "_contracts" + ext)
                state.ui.slate.open("Overview")
            check["stage"], check["time"] = "overview_tab", 0.0
    elif stage == "overview_tab":
        if t >= 0.5 or not args.shot:
            if args.shot:
                base, ext = os.path.splitext(args.shot)
                walking.screenshot(base + "_overview" + ext)
                state.ui.slate.open("Log")
            check["stage"], check["time"] = "log_tab", 0.0
    elif stage == "log_tab":
        if t >= 0.5 or not args.shot:
            if args.shot:
                base, ext = os.path.splitext(args.shot)
                walking.screenshot(base + "_log" + ext)
                menus.set_paused(True, "Settings")
            check["stage"], check["time"] = "settings", 0.0
    elif stage == "settings":
        if t >= 0.5 or not args.shot:
            if args.shot:
                base, ext = os.path.splitext(args.shot)
                walking.screenshot(base + "_settings" + ext)
                state.ui.system.show(True, "Controls")
                check["stage"], check["time"] = "controls", 0.0
                return
            check["stage"] = "controls"
    elif stage == "controls":
        if t >= 0.5 or not args.shot:
            if args.shot:
                base, ext = os.path.splitext(args.shot)
                walking.screenshot(base + "_controls" + ext)
            menus.set_paused(False)
            saved = saving.save_game(CHECK_SAVE_BASE)
            check["saved"] = {"credits": state.economy.market.credits, "cells": walking.game_numbers()["cells"],
                              "props": len(foot.props.all), "sold_kg": state.economy.sold_kg,
                              "feet": foot.bubble.to_world(player.feet()), "time": saved["world"]["time"],
                              "contracts": [(c.id, c.kind, c.state) for c in state.contracts.active],
                              "pinned": state.contracts.pinned, "reputation": dict(state.contracts.reputation),
                              "craters": list(world.craters), "hot": sorted(state.showers.hot.values())}
            state.economy.market.credits += 999.0
            state.economy.sold_kg += 99.0
            state.ui.map.grids = {}
            state.contracts.active, state.contracts.pinned, state.contracts.reputation = [], None, {}
            state.showers.craters, state.showers.hot = [], {}
            world.set_craters([])
            check["loaded"] = saving.load_game(CHECK_SAVE_BASE)
            check["stage"], check["time"] = "reload", 0.0
    elif stage == "reload":
        if player is None:
            if t >= LOAD_SECONDS:
                print("[walker] the loaded player did not come back in %.0f s: FAILED" % LOAD_SECONDS, flush=True)
                os._exit(1)
            return
        check.setdefault("back", t)
        if t - check["back"] >= 1.0:
            saved = check["saved"]
            moved = length(sub(foot.bubble.to_world(player.feet()), saved["feet"]))
            load_ok = (check["loaded"] and state.economy.market.credits == saved["credits"]
                       and state.economy.sold_kg == saved["sold_kg"] and walking.game_numbers()["cells"] == saved["cells"]
                       and len(foot.props.all) == saved["props"] and moved < 2.0
                       and [(c.id, c.kind, c.state) for c in state.contracts.active] == saved["contracts"]
                       and state.contracts.pinned == saved["pinned"] and state.contracts.reputation == saved["reputation"]
                       and list(world.craters) == saved["craters"] and sorted(state.showers.hot.values()) == saved["hot"])
            print("[walker] saved, changed and loaded: %.0f cr, %d cells, %d props, %d contract%s, %d crater%s, "
                  "%d hot, the player %.2f m from where it was saved: %s"
                  % (state.economy.market.credits, walking.game_numbers()["cells"], len(foot.props.all), len(state.contracts.active),
                     "" if len(state.contracts.active) == 1 else "s", len(world.craters),
                     "" if len(world.craters) == 1 else "s", len(state.showers.hot), moved,
                     "passed" if load_ok else "FAILED"),
                  flush=True)
            if args.shot:
                base, ext = os.path.splitext(args.shot)
                walking.screenshot(base + "_load" + ext)
            check["ok"] = (check["walk_ok"] and check["capture_ok"] and check["held_ok"] and check["punt_ok"]
                           and check["map_ok"] and check["suit_ok"] and check["sell_ok"] and check["pause_ok"]
                           and check["shower_ok"] and check["storm_ok"] and check["voice_ok"]
                           and check["scan_rock_ok"] and load_ok)
            check["in_car"] = vehicle.enter_car(anywhere=True)
            check["top_speed"], check["top_height"] = 0.0, 0.0
            check["stage"], check["time"] = "drive", 0.0
    elif stage in ("drive", "fly", "land"):
        car = state.vehicle.car
        check["top_speed"] = max(check["top_speed"], car.speed())
        check["top_height"] = max(check["top_height"], car.height or 0.0)
        if stage == "drive":
            state.vehicle.controls = hovercar.CarControls(throttle=1.0, steer=0.25)
            if args.shot and t >= DRIVE_SECONDS - 1.0 and "drive_shot" not in check:
                check["drive_shot"] = True
                base, ext = os.path.splitext(args.shot)
                walking.screenshot(base + "_drive" + ext)
            if t >= DRIVE_SECONDS:
                check["stage"], check["time"] = "fly", 0.0
        elif stage == "fly":
            state.vehicle.controls = hovercar.CarControls(throttle=0.6, climb=1.0 if t < CLIMB_SECONDS else 0.0)
            if t >= CLIMB_SECONDS + 2.0:
                if args.shot:
                    base, ext = os.path.splitext(args.shot)
                    walking.screenshot(base + "_fly" + ext)
                saving.save_game(CHECK_SAVE_BASE, quiet=True)
                saved_at = vehicle.car_position()
                state.vehicle.car.target = hovercar.HOVER_MIN
                loaded = saving.load_game(CHECK_SAVE_BASE)
                moved = length(sub(vehicle.car_position(), saved_at))
                check["flight_load_ok"] = loaded and state.vehicle.driving and moved < 0.01 and state.vehicle.car.target > 10.0
                print("[walker] saved and loaded in flight: driving %s, the car %.3f m from where it was saved: %s"
                      % (state.vehicle.driving, moved, "passed" if check["flight_load_ok"] else "FAILED"), flush=True)
                check["stage"], check["time"] = "land", 0.0
        else:
            moving = car.speed() > 1.0
            state.vehicle.controls = hovercar.CarControls(throttle=-1.0 if moving and dot(car.velocity, car.heading) > 0.0
                                                         else 0.0, climb=-1.0)
            if car.can_leave() or t >= LAND_SECONDS:
                left = vehicle.leave_car()
                check["stage"], check["time"], check["left"] = "out", 0.0, left
    elif stage == "out":
        if t >= 1.0:
            drive_ok = (check["in_car"] and check["flight_load_ok"] and check["top_speed"] > 30.0
                        and check["top_height"] > 0.8 * CLIMB_SECONDS * hovercar.CLIMB_RATE
                        and check["left"] and player is not None and not state.vehicle.driving)
            print("[walker] drove the car to %.0f km/h, flew it %.0f m up, came down and got out: %s"
                  % (3.6 * check["top_speed"], check["top_height"], "passed" if drive_ok else "FAILED"), flush=True)
            if args.shot:
                base, ext = os.path.splitext(args.shot)
                walking.screenshot(base + "_out" + ext)
            check["ok"] = check["ok"] and drive_ok
            if not world.lakes:
                print("[walker] no lakes were found: FAILED", flush=True)
                os._exit(1)
            check["in_car"] = vehicle.enter_car(anywhere=True)
            lake = min(world.lakes, key=lambda k: -dot(normalize(k.direction), normalize(state.vehicle.car.position)))
            car = state.vehicle.car
            car.position = scale(normalize(lake.direction), planet.radius + lake.level + LAKE_CHECK_DROP)
            car.velocity = (0.0, 0.0, 0.0)
            car.heading = car._level(car.heading)
            car.target = hovercar.HOVER_MIN
            check["lake"] = lake
            check["stage"], check["time"] = "lake", 0.0
    elif stage == "lake":
        state.vehicle.controls = hovercar.CarControls()
        if t >= LAKE_SECONDS:
            car, lake = state.vehicle.car, check["lake"]
            above_water = length(car.position) - planet.radius - lake.level - SHIP_HALF[1]
            bed = world.ground
            lake_ok = (check["in_car"] and car.height is not None and abs(car.height - hovercar.HOVER_MIN) < 0.25
                       and abs(above_water - car.height) < 0.25 and bed is not None and bed < lake.level - 0.5)
            print("[walker] the car over a lake %.0f m across: %.2f m above its water, the ground %s m under it: %s"
                  % (2.0 * lake.radius, above_water, "?" if bed is None else "%.1f" % (lake.level - bed),
                     "passed" if lake_ok else "FAILED"), flush=True)
            check["ok"] = check["ok"] and lake_ok
            if args.shot:
                base, ext = os.path.splitext(args.shot)
                walking.screenshot(base + "_lake" + ext)
            check["stage"], check["time"] = "lake_above" if args.shot else "to_river", 0.0
    elif stage == "lake_above":
        state.vehicle.controls = hovercar.CarControls(climb=1.0 if t < LAKE_CLIMB_SECONDS else 0.0)
        if t >= LAKE_CLIMB_SECONDS + 2.0:
            base, ext = os.path.splitext(args.shot)
            walking.screenshot(base + "_lake_above" + ext)
            check["stage"], check["time"] = "to_river", 0.0
    elif stage == "to_river":
        river, k = site.river_spot()
        if river is None:
            print("[walker] no river at least %.0f m wide was found: FAILED" % RIVER_CHECK_WIDTH, flush=True)
            os._exit(1)
        car = state.vehicle.car
        car.position = scale(normalize(river.points[k]), planet.radius + river.levels[k] + LAKE_CHECK_DROP)
        car.velocity = (0.0, 0.0, 0.0)
        car.heading = car._level(sub(river.points[k + 1], river.points[k]))
        car.target = hovercar.HOVER_MIN
        check["river"] = (river, k)
        check["stage"], check["time"] = "river", 0.0
    elif stage == "river":
        state.vehicle.controls = hovercar.CarControls()
        if t >= LAKE_SECONDS:
            car = state.vehicle.car
            river, k = check["river"]
            level = world.water_level(normalize(car.position))
            above_water = None if level is None else length(car.position) - planet.radius - level - SHIP_HALF[1]
            river_ok = (car.height is not None and above_water is not None
                        and abs(car.height - hovercar.HOVER_MIN) < 0.25 and abs(above_water - car.height) < 0.25)
            print("[walker] the car over a river %.0f m wide: %s m above its water: %s"
                  % (river.widths[k], "?" if above_water is None else "%.2f" % above_water,
                     "passed" if river_ok else "FAILED"), flush=True)
            check["ok"] = check["ok"] and river_ok
            if not args.shot:
                check["stage"], check["time"] = "fuel", 0.0
                return
            base, ext = os.path.splitext(args.shot)
            walking.screenshot(base + "_river" + ext)
            check["stage"], check["time"] = "river_above", 0.0
    elif stage == "river_above":
        state.vehicle.controls = hovercar.CarControls(climb=1.0 if t < RIVER_CLIMB_SECONDS else 0.0)
        if t >= RIVER_CLIMB_SECONDS + 2.0:
            base, ext = os.path.splitext(args.shot)
            walking.screenshot(base + "_river_above" + ext)
            check["stage"], check["time"] = "fuel", 0.0
    elif stage == "fuel":
        car = state.vehicle.car
        if "dry_from" not in check:
            state.vehicle.controls = hovercar.CarControls(climb=-1.0)
            if car.height is None or car.height > hovercar.LEAVE_HEIGHT or car.speed() > 0.3:
                if t > LAND_SECONDS:
                    print("[walker] the car did not come down to call fuel: FAILED", flush=True)
                    os._exit(1)
                return
            car.fuel = 0.0
            check["dry_from"] = car.position
            check["time"] = 0.0
            return
        if "call" not in check:
            state.vehicle.controls = hovercar.CarControls(throttle=1.0)        # dry: it must not drive
            if t >= DRY_SECONDS:
                state.vehicle.controls = hovercar.CarControls()
                check["dry_moved"] = length(sub(car.position, check["dry_from"]))
                check["credits_before"] = state.economy.market.credits
                economy.call_fuel()
                check["call"] = state.vehicle.fuel_call
                if check["call"] is None:
                    print("[walker] no fuel call could be made (%s): FAILED" % state.vehicle.note, flush=True)
                    os._exit(1)
            return
        state.vehicle.controls = hovercar.CarControls()
        call = check["call"]
        if state.vehicle.fuel_call is None or world.time > call["due"] + FUEL_WAIT:
            kg = min(car.params.fuel_capacity, (check["credits_before"] - call["fee"]) / call["price"])
            spent = check["credits_before"] - state.economy.market.credits
            fuel_ok = (check["dry_moved"] < 1.0 and state.vehicle.fuel_call is None and abs(car.fuel - kg) < 1e-6
                       and abs(spent - (call["fee"] + kg * call["price"])) < 1e-6)
            print("[walker] the car dry moved %.2f m at full throttle; fuel called from %s (%.2f cr/kg, a %.0f cr "
                  "fee) came: %.1f kg for %.0f cr: %s"
                  % (check["dry_moved"], call["outpost"].name, call["price"], call["fee"], car.fuel, spent,
                     "passed" if fuel_ok else "FAILED"), flush=True)
            check["ok"] = check["ok"] and fuel_ok
            check["stage"], check["time"] = "space", 0.0
    elif stage == "space":
        if run_space_check(check, t):
            check["ok"] = check["ok"] and check["space_ok"]
            check["stage"], check["time"] = "night", 0.0
    elif stage == "night":
        state.vehicle.controls = hovercar.CarControls()
        if "hour" not in check:
            check["hour"] = clock.hour(survival.day_time())
            state.session.day_offset = clock.time_of(NIGHT_CHECK_HOUR, survival.day_time()) - world.time
        if t >= NIGHT_SECONDS:
            night_ok = (abs(world.exposure_scale - clock.NIGHT_SCALE) < 1e-6 and world.lights_drawn >= 2
                        and abs(clock.hour(survival.day_time()) - NIGHT_CHECK_HOUR) < 0.1)
            print("[walker] the day's hour %s, then night at %s: the exposure x%.1f, %d lights drawn: %s"
                  % ("%05.2f" % check["hour"], survival.hour_text(), world.exposure_scale, world.lights_drawn,
                     "passed" if night_ok else "FAILED"), flush=True)
            check["ok"] = check["ok"] and night_ok
            if args.shot:
                base, ext = os.path.splitext(args.shot)
                walking.screenshot(base + "_night" + ext)
            check["cells_before"] = walking.game_numbers()["cells"]
            menus.quit_to_menu()
            check["stage"], check["time"] = "menu", 0.0
    elif stage == "menu":
        if t >= 0.5:
            clock_before = check.setdefault("menu_clock", world.time)
            audio = state.sound.audio
            check["menu_ok"] = (state.session.starting and state.ui.start.visible and not walking.hud_shown()
                                and world.time == clock_before
                                and (audio is None or (audio.bus_paused("world") and state.sound.track == "music_menu")))
            if args.shot:
                base, ext = os.path.splitext(args.shot)
                walking.screenshot(base + "_menu" + ext)
            menus.start(False)
            check["stage"], check["time"] = "new game", 0.0
        else:
            check.setdefault("menu_clock", world.time)
    elif stage == "new game":
        if foot.player is None:
            if t > LOAD_SECONDS:
                print("[walker] the player did not come back for a new game: FAILED", flush=True)
                os._exit(1)
            return
        if t - check.setdefault("spawned_at", t) < SHOT_SECONDS:
            return
        feet = foot.bubble.to_world(foot.player.feet())
        near = length(sub(feet, site.spawn_point()))
        cells = walking.game_numbers()["cells"]
        fresh = (state.economy.market.credits == trade.START_CREDITS and state.progress.objectives.index == 0
                 and not state.contracts.active and not state.economy.owned and len(foot.props.crates) == 6
                 and not world.craters and world.storm is None and not state.vehicle.driving
                 and cells < 0.5 * check["cells_before"])
        new_ok = check["menu_ok"] and fresh and near < NEW_GAME_NEAR
        print("[walker] quit to the main menu (the world still: %s), then a new game: %s cr, objective %d, "
              "%d crates, %d cells explored of %d before, the player %.1f m from the start: %s"
              % (check["menu_ok"], fs.grouped(state.economy.market.credits), state.progress.objectives.index + 1,
                 len(foot.props.crates), cells, check["cells_before"], near, "passed" if new_ok else "FAILED"),
              flush=True)
        check["ok"] = check["ok"] and new_ok
        played, audio = state.sound.played, state.sound.audio
        air = audio.bus_volume("world") if audio is not None else 1.0
        expected = acoustics.air_audio(sounds.air_share(state.view.position))[0]
        sound_ok = audio is None or (all(played[name] > 0 for name in SOUNDS_HEARD) and air > 0.5
                                     and abs(air - expected) < 0.02 and not audio.bus_paused("world")
                                     and state.sound.mix == "playing")
        print("[walker] sounds played: %s; at Base the world bus at %.2f for its air's %.2f: %s"
              % (", ".join("%s %d" % (name, played[name]) for name in state.sound.entries), air, expected,
                 "passed" if sound_ok else "FAILED"), flush=True)
        check["ok"] = check["ok"] and sound_ok
        if args.shot:
            base, ext = os.path.splitext(args.shot)
            walking.screenshot(base + "_new_game" + ext)
        os._exit(0 if check["ok"] else 1)


def check_poi(kind, n, from_feet=None):
    """--check: a point of interest of `kind` put beside the player (its
    first spot at the feet, or `from_feet` (east, north) metres from them),
    measured, of quartz; its key off the faces (face -1, `n`)."""
    feet = foot.bubble.to_world(foot.player.feet())
    up = normalize(planet.relative(feet))
    east, north = local_frame(up)
    offset = from_feet if from_feet is not None else (
        (-pois.SPOTS[kind][0][0][0], -pois.SPOTS[kind][0][0][2]) if kind in pois.SPOTS else (0.0, 0.0))
    point = add(feet, add(scale(east, offset[0]), scale(north, offset[1])))
    relative = planet.relative(point)
    key = (planet.index, -1, n, 0)
    poi = pois.Poi(key, normalize(relative), 0.0, measured=True, height=length(relative) - planet.radius,
                   climate=(10.0, 0.5), ore=0.5, resource=resources.QUARTZ, kind=kind, valid=True)
    state.exploring.pois.pois[key] = poi
    return poi


def run_poi_check(check, t):
    """--check's points of interest, on Base's pad, a step at a time (see
    run_check); True once done."""
    steps = ["natural", "beacon", "wreck", "rig", "arch", "geode", "pod", "survey", "buy"]
    c = check.setdefault("poi", {"step": 0, "t0": t, "lines": [], "ok": True,
                                 "props": {id(p) for p in foot.props.all}})
    if c["step"] >= len(steps):
        # The test's points (off the faces) and the props they made taken away again.
        for key in [k for k in state.exploring.pois.pois if k[1] < 0]:
            del state.exploring.pois.pois[key]
            for entity, _ in state.exploring.scenes.pop(key, []):
                engine.scene.destroy_entity(entity)
        for prop in [p for p in foot.props.all if id(p) not in c["props"]]:
            foot.props.remove(foot.bubble, prop)
        state.exploring.cleared = None
        print("[walker] points of interest:\n" + "\n".join("[walker]   " + line for line in c["lines"])
              + "\n[walker]   %s" % ("passed" if c["ok"] else "FAILED"), flush=True)
        check["sell_ok"] = check["sell_ok"] and c["ok"]
        return True
    step, since = steps[c["step"]], t - c["t0"]

    def done(ok, line):
        c["ok"] = c["ok"] and ok
        c["lines"].append("%s: %s" % (line, "ok" if ok else "FAILED"))
        c["step"] += 1
        c["t0"] = t
        c.pop("poi", None)
        state.session.check_use = False

    p = state.exploring.pois
    if step == "natural":
        near = [q for q in p.pois.values() if q.key[1] >= 0]
        if p.queue and since < POI_MEASURE_SECONDS:
            return False
        found = sorted(q.name for q in p.found() if q.key[1] >= 0)
        done(near and not p.queue, "%d candidates within %s measured, found %s"
             % (len(near), distance_text(POI_REACH), ", ".join(found) or "none"))
    elif step in ("beacon", "wreck", "rig", "arch"):
        if "poi" not in c:
            c["poi"] = check_poi(step, c["step"])
            c["before"] = (state.economy.survey_data, dict(state.economy.parts), len(foot.props.rocks), len(state.ui.map.revealing))
            return False
        poi = c["poi"]
        state.session.check_use = True
        hold = pois.SPOTS[step][0][2]
        if 0 not in poi.done and since < hold + USE_CHECK_SECONDS:
            return False
        survey, parts, rocks, revealing = c["before"]
        gained = state.economy.survey_data - survey
        new_parts = {k: v - parts.get(k, 0) for k, v in state.economy.parts.items() if v != parts.get(k, 0)}
        ok = 0 in poi.done and poi.discovered
        if step in ("beacon", "arch"):
            ok = ok and abs(gained - pois.survey_value(poi)) < 1e-6
            if step == "beacon":
                ok = ok and len(state.ui.map.revealing) > revealing
        else:
            ok = ok and sum(new_parts.values()) == 1 and since >= hold
            if step == "rig":
                ok = ok and len(foot.props.rocks) == rocks + pois.RIG_ROCKS
        extra = ""
        if step == "beacon":
            extra = ", %d map cells queued" % (len(state.ui.map.revealing) - revealing)
        elif step == "rig":
            extra = ", %d ore rocks more" % (len(foot.props.rocks) - rocks)
        done(ok, "%s used after %.1f s: survey data +%.0f cr, parts %s%s"
             % (poi.name, since, gained, upgrades.parts_text(new_parts) or "none", extra))
    elif step == "geode":
        if "poi" not in c:
            c["poi"] = check_poi("geode", c["step"], from_feet=(60.0, 0.0))
            c["before"] = len(foot.props.rocks)
            return False
        poi = c["poi"]
        if not poi.done and since < USE_CHECK_SECONDS:
            return False
        rocks = [r for r in foot.props.rocks[c["before"]:]]
        ok = (poi.looted and len(rocks) == pois.GEODE_ROCKS
              and all(r.grade == pois.GEODE_GRADE and r.resource == resources.QUARTZ for r in rocks))
        done(ok, "geode field: %d rocks of quartz at grade %.2f" % (len(rocks), rocks[0].grade if rocks else 0.0))
    elif step == "pod":
        if "poi" not in c:
            c["poi"] = check_poi("pod", c["step"], from_feet=(6.0, 0.0))
            c["sold"] = state.exploring.pods_sold
            return False
        poi = c["poi"]
        if not poi.looted and since < POD_CHECK_SECONDS:
            return False
        done(poi.looted and state.exploring.pods_sold == c["sold"] + 1,
             "cargo pod made on Base's pad, at rest sold for %.0f cr after %.1f s" % (pois.POD_VALUE, since))
    elif step == "survey":
        held, credits = state.economy.survey_data, state.economy.market.credits
        outpost = economy.at_outpost()
        expected = trade.survey_sale(outpost, held, world.time)[0] if outpost is not None else 0.0
        economy.sell_survey()
        gained = state.economy.market.credits - credits
        done(outpost is not None and held > 0.0 and state.economy.survey_data == 0.0 and abs(gained - expected) < 1e-6,
             "survey data worth %.0f cr sold at %s for %.0f cr" % (held, outpost.name if outpost else "-", gained))
    elif step == "buy":
        bank, liner = upgrades.BY_KEY["gun_charge_1"], upgrades.BY_KEY["suit_1"]
        coils = state.economy.parts.get("coil", 0)
        cold = state.suit.params.cold_limit
        economy.buy_upgrade(bank)
        bank_ok = ("gun_charge_1" in state.economy.owned) == (coils >= 1)
        economy.buy_upgrade(liner)
        liner_ok = "suit_1" in state.economy.owned and abs(state.suit.params.cold_limit - (cold - 10.0)) < 1e-9
        done(bank_ok and liner_ok, "Capacitor bank with %d coil%s: %s; Thermal liner: the suit's cold limit %.0f C"
             % (coils, "" if coils == 1 else "s", "bought" if "gun_charge_1" in state.economy.owned else "refused",
                state.suit.params.cold_limit))
    return False


def run_contract_check(check, t):
    """--check's contracts, on Base's pad, a step at a time (see
    run_check); True once done. Each but the offers is a contract of Base
    made for the step (check_contract) and accepted there."""
    steps = ["offers", "delivery", "courier", "fragile", "recovery", "survey", "expire", "abandon", "keep"]
    c = check.setdefault("contracts", {"step": 0, "t0": t, "lines": [], "ok": True})
    home, car = state.site.home, state.vehicle.car
    if c["step"] >= len(steps):
        rep = state.contracts.reputation.get(home.name, 0.0)
        ok = c["ok"] and rep == 1.0 and home.reputation == 1.0
        print("[walker] contracts:\n" + "\n".join("[walker]   " + line for line in c["lines"])
              + "\n[walker]   Base's reputation %+.0f, its prices x%.2f: %s"
              % (rep, 1.0 + trade.REPUTATION_PRICE * home.reputation, "passed" if ok else "FAILED"), flush=True)
        check["sell_ok"] = check["sell_ok"] and ok
        return True
    step, since = steps[c["step"]], t - c["t0"]

    def done(ok, line):
        c["ok"] = c["ok"] and ok
        c["lines"].append("%s: %s" % (line, "ok" if ok else "FAILED"))
        c["step"] += 1
        c["t0"] = t
        c.pop("contract", None)

    def begin(contract):
        c["contract"] = contract
        c["before"] = (state.economy.market.credits, state.contracts.reputation.get(home.name, 0.0), state.economy.sold_kg)
        return contracting.accept_contract(contract)

    def ended(contract, how):
        credits, rep, _ = c["before"]
        paid = state.economy.market.credits - credits
        return (contract.state == how and contract not in state.contracts.active
                and abs(paid - (contract.reward if how == "completed" else 0.0)) < 1e-6
                and state.contracts.reputation.get(home.name, 0.0) == rep + (1.0 if how == "completed" else -1.0))

    feet = foot.bubble.to_world(foot.player.feet())
    up = normalize(planet.relative(feet))
    east, north = local_frame(up)
    if step == "offers":
        first, again = contracting.contract_offers(home), contracting.contract_offers(home)
        ok = (economy.at_outpost() is home and len(first) == contracts.OFFERS
              and [contracts.record(o) for o in first] == [contracts.record(o) for o in again])
        done(ok, "%d offers at %s on day %d, the same when asked again: %s"
             % (len(first), home.name, contracting.contract_day(), ", ".join("%s %.0f cr" % (o.kind, o.reward) for o in first)))
    elif step == "delivery":
        if "contract" not in c:
            rock = walking.ore_rock(add(world.site_ground(), scale(up, 1.0)), CHECK_ORE_EXTENT, CHECK_ORE_GRADE)
            c["rock"] = rock
            begin(check_contract("delivery", c["step"], resource=resources.FERRITE, quantity=rock.mass,
                                 min_grade=0.5))
            return False
        contract = c["contract"]
        if contract.state == "active" and since < CONTRACT_CHECK_SECONDS:
            return False
        done(ended(contract, "completed") and c["rock"] not in foot.props.rocks and state.economy.sold_kg == c["before"][2],
             "delivery of %.0f kg of ferrite: the rock on the pad delivered, not sold, after %.1f s, +%.0f cr"
             % (contract.quantity, since, contract.reward))
    elif step == "courier":
        if "contract" not in c:
            contract = c["contract"] = check_contract("courier", c["step"])
            c["accepted"] = economy.car_outpost() is home and begin(contract)
            c["in_hold"] = any(i.get("contract") == contract.id for i in car.cargo)
            return False
        contract = c["contract"]
        if contract.state == "active" and since < CONTRACT_CHECK_SECONDS:
            return False
        done(c["accepted"] and c["in_hold"] and ended(contract, "completed")
             and not any("contract" in i for i in car.cargo),
             "courier accepted with the car on the pad: its crate into the hold, delivered there after %.1f s" % since)
    elif step == "fragile":
        if "contract" not in c:
            c["parked_at"] = car.position
            car.position = add(car.position, scale(car.heading, NET_CHECK_AWAY))
            vehicle.update_ship()
            begin(check_contract("courier", c["step"], destination="Elsewhere"))
            return False
        contract = c["contract"]
        crate = next((p for p in foot.props.crates if state.contracts.couriers.get(id(p)) == contract.id), None)
        if "thrown" not in c and crate is not None and crate.body is not None and since >= 0.5:
            p, q = foot.physics.get_transform(crate.body)
            lift = foot.bubble.up(p)
            foot.physics.set_transform(crate.body, add(p, scale(lift, 3.0)), q)
            foot.physics.set_velocity(crate.body, scale(lift, -CONTRACT_CHECK_DROP), (0.0, 0.0, 0.0))
            c["thrown"] = True
            return False
        if contract.state == "active" and since < CONTRACT_CHECK_SECONDS:
            return False
        done("thrown" in c and ended(contract, "failed") and crate is None and contract.damage >= 1.0,
             "courier's crate made beside the player and thrown down at %.0f m/s: damage %.2f, broken, failed"
             % (CONTRACT_CHECK_DROP, contract.damage))
    elif step == "recovery":
        if "contract" not in c:
            target = normalize(planet.relative(add(feet, scale(east, 6.0))))
            begin(check_contract("recovery", c["step"], target=target))
            return False
        contract = c["contract"]
        if contract.state == "active" and since < CONTRACT_CHECK_SECONDS:
            return False
        done(ended(contract, "completed") and contracting.pod_key(contract) not in state.exploring.pois.pois,
             "recovery: the pod made at its point on the pad and, settled, delivered after %.1f s" % since)
    elif step == "survey":
        if "contract" not in c:
            begin(check_contract("survey", c["step"], target=up))
            c["queued"] = state.ui.map.reveal(planet, up, contracts.SURVEY_RADIUS)
            return False
        contract = c["contract"]
        if contract.state == "active" and since < CONTRACT_CHECK_SECONDS:
            return False
        done(ended(contract, "completed") and contract.explored >= contracts.SURVEY_SHARE,
             "survey around the feet (%d cells queued): %d %% explored after %.1f s"
             % (c["queued"], round(100.0 * contract.explored), since))
    elif step == "expire":
        if "contract" not in c:
            begin(check_contract("survey", c["step"], target=up, deadline=contracting.contract_day() - 1))
            return False
        contract = c["contract"]
        done(ended(contract, "expired"), "a contract past its deadline expired")
    elif step == "abandon":
        contract = check_contract("survey", c["step"], target=normalize(planet.relative(add(feet,
                                                                                           scale(north, 2_000.0)))))
        begin(contract)
        contracting.pin_contract(contract)
        contracting.abandon_pinned()
        done(ended(contract, "abandoned"), "a pinned contract abandoned")
    elif step == "keep":
        if "contract" not in c:
            begin(check_contract("survey", c["step"], target=normalize(planet.relative(add(feet,
                                                                                          scale(east, 2_000.0))))))
            car.position = c["parked_at"]
            vehicle.update_ship()
            return False
        contract = c["contract"]
        if since < 0.5:
            return False
        card = state.ui.hud.card
        marked = any(name == "Contract" for name, _, _ in walking.markers(planet))
        done(contract in state.contracts.active and state.contracts.pinned == contract.id and card[0].startswith("Pinned")
             and card[1] == contracts.short_task(contract) and marked,
             "a survey 2 km off kept active and pinned: on the card (%s) and the compass" % card[0])
    return False


def run_shower_check(check, t):
    """--check's meteor shower by Base (see run_check); True once done."""
    s = check.setdefault("shower", {"step": "plan"})
    player = foot.player
    step = s["step"]
    if step == "plan":
        feet = foot.bubble.to_world(player.feet())
        here = normalize(planet.relative(feet))
        centre = events.along_ground(here, 90.0, SHOWER_CHECK_CENTRE, planet.radius)
        pad = state.site.home.direction
        out = bearing(pad, sub(here, pad))
        if out is None or events.ground_distance(pad, here, planet.radius) < 1.0:
            out = bearing(here, player.heading) or 0.0
        ahead = events.along_ground(pad, out, outposts.PAD_REACH + SHOWER_CHECK_OFF_PAD, planet.radius)
        now = world.time + SHOWER_CHECK_LEAD
        large = events.Impact(now, centre, SHOWER_CHECK_MASS, 0.6, 4_000.0, 60.0, 0.0,
                              events.crater_of(SHOWER_CHECK_MASS))
        small = events.Impact(now + 1.0, ahead, SHOWER_CHECK_ROCK, 0.6, 4_000.0, 60.0, 0.0)
        state.showers.body = None
        showers.set_shower(events.Shower(now, centre, [large, small]))
        s.update(step="fly", craters=list(world.craters), large=large, small=small, heads=0, flash=0.0,
                 shake=0.0, plumes=0, natural=None, t0=t)
        return False
    large, small = s["large"], s["small"]
    if step == "fly":
        s["heads"] = max(s["heads"], state.showers.heads_drawn)
        tint = showers.flash_tint()
        s["flash"] = max(s["flash"], tint[1] if tint is not None else 0.0)
        if state.showers.shake is not None:
            s["shake"] = max(s["shake"], state.showers.shake[1])
        s["plumes"] = max(s["plumes"], len(state.showers.plumes))
        for toast in state.ui.hud.toasts:
            for key, start in (("warned", "Meteor shower inbound"), ("down", "Meteorite down")):
                if toast[0].startswith(start):
                    s[key] = True
        if s["natural"] is None and 0 in state.showers.impact_heights:
            s["natural"] = state.showers.impact_heights[0]
        up = player.up()
        path, before = state.showers.paths.get(0), large.time - world.time
        aim = None
        if args.shot and path is not None and 2.0 < before <= min(5.0, path.duration):
            aim = add(planet.centre(), path.at(before))
        elif args.shot and -5.0 < before <= 2.0:
            aim = add(planet.centre(), scale(large.direction, planet.radius + (s["natural"] or 0.0)))
        if aim is not None:
            to = sub(foot.bubble.to_bubble(aim), player.eye())
            player.heading = player.level(to, up)
            player.pitch = math.asin(max(-1.0, min(1.0, dot(normalize(to), up))))
        if args.shot and before <= 3.0 and "meteor_shot" not in s and state.showers.heads_drawn > 0:
            s["meteor_shot"] = True
            base, ext = os.path.splitext(args.shot)
            walking.screenshot(base + "_meteor" + ext)
        if args.shot and before <= -0.2 and "impact_shot" not in s:
            s["impact_shot"] = True
            base, ext = os.path.splitext(args.shot)
            walking.screenshot(base + "_impact" + ext)
        if args.shot and before <= -4.0 and "plume_shot" not in s:
            s["plume_shot"] = True
            base, ext = os.path.splitext(args.shot)
            walking.screenshot(base + "_plume" + ext)
        if state.showers.shower is None and world.time >= small.time + SHOWER_LAND_SECONDS:
            s["step"], s["t0"] = "measure", t
        elif t - s["t0"] > SHOWER_CHECK_LEAD + 30.0:
            print("[walker] the shower did not strike in %.0f s: FAILED" % (t - s["t0"]), flush=True)
            os._exit(1)
        return False
    if step == "measure":
        meteorite = state.showers.meteorite
        rock = next((p for p in foot.props.rocks if p.resource == resources.IRIDIUM and p is not meteorite), None)
        dug = [c for c in world.craters if c not in s["craters"]]
        crater_ok = (len(dug) == 1 and abs(dug[0].radius - large.crater[0]) < 1e-9
                     and length(sub(dug[0].direction, large.direction)) < 1e-9)
        lies = None
        if meteorite is not None:
            body, position, _ = foot.prop_state(meteorite)
            lies = length(position) - planet.radius - meteorite.rest
        floor = (s["natural"] or 0.0) - large.crater[1]
        meteorite_ok = (meteorite is not None and meteorite.resource == resources.IRIDIUM
                        and abs(meteorite.mass - large.mass) < 1.0 and walking.settled(meteorite)
                        and lies is not None and abs(lies - floor) < 1.0 and id(meteorite) in state.showers.hot)
        rock_ok = rock is not None and abs(rock.mass - small.mass) < 0.5 and id(rock) in state.showers.hot
        warned, down = s.get("warned", False), s.get("down", False)
        seen_ok = (s["heads"] > 0 and s["flash"] > 0.0 and s["shake"] > 0.0 and s["plumes"] >= PLUME_PUFFS
                   and warned and down)
        s["lines"] = [
            "the crater dug, %.0f m across: %s" % (2.0 * large.crater[0], "ok" if crater_ok else "FAILED"),
            "the %.0f kg meteorite of iridium hot, at rest %s m from the crater's floor: %s"
            % (meteorite.mass if meteorite is not None else 0.0,
               "%.2f" % (lies - floor) if lies is not None else "-", "ok" if meteorite_ok else "FAILED"),
            "a %.0f kg rock of iridium hot off the pad: %s" % (rock.mass if rock is not None else 0.0,
                                                          "ok" if rock_ok else "FAILED"),
            "warned and told, %d meteor%s drawn at once, a flash of %.2f, a shake of %.2f degrees, %d puffs: %s"
            % (s["heads"], "" if s["heads"] == 1 else "s", s["flash"], math.degrees(s["shake"]), s["plumes"],
               "ok" if seen_ok else "FAILED")]
        s["ok"] = crater_ok and meteorite_ok and rock_ok and seen_ok
        s["rock"] = rock
        s["step"], s["t0"] = "approach", t
        return False
    rock = s["rock"]
    if step == "approach":
        if rock is None:
            s["step"] = "done"
            return False
        body, position, _ = foot.prop_state(rock)
        to = sub(add(body.centre(), position), foot.bubble.to_world(player.feet()))
        up = player.up()
        along = sub(to, scale(up, dot(to, up)))
        if length(along) <= SHOWER_CHECK_NEAR or t - s["t0"] > 15.0:
            foot.controls.forward = 0.0
            s["step"], s["t0"] = "too_hot", t
        else:
            player.heading = player.level(along, up)
            foot.controls.forward = 1.0
        return False
    if step == "too_hot":
        if rock.body is None:
            if t - s["t0"] > 3.0:
                s["lines"].append("the rock came into the physics: FAILED")
                s["ok"], s["step"] = False, "done"
            return False
        to = sub(foot.physics.get_transform(rock.body)[0], player.eye())
        up = player.up()
        player.heading = player.level(to, up)
        player.pitch = math.asin(max(-1.0, min(1.0, dot(normalize(to), up))))
        foot.gun_controls.grab = True
        if t - s["t0"] >= 0.5:
            status, held = foot.gun.status, foot.gun.held
            ok = status == "too_hot" and held is None
            s["lines"].append("the gun refuses the hot rock (%s): %s" % (status, "ok" if ok else "FAILED"))
            s["ok"] = s["ok"] and ok
            del state.showers.hot[id(rock)]
            s["step"], s["t0"] = "cooled", t
        return False
    if step == "cooled":
        foot.gun_controls.grab = True
        if t - s["t0"] >= 1.0:
            ok = foot.gun.held == rock.body
            s["lines"].append("the gun grabs it cooled: %s" % ("ok" if ok else "FAILED"))
            s["ok"] = s["ok"] and ok
            foot.gun_controls.grab = False
            foot.gun.drop()
            s["step"] = "done"
        return False
    print("[walker] meteor shower:\n" + "\n".join("[walker]   " + line for line in s["lines"])
          + "\n[walker]   %s" % ("passed" if s["ok"] else "FAILED"), flush=True)
    check["shower_ok"] = s["ok"]
    return True


def run_voice_check(check, t):
    """--check's voice management (see run_check): VOICE_HITS hits of
    shuffled volumes on a level ring of VOICE_RING around the camera,
    VOICE_LIFT above it, and VOICE_SAME at the camera, rising to full, in
    one frame; the next frame, passing when at most the rocks' category of
    voices play, the ring's playing are louder as heard (their volume times
    their occlusion's gain) than all it dropped, the hits at the camera are one
    voice, and all took less than VOICE_BUDGET_MS; and a point 30 m into
    the ground is occluded and one 30 m up not, the reverb was set (with a
    wet level within acoustics' range), and the Headphones setting made
    the audio binaural (when it can be), a hit heard through it, and off
    again, and a hit 1.2 times its far distance off is not played. True
    once done."""
    audio = state.sound.audio
    if audio is None:
        check["voice_ok"] = True
        return True
    v = check.get("voices")
    if v is None:
        camera = state.view.position
        up = world.radial_up(camera)
        east, north = local_frame(up)
        middle = add(camera, scale(up, VOICE_LIFT))
        volumes = [0.05 + 0.95 * k / (VOICE_HITS - 1) for k in range(VOICE_HITS)]
        random.Random(7).shuffle(volumes)
        ring = [add(middle, add(scale(east, VOICE_RING * math.cos(2.0 * math.pi * k / VOICE_HITS)),
                                scale(north, VOICE_RING * math.sin(2.0 * math.pi * k / VOICE_HITS))))
                for k in range(VOICE_HITS)]
        start = time.perf_counter()
        voices = [sounds.hit(point, volume) for point, volume in zip(ring, volumes)]
        same = [sounds.hit(camera, 0.9 + 0.1 * k / (VOICE_SAME - 1)) for k in range(VOICE_SAME)]
        ms = 1000.0 * (time.perf_counter() - start)
        heard = [volume * acoustics.occluded_audio(sounds.occluded(point))[0] for point, volume in zip(ring, volumes)]
        check["voices"] = {"t0": t, "voices": voices, "volumes": heard, "same": same, "ms": ms}
        return False
    if t <= v["t0"]:
        return False
    playing = [voice != 0 and audio.active(voice) for voice in v["voices"]]
    kept = [volume for volume, on in zip(v["volumes"], playing) if on]
    dropped = [volume for volume, on in zip(v["volumes"], playing) if not on]
    count, cap = audio.voices_of("rocks"), sounds.CATEGORIES["rocks"]
    one = len(set(v["same"])) == 1 and v["same"][0] != 0 and audio.active(v["same"][0])
    loudest = bool(kept) and (not dropped or min(kept) > max(dropped))
    ok = count <= cap and loudest and one and v["ms"] < VOICE_BUDGET_MS
    print("[walker] %d hits in a frame: %d of the rocks' %d voices playing, the ring's %d the loudest (%s), "
          "%d at one point one voice (%s), in %.1f ms: %s" % (VOICE_HITS + VOICE_SAME, count, cap, len(kept), loudest,
                                                             VOICE_SAME, one, v["ms"], "passed" if ok else "FAILED"),
          flush=True)
    for voice in set(v["voices"] + v["same"]):
        if voice:
            audio.stop(voice)

    camera, up = state.view.position, world.radial_up(state.view.position)
    below, above = sounds.occluded(add(camera, scale(up, -30.0))), sounds.occluded(add(camera, scale(up, 30.0)))
    reverb = state.sound.reverb
    most = acoustics.OPEN_WET + acoustics.ENCLOSED_WET
    reverb_ok = not audio.reverb_available or (reverb is not None and acoustics.OPEN_WET <= reverb[1] <= most + 1e-6)
    headphones = settings["headphones"]
    settings.set("headphones", True)
    sounds.apply_binaural()
    binaural = audio.binaural == audio.binaural_available and not state.sound.loops
    heard = sounds.play("hit", add(camera, scale(up, 2.0)))
    audio.stop(heard)
    culled = sounds.play("hit", add(camera, scale(up, 1.2 * sounds.SOUNDS["hit"]["far"]))) == 0
    settings.set("headphones", headphones)
    sounds.apply_binaural()
    steam_ok = below and not above and reverb_ok and binaural and heard != 0 and not audio.binaural and culled
    print("[walker] 30 m into the ground occluded (%s), 30 m up not (%s); the reverb %s; with headphones binaural "
          "(%s) and heard (%s); a hit beyond its far distance not played (%s); the listener at %.1f m/s: %s"
          % (below, above, "unset" if reverb is None else "%.2f s, wet %.2f" % (reverb[0][1], reverb[1]), binaural,
             heard != 0, culled, length(sounds.listener_velocity()), "passed" if steam_ok else "FAILED"), flush=True)
    check["voice_ok"] = ok and steam_ok
    return True


def run_scan_rock_check(check, t):
    """--check's scanned rocks (see run_check): the ground made desert by
    SCAN_ROCK_MOISTURE, until a scanned rock (rocks.SCAN_ROCKS) has a
    static hull near the player (within SCAN_ROCK_SECONDS); it is let
    loose as the gun's grab does (Rocks.promote), passing when it is a
    loose rock with a mass, its cell (of the scans' grids, 2 or 3) taken
    and its entity textured; the rock is then taken away (its cell back)
    and the moisture put back. True once done."""
    s = check.setdefault("scan rocks", {"t0": t, "moisture": world.moisture_offset})
    world.moisture_offset = SCAN_ROCK_MOISTURE
    found = next(((key, rock) for key, rock in foot.rocks.rocks.items() if rock.kind in SCAN_ROCKS), None)
    if found is None and t - s["t0"] < SCAN_ROCK_SECONDS:
        return False
    ok, line = False, "no scanned rock near the player in %.0f s" % SCAN_ROCK_SECONDS
    if found is not None:
        key, rock = found
        body = rock.body
        promoted = foot.rocks.promote(foot.bubble, body)
        prop = foot.props.find(body) if promoted else None
        taken = key in world.taken_rocks
        ok = (promoted and prop is not None and prop.mass > 0.0 and taken and key[1] in (2, 3)
              and rock.kind in foot.rocks.scan_meshes)
        line = ("a %s (%.2f m) with its hull, let loose: a loose rock of %.0f kg (%s), its cell on grid %d taken (%s)"
                % (rock.kind, rock.scale[1], prop.mass if prop is not None else 0.0, prop is not None, key[1], taken))
        if prop is not None:
            foot.props.remove(foot.bubble, prop)
            ok = ok and key not in world.taken_rocks
    world.moisture_offset = s["moisture"]
    print("[walker] %s: %s" % (line, "passed" if ok else "FAILED"), flush=True)
    check["scan_rock_ok"] = ok
    return True


def run_storm_check(check, t):
    """--check's storm over the player (see run_check); True once done."""
    s = check.setdefault("storm", {"step": "crates", "t0": t})
    player = foot.player
    step, since = s["step"], t - s["t0"]
    feet = foot.bubble.to_world(player.feet())
    here = normalize(planet.relative(feet))
    if step == "crates":
        up = player.up()
        right = normalize(cross(player.heading, up))
        s["crates"] = [foot.props.spawn_crate(foot.bubble, add(player.feet(), add(scale(right, side), scale(up, 0.5))),
                                              quat_from_basis(right, up, scale(player.heading, -1.0)),
                                              CRATE_SIZE, mass=mass)
                       for side, mass in ((STORM_CHECK_APART, STORM_CHECK_LIGHT),
                                          (-STORM_CHECK_APART, STORM_CHECK_HEAVY))]
        s["step"], s["t0"] = "settle", t
        return False
    crates = s["crates"]
    if step == "settle":
        if all(c.body is not None and not foot.physics.is_active(c.body) for c in crates) or since > 5.0:
            s["before"] = [foot.prop_state(c)[1] for c in crates]
            steady = length(world.wind_at(planet, planet.relative(feet), 10.0))
            axis = normalize(cross(here, local_frame(here)[1]))
            world.storm = wind_field.Storm(here, axis, 0.0, STORM_CHECK_RADIUS, world.time - wind_field.FADE,
                                           world.time + 3600.0, planet.radius)
            s["wind"] = (steady, length(world.wind_at(planet, planet.relative(feet), 10.0)))
            home = state.site.home
            s["ore"] = walking.ore_rock(add(home.point(), scale(home.direction, 1.0)), CHECK_ORE_EXTENT, CHECK_ORE_GRADE)
            economy.fill_tank()
            s["tank"] = state.vehicle.note
            c = contracting.pinned_contract()
            s["contract"] = (c, c.paused if c is not None else 0.0, world.time)
            s["rain"] = 0
            s["step"], s["t0"] = "storm", t
        return False
    if step == "storm":
        s["rain"] = max(s["rain"], state.storms.rain_drawn)
        for toast in state.ui.hud.toasts:
            if toast[0].startswith("Storm approaching " + state.site.home.name):
                s["warned"] = True
        if since < STORM_CHECK_SECONDS:
            return False
        if args.shot:
            base, ext = os.path.splitext(args.shot)
            walking.screenshot(base + "_storm" + ext)
        moved = [length(sub(foot.prop_state(c)[1], b)) for c, b in zip(crates, s["before"])]
        light_ok = moved[0] > STORM_CHECK_MOVED
        heavy = crates[1]
        heavy_ok = moved[1] < 0.05 and heavy.body is not None and not foot.physics.is_active(heavy.body)
        unsold = any(p is s["ore"] for p in foot.props.rocks)
        c, paused, clock_then = s["contract"]
        held = c.paused - paused if c is not None else 0.0
        held_ok = c is not None and abs(held - (world.time - clock_then)) < 0.1
        steady, stormy = s["wind"]
        ratio = stormy / steady if steady > 0.0 else 0.0
        lines = [
            "the wind 10 m up %.1f m/s, in the storm %.1f m/s (x%.2f): %s"
            % (steady, stormy, ratio, "ok" if 1.6 <= ratio <= 3.1 else "FAILED"),
            "the %.0f kg crate moved %.2f m, the %.0f kg one %.3f m and asleep: %s"
            % (STORM_CHECK_LIGHT, moved[0], STORM_CHECK_HEAVY, moved[1], "ok" if light_ok and heavy_ok else "FAILED"),
            "Base closed: announced %s, the ore on its pad unsold %s, the tank: \"%s\": %s"
            % (s.get("warned", False), unsold, s["tank"],
               "ok" if s.get("warned") and unsold and "closed" in s["tank"] else "FAILED"),
            "the pinned contract's deadline held back %.1f s of %.1f: %s"
            % (held, world.time - clock_then, "ok" if held_ok else "FAILED"),
            "%d rain streaks drawn: %s" % (s["rain"], "ok" if s["rain"] > 0 else "FAILED")]
        s["ok"] = (1.6 <= ratio <= 3.1 and light_ok and heavy_ok and s.get("warned", False) and unsold
                   and "closed" in s["tank"] and held_ok and s["rain"] > 0)
        s["lines"] = lines
        world.storm = None
        s["step"], s["t0"] = "passed", t
        return False
    if step == "passed":
        sold = not any(p is s["ore"] for p in foot.props.rocks)
        if not sold and since < SELL_SECONDS:
            return False
        reopened = any(toast[0].startswith("The storm has passed " + state.site.home.name)
                       for toast in state.ui.hud.toasts)
        s["lines"].append("the storm gone: Base open again (%s), the ore sold after %.1f s: %s"
                          % (reopened, since, "ok" if sold and reopened else "FAILED"))
        s["ok"] = s["ok"] and sold and reopened
        for crate in crates:
            if any(p is crate for p in foot.props.crates):
                foot.props.remove(foot.bubble, crate)
        print("[walker] storm:\n" + "\n".join("[walker]   " + line for line in s["lines"])
              + "\n[walker]   %s" % ("passed" if s["ok"] else "FAILED"), flush=True)
        check["storm_ok"] = s["ok"]
        return True
    return False


def run_space_check(check, t):
    """--check's space mode (see run_check); True once done."""
    s = check.setdefault("space", {"step": "climb", "t0": t, "lines": [], "ok": True})
    car, step, since = state.vehicle.car, s["step"], t - s["t0"]

    def done(ok, line, next_step):
        s["ok"] = s["ok"] and ok
        s["lines"].append("%s: %s" % (line, "ok" if ok else "FAILED"))
        s["step"], s["t0"] = next_step, t

    if step == "climb":
        state.vehicle.controls = hovercar.CarControls(climb=1.0)
        if car.height is not None and car.height >= SPACE_HEIGHT + 5.0:
            state.vehicle.controls = hovercar.CarControls()
            entered = spaceflight.enter_space()
            s["from"] = (car.height, car.fuel)
            done(entered, "into space mode %.0f m up" % car.height, "rise")
        elif since > SPACE_CHECK_SECONDS:
            done(False, "the car did not climb to %.0f m" % SPACE_HEIGHT, "done")
        return False
    ship, controls = state.space.ship, state.space.controls
    if step == "rise":
        controls.vertical = 1.0
        if since >= SPACE_RISE_SECONDS:
            controls.vertical = 0.0
            rose, burnt = car.height - s["from"][0], s["from"][1] - car.fuel
            ok = rose > SPACE_RISE_MIN and burnt > 0.0 and car.fuel == ship.fuel
            done(ok, "rose %.0f m on the assist in %.0f s, burning %.2f kg" % (rose, SPACE_RISE_SECONDS, burnt), "saved")
            lines = spaceflight.space_lines()
            planner = len(lines) == 6 and lines[4].startswith("MOON LEAD") and lines[5].startswith("BURN IN")
            done(planner, "the space readout with the transfer planner: %s" % " | ".join(lines[3:]), "saved")
            if args.shot:
                base, ext = os.path.splitext(args.shot)
                walking.screenshot(base + "_space" + ext)
            saving.save_game(CHECK_SAVE_BASE, quiet=True)
            s["saved_at"] = ship.position
            state.space.ship.position = add(ship.position, (100.0, 0.0, 0.0))
            loaded = saving.load_game(CHECK_SAVE_BASE)
            moved = length(sub(state.space.ship.position, s["saved_at"])) if state.space.ship is not None else math.inf
            done(loaded and moved < 0.01, "saved and loaded in space mode: %.3f m from where it was saved" % moved,
                 "down")
        return False
    if step == "down":
        if ship is None:
            done(False, "space mode ended on its own", "done")
            return False
        controls.vertical = -1.0
        if car.height is not None and car.height < HOVER_BACK - 20.0:
            controls.vertical = 0.0
            back = spaceflight.leave_space()
            done(back and state.space.ship is None and spaceflight.car_body() is planet,
                 "back to the hover car %.0f m up over the planet" % car.height, "to_moon")
        elif since > SPACE_CHECK_SECONDS:
            done(False, "it did not come down within %.0f m" % HOVER_BACK, "done")
        return False
    if step == "to_moon":
        outpost = next(o for o in state.site.outposts if o.body is not planet)
        moon = outpost.body
        up = outpost.direction
        east, north = local_frame(up)
        position = add(moon.centre(), scale(up, moon.radius + outpost.height + SPACE_CHECK_DROP))
        state.space.ship = flight.Ship(position, moon.velocity(), quat_from_basis(east, up, scale(north, -1.0)),
                                     params=hovercar.space_params(state.economy.params["car"], SHIP_HALF), fuel=car.fuel,
                                     cargo=car.cargo)
        state.space.controls = flight.Controls(vertical=-1.0, assist=True)
        state.space.time, state.space.warp = world.time, 0
        s["outpost"] = outpost
        s["step"], s["t0"] = "land", t
        return False
    if step == "land":
        outpost = s["outpost"]
        if ship is not None:
            if since > SPACE_LAND_SECONDS:
                done(False, "the car did not land on the moon in %.0f s" % SPACE_LAND_SECONDS, "done")
            return False
        away = outposts.ground_distance(outpost.body.radius, outpost.direction, normalize(car.position))
        hard = any(toast[0].startswith("Hard landing") for toast in state.ui.hud.toasts)
        done(spaceflight.car_body() is outpost.body and not hard and away < 50.0,
             "came down %.0f m on the assist and landed on the moon %.0f m from %s's pad, the hover car again"
             % (SPACE_CHECK_DROP, away, outpost.name), "settle")
        return False
    if step == "settle":
        state.vehicle.controls = hovercar.CarControls()
        if since >= 2.0:
            left = vehicle.leave_car()
            s["left"] = left
            s["step"], s["t0"] = "walk", t
        return False
    if step == "walk":
        player = foot.player
        if player is None:
            if since > LOAD_SECONDS:
                done(False, "no player on the moon", "done")
            return False
        if since < 1.0:
            return False
        temperature = survival.here_temperature()
        moon_ok = (foot.bubble.body is s["outpost"].body and temperature is not None
                   and abs(temperature - suit.moon_temperature(dot(world.sun_direction(),
                                                                   normalize(foot.bubble.body.relative(
                                                                       state.view.position))))) < 1e-6)
        if args.shot:
            base, ext = os.path.splitext(args.shot)
            walking.screenshot(base + "_moon" + ext)
        done(s["left"] and moon_ok, "out of the car on the moon, standing, %.0f C" % (temperature or 0.0),
             "vacuum")
        return False
    if step == "vacuum":
        audio = state.sound.audio
        if audio is None:
            done(True, "no sound to check in vacuum", "moon_shower")
            return False
        if foot.player is not None and not foot.player.on_ground and since < VACUUM_STAND:
            return False
        right, _, forward = walking.view_basis()
        camera, before = state.view.position, Counter(state.sound.played)
        sounds.strike("hit", add(camera, scale(forward, VACUUM_NEAR)))
        sounds.strike("hit", add(camera, scale(right, VACUUM_FAR)))
        played = state.sound.played
        contacts, hits = played["contact"] - before["contact"], played["hit"] - before["hit"]
        sounds.strike("hit", add(camera, scale(right, VACUUM_FAR)), held=True)
        held = played["contact"] - before["contact"] - contacts
        grounded = foot.player is not None and foot.player.on_ground
        sounds.strike("hit", add(camera, scale(forward, VACUUM_GROUND)))
        ground = played["contact"] - before["contact"] - contacts - held
        silent = audio.bus_volume("world") == 0.0 and audio.world_lowpass <= acoustics.VACUUM_CUTOFF
        done(silent and contacts == 1 and hits == 0 and held == 1 and ground == (1 if grounded else 0),
             "in vacuum the world bus silent (%.2f, %.0f Hz), a strike %.0f m off heard through the suit (%d), "
             "one %.0f m off not at all (%d) unless the gun holds it (%d), one %.0f m off through the ground (%d)"
             % (audio.bus_volume("world"), audio.world_lowpass, VACUUM_NEAR, contacts, VACUUM_FAR, hits, held,
                VACUUM_GROUND, ground), "moon_shower")
        return False
    if step == "moon_shower":
        moon = s["outpost"].body
        feet = foot.bubble.to_world(foot.player.feet())
        here = normalize(moon.relative(feet))
        centre = events.along_ground(here, 90.0, MOON_SHOWER_CHECK_AWAY, moon.radius)
        when = world.time + MOON_SHOWER_CHECK_LEAD
        large = events.Impact(when, centre, SHOWER_CHECK_MASS, 0.6, 3_000.0, 60.0, 0.0,
                              events.crater_of(SHOWER_CHECK_MASS))
        state.showers.body = moon
        showers.set_shower(events.Shower(when, centre, [large]))
        s["impact"], s["craters"] = large, list(world.craters)
        s["step"], s["t0"] = "moon_strike", t
        return False
    if step == "moon_strike":
        large = s["impact"]
        if "natural" not in s and 0 in state.showers.impact_heights:
            s["natural"] = state.showers.impact_heights[0]
        if state.showers.shower is not None or world.time < large.time + SHOWER_LAND_SECONDS:
            if since > MOON_SHOWER_CHECK_LEAD + 30.0:
                done(False, "the moon's shower did not strike", "home")
            return False
        moon = s["outpost"].body
        dug = [c for c in world.craters if c not in s["craters"]]
        meteorite = state.showers.meteorite
        lies = None
        if meteorite is not None:
            body, position, _ = foot.prop_state(meteorite)
            lies = length(position) - moon.radius - meteorite.rest if body is moon else None
        floor = s.get("natural", 0.0) - large.crater[1]
        ok = (len(dug) == 1 and dug[0].body == moon.index and lies is not None and abs(lies - floor) < 1.0
              and state.showers.body is moon)
        done(ok, "a meteorite struck the moon %.0f m away: its crater on the moon, the meteorite %s m from its floor"
             % (MOON_SHOWER_CHECK_AWAY, "%.2f" % (lies - floor) if lies is not None else "-"), "refine")
        return False
    if step == "refine":
        refinery = next(o for o in state.site.outposts if o.refinery)
        up = refinery.direction
        car.position = scale(up, refinery.body.radius + refinery.height + SHIP_CLEARANCE)
        car.velocity = (0.0, 0.0, 0.0)
        state.vehicle.body = refinery.body
        car.cargo.append({"body": refinery.body.name, "direction": up, "mass": REFINE_CHECK_ICE, "grade": 0.5,
                          "resource": resources.ICE})
        car.fuel = car.params.fuel_capacity - REFINE_CHECK_ROOM
        economy.refine_ice()
        left = sum(i["mass"] for i in car.cargo if i.get("resource") == resources.ICE)
        expected = REFINE_CHECK_ICE - REFINE_CHECK_ROOM * outposts.REFINE_ICE
        ok = abs(car.fuel - car.params.fuel_capacity) < 1e-6 and abs(left - expected) < 1e-6
        done(ok, "at %s's refinery %.0f kg of ice made %.0f kg of fuel, filling the tank, %.0f kg of ice left"
             % (refinery.name, REFINE_CHECK_ICE - left, REFINE_CHECK_ROOM, left), "home")
        car.cargo = [i for i in car.cargo if i.get("resource") != resources.ICE]
        return False
    if step == "home":
        vehicle.enter_car(anywhere=True)
        position, _, _, north = site.ship_pose()
        vehicle.make_car(position, north)
        vehicle.update_ship()
        state.view.chase.reset()
        print("[walker] space mode:\n" + "\n".join("[walker]   " + line for line in s["lines"])
              + "\n[walker]   %s" % ("passed" if s["ok"] else "FAILED"), flush=True)
        check["space_ok"] = s["ok"]
        return True
    print("[walker] space mode:\n" + "\n".join("[walker]   " + line for line in s["lines"])
          + "\n[walker]   FAILED", flush=True)
    os._exit(1)


def check_contract(kind, n, **fields):
    """--check: a contract of `kind` from Base to Base for 100 cr, due in contracts.DEADLINE_DAYS days."""
    home = state.site.home.name
    values = dict({"deadline": contracting.contract_day() + contracts.DEADLINE_DAYS}, **fields)
    return contracts.Contract("check/%d" % n, kind, home, values.pop("destination", home), 100.0, **values)
