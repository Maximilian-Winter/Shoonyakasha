"""Tests for shoonyakasha.planet.scans and scan_export: a prepared scan
loaded back, every kind's availability, the asset check passing and
failing on a scan over its budget, a rock's hull or a missing credit, and
an archive with an entry outside its folder refused. A temporary folder
stands in for the scans' cache (SHOONYAKASHA_SCANS).

    python -m unittest discover -s tests/python
"""

import json
import os
import tempfile
import unittest
import zipfile

import numpy as np


from shoonyakasha.planet import scan_export, scans


def write_scan(kind, triangles=None, hull_points=8):
    """A small prepared scan of `kind` in the cache: a tetrahedron's worth of triangles."""
    folder = scans.prepared_directory(kind.name)
    os.makedirs(folder, exist_ok=True)
    count = triangles if triangles is not None else min(kind.triangles, 4)
    positions = np.array([[0.0, 0.0, 0.0], [1.0, 0.0, 0.0], [0.0, 0.0, 1.0], [0.0, 1.5, 0.0]], np.float32)
    indices = np.tile(np.array([0, 1, 2], np.uint32), count)
    hull = np.random.default_rng(1).random((hull_points, 3)).astype(np.float32) if kind.rock else np.zeros((0, 3))
    np.savez(os.path.join(folder, "mesh.npz"), positions=positions, normals=np.tile([0.0, 1.0, 0.0], (4, 1)),
             uvs=np.zeros((4, 2)), tangents=np.tile([1.0, 0.0, 0.0, 1.0], (4, 1)),
             materials=np.zeros(4, np.uint8), indices=indices, hull=hull.astype(np.float32))
    open(os.path.join(folder, "albedo_0.png"), "wb").close()
    with open(os.path.join(folder, "materials.json"), "w", encoding="utf-8") as f:
        json.dump([{"name": "m", "albedo": "albedo_0.png", "normal": None, "cutout": False}], f)
    with open(os.path.join(folder, "meta.json"), "w", encoding="utf-8") as f:
        json.dump({"kind": kind.name}, f)


class ScansTest(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.TemporaryDirectory()
        self.saved = os.environ.get("SHOONYAKASHA_SCANS")
        os.environ["SHOONYAKASHA_SCANS"] = self.folder.name

    def tearDown(self):
        if self.saved is None:
            del os.environ["SHOONYAKASHA_SCANS"]
        else:
            os.environ["SHOONYAKASHA_SCANS"] = self.saved
        self.folder.cleanup()

    def test_table(self):
        self.assertEqual(len(set(scans.NAMES)), len(scans.SCANS))
        for kind in scans.SCANS:
            self.assertIn(kind.scene, scans.SCENES)
            self.assertIn(kind.family, ("desert", "forest", "rock"))
            self.assertGreater(kind.triangles, 0)
            self.assertTrue(0.0 <= kind.sink < 0.5)

    def test_prepared_scan_loads_back(self):
        kind = scans.SCANS[0]
        self.assertIsNone(scans.prepared(kind))
        write_scan(kind)
        scan = scans.prepared(kind.name)
        self.assertEqual(scan.kind, kind)
        self.assertAlmostEqual(scan.height, 1.5)
        self.assertEqual(len(scan.indices) % 3, 0)
        self.assertTrue(scan.textures[0]["albedo"].startswith(scans.prepared_directory(kind.name)))
        self.assertFalse(scans.available())
        for k in scans.SCANS[1:]:
            write_scan(k)
        self.assertTrue(scans.available())

    def test_check(self):
        for k in scans.SCANS:
            write_scan(k)
        scan_export.write_credits()
        lines, ok = scan_export.check()
        self.assertTrue(ok, lines)
        rock = next(k for k in scans.SCANS if k.rock)
        write_scan(rock, hull_points=scans.HULL_POINTS + 1)
        self.assertFalse(scan_export.check()[1])
        write_scan(rock)
        plant = next(k for k in scans.SCANS if not k.rock)
        write_scan(plant, triangles=int(plant.triangles * scan_export.TRIANGLE_SLACK) + 1)
        self.assertFalse(scan_export.check()[1])
        write_scan(plant)
        os.remove(os.path.join(scans.directory(), "prepared", "CREDITS.md"))
        self.assertFalse(scan_export.check()[1])

    def test_archive_with_an_entry_outside_is_refused(self):
        with tempfile.TemporaryDirectory() as source:
            archive, blend = scans.SCENES["namaqualand"]
            with zipfile.ZipFile(os.path.join(source, archive), "w") as z:
                z.writestr("../outside.txt", "no")
            saved = os.environ.get("SHOONYAKASHA_SPACE_ASSETS")
            os.environ["SHOONYAKASHA_SPACE_ASSETS"] = source
            try:
                with self.assertRaises(ValueError):
                    scan_export.extracted("namaqualand")
            finally:
                if saved is None:
                    del os.environ["SHOONYAKASHA_SPACE_ASSETS"]
                else:
                    os.environ["SHOONYAKASHA_SPACE_ASSETS"] = saved
            self.assertFalse(os.path.exists(os.path.join(os.path.dirname(scans.directory()), "outside.txt")))


if __name__ == "__main__":
    unittest.main()
