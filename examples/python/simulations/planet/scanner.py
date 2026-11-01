"""
Scanning for ore from the ship (planet.py's SCAN page).

While scanning, `Scanner.update` probes the ore richness (resources.glsl,
through the world's extra probes, while nothing else uses them) at RATE
points a second along the ground track ahead: the point under where the
ship will be AHEAD seconds on, coasting around the body under it by that
body's gravity alone (steps of STEP), the points spaced evenly between one
frame's and the next. Each richness goes into `strip`, the last STRIP
values. A point at CONTACT or richer is a contact (`Contact`: its body, its
direction from the body's centre, its richness and the terrain's height
there); one within MERGE metres of a contact on the same body counts as
it, raising its richness (and moving it there) when richer. At most
MAX_CONTACTS are kept, the poorest dropped first. `nearest` gives the
contact nearest a point, and its distance.

`records` and `restore` give the contacts as plain values for a save
file, and take them back.
"""

from collections import deque
from dataclasses import dataclass

from shoonyakasha.planet.vec import add, length, normalize, scale, sub

RATE = 32.0                      # probes a second
AHEAD = 10.0                     # s ahead along the ship's path
STEP = 0.25                      # s
STRIP = 64
CONTACT = 0.6
MERGE = 1_000.0                  # m
MAX_CONTACTS = 64


@dataclass
class Contact:
    body: object                 # the world body it lies on
    direction: tuple             # unit, from the body's centre
    richness: float
    height: float                # m above the body's radius

    def point(self):
        """Where it is, from the planet's centre."""
        return add(self.body.centre(), scale(self.direction, self.body.radius + self.height))


class Scanner:
    def __init__(self, world, probes):
        """`probes`: how many of the world's extra probes a round may use."""
        self.world = world
        self.probes = probes
        self.contacts = []
        self.strip = deque(maxlen=STRIP)
        self.scanned = 0             # points measured
        self.credit = 0.0            # probes owed by RATE
        self.last = None             # (body, direction) of the last point asked for or passed
        self.asked = None            # the probes asked for and not yet back

    def ahead(self, position, velocity, body, gm):
        """The direction from `body`'s centre under the point the ship
        (`position`, `velocity` from the planet's centre) coasts to in AHEAD
        seconds."""
        p, v = body.relative(position), sub(velocity, body.velocity())
        t = 0.0
        while t < AHEAD - 1e-9:
            h = min(STEP, AHEAD - t)
            r = length(p)
            v = add(v, scale(p, -gm / (r * r * r) * h))
            p = add(p, scale(v, h))
            t += h
        return normalize(p)

    def update(self, dt, position, velocity, body, gm, active=True):
        """Take the points that came back, and while `active` ask for the
        next ones along the ground track of the ship at `position` with
        `velocity` (from the planet's centre) over `body` (gravity `gm`)."""
        world = self.world
        if self.asked is not None and [probe for probe, _ in world.extra_heights] == self.asked:
            self.take()
        if not active:
            if self.asked is not None and world.extra_probes == self.asked:
                world.extra_probes = []
            self.asked, self.last, self.credit = None, None, 0.0
            return
        point = self.ahead(position, velocity, body, gm)
        if self.last is None or self.last[0] is not body:
            self.last = (body, point)
            return
        self.credit = min(self.credit + RATE * dt, 2.0 * self.probes)
        if self.asked is not None or (world.extra_probes and world.extra_probes != self.asked):
            return
        count = min(self.probes, int(self.credit))
        if count == 0:
            return
        start = self.last[1]
        asked = [(normalize(add(scale(start, 1.0 - (k + 1) / count), scale(point, (k + 1) / count))), body)
                 for k in range(count)]
        world.extra_probes = asked
        self.asked = asked
        self.credit -= count
        self.last = (body, point)

    def take(self):
        world = self.world
        for ((direction, body), height), (_, ore) in zip(world.extra_heights, world.extra_ores):
            self.strip.append(ore)
            self.scanned += 1
            if ore >= CONTACT:
                self.note(body, direction, ore, height)
        world.extra_probes = []
        self.asked = None

    def note(self, body, direction, richness, height):
        for contact in self.contacts:
            if contact.body is body and length(sub(contact.direction, direction)) * body.radius < MERGE:
                if richness > contact.richness:
                    contact.direction, contact.richness, contact.height = direction, richness, height
                return
        self.contacts.append(Contact(body, direction, richness, height))
        if len(self.contacts) > MAX_CONTACTS:
            self.contacts.remove(min(self.contacts, key=lambda c: c.richness))

    def nearest(self, body, point):
        """(the contact on `body` nearest `point`, from the planet's
        centre, and its distance), or None when there is none."""
        found = [(length(sub(c.point(), point)), c) for c in self.contacts if c.body is body]
        if not found:
            return None
        distance, contact = min(found, key=lambda f: f[0])
        return contact, distance

    def records(self):
        return [{"on": c.body.name, "direction": list(c.direction), "richness": c.richness, "height": c.height}
                for c in self.contacts]

    def restore(self, records, bodies):
        """The contacts from `records`; `bodies` the world bodies by name."""
        self.contacts = [Contact(bodies[r["on"]], tuple(r["direction"]), r["richness"], r["height"])
                         for r in records]
        self.strip.clear()
        self.asked, self.last, self.credit = None, None, 0.0
