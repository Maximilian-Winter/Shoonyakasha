"""Tests for the walker's log and ledger
(examples/python/simulations/planet/gamelog.py) and the slate's items
(slate.py): entries kept newest first and by kind, their record, the
credits earned and spent from their changes, the ore sold by resource,
the texts' helpers and the rail's icons. The items' pointer is tested in
test_planet_walker_ui.py.

    python -m unittest discover -s tests/python
"""

import importlib.util
import sys
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
PLANET_DIR = REPO / "examples" / "python" / "simulations" / "planet"
sys.path.insert(0, str(PLANET_DIR))


def load(name, file):
    spec = importlib.util.spec_from_file_location(name, PLANET_DIR / file)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


gamelog = load("planet_gamelog", "gamelog.py")
slate = load("planet_slate", "slate.py")


class LogTest(unittest.TestCase):
    def test_newest_first_and_by_kind(self):
        log = gamelog.Log()
        log.add(0, "09:00", "Sold ore", "+100 cr", kind="money")
        log.add(0, "09:05", "Storm", kind="world")
        log.add(1, "10:00", "Bought racks", "-2,000 cr", kind="money")
        self.assertEqual([e["text"] for e in log.shown()], ["Bought racks", "Storm", "Sold ore"])
        self.assertEqual([e["text"] for e in log.shown("money")], ["Bought racks", "Sold ore"])
        self.assertEqual(log.shown("finds"), [])

    def test_unknown_kind_is_world_and_old_entries_go(self):
        log = gamelog.Log()
        log.add(0, "", "x", kind="nonsense")
        self.assertEqual(log.entries[0]["kind"], "world")
        for k in range(gamelog.MAX_ENTRIES + 5):
            log.add(0, "", str(k))
        self.assertEqual(len(log.entries), gamelog.MAX_ENTRIES)
        self.assertEqual(log.shown()[0]["text"], str(gamelog.MAX_ENTRIES + 4))

    def test_record_restores(self):
        log = gamelog.Log()
        log.add(2, "12:30", "Found a wreck", "", (1.0, 0.5, 0.2, 1.0), "finds")
        again = gamelog.Log()
        again.restore(log.record())
        self.assertEqual(again.entries, log.entries)
        again.restore(None)
        self.assertEqual(again.entries, [])


class LedgerTest(unittest.TestCase):
    def test_earned_and_spent_from_the_credits(self):
        ledger = gamelog.Ledger()
        ledger.watch(2_000.0)                 # the first watch only takes the credits
        ledger.watch(2_500.0)
        ledger.watch(1_000.0)
        ledger.watch(1_000.0)
        self.assertEqual((ledger.earned, ledger.spent), (500.0, 1_500.0))

    def test_sold_by_resource_and_record(self):
        ledger = gamelog.Ledger(100.0)
        ledger.sold(3, 40.0)
        ledger.sold(3, 2.0)
        ledger.sold(1, 5.0)
        ledger.watch(150.0)
        again = gamelog.Ledger()
        again.restore(ledger.record(), 150.0)
        self.assertEqual(again.sold_by, {3: 42.0, 1: 5.0})
        self.assertEqual((again.earned, again.spent), (50.0, 0.0))
        again.watch(150.0)
        self.assertEqual(again.earned, 50.0)


class ItemsTest(unittest.TestCase):
    def test_wrap_and_fit(self):
        lines = slate.wrap_text("one two three four five six seven", 80.0)
        self.assertGreater(len(lines), 1)
        self.assertEqual(" ".join(lines), "one two three four five six seven")
        self.assertTrue(slate.fit_text("a long line that does not fit", 60.0).endswith("…"))
        self.assertEqual(slate.fit_text("fits", 500.0), "fits")

    def test_icons_and_app_keys(self):
        self.assertEqual(set(slate.ICONS), {"overview", "market", "map", "contracts", "workshop", "vehicle", "log",
                                            "system", "debug"})


if __name__ == "__main__":
    unittest.main()
