"""The viewer's screen: readings, explanation cards, key hints, the controls
sheet and the settings menu (shoonyakasha.toolkit.overlay).

    top left        a card of readings: time, speed, counts of events
    bottom left     the explanation card of the scene's story (story.py)
    bottom          the key hints
    F1              the settings menu, down the right side
    F2              the controls sheet
    P               a screenshot into nuclear_captures/

The menu reads and writes `settings`, a dict the viewer keeps: what the
scenes are built with (applied when one starts), the rules' constants
(applied at once) and what is drawn.
"""

import time
from pathlib import Path

import shoonyakasha as sk
from shoonyakasha.toolkit.overlay import Card, HelpOverlay, HintBar, Narration, SettingsMenu

import story

REFERENCE = (1600, 900)
CAPTURES = Path(__file__).resolve().parent / "nuclear_captures"
TITLES = {"fission": "FISSION", "chain": "CHAIN REACTION", "fusion": "FUSION"}

HELP = [
    [("Scenes", [(("1",), "Fission: a neutron into uranium-235"),
                 (("2",), "Chain reaction: 64 uranium nuclei"),
                 (("3",), "Fusion: deuterium and tritium"),
                 (("R",), "Start the scene again"),
                 (("Space",), "Pause"),
                 (("+", "-"), "Faster, slower")]),
     ("Camera", [(("RMB",), "Hold to turn the camera"),
                 (("W", "A", "S", "D"), "Fly"),
                 (("Q", "E"), "Down, up")])],
    [("Explanations", [(("[", "]"), "Previous, next card"),
                       (("H",), "Show or hide the cards")]),
     ("Panels", [(("F1",), "Settings"),
                 (("F2",), "These controls"),
                 (("P",), "Screenshot into nuclear_captures")])],
]
HINTS = [(("1", "2", "3"), "Scenes"), (("R",), "Restart"), (("Space",), "Pause"), (("+", "-"), "Speed"),
         (("F1",), "Settings"), (("F2",), "Controls"), (("P",), "Screenshot")]
CARD_HINTS = [(("[", "]"), "Back, next"), (("H",), "Hide")]
CONTINUE_HINTS = [(("Space",), "Continue")] + CARD_HINTS


class Interface:
    def __init__(self, engine, state, settings, load, restart, apply):
        """`load(scene)` starts a scene, `restart()` the current one again,
        and `apply()` hands the engine what `settings` say it should draw."""
        self.engine = engine
        self.state = state
        self.settings = settings
        self.load = load
        self.restart = restart
        self.apply = apply
        ui = engine.ui
        canvas = ui.create_canvas(reference_size=REFERENCE)
        self.readings = Card(ui, canvas, (0, 0), 10)
        self.narration = Narration(ui, canvas, (0, 1), max_lines=7, margin=(24, 72))
        self.hints = HintBar(ui, canvas)
        self.hints.update(HINTS)
        self.help = HelpOverlay(engine, "CONTROLS", HELP, footer="F2 to close", reference_size=REFERENCE)
        self.menu = SettingsMenu(engine, "Settings", self.menu_items(), reference_size=REFERENCE)
        self.story = None
        self.shot_in = 0         # frames until a screenshot asked for from the menu is taken

    def menu_items(self):
        s, state = self.settings, self.state

        def setting(key, kind=float):
            return {"get": lambda: s[key], "set": lambda v: s.update({key: kind(round(v) if kind is int else v)})}

        def display(key):
            return {"get": lambda: s[key], "set": lambda on: (s.update({key: on}), self.apply())}

        return [
            {"heading": "Simulation"},
            {"button": "Screenshot", "do": self.screenshot_from_menu, "keys": ("P",),
             "hint": "Close this menu and save the view into nuclear_captures."},
            {"button": "Fission", "do": lambda: self.load("fission"), "keys": ("1",),
             "hint": "A slow neutron into a uranium-235 nucleus."},
            {"button": "Chain reaction", "do": lambda: self.load("chain"), "keys": ("2",),
             "hint": "A cube of uranium nuclei and one neutron."},
            {"button": "Fusion", "do": lambda: self.load("fusion"), "keys": ("3",),
             "hint": "Two light nuclei flying at each other."},
            {"button": "Restart", "do": self.restart, "keys": ("R",),
             "hint": "Start the scene again, with the scene settings below."},
            {"toggle": "Paused", "get": lambda: state["paused"], "set": lambda on: state.update(paused=on),
             "keys": ("Space",)},
            {"slider": "Time scale", "min": 0.1, "max": 4.0, "get": lambda: state["time_scale"],
             "set": lambda v: state.update(time_scale=v), "show": lambda v: "x%.2f" % v, "keys": ("+", "-"),
             "hint": "Simulation time per second of real time; 1 is 20 time units a second."},

            {"heading": "Scenes (on restart)"},
            dict(setting("fission_speed"), slider="Neutron speed, fission", min=0.2, max=3.0, format="%.2f",
                 hint="How fast the neutron flies at the uranium nucleus."),
            dict(setting("chain_speed"), slider="Neutron speed, chain", min=0.2, max=3.0, format="%.2f",
                 hint="How fast the first neutron flies at the corner nucleus."),
            dict(setting("spacing", int), slider="Chain spacing", min=21, max=40, show=lambda v: "%d" % round(v),
                 hint="Distance between the uranium nuclei. At 30 most neutrons escape and the chain dies out."),
            dict(setting("lattice", int), slider="Chain cube side", min=2, max=4, show=lambda v: "%d" % round(v),
                 hint="Nuclei along each side of the cube: 8, 27 or 64 in all."),
            {"toggle": "Fusion: deuterium + deuterium", "get": lambda: s["reaction"] == "dd",
             "set": lambda on: s.update(reaction="dd" if on else "dt"),
             "hint": "Two deuterium nuclei in place of deuterium and tritium."},
            dict(setting("fusion_speed"), slider="Fusion closing speed", min=0.1, max=2.0, format="%.2f",
                 hint="How fast the two nuclei approach each other. Below about 0.4 they bounce off."),

            {"heading": "Rules (at once)"},
            {"toggle": "Capture stretches the nucleus", "get": lambda: s["capture_rule"],
             "set": lambda on: s.update(capture_rule=on),
             "hint": "The rule that turns a captured neutron's heat into stretching. Without it, uranium "
                     "keeps the neutron and does not split."},
            dict(setting("capture_radius"), slider="Capture radius", min=0.0, max=14.0, format="%.1f",
                 hint="Free neutrons this close to a uranium nucleus's centre are turned toward it. 0 turns "
                      "the rule off."),
            dict(setting("emission_speed"), slider="Emitted neutron speed", min=0.5, max=4.0, format="%.2f",
                 hint="The speed at which a fragment gives off its neutron, if its heat can pay for it."),
            dict(setting("radiate"), slider="Heat radiated", min=0.0, max=1.0, format="%.2f",
                 hint="The share of a fragment's remaining heat that leaves as radiation (the flash)."),

            {"heading": "Display"},
            dict(display("explanations"), toggle="Explanation cards", keys=("H",)),
            {"toggle": "Pause at each new card", "get": lambda: s["pause_on_cards"],
             "set": lambda on: s.update(pause_on_cards=on),
             "hint": "Stop the simulation when a card appears, until Space; takes effect on restart."},
            dict(display("readings"), toggle="Readings"),
            dict(display("key_hints"), toggle="Key hints"),
            dict(display("trails"), toggle="Neutron trails"),
            {"slider": "Trail length", "min": 1.0, "max": 20.0, "get": lambda: s["trail_time"],
             "set": lambda v: (s.update(trail_time=v), self.apply()), "format": "%.1f",
             "hint": "Time units of path drawn behind each free neutron."},
            dict(display("flashes"), toggle="Flashes"),
        ]

    def screenshot(self):
        """Save the view as it is drawn now into CAPTURES, under the time."""
        CAPTURES.mkdir(exist_ok=True)
        path = CAPTURES / time.strftime("nuclear_%Y%m%d_%H%M%S.png")
        print("screenshot ->", path if self.engine.capture_screenshot(str(path)) else "failed")

    def screenshot_from_menu(self):
        """Close the menu, then take the screenshot once it is off the screen."""
        self.menu.show(False)
        self.shot_in = 2

    def new_scene(self, scene):
        """Start the scene's story; with pause_on_cards and the cards shown, pause on its first card."""
        pausing = self.settings["pause_on_cards"] and self.settings["explanations"]
        self.story = story.Story(story.script(scene, self.settings["reaction"]),
                                 min_seconds=0.0 if pausing else story.MIN_SECONDS)
        if pausing:
            self.state["paused"] = True

    def key(self, key):
        """Handle `key` if it is the interface's; returns whether it was."""
        if key == sk.keys.F1:
            self.menu.show(not self.menu.visible)
        elif key == sk.keys.F2:
            self.help.show(not self.help.visible)
        elif key == sk.keys.P:
            self.screenshot()
        elif key == sk.keys.H:
            self.settings["explanations"] = not self.settings["explanations"]
        elif key == sk.keys.LEFT_BRACKET and self.story:
            self.story.previous()
        elif key == sk.keys.RIGHT_BRACKET and self.story:
            self.story.next()
        else:
            return False
        return True

    @property
    def pointer_over(self):
        return self.menu.pointer_over

    def update(self, seconds):
        """Once a frame, after the simulation's counts are current."""
        state, s = self.state, self.settings
        if self.shot_in:
            self.shot_in -= 1
            if not self.shot_in:
                self.screenshot()
        self.menu.update()
        self.hints.show(s["key_hints"])

        self.readings.show(s["readings"])
        if s["readings"]:
            title = TITLES[state["scene"]]
            if state["scene"] == "fusion":
                title += " · D + D" if state["reaction"] == "dd" else " · D + T"
            rows = [("TIME", "%.0f units" % state["sim_time"]),
                    ("SPEED", "paused" if state["paused"] else "x%.2f" % state["time_scale"]),
                    ("NUCLEONS", "%d" % state["count"]),
                    ("EVENTS",)]
            if state["scene"] == "fusion":
                rows += [("REACTIONS", "%d" % state["reaction_count"]),
                         ("LATEST", state["last_reaction"] or "none yet")]
            else:
                rows += [("CAPTURES", "%d" % state["captures"]),
                         ("SPLITS", "%d" % state["events"]),
                         ("NEUTRONS GIVEN OFF", "%d" % state["neutrons"])]
            rows.append(("FREE NEUTRONS", "%d" % state["free_neutrons"]))
            self.readings.update(title, rows)

        if self.story is None:
            return
        # The story moves on only while the simulation runs.
        reached = not state["paused"] and self.story.update(
            {"time": state["sim_time"], "captures": state["captures"], "splits": state["events"],
             "neutrons": state["neutrons"], "reactions": state["reaction_count"],
             "capture_time": state["capture_time"], "split_time": state["split_time"]}, seconds)
        if reached and s["pause_on_cards"] and s["explanations"]:
            state["paused"] = True
        self.narration.show(s["explanations"])
        if s["explanations"]:
            beat = self.story.beat
            self.narration.update(beat.title.upper(), beat.text, self.story.step,
                                  CONTINUE_HINTS if state["paused"] else CARD_HINTS)
