"""
Walker: the planet on foot, as a game. The player starts beside the parked
ship at Base, on the planet of the planet example (planet.py), and walks,
runs, jumps and jets with the gravity gun in hand
(shoonyakasha.planet.on_foot). The ship, a hover car, floats
HOVER_HEIGHT above the ground 16 m south of the blocks as a landmark, a
static box in the physics. The outposts on the planet and the moon are
found by the same survey as in planet.py and drawn as pads. Lakes on the
planet within 40 km of the site (shoonyakasha.planet.lakes) are surveyed
with them and kept clear of the outposts' sites and the site's pad; then
the rivers are traced over a height grid around the site
(shoonyakasha.planet.rivers), round those sites and out of the lakes,
before the terrain is built. The car's ground patch takes the lakes' and
the rivers' water as a floor, as it does the sea's.

The flying car: the ship is a hover car (hovercar.py, carview.py). F
within ENTER_REACH of it gets in; the camera then chases it, and W S A
D drive and steer it, Space and left Ctrl move the height it keeps above
the ground (from skimming to hovercar.MAX_HEIGHT), left Shift boosts and
the mouse looks around it. F gets out at its left side once it is slow
and low (hovercar.Car.can_leave); left, it settles on its cushion. Its
ground is a patch of terrain heights measured around it each frame
(shoonyakasha.planet.ground_patch), ahead along its velocity; in the
physics bubble it is a kinematic box that pushes the props, and a ray
ahead of it bumps it off trunks and fixed rocks (bump_car). The bubble
follows it while it is driven.

The HUD (hud.py) is in the Field Slate style (fieldslate.py). On foot
it shows the pinned contract or the objective, a compass with the
outposts, the ship, the waypoint and the rest of the markers, toasts,
the gun's reticle, a prompt for what is in reach (what the gun aims at
and what it is worth, a point of interest's spot, the car), the suit's
reserve with the temperature and the jet's fuel, and the speed, the body
and the hour; in the car its fuel and hold, a height ladder and the
speed and heights; in space mode the transfer planner, the orbit, the
prograde marker and the throttle. The ground around the player is
measured as it explores (explored_map.py) and shown on foot on a round
scope at the bottom right; M opens the full map, in the style of
scifi.py, which frees the mouse and stops the player while open, and on
which a left click sets the waypoint (cleared within WAYPOINT_REACH of
it, or by a right click).

Ore: a loose ore rock (a small one from the scatter, or a chunk of an ore
boulder broken by a strike at rocks.BREAK_SPEED) that comes to rest on an
outpost's pad, not held, sells there (objectives.sale_items, trade.sale),
priced from the body and place it was first seen at. CRATE_STACKS of
CRATE_MASS kg crates stand west of the blocks to throw. Meeting an
objective (objectives.GOALS), a sale and a reached waypoint show as
toasts.

The menu (terminal.py): Escape opens it, and F1 opens it on the
controls; the world's clock and the player stand still behind it. Its
System tab has Resume, Save game, Load game and Quit beside how the game
stands; its Settings tab the settings (config.py), kept in
walker_config.json: the mouse's sensitivity, inverted look, the head bob,
the field of view, the HUD's scale, the window's size (taken at the next
start), the render scale, anti-aliasing, the clouds and autosave; its
Controls tab the controls.

Saving (save.py): F5 or Save game writes the game to walker_save.json
and walker_save.npz: the clock, the player, the crates and loose rocks,
the rocks taken from the scatter, the credits and the outposts' supplies,
the objectives and the game's numbers, the waypoint, the car (whether it
is driven, its fuel and its cargo), the upgrades owned, the day's offset,
the suit's reserve and health, the explored map, the meteorites' craters,
the next shower's start and which rocks are hot. F9 or Load game puts it back; the player comes back LOAD_LIFT above
the ground where it stood once that ground is measured. With autosave on,
the game is saved every AUTOSAVE_SECONDS of play, on getting out of the
car parked on an outpost's pad, on Quit and as the window closes. The
save is of save.VERSION; one of version 1 is migrated as it is read. When a save exists, the walker starts on a menu with
Continue (the save loaded) and New game.

The wind (PlanetWorld.wind_at): the car flies in it at its height above
the ground (`step_car`; on its cushion it does not move it), the props are
dragged through it, and the suit's chill takes its speed CHILL_HEIGHT
above the ground at the camera.

Space mode (G, `enter_space`): at least SPACE_HEIGHT above the ground the
car becomes a little space ship, a flight.Ship of hovercar.space_params
(its tank burnt by its space drive, its cargo aboard) under the planet's
and the moon's gravity, stepped in FLIGHT_STEP steps (`step_space`) with
carview.read_space_controls, translation assist on at first (T switches
it). From WARP_HEIGHT up `,` and `.` change the time warp (WARPS); the
HUD shows the body it is reckoned around, the assist, the throttle, the
warp, the nose hold, the orbit's periapsis and apoapsis and the delta-v
left, and around the planet the transfer planner (`space_lines`); H
cycles the nose hold, prograde, retrograde or off (`hold_nose`). G within HOVER_BACK of the ground, or touching down
slower than LANDED_SPEED, makes it the hover car again over the body
under it (`leave_space`), the planet's or the moon's (`car_body`); on the
moon it hovers, parks and is got out of as on the planet, and the player
walks there. The save keeps the car's body and its space mode.

The car's fuel (hovercar.py's CarParams and fuel): the HUD shows its tank
(FUEL) while driving; dry, it settles onto its cushion and only steers,
and a toast says so. The menu's Car tab shows the fuel, where the car is
and the credits, and fills the tank at the outpost whose pad the car is
on (`fill_tank`, trade.Market.buy_fuel) or calls fuel to it off the
pads, on its cushion, from the nearest outpost on the planet
(`call_fuel`, trade.fuel_call), put in the tank when it is due if the car
is still within CALL_REACH (`update_fuel_call`); the save keeps the fuel.

The car's hold (hovercar.Car.cargo): getting in loads the loose ore
rocks lying in the cargo net behind the parked car (CAR_NET_CENTRE,
CAR_NET_HALF, shown by four posts; `in_net`, `load_cargo`), and the
contracts' crates, each that fits (hovercar.Car.can_take), a rock with
where it was first seen; the rest stay in the net. The menu's Outpost
tab shows the outpost whose pad the car is on, its price for each
resource at grade 1, the car's cargo and what its ore would fetch, and
sells the ore (`sell_car_cargo`, trade.sale); at the refinery
(outposts.REFINERY) it makes the ice in the hold fuel (`refine_ice`). The
save keeps the cargo.

Upgrades (upgrades.py, the "car" and "gun" groups): the menu's Upgrades
tab has a button for each, bought with the car or the player's feet on
an outpost's pad (`at_outpost`, `buy_upgrade`), and a line of its cost or
why it cannot be bought and what it changes, from now to after. The
upgrades owned give the effective car and gun parameters
(`apply_upgrades`), which the car and the gun take at once, as do every
car made and every gun spawned after (OnFoot.gun_params); the save keeps
them.

Points of interest (pois.py), on the planet: `update_pois` looks for the
candidates within POI_REACH (again once the player has moved
POI_WANT_MOVE) and `probe_pois` measures them with the extra probes (the
explored map yields while any wait), which gives each its kind or none.
A found one is discovered within SCAN_RANGE, or within SEEN_RANGE in the
view's cone, shown on the compass and the map by its name (greyed once
looted); one not discovered within POI_REACH as a signal. Within
DRAW_RANGE it is drawn from boxes (pois.LAYOUTS) in a clearing of the
plants and rocks (pois.CLEAR_RADIUS); within PROP_RANGE a pod's crate and
a geode field's rocks are made. E (held for a wreck's spot) uses the spot
in reach (`usable_spot`, `use_spot`): a beacon reveals the map around it
(ExploredMap.reveal) and gives survey data, a wreck's spots and a rig's
crates give parts (a rig's ore rocks too), an arch gives survey data. A
pod's crate settled on an outpost's pad sells (`sell_pods`). Each map
cell walked (not revealed) is CELL_SURVEY of survey data; the Outpost
tab sells it (`sell_survey`, trade.survey_sale). Parts pay for upgrades
with the credits (the Upgrades tab offers WALKER_UPGRADES: the car's,
the gun's and the suit's). The save keeps the points' states, the
parts, the survey data and which crates are pods. Loose ore rocks and
pods sell only once settled (`settled`).

Contracts (contracts.py): the menu's Contracts tab shows the offers of
the outpost whose pad the car or the player is on (contracts.offers for
the day, which turns at dawn, clock.day), each with Accept, and the
active contracts (contracts.MAX_ACTIVE at most), each with Pin, and
Abandon for the pinned one. The pinned contract takes the objective's
place on the card and marks its target on the compass and the map
(`contract_target`). A delivery takes the ore of its resource and grade
that comes to rest on its outpost's pad, or that the car's cargo brings
there (`deliver`), in place of selling it. A courier's crate goes into
the car's hold when the car is on the pad, else beside the player; it is
struck by how much its velocity changes as its speed falls
(`watch_couriers`), and in the hold by the car's bumps (`strike_hold`),
and breaks at contracts.BREAK_DAMAGE, failing the contract. A recovery's
pod is a point of interest of the pod's kind at its target (`place_pod`).
A courier's crate or a pod is delivered settled on its destination's
pad, or in the hold with the car slow on that pad; either goes into the
hold through the net. A survey completes with contracts.SURVEY_SHARE of
the map's cells around its target measured (ExploredMap.share). Done, a
contract pays its reward and raises its outpost's reputation by one;
failed, past its deadline or abandoned, it lowers it by one (no lower
than MIN_REPUTATION). The reputation raises the outpost's offers' rewards
(contracts.REWARD_PER_REPUTATION) and its prices (trade.REPUTATION_PRICE).
The save keeps the active contracts, today's offers accepted, the pin,
the reputations and which crates are couriers'.

The suit (suit.py): `here_temperature` is the air's temperature where the
camera is (on the planet the climate under it, with its height, swinging
with the local hour and chilled by the wind; on the moon by the sun or
the shade). Each frame `update_suit` steps the suit's reserve and the
player's health, sheltered in the car or on an outpost's pad (where the
health also recovers). Low, the screen is tinted frost-blue or
heat-orange and the walk slowed (`suit_effects`, `slow_down`); the card
shows SUIT and TEMP. With the health gone the player comes to on the
pad of the nearest outpost, charged RECOVERY_FEE, what the gun held
dropped where it fell (`wake_at_outpost`). The save keeps the suit.

The day (clock.py): the sun goes round with the world's clock plus the
day's offset (`day_time`; START_HOUR in a new game, kept in the save),
the exposure scaled for its elevation seen from the camera
(clock.set_sun); the readout shows the hour at Base. With the sun below
lamps.LIGHTS_ON_ELEVATION the lights come on (`update_lights`): over each
outpost's pad, the car's headlights (CAR_HEADLIGHTS, CAR_HEADLIGHT_DIP
down) and on foot the suit lamp.

Storms (events.schedule_storm, `update_storms`, PlanetWorld.storm): one at
a time over the planet, planned upwind once the last has ended. In it the
wind is stronger, the clouds close in, a haze closes around the camera
(PlanetWorld.upload_storm) and rain falls (`draw_rain`); the car is
buffeted in a strong wind (`buffet_car`). An outpost it covers is closed
(`closed`): it buys, sells, fuels and offers nothing, announced
STORM_WARNING before by a toast and again once open. While the camera is
in it, the contracts' deadlines are held (contracts.Contract.paused). The
compass marks its centre; the save keeps it ("events").

Meteor showers (events.py, `update_showers`): the next shower is planned
around the camera on the body it is over (`shower_body`; on the moon more
often, its meteors dim and trailless), its impacts' ground measured with the
extra probes (`probe_shower`) and, while its large impact falls in the sea,
a lake or a river, planned again. WARNING_SECONDS before it starts, a toast
gives its bearing and distance, and the compass marks its centre. Each
meteor flies its path in, drawn as glowing quads (`draw_showers`,
viewmodel.Glows): a head at least HEAD_PIXELS across and a trail of its
last TRAIL_SECONDS. As each strikes (`strike`), there is a flash over the
screen (the HUD's flash), within SHAKE_REACH the view shakes (`shaken`),
a plume of dust rises and fades, and the props in the bubble near it are
pushed away (`push`). The large impact digs a crater
(PlanetWorld.set_craters; events.keep keeps events.MAX_CRATERS) and
leaves a meteorite, an ore boulder of iridium marked on the compass while
it lies whole; each small one leaves a rock of iridium (`meteor_rock`).
They glow while hot (events.heat), and while hot the gun will not grab
them (TOO HOT, `too_hot`); a hot meteorite's chunks are as hot
(`split_hot`). The save keeps the craters ("modifiers"), the next
shower's start ("events") and which props are hot.

The player is put down SPAWN_LIFT above the ground EAST_OF_SHIP east of
the ship, facing north, once the terrain is built and the ground there is
measured. Until then the camera waits 2 m above that spot.

Keys:
  W A S D                walk; left Shift run; left Ctrl crouch
  Space                  jump on the ground, jet in the air
  mouse                  look (captured while playing)
  Escape                 the menu: the world stands still
  P                      screenshot into walker_captures/
  right mouse            gravity gun: grab, or pull from farther; again: drop
  left mouse             punt what is held or in front; hold while holding to charge
  R + mouse              turn the held body
  mouse wheel            the hold distance; on the full map, the zoom
  M                      the full map (Escape closes it); left click a waypoint, right click clears it
  F                      into the car near it; out of it when slow and low
  F5, F9                 save, load
  F1                     the menu on the controls

Gamepad: left stick walk, right stick look, A jump and jet, left trigger
grab, right trigger punt, the bumpers the hold distance.

    python walker.py
    python walker.py --check [--shot FILE]   walk, grab and throw a crate, set a waypoint, run the suit
                                             out in the cold, sell ore,
                                             load the car's net and sell its cargo, buy upgrades,
                                             use a point of interest of each kind, sell survey data,
                                             take and end a contract of each kind, bring down a meteor shower,
                                             stand in a storm, pause, save, change the game and load it, drive and fly
                                             the car, hover it on a lake and a river, run it dry and
                                             call fuel, fly it in space mode and land it on the moon,
                                             then night; report and exit
"""

from shoonyakasha import keys
from shoonyakasha.planet.bubble import Bubble
from shoonyakasha.planet.vec import add, scale, sub
from shoonyakasha.planet.world import EXTRA_PROBES

import carview
import hovercar
import save
from outposts import home_outpost
from explored_map import ExploredMap, MapView
from hud import Hud
from terminal import Terminal
from viewmodel import Glows, Viewmodel
from play.core import RAIN_DROPS, args, blocks, bodies, box, engine, foot, pads, save_base, settings, state, world
from play import (checks, contracting, economy, exploring, menus, saving, showers, site, spaceflight, storms, survival,
                  vehicle, walking)

METEOR_DISCS, METEOR_STREAKS = 96, 48
HOLDS = (None, "prograde", "retrograde")


def frame(dt):
    clock = world.time
    world.begin_frame(state["position"])
    site.update_outposts()
    walking.maybe_spawn()
    paused = state["menu"].visible
    state["menu"].update(engine.input)
    elapsed = dt                 # the frame's real time; the world's stands still while paused
    if args.check:
        checks.run_check(elapsed)
    elif state["driving"] and state["space"] is not None and not paused and not state["map_view"].open:
        carview.read_space_controls(engine.input, state["space_controls"], elapsed)
    elif state["driving"] and not paused and not state["map_view"].open:
        vehicle.read_car_controls()
    elif foot.player is not None and not paused and not state["map_view"].open:
        foot.read_controls(engine.input)
        survival.slow_down()
        walking.ignore_held_buttons()
        walking.read_bumpers()
        walking.read_pad_enter()
    if state["driving"] and (paused or state["map_view"].open):
        state["car_controls"] = hovercar.CarControls()
    if paused:
        dt = 0.0
    dt *= spaceflight.time_warp()
    world.time += dt
    latency = world.probe_latency * state["frame_ms"] / 1000.0
    foot.props.fly(world.time - clock, latency)
    active = foot.player is not None and not state["map_view"].open and not paused
    vehicle.step_car(dt)
    viewpoint = state["position"]
    if state["driving"]:
        viewpoint = vehicle.car_position()
        if foot.bubble is None and (state["car"].height or 0.0) < 0.5 * Bubble.RADIUS:
            foot.ensure_bubble(viewpoint)
    foot.step(dt, viewpoint, state["position"], latency, active=active, before_step=vehicle.move_car_body)
    showers.update_showers()
    storms.update_storms(dt)
    foot.props.probe(foot.bubble, EXTRA_PROBES)
    walking.place_eye()
    if state["driving"]:
        vehicle.place_chase(elapsed)
    walking.capture_mouse()
    walking.set_max_levels()

    camera = state["position"]
    basis = showers.shaken(walking.view_basis())
    survival.update_lights(camera)
    foot.request_nearby(camera, bodies)
    world.end_frame(camera, basis, dt, viewpoint=camera,
                    patch=vehicle.car_patch_request() if state["car"] is not None else None)
    ground = world.site_ground()
    for entity, offset in blocks:
        engine.scene.set_position(entity, sub(add(ground, offset), camera))
    pads.draw(camera)
    vehicle.draw_net(camera)
    exploring.draw_poi_scenes(camera)
    showers.draw_showers(camera, basis)
    storms.draw_rain(camera, basis)
    foot.props.draw(foot.bubble, camera)
    site.place_ship(camera)
    walking.place_viewmodel(dt, camera)
    economy.update_fuel_call()
    survival.update_suit(dt)
    exploring.update_pois(dt)
    contracting.update_contracts(dt)
    walking.update_hud(dt)
    walking.update_map(dt)
    walking.update_game()
    saving.autosave(elapsed)
    walking.capture_toast()
    foot.end_frame()
    state["frame_ms"] = 0.9 * state["frame_ms"] + 0.1 * elapsed * 1000.0
    return True


def on_init():
    world.setup()
    site.build_site()
    state["home"] = home_outpost(world)
    state["outposts"] = [state["home"]]
    site.begin_survey()
    site.build_ship_model()
    vehicle.build_net()
    state["viewmodel"] = Viewmodel(engine, box())
    state["car_glows"] = carview.EngineGlows(engine)
    state["meteor_glows"] = Glows(engine, METEOR_DISCS, METEOR_STREAKS)
    state["rain_glows"] = Glows(engine, 0, RAIN_DROPS)
    foot.too_hot = showers.too_hot
    foot.rocks.on_split = showers.split_hot
    state["hud"] = Hud(engine)
    state["hud"].show(False)
    state["map"] = ExploredMap(world)
    state["map_view"] = MapView(engine)
    state["map_view"].show(False)
    state["menu"] = Terminal(engine, "Paused", menus.menu_tabs(), subtitle=menus.menu_subtitle, close_text="ESC  RESUME")
    menus.apply_settings()
    if not args.check and save.exists(save_base):
        try:
            state["last_save"] = save.read(save_base)[0]
        except save.SaveError as error:
            print("[walker] %s" % error, flush=True)
    if state["last_save"] is not None:
        menu = state["menu"]
        state["starting"] = True
        menu.title, menu.close_text = "Walker", ""
        menu.set_tabs([("Start", menus.start_items())])
        menu.show(True)
    _, up, _, north = world.site_frame()
    state["position"] = add(world.site_ground(), scale(up, 5.0))
    state["forward"] = north
    engine.ecs.add_system("WalkerFrame", frame, priority=-1)


def on_key(key):
    """F1 the menu on the controls, M the map, Escape the menu, P a screenshot, F the car (in or out),
    G its space mode (in or out), in space mode T the assist, H the nose hold and , . the time warp, F5 a save, F9 a
    load; the start's menu waits for its buttons."""
    view, menu = state["map_view"], state["menu"]
    if state["starting"]:
        return
    if key == keys.F1 and (foot.player is not None or state["driving"]):
        controls = menu.tab_named("Controls")
        menus.set_paused(not (menu.visible and menu.tab == controls), "Controls")
    elif key == keys.M and (foot.player is not None or state["driving"]) and not menu.visible:
        view.set_open(not view.open)
    elif key == keys.ESCAPE and view.open:
        view.set_open(False)
    elif key == keys.P:
        walking.take_capture()
    elif key == keys.F and not menu.visible and not view.open:
        if state["driving"]:
            vehicle.leave_car()
        else:
            vehicle.enter_car()
    elif key == keys.G and state["driving"] and not menu.visible:
        if state["space"] is None:
            spaceflight.enter_space()
        else:
            spaceflight.leave_space()
    elif key == keys.T and state["space"] is not None and not menu.visible:
        controls = state["space_controls"]
        controls.assist = not controls.assist
        state["hud"].toast("Assist %s" % ("on" if controls.assist else "off"))
    elif key in (keys.COMMA, keys.PERIOD) and state["space"] is not None and not menu.visible:
        spaceflight.set_warp(1 if key == keys.PERIOD else -1)
    elif key == keys.H and state["space"] is not None and not menu.visible:
        state["hold"] = HOLDS[(HOLDS.index(state["hold"]) + 1) % len(HOLDS)]
        state["hud"].toast("Nose hold: %s" % (state["hold"] or "off"))
    elif key == keys.ESCAPE and (foot.player is not None or state["driving"]):
        menus.set_paused(not menu.visible)
    elif key == keys.F5:
        saving.save_game()
    elif key == keys.F9:
        saving.load_game()


def on_mouse_button(button, pressed):
    """On the open full map: a left click sets the waypoint, a right click clears it."""
    view = state["map_view"]
    if not pressed or not view.open:
        return
    if button == keys.MOUSE_RIGHT:
        state["waypoint"] = None
    elif button == keys.MOUSE_LEFT:
        x, y = engine.input.get_mouse_position()
        width, height = view.ui.get_canvas_size(view.canvas)
        fb = engine.framebuffer_size
        if fb[0] > 0 and fb[1] > 0:
            walking.set_waypoint((x * width / fb[0], y * height / fb[1]))


def on_scroll(x_offset, y_offset):
    if state["map_view"].open:
        state["map_view"].zoom_by(y_offset)
    elif foot.player is not None and not state["driving"]:
        foot.gun_controls.distance += y_offset


def on_cleanup():
    """With autosave on, the game saved as the window closes; the settings written."""
    if args.check:
        return
    if settings["autosave"] and foot.player is not None:
        try:
            saving.save_game(quiet=True)
        except Exception as error:      # the engine is shutting down: report it and close anyway
            print("[walker] the game was not saved on closing: %s" % error, flush=True)
    settings.save()


engine.set_on_init(on_init)
engine.set_on_cleanup(on_cleanup)
engine.set_on_key_pressed(on_key)
engine.input.set_on_mouse_scroll(on_scroll)
engine.input.set_on_mouse_button(on_mouse_button)
engine.run()
