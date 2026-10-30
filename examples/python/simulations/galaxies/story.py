"""The explanation cards each scene shows, and when.

A scene's script is a list of beats: a title, a few sentences, and a
trigger, a test of the simulation's progress (a dict, see Story.update).
The first beat shows at once; each later one when its trigger first holds,
but not before the one before has been up for `min_seconds`. Beats that
trigger together queue. `previous` and `next` step through the beats
reached so far.
"""

from dataclasses import dataclass
from typing import Callable

MIN_SECONDS = 6.0


@dataclass(frozen=True)
class Beat:
    title: str
    text: str
    trigger: Callable[[dict], bool] = lambda progress: True


def after_pass(myr, passes=1):
    """True once `myr` have passed since pass number `passes`."""
    return lambda p: p["passes"] >= passes and p["myr"] - p["pass_myr"][passes - 1] >= myr


MERGER = [
    Beat("Two disc galaxies",
         "Each galaxy here is a dark matter halo, a bulge of old stars and a spinning disc of stars and gas, "
         "half a million particles each. Most of a galaxy's mass is the halo, which you cannot see; in this "
         "model each halo moves as one rigid body, and the stars and gas feel its pull."),
    Beat("Closing in",
         "The galaxies fall toward each other at a few hundred km/s. Each disc turns the same way as the "
         "orbit (prograde), so its outer stars move along with the other galaxy for a while: the pull acts "
         "on them for longest.",
         lambda p: p["closest"] < 25.0),
    Beat("Tidal tails",
         "After the first pass the far sides of the discs are flung out into long tails, and a bridge of "
         "stars and gas joins the galaxies. Stars do not collide; only gravity reshapes the discs. Toomre "
         "and Toomre explained the Antennae and the Mice this way in 1972.",
         after_pass(80.0)),
    Beat("A starburst",
         "Where gas streams of the two discs cross, they lose their relative motion and pile up. Dense, "
         "compressed gas turns into young stars, blue and bright, glowing pink while their hydrogen is lit. "
         "The star formation rate climbs to several times that of two quiet discs.",
         lambda p: (p["sfr"] > 15.0 and p["passes"] >= 1) or after_pass(250.0)(p)),
    Beat("Friction",
         "The galaxies swing apart, slow, and fall back. Each halo drags on the other as it moves through "
         "it, dynamical friction, and the orbit loses energy. Without it they would fly apart for good.",
         lambda p: p["apocentres"] >= 1),
    Beat("The second pass",
         "Closer and slower this time. The cores spiral together, and the gas that sank to the centres is "
         "squeezed again.",
         lambda p: p["passes"] >= 2),
    Beat("One galaxy",
         "The cores have merged. The discs are gone: their stars move on mixed orbits in a rounder, "
         "smoother remnant, ringed with shells and the fading tails. Many elliptical galaxies are thought "
         "to have formed this way. Press 3 for the same merger with every particle's own gravity.",
         lambda p: p["merged"]),
]

RING = [
    Beat("A target and a bullet",
         "A disc galaxy, and a companion a fifth of its mass falling straight down its axis at about "
         "1,000 km/s. Head-on, almost through the centre."),
    Beat("The plunge",
         "As the companion passes through, the disc's stars are pulled inward all at once; then the pull "
         "vanishes as fast as it came. Each star swings back out on its own orbit.",
         lambda p: p["passes"] >= 1),
    Beat("A ring wave",
         "Stars at each radius swing out and back in step, so their orbits crowd together into a ring that "
         "moves outward, about 80 km/s here. Behind it a second and a third ring follow. It is a wave: the "
         "stars move in and out, the crowd moves on.",
         after_pass(120.0)),
    Beat("Stars born in the ring",
         "The ring squeezes the gas it passes through, and the compressed gas forms stars, so the ring is "
         "edged with young blue stars and pink knots. The Cartwheel galaxy, 500 million light years away, "
         "looks like this.",
         after_pass(250.0)),
]

REPLAY = [
    Beat("Every particle pulls",
         "The same two galaxies, precomputed: 131,072 bodies, each pulling on every other, 17 billion pairs "
         "a step. The halos are made of bodies too, so they bend, stretch and drag on each other. It took "
         "3.5 minutes on the GPU and is streamed here frame by frame."),
    Beat("Spiral arms",
         "The discs feel their own gravity now, and grow spiral arms and, later, bars by themselves. The "
         "live model, where only the halos pull, cannot do this.",
         lambda p: p["myr"] >= 90.0),
    Beat("Halos that merge",
         "The live halos deform and drag on each other far more than the rule of the live model, and the "
         "galaxies merge in half the time: about 460 million years instead of 900.",
         lambda p: p["myr"] >= 380.0),
    Beat("Two models",
         "The live scenes trade self-gravity for a million particles and gas that forms stars; this one "
         "trades the gas and most of the particles for gravity between all of them. Both are models: each "
         "keeps the part of the physics it is built to show.",
         lambda p: p["myr"] >= 520.0),
]


def script(scene):
    return {"merger": MERGER, "ring": RING, "replay": REPLAY}[scene]


class Story:
    """Which beat of a script to show."""

    def __init__(self, beats, min_seconds=MIN_SECONDS):
        self.beats = beats
        self.min_seconds = min_seconds
        self.reached = 0         # the last beat whose trigger has held
        self.shown = 0
        self.held = 0.0          # seconds the latest beat has been up
        self.browsing = False    # stepped back with `previous`

    def update(self, progress, seconds):
        """Advance by `seconds` of real time, given the simulation's
        `progress`: myr, closest (the least separation so far), passes, pass_myr (each pass's time),
        apocentres, merged and sfr. Returns whether a new beat was reached."""
        self.held += seconds
        following = self.reached + 1
        if (following < len(self.beats) and self.held >= self.min_seconds
                and self.beats[following].trigger(progress)):
            self.reached = following
            self.held = 0.0
            if not self.browsing:
                self.shown = following
            return True
        return False

    def previous(self):
        self.shown = max(0, self.shown - 1)
        self.browsing = self.shown < self.reached

    def next(self):
        self.shown = min(self.reached, self.shown + 1)
        self.browsing = self.shown < self.reached

    @property
    def beat(self):
        return self.beats[self.shown]

    @property
    def step(self):
        return "%d / %d" % (self.shown + 1, len(self.beats))
