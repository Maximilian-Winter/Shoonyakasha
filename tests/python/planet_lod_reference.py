"""The Python level of detail of shoonyakasha.planet before it moved to C++
(sk.PlanetLod): quadtree.QuadTree with the chunk pool, the sun's rebuild
queue, culling and the draw lists as PlanetWorld ran them, without the
engine. tests/python/test_planet_native_lod.py compares the two.
"""

import math

import numpy as np

from shoonyakasha.planet.quadtree import QuadTree, hidden_by_horizon
from shoonyakasha.planet.vec import add, dot, length, sub


class Pool:
    def __init__(self, slots, max_jobs, skirt_fraction):
        self.slots, self.max_jobs, self.skirt_fraction = slots, max_jobs, skirt_fraction
        self.free = list(range(slots - 1, -1, -1))
        self.jobs = []
        self.job_slots = set()
        self.pending = {}
        self.body = {}
        self.stamp = 0

    def make(self, node, body):
        if not self.free or len(self.jobs) == self.max_jobs:
            return None
        slot = self.free.pop()
        self.body[slot] = body
        self.add_job(node, slot)
        return slot

    def rebuild(self, node):
        if node.chunk in self.job_slots:
            return True
        if len(self.jobs) == self.max_jobs:
            return False
        self.add_job(node, node.chunk)
        return True

    def add_job(self, node, slot):
        self.stamp += 1
        self.jobs.append(((float(node.face), float(node.level), float(node.x), float(node.y)),
                          (float(slot), self.skirt_fraction * node.edge, float(self.stamp), float(self.body[slot]))))
        self.pending[slot] = (node, float(self.stamp))
        self.job_slots.add(slot)

    def release(self, node):
        self.pending.pop(node.chunk, None)
        self.body.pop(node.chunk, None)
        self.free.append(node.chunk)

    def clear_jobs(self):
        self.jobs = []
        self.job_slots = set()

    def take_heights(self, heights):
        for slot, (node, stamp) in list(self.pending.items()):
            if heights[slot, 2] == stamp:
                node.set_heights(float(heights[slot, 0]), float(heights[slot, 1]))
                del self.pending[slot]

    @property
    def used(self):
        return self.slots - len(self.free)


def level_colour(level):
    hue = level * 0.61803398875
    return tuple(0.45 + 0.30 * math.cos(2.0 * math.pi * (hue - offset)) for offset in (0.0, 1 / 3, 2 / 3))


class ReferenceLod:
    """Bodies are (index, centre, occluder, ocean); a view (position, forward, fov, aspect)."""

    def __init__(self, slots, max_jobs, vertices_per_chunk, skirt_fraction, face_colours):
        self.pool = Pool(slots, max_jobs, skirt_fraction)
        self.vertices_per_chunk = vertices_per_chunk
        self.face_colours = face_colours
        self.trees = {}
        self.rebuild = []

    def create_tree(self, body, radius, low, high, max_level, split_factor, height_pad):
        self.trees[body] = QuadTree(radius, lambda node: self.pool.make(node, body), self.pool.release,
                                    max_level=max_level, split_factor=split_factor, heights=(low, high),
                                    height_pad=height_pad)

    def drop_tree(self, body):
        tree = self.trees.pop(body)
        for node in tree.nodes():
            if node.chunk is not None:
                self.pool.release(node)
                node.chunk = None

    @staticmethod
    def in_view(view, centre, radius):
        position, forward, fov, aspect = view
        to_centre = sub(centre, position)
        distance = length(to_centre)
        if distance <= radius:
            return True
        half_diagonal = math.atan(math.tan(math.radians(fov) * 0.5) * math.sqrt(1.0 + aspect * aspect))
        angle = math.acos(max(-1.0, min(1.0, dot(to_centre, forward) / distance)))
        return angle - math.asin(min(1.0, radius / distance)) < half_diagonal

    def update(self, view, bodies, frame, frozen=False, requeue=False):
        position = view[0]
        with_trees = [b for b in bodies if b[0] in self.trees]
        if not with_trees or frozen:
            return
        for index, centre, _, _ in with_trees:
            tree = self.trees[index]
            tree.update(sub(position, centre), frame)
            tree.build(self.pool.max_jobs, priority=lambda distance, node, centre=centre: (
                not self.in_view(view, add(centre, node.bound_centre), node.bound_radius), distance))
            tree.evict(frame)
        if requeue:
            distances = []
            for index, centre, _, _ in with_trees:
                eye = sub(position, centre)
                distances += [(length(sub(node.bound_centre, eye)), node)
                              for node in self.trees[index].nodes() if node.chunk is not None]
            distances.sort(key=lambda pair: pair[0], reverse=True)
            self.rebuild = [node for _, node in distances]
        queue = self.rebuild
        while queue:
            node = queue[-1]
            if node.chunk is None:
                queue.pop()
                continue
            if not self.pool.rebuild(node):
                break
            queue.pop()

    def chunk_colour(self, node, mode):
        if mode == 1:
            return tuple(self.face_colours[node.face]) + (1.0,)
        if mode == 2:
            return level_colour(node.level) + (1.0,)
        return (-1.0, 0.0, 0.0, 0.0)

    def write_draws(self, view, bodies, colour_mode, shadow_reach, index_count):
        """(instances, chunk draws, ocean draws, shadow draws, culled) as numpy arrays."""
        position = view[0]
        slots = self.pool.slots
        instances = np.zeros((slots, 3, 4), dtype=np.float32)
        selected, casters, wet = [], [], []
        culled = 0
        for index, centre, occluder, ocean in bodies:
            tree = self.trees.get(index)
            if tree is None:
                continue
            eye = sub(position, centre)
            for node in tree.selected:
                slot = node.chunk
                instances[slot, 0, :3] = sub(node.centre, eye)
                instances[slot, 1] = self.chunk_colour(node, colour_mode)
                parent_split = tree.split_factor * 2.0 * node.edge if node.level > 0 else 0.0
                instances[slot, 2, :3] = (parent_split, node.level, index)
                if length(sub(node.bound_centre, eye)) - node.bound_radius < shadow_reach:
                    casters.append(node)
                visible = (self.in_view(view, add(centre, node.bound_centre), node.bound_radius)
                           and not hidden_by_horizon(node.bound_centre, node.bound_radius, eye, occluder))
                if not visible:
                    culled += 1
                    continue
                selected.append(node)
                if ocean and node.low < 0.0:
                    wet.append(node)

        def draws(nodes):
            out = np.zeros(4 + 5 * len(nodes), dtype=np.uint32)
            out[0] = len(nodes)
            for k, node in enumerate(nodes):
                out[4 + 5 * k:9 + 5 * k] = (index_count, 1, 0, node.chunk * self.vertices_per_chunk, node.chunk)
            return out

        return instances, draws(selected), draws(wet), draws(casters), culled
