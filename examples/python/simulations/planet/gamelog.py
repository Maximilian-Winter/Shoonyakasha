"""
The walker's log (walker.py): what happened, newest last, for the
Slate's Log app. Each entry is a dict of "day" and "hour" (the game's,
clock.py), "text", "detail" (a short value at its right, or ""),
"colour" (RGBA) and "kind", one of KINDS. `Log.add` keeps the last
MAX_ENTRIES; `Log.shown(kind)` gives them newest first, all of them or
those of one kind; `record` and `Log.restore` turn it into a save's
list and back.

The ledger in the same save: `Ledger` counts the credits earned and
spent, from the market's credits as they change (`Ledger.watch`), and
the ore sold by resource (`Ledger.sold`).
"""

KINDS = ("money", "contracts", "world", "finds")
MAX_ENTRIES = 200


class Log:
    def __init__(self):
        self.entries = []

    def add(self, day, hour, text, detail="", colour=(1.0, 1.0, 1.0, 1.0), kind="world"):
        self.entries.append({"day": int(day), "hour": hour, "text": text, "detail": detail or "",
                             "colour": tuple(colour), "kind": kind if kind in KINDS else "world"})
        del self.entries[:-MAX_ENTRIES]

    def shown(self, kind=None):
        """The entries newest first, of `kind` only when given."""
        return [e for e in reversed(self.entries) if kind is None or e["kind"] == kind]

    def record(self):
        return [dict(e, colour=list(e["colour"])) for e in self.entries]

    def restore(self, entries):
        self.entries = []
        for e in entries or []:
            self.add(e.get("day", 0), e.get("hour", ""), e.get("text", ""), e.get("detail", ""),
                     e.get("colour", (1.0, 1.0, 1.0, 1.0)), e.get("kind", "world"))


class Ledger:
    def __init__(self, credits=None):
        self.earned = 0.0
        self.spent = 0.0
        self.sold_by = {}             # resource: kg sold
        self.last = credits           # the credits last watched

    def watch(self, credits):
        """The credits now: a rise since the last watch is earned, a fall spent."""
        if self.last is not None:
            change = credits - self.last
            if change > 0.0:
                self.earned += change
            elif change < 0.0:
                self.spent -= change
        self.last = credits

    def sold(self, resource, kg):
        self.sold_by[int(resource)] = self.sold_by.get(int(resource), 0.0) + kg

    def record(self):
        return {"earned": self.earned, "spent": self.spent,
                "sold_by": {str(k): v for k, v in self.sold_by.items()}}

    def restore(self, data, credits):
        """From a save's record (None for none), watching from `credits`."""
        data = data or {}
        self.earned = float(data.get("earned", 0.0))
        self.spent = float(data.get("spent", 0.0))
        self.sold_by = {int(k): float(v) for k, v in data.get("sold_by", {}).items()}
        self.last = credits
