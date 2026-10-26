"""
Shoonyakasha planet explorer: the planet and its moon of shoonyakasha.planet,
looked at like a digital globe or a planetarium of the system.

Two cameras (shoonyakasha.planet.cameras), switched with Tab:
  - the globe view (GlobeCamera): drag the ground with the left button, zoom
    to the pointer with the wheel, tilt and turn with the right button,
    double-click to fly to a point;
  - the system view (FocusCamera): circle the focused body with the left
    button, zoom with the wheel, click a body to focus it.
The moon moves on its orbit with the world's clock, which runs at the time
warp (Space pauses it). The settings menu (F1) has the sky, the clouds,
the ground, the view and the planet's terrain.

Keys:
    Tab                    globe view / system view
    1 / 2                  the planet / the moon
    Home                   back to where the view started
    Space                  pause / run the clock
    , / .                  time warp down / up (1x to 10,000x)
  globe view:
    left mouse + drag      move the globe
    right mouse + drag     tilt (up / down), turn (sideways)
    mouse wheel            zoom to the pointer
    double-click           fly to the point
    W/A/S/D                move over the ground
    Q / E                  turn
    N                      north up
    U                      look straight down
  system view:
    left mouse + drag      circle the body
    mouse wheel            zoom
    click                  focus the body clicked
    Q / E, R / F           circle the body, up / down
  anywhere:
    F1                     settings menu
    F2                     the controls
    I                      debug card
    P                      screenshot into explorer_captures/

    python explorer.py
    python explorer.py --capture DIR [--shots NAME...]   screenshots of the views, then exit
"""

import argparse
import math
import os
import random
import time

import shoonyakasha as sk
from shoonyakasha import keys
from shoonyakasha.planet import cameras
from shoonyakasha.planet import settings as planet_settings
from shoonyakasha.planet import world as planet_world
from shoonyakasha.planet.vec import add, dot, length, normalize, scale, sub
from shoonyakasha.toolkit.overlay import Card, HelpOverlay, HintBar, SettingsMenu

HERE = os.path.dirname(os.path.abspath(__file__))
CAPTURES = os.path.join(HERE, "explorer_captures")
WARPS = (1, 10, 100, 1_000, 10_000)
START_RANGE = 400_000.0           # metres from the site the globe view starts at
START_TILT = math.radians(30.0)
ORBIT_POINTS = planet_world.ORBIT_POINTS

parser = argparse.ArgumentParser(description="The planet and its moon, looked at like a globe or a planetarium")
parser.add_argument("--capture", metavar="DIR", help="save screenshots of the views and exit")
parser.add_argument("--shots", nargs="+", metavar="NAME", help="with --capture: only the shots whose names contain one of these")
args = parser.parse_args()

planet_world.compile_shaders()
engine = sk.Engine(title="Shoonyakasha - planet explorer", width=1600, height=900,
                   pipeline_json_path=str(planet_world.PIPELINE))
world = planet_world.PlanetWorld(engine, pad=False)

state = {
    "globe": None,
    "focus": None,
    "mode": "globe",              # "globe" or "focus"
    "warp": 0,                    # index into WARPS
    "paused": False,
    "moon_orbit": True,           # the moon's orbit drawn in the system view
    "hints": True,
    "debug_text": False,
    "frame_ms": 16.0,
    "cards": None,
    "hint_bar": None,
    "menu": None,
    "help": None,
    "terrain_edit": None,
}


def camera():
    return state[state["mode"]]


def distance_text(metres):
    if abs(metres) < 10_000.0:
        return "%.0f m" % metres
    return "%.1f km" % (metres / 1000.0) if abs(metres) < 1e7 else "%.0f km" % (metres / 1000.0)


def start_view():
    """The globe view over the site from START_RANGE, tilted START_TILT, north up."""
    _, up, _, _ = world.site_frame()
    state["mode"] = "globe"
    state["globe"].look_at(world.planet, up, range_=START_RANGE, heading=0.0, tilt=START_TILT)


def to_body(index):
    """The planet (0) or the moon (1): in the globe view, over the point of
    it facing the camera; in the system view, focused."""
    body = world.bodies[index] if index < len(world.bodies) else world.planet
    if state["mode"] == "focus":
        state["focus"].focus(body)
        return
    globe = state["globe"]
    facing = normalize(sub(globe.position, body.centre()))
    if body is globe.body:
        globe.fly_to(facing, range_=2.0 * body.radius, tilt=0.0)
    else:
        globe.look_at(body, facing, range_=2.0 * body.radius, tilt=0.0)


def switch_mode():
    """Globe view to system view focused on the same body, from where the
    camera is; and back, over the point below the camera."""
    globe, focus = state["globe"], state["focus"]
    if state["mode"] == "globe":
        body = globe.body
        offset = sub(globe.position, body.centre())
        along, across, normal = focus.axes()
        focus.body, focus.transition = body, None
        focus.distance = length(offset)
        direction = normalize(offset)
        focus.pitch = math.asin(max(-1.0, min(1.0, dot(direction, normal))))
        focus.yaw = math.atan2(dot(direction, across), dot(direction, along))
        focus.clamp()
        focus.place()
        world.extra_probes = []
        state["mode"] = "focus"
    else:
        body = focus.body
        offset = sub(focus.position, body.centre())
        globe.look_at(body, offset, range_=max(length(offset) - body.radius, globe.MIN_RANGE), tilt=0.0)
        state["mode"] = "globe"


def moon_orbit_points():
    """The moon's circle around the planet's centre, closed."""
    moon = world.physics.moon
    if moon is None:
        return []
    return [add(scale(moon.u, moon.distance * math.cos(a)), scale(moon.v, moon.distance * math.sin(a)))
            for a in (2.0 * math.pi * k / (ORBIT_POINTS - 1) for k in range(ORBIT_POINTS))]


# ── Overlays ───────────────────────────────────────────────────────────────

def current_hints():
    if state["help"].visible:
        return [(("F2",), "Close the controls")]
    if state["menu"].visible:
        return [(("F1",), "Close settings"), (("Wheel",), "Scroll"), (("F2",), "Controls")]
    if state["mode"] == "globe":
        return [(("LMB",), "Drag the globe"), (("RMB",), "Tilt, turn"), (("Wheel",), "Zoom"),
                (("Double-click",), "Fly there"), (("N",), "North up"), (("U",), "Look down"),
                (("Tab",), "System view"), (("F1",), "Settings"), (("F2",), "Controls")]
    return [(("LMB",), "Circle"), (("Wheel",), "Zoom"), (("Click",), "Focus a body"), (("1", "2"), "Planet, moon"),
            (("Space",), "Pause" if not state["paused"] else "Run"), ((",", "."), "Time warp"),
            (("Tab",), "Globe view"), (("F1",), "Settings"), (("F2",), "Controls")]


HELP_COLUMNS = [
    [("Globe view", [
        (("LMB",), "Drag the globe"),
        (("RMB",), "Tilt (up, down), turn"),
        (("Wheel",), "Zoom to the pointer"),
        (("Double-click",), "Fly to the point"),
        (("W", "A", "S", "D"), "Move over the ground"),
        (("Q", "E"), "Turn"),
        (("N",), "North up"),
        (("U",), "Look straight down"),
    ])],
    [("System view", [
        (("LMB",), "Circle the body"),
        (("Wheel",), "Zoom"),
        (("Click",), "Focus the body clicked"),
        (("Q", "E"), "Circle the body"),
        (("R", "F"), "Up / down"),
    ]), ("Both", [
        (("Tab",), "Globe view / system view"),
        (("1", "2"), "The planet / the moon"),
        (("Home",), "Back to the start"),
    ])],
    [("Time and display", [
        (("Space",), "Pause / run the clock"),
        ((",", "."), "Time warp down / up"),
        (("F1",), "Settings"),
        (("F2",), "These controls"),
        (("I",), "Debug card"),
        (("P",), "Screenshot"),
    ])],
]


def latitude_longitude(direction):
    """Degrees north of the equator and east of the +Z axis about +Y."""
    return math.degrees(math.asin(max(-1.0, min(1.0, direction[1])))), math.degrees(math.atan2(direction[0], direction[2]))


def update_cards():
    view_card, debug_card = state["cards"]
    if state["help"].visible:
        view_card.show(False)
        debug_card.show(False)
        return
    warp = "paused" if state["paused"] else "x%d" % WARPS[state["warp"]]
    cam = camera()
    body = cam.body
    altitude = world.altitude(cam.position)
    if state["mode"] == "globe":
        lat, lon = latitude_longitude(cam.target)
        view_card.update("GLOBE VIEW", [
            ("BODY", body.name),
            ("LATITUDE", "%+.3f deg" % lat),
            ("LONGITUDE", "%+.3f deg" % lon),
            ("GROUND THERE", distance_text(cam.target_height)),
            ("RANGE", distance_text(cam.range)),
            ("CAMERA ABOVE " + world.datum_name(cam.position).upper(), distance_text(altitude)),
            ("HEADING", "%.0f deg" % (math.degrees(cam.heading) % 360.0)),
            ("TILT", "%.0f deg" % math.degrees(cam.tilt)),
            ("TIME WARP", warp),
        ])
    else:
        view_card.update("SYSTEM VIEW", [
            ("FOCUS", body.name),
            ("FROM ITS CENTRE", distance_text(cam.distance)),
            ("ABOVE " + world.datum_name(cam.position).upper(), distance_text(altitude)),
            ("TIME WARP", warp),
            ("CLOCK", "%.1f h" % (world.time / 3600.0)),
        ])
    view_card.show(True)
    debug_card.show(state["debug_text"])
    if not state["debug_text"] or world.planet.tree is None:
        return
    trees = [b.tree for b in world.bodies if b.tree is not None]
    selected = sum(len(t.selected) for t in trees)
    debug_card.update("DEBUG", [
        ("FRAME", "%.1f ms" % state["frame_ms"]),
        ("CHUNKS DRAWN", "%d of %d" % (selected - world.chunks_culled, selected)),
        ("POOL", "%d / %d" % (world.pool.used, planet_world.SLOTS)),
        ("MISSING", "%d" % sum(len(t.missing) for t in trees)),
        ("DEEPEST LEVEL", "%d" % max(n.level for t in trees for n in t.selected)),
    ])


def menu_items():
    return [
        {"heading": "Simulation"},
        {"slider": "Time warp", "min": 0, "max": len(WARPS) - 1, "get": lambda: state["warp"],
         "set": lambda v: state.update(warp=int(round(v))), "show": lambda v: "x%d" % WARPS[int(round(v))],
         "keys": (",", "."), "hint": "How much faster than real time the moon moves and the clouds drift."},
        {"toggle": "Paused", "get": lambda: state["paused"], "set": lambda on: state.update(paused=on),
         "keys": ("Space",), "hint": "The world's clock stopped: the moon and the clouds stay where they are."},
    ] + planet_settings.sky_items(world) + planet_settings.cloud_items(world) + planet_settings.ground_items(world) + [
        {"heading": "Display"},
        {"toggle": "Button hints", "get": lambda: state["hints"], "set": lambda on: state.update(hints=on),
         "hint": "The bar of keys and buttons along the bottom of the screen."},
        {"toggle": "Moon's orbit", "get": lambda: state["moon_orbit"],
         "set": lambda on: state.update(moon_orbit=on), "hint": "The moon's circle around the planet, in the system view."},
        {"toggle": "Debug text", "get": lambda: state["debug_text"], "set": lambda on: state.update(debug_text=on),
         "keys": ("I",), "hint": "The debug card: frame time, chunks and the pool."},
    ] + planet_settings.view_items(world) + planet_settings.planet_items(world, state["terrain_edit"], new_seed)


def new_seed():
    world.new_seed([random.uniform(-100.0, 100.0) for _ in range(3)])
    state["terrain_edit"].reset()


# ── Per frame ──────────────────────────────────────────────────────────────

def frame(dt):
    cam = camera()
    world.begin_frame(cam.position)
    if not state["paused"]:
        world.time += dt * WARPS[state["warp"]]
    ui_busy = engine.ui.pointer_over_ui or state["help"].visible
    cam.update(dt, active=not ui_busy)
    world.end_frame(cam.position, cam.basis, dt, warp=0.0 if state["paused"] else WARPS[state["warp"]])
    world.set_orbit_path(moon_orbit_points() if state["mode"] == "focus" and state["moon_orbit"] else [])
    state["menu"].update()
    state["hint_bar"].show(state["hints"])
    if state["hints"]:
        state["hint_bar"].update(current_hints())
    state["frame_ms"] = 0.9 * state["frame_ms"] + 0.1 * dt * 1000.0
    update_cards()
    return True


def on_key(key):
    if key == keys.TAB:
        switch_mode()
    elif key == keys.NUM_1:
        to_body(0)
    elif key == keys.NUM_2:
        to_body(1)
    elif key == keys.HOME:
        start_view()
    elif key == keys.SPACE:
        state["paused"] = not state["paused"]
    elif key in (keys.COMMA, keys.PERIOD):
        state["warp"] = max(0, min(len(WARPS) - 1, state["warp"] + (1 if key == keys.PERIOD else -1)))
    elif key == keys.N and state["mode"] == "globe":
        state["globe"].north_up()
    elif key == keys.U and state["mode"] == "globe":
        state["globe"].straight_down()
    elif key == keys.F1:
        state["menu"].show(not state["menu"].visible)
    elif key == keys.F2:
        state["help"].show(not state["help"].visible)
    elif key == keys.I:
        state["debug_text"] = not state["debug_text"]
    elif key == keys.P:
        os.makedirs(CAPTURES, exist_ok=True)
        path = os.path.join(CAPTURES, time.strftime("explorer_%Y%m%d_%H%M%S.png"))
        if engine.capture_screenshot(path):
            print("saved", path, flush=True)


def on_scroll(x_offset, y_offset):
    if state["menu"].pointer_over or state["help"].visible:
        return
    camera().scroll(y_offset)


# ── Captures ───────────────────────────────────────────────────────────────

def globe_at(body_name, direction_from, range_, heading, tilt):
    """The globe view on the body, over the direction `direction_from(body)`."""
    def setup():
        body = next(b for b in world.bodies if b.name == body_name)
        state["mode"] = "globe"
        state["globe"].look_at(body, direction_from(body), range_=range_, heading=math.radians(heading),
                               tilt=math.radians(tilt))
    return setup


def focus_on(body_name, distance_radii, yaw, pitch):
    def setup():
        body = next(b for b in world.bodies if b.name == body_name)
        focus = state["focus"]
        state["mode"] = "focus"
        focus.body, focus.transition = body, None
        focus.distance = distance_radii * body.radius
        focus.yaw, focus.pitch = math.radians(yaw), math.radians(pitch)
        focus.spin = (0.0, 0.0)
        focus.place()
        world.extra_probes = []
    return setup


def site_up(body):
    return world.site_frame()[1]


def mountain_direction(body):
    """60 km from the site at 67.5 degrees from east: a peak of the default terrain."""
    _, up, east, north = world.site_frame()
    azimuth = math.radians(67.5)
    return normalize(add(up, scale(add(scale(east, math.cos(azimuth)), scale(north, math.sin(azimuth))),
                                   60_000.0 / world.planet.radius)))


def towards_sun(body):
    return world.sun_direction()


class CaptureTour:
    """--capture: each shot in turn, a screenshot once the world has
    settled (or after MAX_FRAMES), then exit."""

    MIN_FRAMES, MAX_FRAMES = 20, 600
    SHOTS = [
        ("globe_start", start_view),
        ("globe_mountains", globe_at("planet", mountain_direction, 6_000.0, 220.0, 70.0)),
        ("globe_coast_low", globe_at("planet", site_up, 1_500.0, 300.0, 80.0)),
        ("globe_whole_planet", globe_at("planet", site_up, 2_000_000.0, 0.0, 0.0)),
        ("globe_moon_craters", globe_at("moon", towards_sun, 4_000.0, 0.0, 45.0)),
        ("focus_planet", focus_on("planet", 3.0, 30.0, 20.0)),
        ("focus_system", focus_on("planet", 9.0, 60.0, 35.0)),
        ("focus_moon", focus_on("moon", 3.0, 200.0, 15.0)),
        ("menu", lambda: (start_view(), state["menu"].show(True))),
        ("controls", lambda: (start_view(), state["help"].show(True))),
    ]

    def __init__(self, directory, names=None):
        os.makedirs(directory, exist_ok=True)
        self.directory = directory
        self.shots = [s for s in self.SHOTS if not names or any(n in s[0] for n in names)]
        self.shot = 0
        self.frames = 0
        self.captured = None

    def __call__(self, dt):
        if world.planet.tree is None:
            return True
        name, setup = self.shots[self.shot]
        self.frames += 1
        if self.frames == 1:
            state["menu"].show(False)
            state["help"].show(False)
            state["paused"] = True
            setup()
        elif self.captured is None:
            done = self.frames >= self.MIN_FRAMES and world.settled()
            if done or self.frames >= self.MAX_FRAMES:
                path = os.path.join(self.directory, name + ".png")
                ok = engine.capture_screenshot(path)
                print("[explorer] %s after %d frames%s" % (path if ok else "screenshot failed", self.frames,
                                                         "" if done else " (not settled)"), flush=True)
                self.captured = self.frames
        elif self.frames > self.captured + 2:
            self.shot += 1
            self.frames = 0
            self.captured = None
            if self.shot == len(self.shots):
                os._exit(0)        # the engine has no quit call
        return True


def on_init():
    world.setup()
    canvas = engine.ui.create_canvas(reference_size=(1600, 900))
    state["cards"] = (Card(engine.ui, canvas, (0, 0), 10), Card(engine.ui, canvas, (0, 1), 6, margin=(24, 76)))
    state["hint_bar"] = HintBar(engine.ui, canvas)
    state["terrain_edit"] = planet_settings.TerrainEdit(world)
    state["menu"] = SettingsMenu(engine, "Settings", menu_items())
    state["help"] = HelpOverlay(engine, "CONTROLS", HELP_COLUMNS, footer="F2 to close")
    state["globe"] = cameras.GlobeCamera(world)
    state["focus"] = cameras.FocusCamera(world)
    start_view()
    # Before the transform system (priority 0), so this frame's positions are used.
    engine.ecs.add_system("ExplorerFrame", frame, priority=-1)
    if args.capture:
        engine.ecs.add_system("ExplorerCapture", CaptureTour(args.capture, args.shots), priority=-2)
    print(__doc__.split("Keys:")[1].split("    python explorer.py")[0].rstrip(), flush=True)


engine.set_on_init(on_init)
engine.set_on_key_pressed(on_key)
engine.input.set_on_mouse_scroll(on_scroll)
engine.run()
