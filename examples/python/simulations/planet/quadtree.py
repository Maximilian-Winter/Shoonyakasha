"""
Level-of-detail selection over the cube-sphere: one quadtree per cube face.

Each frame `QuadTree.update(camera, frame)` chooses the chunks to draw and
lists the chunks it is missing. A node splits into its four children when
the camera is within `split_factor` times the node's edge length of its
bounding sphere, and stays split until the camera is farther than
`merge_ratio` times that. A node is drawn until all four of its children
have a chunk, so there are no holes while chunks are built.

A node's bounding sphere holds its patch of the sphere raised and lowered
through its height range: the range its parent had until
`Node.set_heights` gives the node its own, plus `height_pad` times its
edge length for detail between the heights measured.

The tree does not touch the engine: `make(node)` builds a node's chunk and
`release(node)` destroys it, both given by the caller.
"""

import math

import cube_sphere


class Node:
    __slots__ = ("face", "level", "x", "y", "centre", "edge", "horizontal_radius", "radius", "height_pad",
                 "low", "high", "measured", "bound_centre", "bound_radius", "children", "chunk", "last_used")

    def __init__(self, face, level, x, y, radius, low, high, height_pad):
        self.face, self.level, self.x, self.y = face, level, x, y
        self.radius = radius
        self.centre = cube_sphere.chunk_centre(face, level, x, y, radius)
        u0, v0, size = cube_sphere.chunk_bounds(level, x, y)
        farthest = 0.0
        for su in (0.0, 0.5, 1.0):
            for sv in (0.0, 0.5, 1.0):
                d = cube_sphere.direction(face, u0 + su * size, v0 + sv * size)
                farthest = max(farthest, _distance(self.centre, (d[0] * radius, d[1] * radius, d[2] * radius)))
        self.horizontal_radius = farthest   # from the centre to the farthest point of the patch, at `radius`
        self.edge = radius * (math.pi / 2.0) / (1 << level)     # metres along one side
        self.height_pad = height_pad * self.edge
        self.measured = False
        self._set_range(low, high)
        self.children = None
        self.chunk = None         # what `make` returned; None until built
        self.last_used = 0

    def set_heights(self, low, high):
        """The lowest and highest heights measured on the node's chunk."""
        self.measured = True
        self._set_range(low, high)

    def _set_range(self, low, high):
        self.low, self.high = low, high
        low, high = low - self.height_pad, high + self.height_pad
        middle = self.radius + 0.5 * (low + high)
        self.bound_centre = tuple(c * middle / self.radius for c in self.centre)
        self.bound_radius = self.horizontal_radius * (self.radius + high) / self.radius + 0.5 * (high - low)

    @property
    def ready(self):
        return self.chunk is not None

    def key(self):
        return (self.face, self.level, self.x, self.y)


def _distance(a, b):
    return math.sqrt((a[0] - b[0]) ** 2 + (a[1] - b[1]) ** 2 + (a[2] - b[2]) ** 2)


class QuadTree:
    def __init__(self, radius, make, release, max_level=12, split_factor=2.0, merge_ratio=1.25,
                 heights=(0.0, 0.0), height_pad=0.0, evict_frames=120):
        """`heights` is the (lowest, highest) height range of the roots
        until their chunks are measured."""
        self.radius = radius
        self.make, self.release = make, release
        self.max_level = max_level
        self.split_factor = split_factor
        self.merge_ratio = merge_ratio
        self.height_pad = height_pad
        self.evict_frames = evict_frames
        self.roots = [Node(face, 0, 0, 0, radius, heights[0], heights[1], height_pad) for face in range(6)]
        for root in self.roots:
            root.chunk = make(root)
        self.selected = []
        self.missing = []          # (distance, node) the last update wanted built
        self._split = set()        # keys of nodes drawn through their children last update

    def update(self, camera, frame):
        """Choose the nodes to draw (`self.selected`) and the missing ones
        (`self.missing`, nearest first) for a camera at `camera` (planet
        space, metres)."""
        self.selected = []
        missing = []
        split_now = set()
        for root in self.roots:
            self._select(root, camera, frame, missing, split_now)
        self._split = split_now
        missing.sort(key=lambda item: item[0])
        self.missing = missing

    def _select(self, node, camera, frame, missing, split_now):
        node.last_used = frame
        distance = max(0.0, _distance(camera, node.bound_centre) - node.bound_radius)
        factor = self.split_factor * (self.merge_ratio if node.key() in self._split else 1.0)
        if node.level < self.max_level and distance < factor * node.edge:
            if node.children is None:
                node.children = [Node(node.face, node.level + 1, 2 * node.x + dx, 2 * node.y + dy,
                                      self.radius, node.low, node.high, self.height_pad)
                                 for dy in (0, 1) for dx in (0, 1)]
            if all(child.ready for child in node.children):
                split_now.add(node.key())
                for child in node.children:
                    self._select(child, camera, frame, missing, split_now)
                return
            for child in node.children:
                child.last_used = frame
                if not child.ready:
                    missing.append((max(0.0, _distance(camera, child.bound_centre) - child.bound_radius), child))
        self.selected.append(node)

    def build(self, budget, priority=None):
        """Build up to `budget` of the missing chunks, nearest first, or by
        `priority(distance, node)` (smaller first) when given. Returns how
        many were built."""
        queue = self.missing
        if priority is not None:
            queue = sorted(queue, key=lambda item: priority(*item))
        built = 0
        for _, node in queue:
            if built == budget:
                break
            if node.chunk is None:
                node.chunk = self.make(node)
                built += 1
        return built

    def evict(self, frame):
        """Release the chunks of every subtree no update has used for
        `evict_frames` frames, and forget those subtrees."""
        for root in self.roots:
            self._evict(root, frame)

    def _evict(self, node, frame):
        if node.children is None:
            return
        if all(frame - child.last_used > self.evict_frames for child in node.children):
            for child in node.children:
                self._release_subtree(child)
            node.children = None
            return
        for child in node.children:
            self._evict(child, frame)

    def _release_subtree(self, node):
        if node.children is not None:
            for child in node.children:
                self._release_subtree(child)
        if node.chunk is not None:
            self.release(node)
            node.chunk = None

    def nodes(self):
        """Every node in the tree."""
        stack = list(self.roots)
        while stack:
            node = stack.pop()
            yield node
            if node.children is not None:
                stack.extend(node.children)
