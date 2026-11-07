"""
The menu's tabs (terminal.Terminal): pausing, the settings and applying
them, the System and Start tabs, the tabs in order and the subtitle, and
starting or quitting the game.
"""

import os

import config
import save
from play.core import HOVER_BACK, SPACE_HEIGHT, WARP_HEIGHT, args, engine, foot, planet, settings, state, world
from play import contracting, economy, saving, walking

HELP_COLUMNS = [
    [("On foot", [
        (("Mouse",), "Look around"),
        (("W", "A", "S", "D"), "Walk"),
        (("Shift", "Ctrl"), "Run / crouch"),
        (("Space",), "Jump; held in the air: jetpack"),
        (("M",), "Map: wheel to zoom, left click a waypoint, right click to clear"),
        (("E",), "Use what is here: activate, salvage (hold), open, scan"),
    ]), ("Game", [
        (("Esc",), "Pause"),
        (("P",), "Screenshot into walker_captures"),
        (("F5", "F9"), "Save / load"),
        (("F1",), "These controls"),
    ])],
    [("Gravity gun", [
        (("RMB",), "Grab, or pull from farther; again: drop"),
        (("LMB",), "Punt; hold while holding to charge"),
        (("R",), "With the mouse: turn the held body"),
        (("Wheel",), "Hold distance"),
    ]), ("Gamepad", [
        (("pad:LS", "pad:RS"), "Walk / look"),
        (("pad:A",), "Jump and jet; in the car, boost"),
        (("pad:LT", "pad:RT"), "Grab / punt; in the car, down / up"),
        (("pad:LB", "pad:RB"), "Hold distance"),
        (("pad:X",), "Into the car and out"),
    ])],
    [("Flying car", [
        (("F",), "Get in; get out when slow and low"),
        (("W", "S"), "Drive / brake and reverse"),
        (("A", "D"), "Steer"),
        (("Space", "Ctrl"), "Up / down"),
        (("Shift",), "Boost"),
        (("Mouse",), "Look around the car"),
        (("G",), "Space mode, %.0f m up; back to hovering within %.0f m of the ground" % (SPACE_HEIGHT, HOVER_BACK)),
    ]), ("Space mode", [
        (("W", "S"), "Throttle up / down"),
        (("Space", "Ctrl"), "Thrust up / down"),
        (("Arrows",), "Pitch and yaw"),
        (("Q", "E"), "Roll"),
        (("T",), "Translation assist on / off"),
        (("H",), "Nose hold: prograde, retrograde, off"),
        ((",", "."), "Time warp down / up, %.0f km up" % (WARP_HEIGHT / 1000.0)),
    ])],
]


def set_paused(on, tab="System"):
    """The menu shown on `tab`, or hidden; the world stands still while it shows."""
    menu = state["menu"]
    if not on:
        menu.show(False)
        if not args.check:
            settings.save()
        return
    menu.show(True, menu.tab_named(tab))


def apply_settings():
    """The settings (config.py) put into the walker: the look, the head bob,
    the field of view, the HUD's scale, the render scale, anti-aliasing and
    the clouds."""
    foot.mouse_sensitivity = settings["mouse_sensitivity"]
    foot.invert_y = settings["invert_y"]
    foot.head_bob = settings["head_bob"]
    if world.fov != settings["fov"]:
        world.fov = settings["fov"]
        if world.camera is not None:
            engine.scene.set_camera_fov(world.camera, world.fov)
    s = settings["hud_scale"]
    for part in (state["hud"], state["map_view"], state["menu"]):
        part.set_scale(s)
    engine.render_scale = settings["render_scale"]
    world.taa = settings["anti_aliasing"]
    if world.clouds != settings["clouds"]:
        world.clouds = settings["clouds"]
        world.upload_clouds()


def setting(name):
    """(get, set) of a setting; setting it applies the settings."""
    def set_(value):
        settings.set(name, value)
        apply_settings()
    return (lambda: settings[name]), set_


def slider(label, name, show, hint, step=None):
    get, set_ = setting(name)
    lo, hi = config.RANGES[name]
    item = {"slider": label, "min": lo, "max": hi, "get": get, "set": set_, "show": show, "hint": hint}
    if step:
        item["step"] = step
    return item


def toggle(label, name, hint):
    get, set_ = setting(name)
    return {"toggle": label, "get": get, "set": set_, "hint": hint}


def settings_items():
    """The Settings tab's two columns."""
    get, set_ = setting("window")
    window = {"choice": "Window", "options": [(list(w), "%d x %d" % w) for w in config.WINDOWS], "get": get,
              "set": set_, "hint": "The window's size, taken when the walker next starts."}
    return ("columns", [
        [{"heading": "Input"},
         slider("Mouse sensitivity", "mouse_sensitivity", lambda v: "%.2f x" % v,
                "How far the view turns for the mouse's movement.", 0.05),
         toggle("Invert Y", "invert_y", "Moving the mouse or the right stick up looks down."),
         toggle("Head bob", "head_bob", "The view rises and falls a little with each step."),
         {"heading": "Game"},
         toggle("Autosave", "autosave", "Save every two minutes of play and on quitting."),
         ],
        [{"heading": "View"},
         slider("Field of view", "fov", lambda v: "%.0f deg" % v, "The view's height in degrees.", 1.0),
         slider("HUD scale", "hud_scale", lambda v: "%.2f x" % v, "The size of the HUD, the map and this menu.",
                0.05),
         {"heading": "Graphics"},
         window,
         slider("Render scale", "render_scale", lambda v: "%d %%" % round(100.0 * v),
                "The scene renders at this share of the window's size, then is scaled to it.", 0.05),
         toggle("Anti-aliasing", "anti_aliasing",
                "Temporal anti-aliasing: smooths edges and stops foliage crawling."),
         toggle("Clouds", "clouds", "The cloud layer and its shadows."),
         ],
    ])


def save_note():
    data = state["last_save"]
    return save.describe(data) if data is not None else "No save yet"


def system_items():
    """The System tab: the buttons, and a column of how the game stands."""
    def goal():
        current = state["objectives"].current()
        return current.title if current is not None else "All done"
    return ("columns", [
        [{"button": "Resume", "keys": "ESC", "do": lambda: set_paused(False), "hint": "Back to the game."},
         {"button": "Save game", "keys": "F5", "do": lambda: saving.save_game(), "hint": "Save the game as it is now."},
         {"button": "Load game", "keys": "F9", "do": lambda: saving.load_game(), "hint": "Go back to the last save."},
         {"button": "Quit", "do": quit_game, "hint": "Quit the walker; with autosave on, the game is saved first."},
         {"note": save_note}],
        [{"heading": "Status"},
         {"stat": "Objective", "get": goal},
         {"stat": "Objectives done", "get": lambda: "%d / %d" % (state["objectives"].index,
                                                                 len(state["objectives"].goals))},
         {"stat": "Credits", "get": lambda: "%s cr" % format(int(round(state["market"].credits)), ",")},
         {"stat": "Ore sold", "get": lambda: "%s kg" % format(int(round(state["sold_kg"])), ",")},
         {"stat": "Cells explored", "get": lambda: format(walking.game_numbers()["cells"], ",")},
         {"stat": "Waypoints reached", "get": lambda: str(state["waypoints"])},
         {"stat": "Contracts done / failed", "get": lambda: "%d / %d" % (state["contracts_done"],
                                                                         state["contracts_failed"])},
         {"stat": "Outposts found", "get": lambda: str(len(state["outposts"]))}],
    ])


def start_items():
    return [
        {"button": "Continue", "do": lambda: start(True), "hint": "Carry on from the save."},
        {"button": "New game", "do": lambda: start(False),
         "hint": "Start again beside the ship; the next save replaces the old one."},
        {"button": "Quit", "do": lambda: os._exit(0)},
        {"note": save_note},
    ]


def menu_tabs():
    return [("System", system_items()), ("Car", economy.car_items()), ("Outpost", economy.outpost_items()),
            ("Contracts", contracting.contract_items()), ("Upgrades", economy.upgrade_items()), ("Settings", settings_items()),
            ("Controls", ("controls", HELP_COLUMNS))]


def menu_subtitle():
    body = foot.bubble.body.name if foot.bubble is not None else planet.name
    return "WALKER  //  %s  //  T+%s" % (body.upper(), clock_text(world.time))


def clock_text(seconds):
    s = int(seconds)
    return "%02d:%02d:%02d" % (s // 3600, s // 60 % 60, s % 60)


def start(continue_):
    """From the start's menu: the save loaded, or a new game."""
    menu = state["menu"]
    state["starting"] = False
    menu.title, menu.close_text = "Paused", "ESC  RESUME"
    menu.set_tabs(menu_tabs())
    menu.show(False)
    if continue_:
        saving.load_game()


def quit_game():
    if settings["autosave"] and foot.player is not None and not args.check:
        saving.save_game()
    settings.save()
    os._exit(0)
