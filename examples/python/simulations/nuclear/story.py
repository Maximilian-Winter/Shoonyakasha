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


def after(event, time_units):
    """True once `time_units` have passed since the first `event`: "capture" or "split"."""
    key = event + "_time"
    return lambda p: p[key] is not None and p["time"] - p[key] >= time_units


FISSION = [
    Beat("A neutron and a uranium nucleus",
         "Uranium-235: 92 protons (orange) and 143 neutrons (blue). The strong force pulls neighbouring "
         "nucleons together but reaches no further; the protons' electric repulsion reaches across the "
         "whole nucleus. The neutron has no charge, so nothing turns it away."),
    Beat("Capture",
         "The neutron is caught, making uranium-236. The energy it brings, and the energy it gives up by "
         "being bound, heat the nucleus: its nucleons move faster. In this model a rule turns that heat "
         "into stretching, as the excitation of a real compound nucleus turns into deformation.",
         lambda p: p["captures"] >= 1),
    Beat("Stretching",
         "The nucleus lengthens and rebounds. The strong force acts like surface tension, pulling it back "
         "into a ball; the protons' repulsion pushes its ends apart. Stretched far enough, repulsion wins: "
         "that point is the fission barrier.",
         after("capture", 20.0)),
    Beat("Scission",
         "A neck forms and snaps. The two fragments are usually unequal, as in real fission. Nothing holds "
         "their protons together any more, so they push each other apart: that repulsion is where most "
         "of the energy of fission comes from.",
         lambda p: p["splits"] >= 1),
    Beat("Neutrons and light",
         "Each fragment gives off a neutron (the blue trails) and sheds heat (the flash). Real fragments "
         "give off two or three neutrons and gamma rays because of quantum effects; here a rule does it. "
         "Those neutrons can split other nuclei: press 2 for the chain reaction.",
         after("split", 8.0)),
]

CHAIN = [
    Beat("Sixty-four uranium nuclei",
         "A cube of uranium nuclei, 21 units apart, and one neutron. In matter, electrons screen each "
         "nucleus's charge from its neighbours; here the electric force fades out beyond 11 units for the "
         "same reason, so the nuclei sit still until a neutron arrives."),
    Beat("The first fission",
         "The first nucleus splits and gives off two neutrons. Whether the reaction grows depends on how "
         "many nuclei each fission goes on to split: the multiplication factor, k.",
         lambda p: p["splits"] >= 1),
    Beat("A second generation",
         "A neighbour captures a neutron from that fission. Slow neutrons are really captured from well "
         "beyond the edge of a nucleus, a quantum wave effect; a rule gives the model a capture radius "
         "of 10 units to match.",
         lambda p: p["captures"] >= 2),
    Beat("Critical",
         "Each fission now leads to more than one more: k is above 1 and the count of splits keeps "
         "growing. Spread the nuclei out (F1, spacing 30, then R) and most neutrons escape between them: "
         "k falls below 1 and the chain dies. That is the difference between a critical and a "
         "subcritical mass.",
         lambda p: p["splits"] >= 6),
]

FUSION_DT = [
    Beat("Deuterium and tritium",
         "Two heavy kinds of hydrogen: deuterium, one proton and one neutron, and tritium, one proton and "
         "two neutrons. Both are positive, so they repel. Too slow, and they turn back before the strong "
         "force can take hold: set the speed to 0.3 in F1 and press R to watch them bounce."),
    Beat("A compound nucleus",
         "They touch and merge into helium-5: two protons and three neutrons, and hot. Helium-5 is real "
         "and short-lived; the reaction in a fusion reactor passes through it.",
         lambda p: p["reactions"] >= 1),
    Beat("Helium-4 and a neutron",
         "Helium-5 sheds a neutron and leaves helium-4, the most tightly bound of the light nuclei. In "
         "nature D + T releases 17.6 MeV, most of it carried by the neutron. The model's light nuclei are "
         "less tightly bound than real ones, so it releases less, and much of it stays as heat.",
         lambda p: p["reactions"] >= 2),
]

FUSION_DD = [
    Beat("Two deuterons",
         "Deuterium nuclei, one proton and one neutron each, flying at each other. Their charges repel; "
         "fast enough, they come close enough for the strong force to take hold."),
    Beat("Helium-4, hot",
         "They merge into helium-4. It holds energy from the collision and from binding, as motion of its "
         "nucleons. A real one sheds it as a gamma ray, rarely; a classical nucleus keeps it.",
         lambda p: p["reactions"] >= 1),
    Beat("Two ways apart",
         "Hot enough (speed 0.8 or more), helium-4 throws off a nucleon: tritium and a proton, or "
         "helium-3 and a neutron. Those are the two ways real D + D fusion goes, about equally often.",
         lambda p: p["reactions"] >= 2),
]


def script(scene, reaction="dt"):
    """The beats of `scene` ("fission", "chain" or "fusion" with its `reaction`)."""
    if scene == "fusion":
        return FUSION_DD if reaction == "dd" else FUSION_DT
    return {"fission": FISSION, "chain": CHAIN}[scene]


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
        `progress`: time, captures, splits, neutrons, reactions and
        capture_time and split_time (the times of the first capture and
        split, or None). Returns
        whether a new beat was reached."""
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
