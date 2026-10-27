"""The viewer's screen: readings, explanation cards, key hints, the controls
sheet and the settings menu (shoonyakasha.toolkit.overlay).

    top left        a card of readings: time, speed, the collision's state
    bottom left     the explanation card of the scene's story (story.py)
    bottom          the key hints
    F1              the settings menu, down the right side
    F2              the controls sheet
    P               a screenshot into galaxy_captures/

The menu reads and writes `settings`, a dict the viewer keeps: how the
merger is set up (applied when a scene starts), the rules' constants and
what is drawn (applied at once).
"""

import time
from pathlib import Path

import shoonyakasha as sk
from shoonyakasha.toolkit.overlay import Card, HelpOverlay, HintBar, Narration, SettingsMenu

import story

REFERENCE = (1600, 900)
CAPTURES = Path(__file__).resolve().parent / "galaxy_captures"
TITLES = {"merger": "MERGER", "ring": "RING GALAXY", "replay": "SELF-GRAVITY REPLAY"}

HELP = [
    [("Scenes", [(("1",), "Merger: two equal disc galaxies"),
                 (("2",), "Ring galaxy: a companion through a disc"),
                 (("3",), "Replay: every particle's own gravity"),
                 (("R",), "Start the scene again"),
                 (("Space",), "Pause"),
                 (("+", "-"), "Faster, slower")]),
     ("Camera", [(("RMB",), "Hold to turn the camera"),
                 (("W", "A", "S", "D"), "Fly"),
                 (("Q", "E"), "Down, up"),
                 (("C",), "Cinematic camera"),
                 (("G",), "Colour by galaxy or by age")])],
    [("Explanations", [(("[", "]"), "Previous, next card"),
                       (("H",), "Show or hide the cards")]),
     ("Panels", [(("F1",), "Settings"),
                 (("F2",), "These controls"),
                 (("P",), "Screenshot into galaxy_captures")])],
]
HINTS = [(("1", "2", "3"), "Scenes"), (("R",), "Restart"), (("Space",), "Pause"), (("+", "-"), "Speed"),
         (("C",), "Cinematic"), (("F1",), "Settings"), (("F2",), "Controls"), (("P",), "Screenshot")]
CARD_HINTS = [(("[", "]"), "Back, next"), (("H",), "Hide")]
CONTINUE_HINTS = [(("Space",), "Continue")] + CARD_HINTS


def suns(mass):
    """A mass in solar masses, in words: "12 billion suns"."""
    if mass >= 1e9:
        return "%.1f billion suns" % (mass / 1e9)
    if mass >= 1e6:
        return "%.0f million suns" % (mass / 1e6)
    return "%.0f suns" % mass


class Interface:
    def __init__(self, engine, state, settings, load, restart, apply):
        """`load(scene)` starts a scene, `restart()` the current one again,
        and `apply()` hands the engine the rules and what `settings` say it
        should draw."""
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

        def applied(key):
            return {"get": lambda: s[key], "set": lambda v: (s.update({key: v}), self.apply())}

        return [
            {"heading": "Simulation"},
            {"button": "Screenshot", "do": self.screenshot_from_menu, "keys": ("P",),
             "hint": "Close this menu and save the view into galaxy_captures."},
            {"button": "Merger", "do": lambda: self.load("merger"), "keys": ("1",),
             "hint": "Two equal disc galaxies on a bound orbit."},
            {"button": "Ring galaxy", "do": lambda: self.load("ring"), "keys": ("2",),
             "hint": "A small companion falls straight through a disc."},
            {"button": "Self-gravity replay", "do": lambda: self.load("replay"), "keys": ("3",),
             "hint": "The merger precomputed with every particle's own gravity (precompute_replay.py)."},
            {"button": "Restart", "do": self.restart, "keys": ("R",),
             "hint": "Start the scene again, with the merger settings below."},
            {"toggle": "Paused", "get": lambda: state["paused"], "set": lambda on: state.update(paused=on),
             "keys": ("Space",)},
            {"slider": "Time scale", "min": 0.1, "max": 4.0, "get": lambda: state["time_scale"],
             "set": lambda v: state.update(time_scale=v), "show": lambda v: "x%.2f" % v, "keys": ("+", "-"),
             "hint": "Simulation time per second of real time; 1 is 38 million years a second."},

            {"heading": "Merger (on restart)"},
            dict(setting("tilt"), slider="Second disc's tilt", min=0.0, max=180.0,
                 show=lambda v: "%.0f degrees" % v,
                 hint="The second disc's tilt from the orbit's axis. Above 90 it turns against the orbit "
                      "(retrograde), and its tails come out short."),
            dict(setting("ratio"), slider="Mass ratio", min=0.2, max=1.0, format="%.2f",
                 hint="The second galaxy's mass over the first's."),
            dict(setting("pericentre"), slider="Closest approach", min=2.0, max=30.0, show=lambda v: "%.0f kpc" % v,
                 hint="How close the first pass comes. Far passes raise long tails but merge late."),

            {"heading": "Rules (at once)"},
            dict(applied("log_lambda"), slider="Dynamical friction", min=0.0, max=0.6, format="%.2f",
                 hint="How strongly each halo drags on the other (its Coulomb logarithm). 0: the galaxies "
                      "pass and never merge."),
            dict(applied("gas_relax"), slider="Gas dissipation", min=0.0, max=2.0, format="%.2f",
                 hint="How fast gas takes on its neighbourhood's mean velocity, per time unit. Colliding gas "
                      "streams lose their relative motion."),
            dict(applied("sf_density"), slider="Star formation density", min=0.002, max=0.05, format="%.3f",
                 hint="Gas denser than this (10^10 solar masses per kpc^3) forms stars steadily."),
            dict(applied("sf_efficiency"), slider="Star formation efficiency", min=0.0, max=0.2, format="%.2f",
                 hint="Share of dense gas turned into stars per free-fall time."),
            dict(applied("sf_compression"), slider="Compression star formation", min=0.0, max=1.0, format="%.2f",
                 hint="Share of converging gas turned into stars per unit of compression: what lights up "
                      "the ring and the colliding gas."),

            {"heading": "Display"},
            dict(applied("explanations"), toggle="Explanation cards", keys=("H",)),
            {"toggle": "Pause at each new card", "get": lambda: s["pause_on_cards"],
             "set": lambda on: s.update(pause_on_cards=on),
             "hint": "Stop the simulation when a card appears, until Space; takes effect on restart."},
            dict(applied("readings"), toggle="Readings"),
            dict(applied("key_hints"), toggle="Key hints"),
            {"toggle": "Cinematic camera", "get": lambda: state["cinematic"],
             "set": lambda on: state.update(cinematic=on), "keys": ("C",),
             "hint": "A slow orbit around the galaxies."},
            {"toggle": "Colour by galaxy", "get": lambda: state["color_mode"] == 1,
             "set": lambda on: (state.update(color_mode=1 if on else 0), self.apply()), "keys": ("G",),
             "hint": "Each galaxy's stars in its own colour, to see where they end up; off, stars by age."},
            dict(applied("exposure"), slider="Exposure", min=0.2, max=3.0, format="%.2f"),
            dict(applied("bloom"), slider="Bloom", min=0.0, max=1.0, format="%.2f"),
            dict(applied("brightness"), slider="Star brightness", min=0.02, max=0.25, format="%.2f"),
            dict(applied("point_size"), slider="Star size", min=1.0, max=4.0, show=lambda v: "%.1f px" % v),
        ]

    def screenshot(self):
        """Save the view as it is drawn now into CAPTURES, under the time."""
        CAPTURES.mkdir(exist_ok=True)
        path = CAPTURES / time.strftime("galaxy_%Y%m%d_%H%M%S.png")
        print("screenshot ->", path if self.engine.capture_screenshot(str(path)) else "failed")

    def screenshot_from_menu(self):
        """Close the menu, then take the screenshot once it is off the screen."""
        self.menu.show(False)
        self.shot_in = 2

    def new_scene(self, scene):
        """Start the scene's story; with pause_on_cards and the cards shown, pause on its first card."""
        pausing = self.settings["pause_on_cards"] and self.settings["explanations"]
        self.story = story.Story(story.script(scene), min_seconds=0.0 if pausing else story.MIN_SECONDS)
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

    def rows(self):
        state = self.state
        rows = [("TIME", "%.0f million years" % (state["sim_time"] * 4.714)),
                ("SPEED", "paused" if state["paused"] else "x%.2f" % state["time_scale"])]
        if state["scene"] == "replay":
            replay = state["replay"]
            rows += [("BODIES", "{:,}".format(replay["bodies"])),
                     ("STARS SHOWN", "{:,}".format(replay["stars"])),
                     ("THE RUN",),
                     ("GPU TIME", "%.0f s" % replay["seconds"]),
                     ("PASSES", ", ".join("%d" % p["myr"] for p in replay["pericentres"]) + " Myr"),
                     ("MERGED", "%d Myr" % replay["merged_myr"] if replay["merged_myr"] else "not yet")]
            return rows
        rows += [("PARTICLES", "{:,}".format(state["count"])),
                 ("THE COLLISION",),
                 ("SEPARATION", "%.1f kpc" % state["separation"]),
                 ("RELATIVE SPEED", "%s %.0f km/s" % ("approaching" if state["closing"] >= 0 else "receding",
                                                     abs(state["closing"]))),
                 ("PASSES", "%d" % state["passes"] + (", merged" if state["merged"] else "")),
                 ("STAR FORMATION", "%.1f suns a year" % state["sfr"]),
                 ("STARS FORMED", suns(state["formed_mass"]))]
        return rows

    def progress(self):
        state = self.state
        return {"myr": state["sim_time"] * 4.714, "closest": state.get("closest", 1e9),
                "passes": state.get("passes", 0), "pass_myr": state.get("pass_myr", []),
                "apocentres": state.get("apocentres", 0), "merged": state.get("merged", False),
                "sfr": state.get("sfr", 0.0)}

    @property
    def pointer_over(self):
        return self.menu.pointer_over

    def update(self, seconds):
        """Once a frame, after the simulation's readings are current."""
        state, s = self.state, self.settings
        if self.shot_in:
            self.shot_in -= 1
            if not self.shot_in:
                self.screenshot()
        self.menu.update()
        self.hints.show(s["key_hints"])
        self.readings.show(s["readings"])
        if s["readings"]:
            self.readings.update(TITLES[state["scene"]], self.rows())

        if self.story is None:
            return
        # The story moves on only while the simulation runs.
        reached = not state["paused"] and self.story.update(self.progress(), seconds)
        if reached and s["pause_on_cards"] and s["explanations"]:
            state["paused"] = True
        self.narration.show(s["explanations"])
        if s["explanations"]:
            beat = self.story.beat
            self.narration.update(beat.title.upper(), beat.text, self.story.step,
                                  CONTINUE_HINTS if state["paused"] else CARD_HINTS)
