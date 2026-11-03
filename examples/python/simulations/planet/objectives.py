"""
The walker's objectives (walker.py): GOALS in order, each met once a
number the game measures reaches its target.

`Objectives.update(snapshot)` takes the game's numbers (a dict with every
key the goals read) and moves on past each goal they meet, returning the
goals met by this call; `current()` is the goal under way, or None once
all are met, and `progress(snapshot)` how far it has got (shown, target).

Ore is sold where it comes to rest on an outpost's pad (`sale_items`):
each loose ore rock lying still there, not held, is an item for
trade.sale with the body and the place it was first seen on.
"""

from dataclasses import dataclass


@dataclass(frozen=True)
class Goal:
    key: str                     # the snapshot's number it reads
    title: str
    hint: str
    target: float
    unit: str = ""


GOALS = (
    Goal("cells", "Explore around Base", "Walk about; the map (M) fills in as you go.", 400, "cells"),
    Goal("waypoints", "Set a waypoint and reach it", "Open the map with M and click somewhere near.", 1),
    Goal("ore_seen", "Find an ore boulder", "Ore shows amber on the map; ore rocks glow with specks.", 1),
    Goal("ore_chunks", "Break an ore boulder",
         "Throw a crate or a rock into it hard: hold the punt to charge it.", 1),
    Goal("sold_kg", "Deliver 500 kg of ore", "Carry or throw ore onto an outpost's pad; it sells there.", 500,
         "kg"),
    Goal("earned", "Earn 2,000 credits", "Ore from farther away sells for more.", 2000, "cr"),
)

REST_SPEED = 0.5                 # m/s: ore slower than this on a pad sells


class Objectives:
    def __init__(self, goals=GOALS):
        self.goals = goals
        self.index = 0

    def current(self):
        return self.goals[self.index] if self.index < len(self.goals) else None

    def update(self, snapshot):
        met = []
        while self.index < len(self.goals) and snapshot[self.goals[self.index].key] >= self.goals[self.index].target:
            met.append(self.goals[self.index])
            self.index += 1
        return met

    def progress(self, snapshot):
        goal = self.current()
        if goal is None:
            return None
        return min(snapshot[goal.key], goal.target), goal.target


def progress_text(goal, shown, target):
    """"212 / 400 cells", or "done" style for a one-off goal."""
    if target == 1:
        return "done" if shown >= 1 else "not yet"
    unit = (" " + goal.unit) if goal.unit else ""
    return "%s / %s%s" % (format(int(shown), ","), format(int(target), ","), unit)


def sale_items(ore, outposts, held):
    """For ore rocks (prop, body, position from the body's centre, speed,
    first seen as (body name, unit direction)): (outpost, prop, item) of
    each lying on an outpost's pad of its body slower than REST_SPEED and
    not `held` (a prop or None)."""
    found = []
    for prop, body, position, speed, seen in ore:
        if prop is held or speed >= REST_SPEED:
            continue
        for outpost in outposts:
            if outpost.body is body and outpost.at(_add(body.centre(), position)):
                found.append((outpost, prop, {"body": seen[0], "direction": seen[1], "mass": prop.mass,
                                              "grade": prop.grade}))
                break
    return found


def _add(a, b):
    return (a[0] + b[0], a[1] + b[1], a[2] + b[2])
