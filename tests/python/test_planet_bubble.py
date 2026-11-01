"""Tests for the physics bubble (shoonyakasha.planet.bubble) and the
terrain collider (shoonyakasha.planet.collider_probes), against stand-ins
for the physics and the engine.

    python -m unittest discover -s tests/python
"""

import unittest

import numpy as np

from shoonyakasha.planet.bubble import Bubble
from shoonyakasha.planet.collider_probes import (COLUMNS, SPACING, ColliderPatch, ColliderProbes, TerrainCollider,
                                                 plane_coordinates)
from shoonyakasha.planet.vec import add, dot, length, local_frame, normalize, scale, sub

RADIUS = 100_000.0
GM = 4.905 * RADIUS ** 2


class FakeBody:
    def __init__(self, centre=(0.0, 0.0, 0.0), radius=RADIUS, ocean=True, index=0):
        self._centre = centre
        self.radius = radius
        self.ocean = ocean
        self.index = index

    def centre(self):
        return self._centre

    def relative(self, point):
        return sub(point, self._centre)


class FakePhysics:
    def __init__(self):
        self.shifts = []
        self.steps = []
        self.meshes = {}
        self.next_id = 1

    def shift_origin(self, offset):
        self.shifts.append(offset)

    def apply_central_gravity(self, centre, gm):
        self.steps.append(("gravity", centre, gm))

    def step(self, dt):
        self.steps.append(("step", dt))

    def create_grid_mesh(self, points, columns, rows, position, friction=0.8):
        body = self.next_id
        self.next_id += 1
        self.meshes[body] = (np.array(points), columns, rows, position)
        return body

    def remove(self, body):
        del self.meshes[body]


class FakeEngine:
    def __init__(self):
        self.buffers = {}
        self.custom = {}
        self.readback = None

    def set_buffer(self, name, data):
        self.buffers[name] = np.array(data)

    def set_custom_vec4(self, key, value):
        self.custom[key] = tuple(value)

    def get_readback(self, name):
        return self.readback

    def answer(self, heights_of):
        """The ColliderHeights readback for the last request: heights_of(direction) at each."""
        requests = self.buffers["ColliderRequests"]
        count, stamp = self.custom["planet.collider.params"][:2]
        rows = np.zeros((4225, 4), dtype=np.float32)
        for k in range(int(count)):
            rows[k] = (heights_of(requests[k, :3]), stamp, 0.0, 0.0)
        self.readback = {"data": rows.tobytes()}


SURFACE = normalize((0.3, 0.8, 0.52))


class BubbleTest(unittest.TestCase):
    def test_coordinates_follow_the_moving_body(self):
        body = FakeBody(centre=(1000.0, 2000.0, -3000.0))
        bubble = Bubble(FakePhysics(), body, GM, scale(SURFACE, RADIUS))
        point = add(body.centre(), scale(SURFACE, RADIUS + 12.0))
        p = bubble.to_bubble(point)
        self.assertAlmostEqual(length(p), 12.0, places=6)
        body._centre = (5000.0, 0.0, 0.0)
        moved = bubble.to_world(p)
        self.assertLess(length(sub(sub(moved, body.centre()), scale(SURFACE, RADIUS + 12.0))), 1e-6)

    def test_gravity_points_to_the_centre(self):
        bubble = Bubble(FakePhysics(), FakeBody(), GM, scale(SURFACE, RADIUS))
        g = bubble.gravity((0.0, 0.0, 0.0))
        self.assertAlmostEqual(length(g), 4.905, places=6)
        self.assertAlmostEqual(dot(normalize(g), SURFACE), -1.0, places=9)

    def test_step_pulls_towards_the_centre_in_bubble_coordinates(self):
        physics = FakePhysics()
        origin = scale(SURFACE, RADIUS)
        Bubble(physics, FakeBody(), GM, origin).step(0.5)
        (_, centre, gm), (_, dt) = physics.steps
        self.assertLess(length(add(centre, origin)), 1e-9)
        self.assertEqual((gm, dt), (GM, 0.5))

    def test_rebase_shifts_bodies_and_listeners(self):
        physics = FakePhysics()
        origin = scale(SURFACE, RADIUS)
        bubble = Bubble(physics, FakeBody(), GM, origin)
        heard = []
        bubble.listeners.append(heard.append)
        self.assertFalse(bubble.maybe_rebase((300.0, 0.0, 0.0)))
        east, _ = local_frame(SURFACE)
        self.assertTrue(bubble.maybe_rebase(scale(east, 600.0)))
        self.assertEqual(physics.shifts, [scale(east, 600.0)])
        self.assertEqual(heard, [scale(east, 600.0)])
        self.assertLess(length(sub(bubble.origin, add(origin, scale(east, 600.0)))), 1e-9)
        self.assertEqual(bubble.anchor, SURFACE)       # within ANCHOR_REACH of the anchor
        bubble.rebase(scale(east, 600.0))
        self.assertLess(length(sub(bubble.anchor, normalize(bubble.origin))), 1e-12)


class ColliderPatchTest(unittest.TestCase):
    def setUp(self):
        self.body = FakeBody()
        self.east, self.north = local_frame(SURFACE)

    def patch(self, cell):
        return ColliderPatch(self.body, SURFACE, self.east, self.north, cell)

    def test_grid_spacing_on_the_plane(self):
        patch = self.patch((10, -4))
        self.assertEqual(patch.directions.shape, (COLUMNS, COLUMNS, 3))
        mid = (COLUMNS - 1) // 2
        x0, y0 = plane_coordinates(self.body, SURFACE, self.east, self.north, tuple(patch.directions[mid, mid]))
        self.assertAlmostEqual(x0, 10 * SPACING, places=6)
        self.assertAlmostEqual(y0, -4 * SPACING, places=6)
        x1, y1 = plane_coordinates(self.body, SURFACE, self.east, self.north, tuple(patch.directions[mid + 1, mid + 2]))
        self.assertAlmostEqual(x1 - x0, 2 * SPACING, places=6)
        self.assertAlmostEqual(y1 - y0, SPACING, places=6)

    def test_overlapping_patches_share_directions(self):
        a, b = self.patch((0, 0)), self.patch((7, 3))
        np.testing.assert_allclose(a.directions[3:, 7:], b.directions[:-3, :-7], atol=1e-15)

    def test_sea_floor_and_points(self):
        patch = self.patch((0, 0))
        patch.set_heights(np.full(COLUMNS * COLUMNS, -50.0))
        self.assertTrue(np.all(patch.heights == 0.0))
        dry = ColliderPatch(FakeBody(ocean=False), SURFACE, self.east, self.north, (0, 0))
        dry.set_heights(np.full(COLUMNS * COLUMNS, -50.0))
        np.testing.assert_allclose(np.linalg.norm(dry.points(), axis=2), RADIUS - 50.0)

    def test_covers_and_normal(self):
        patch = self.patch((0, 0))
        patch.set_heights(np.full(COLUMNS * COLUMNS, 20.0))
        inside = normalize(add(scale(SURFACE, RADIUS), scale(self.east, 10.0)))
        outside = normalize(add(scale(SURFACE, RADIUS), scale(self.east, 17.0)))
        self.assertTrue(patch.covers(scale(inside, RADIUS)))
        self.assertFalse(patch.covers(scale(inside, RADIUS), margin=7.0))
        self.assertFalse(patch.covers(scale(outside, RADIUS)))
        self.assertGreater(dot(patch.normal(scale(inside, RADIUS + 20.0)), inside), 0.999999)
        self.assertIsNone(patch.normal(scale(outside, RADIUS)))


class TerrainColliderTest(unittest.TestCase):
    def setUp(self):
        self.engine = FakeEngine()
        self.physics = FakePhysics()
        self.body = FakeBody(ocean=False)
        self.bubble = Bubble(self.physics, self.body, GM, scale(SURFACE, RADIUS + 3.0))
        self.probes = ColliderProbes(self.engine)
        self.terrain = TerrainCollider(self.bubble, self.probes)

    def test_mesh_built_when_the_heights_come_back(self):
        here = (0.0, 0.0, 0.0)
        self.terrain.update(here, (0.0, 0.0, 0.0), 0.05, version=1)
        self.assertIsNone(self.terrain.mesh)
        self.assertEqual(self.engine.buffers["ColliderRequests"].shape, (COLUMNS * COLUMNS, 4))
        self.engine.answer(lambda d: 5.0)
        self.terrain.update(here, (0.0, 0.0, 0.0), 0.05, version=1)
        points, columns, rows, position = self.physics.meshes[self.terrain.mesh]
        self.assertEqual((columns, rows), (COLUMNS, COLUMNS))
        self.assertAlmostEqual(length(add(self.bubble.origin, position)), RADIUS, places=6)
        # The mesh's centre point is 5 m above the radius: 2 m below the bubble's origin.
        mid = (COLUMNS - 1) // 2
        centre = add(position, tuple(float(c) for c in points[mid, mid]))
        self.assertAlmostEqual(dot(centre, SURFACE), 2.0, places=3)
        self.assertTrue(self.terrain.covers(here, margin=15.0))

    def test_new_patch_once_the_focus_moves_a_quarter_away(self):
        east = self.bubble.east
        self.terrain.update((0.0, 0.0, 0.0), (0.0, 0.0, 0.0), 0.05, version=1)
        self.engine.answer(lambda d: 0.0)
        self.terrain.update((0.0, 0.0, 0.0), (0.0, 0.0, 0.0), 0.05, version=1)
        first = self.terrain.mesh
        self.terrain.update(scale(east, 7.0), (0.0, 0.0, 0.0), 0.05, version=1)
        self.assertIsNone(self.terrain.requested)
        # 2 s of 2 m/s ahead: 11 m from the centre, more than a quarter of 32 m.
        self.terrain.update(scale(east, 7.0), scale(east, 2.0), 2.0, version=1)
        self.assertIsNotNone(self.terrain.requested)
        self.assertEqual(self.terrain.requested.cell, (22, 0))
        self.engine.answer(lambda d: 0.0)
        self.terrain.update(scale(east, 7.0), (0.0, 0.0, 0.0), 0.05, version=1)
        self.assertNotEqual(self.terrain.mesh, first)
        self.assertEqual(list(self.physics.meshes), [self.terrain.mesh])

    def test_heights_from_an_earlier_terrain_are_dropped(self):
        self.terrain.update((0.0, 0.0, 0.0), (0.0, 0.0, 0.0), 0.05, version=1)
        self.engine.answer(lambda d: 0.0)
        self.terrain.update((0.0, 0.0, 0.0), (0.0, 0.0, 0.0), 0.05, version=2)
        self.assertIsNone(self.terrain.mesh)
        self.assertIsNotNone(self.terrain.requested)        # asked again under version 2
        self.engine.answer(lambda d: 0.0)
        self.terrain.update((0.0, 0.0, 0.0), (0.0, 0.0, 0.0), 0.05, version=2)
        self.assertIsNotNone(self.terrain.mesh)

    def test_pass_told_to_measure_nothing_when_idle(self):
        self.terrain.update((0.0, 0.0, 0.0), (0.0, 0.0, 0.0), 0.05, version=1)
        self.probes.end_frame()
        self.assertEqual(self.engine.custom["planet.collider.params"][0], float(COLUMNS * COLUMNS))
        self.probes.end_frame()
        self.assertEqual(self.engine.custom["planet.collider.params"][0], 0.0)

    def test_drop_removes_the_mesh(self):
        self.terrain.update((0.0, 0.0, 0.0), (0.0, 0.0, 0.0), 0.05, version=1)
        self.engine.answer(lambda d: 0.0)
        self.terrain.update((0.0, 0.0, 0.0), (0.0, 0.0, 0.0), 0.05, version=1)
        self.terrain.drop()
        self.assertEqual(self.physics.meshes, {})
        self.assertFalse(self.terrain.covers((0.0, 0.0, 0.0)))


if __name__ == "__main__":
    unittest.main()
