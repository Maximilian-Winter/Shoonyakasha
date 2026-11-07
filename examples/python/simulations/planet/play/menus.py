"""
The System screen (Esc, the only pause) and the start's screen
(slate.SystemScreen), the settings and applying them, and starting or
quitting the game.

The System screen's entries: Resume, Save (its section: the last save,
and Save now), Load (the last save, and Load), Settings, Controls, Quit
to main menu and Quit to desktop; while Resume is chosen, its pane gives
how the game stands. The start's screen (`show_start`), at the start when
a save exists and after Quit to main menu: a card of the save and
Continue when there is one, New game, Settings and Quit; the world stands
still behind it.
"""

import os

import config
import fieldslate as fs
import slate
from play.core import HOVER_BACK, SPACE_HEIGHT, WARP_HEIGHT, args, engine, foot, settings, state, world
from play import apps, saving, walking

HELP_COLUMNS = [
    [("On foot", [
        (("Mouse",), "Look around"),
        (("W", "A", "S", "D"), "Walk"),
        (("Shift", "Ctrl"), "Run / crouch"),
        (("Space",), "Jump; held in the air: jetpack"),
        (("M",), "Map: wheel to zoom, left click a waypoint, right click to clear"),
        (("E",), "Use what is here: activate, salvage (hold), open, scan"),
    ]), ("Game", [
        (("Tab",), "The slate: Q and E switch its apps"),
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


def set_paused(on, section="Resume"):
    """The System screen shown on `section`, the slate closed; or hidden. The world stands still while it shows."""
    system = state.ui.system
    if not on:
        system.show(False)
        if not args.check:
            settings.save()
        return
    state.ui.slate.close(at_once=True)
    system.show(True, section)


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
    for part in (state.ui.hud, state.ui.map_view, state.ui.slate, state.ui.system, state.ui.start):
        if part is not None:
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
    """The settings in two columns."""
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
         slider("HUD scale", "hud_scale", lambda v: "%.2f x" % v, "The size of the HUD, the map and the slate.",
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


def status_items():
    """How the game stands."""
    def goal():
        current = state.progress.objectives.current()
        return current.title if current is not None else "All done"
    return [{"stat": "Objective", "get": goal},
            {"stat": "Objectives done", "get": lambda: "%d / %d" % (state.progress.objectives.index,
                                                                    len(state.progress.objectives.goals))},
            {"stat": "Credits", "get": lambda: fs.credits(state.economy.market.credits)},
            {"stat": "Ore sold", "get": lambda: fs.mass(state.economy.sold_kg)},
            {"stat": "Cells explored", "get": lambda: format(walking.game_numbers()["cells"], ",")},
            {"stat": "Waypoints reached", "get": lambda: str(state.progress.waypoints)},
            {"stat": "Contracts done / failed", "get": lambda: "%d / %d" % (state.contracts.done,
                                                                            state.contracts.failed)},
            {"stat": "Outposts found", "get": lambda: str(len(state.site.outposts))}]


def system_screen():
    status = slate.items_section("How it stands", status_items)
    entries = [("Resume", "Esc", lambda: set_paused(False)),
               ("Save", "F5", apps.saves_section("Save", ("Save now", lambda: saving.save_game()),
                                                 "F5 saves at any time; F9 loads the last save.")),
               ("Load", "F9", apps.saves_section("Load", ("Load", lambda: saving.load_game()),
                                                 "Loading replaces the world you're in with the save's.")),
               ("Settings", None, slate.items_section("Settings", settings_items())),
               ("Controls", None, slate.items_section("Controls", ("controls", HELP_COLUMNS))),
               "divider",
               ("Quit to main menu", None, quit_to_menu),
               ("Quit to desktop", None, quit_game)]
    return slate.SystemScreen(engine, "Paused · the world is stopped", "System", entries, note=apps.autosave_note,
                              idle=status)


def start_screen():
    def card(surface, left, top, width):
        data = state.session.last_save
        return apps.save_card(surface, left, top, width, data) if data is not None else 0.0
    return slate.SystemScreen(engine, "A planet, a moon, a hover car", "Shoonyakasha", start_entries(), card=card,
                              sort_order=4)


def start_entries():
    """Continue when a save exists, New game, Settings and Quit."""
    found = [("Continue", "Enter", lambda: start(True))] if state.session.last_save is not None else []
    return found + [("New game", None if found else "Enter", lambda: start(False)),
                    ("Settings", None, slate.items_section("Settings", settings_items())),
                    "divider",
                    ("Quit", None, lambda: os._exit(0))]


def show_start():
    """The start's screen shown, its entries as the save stands, the first chosen; no player until it is left."""
    screen = state.ui.start
    screen.entries, screen.choice = start_entries(), 0
    state.session.starting = True
    screen.show(True)


def start(continue_):
    """From the start's screen: the save loaded, or a new game (saving.new_game)."""
    state.session.starting = False
    state.ui.start.show(False)
    if continue_:
        saving.load_game()
    else:
        saving.new_game()


def quit_to_menu():
    """The game saved when autosave is on (as `quit_game`), then the start's screen over the world."""
    if settings["autosave"] and (foot.player is not None or state.vehicle.driving) and not args.check:
        saving.save_game(quiet=True)
    set_paused(False)
    show_start()


def quit_game():
    if settings["autosave"] and foot.player is not None and not args.check:
        saving.save_game()
    settings.save()
    os._exit(0)
