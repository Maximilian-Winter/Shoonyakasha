"""
Level-of-detail selection over the cube-sphere: one quadtree per cube face.

Each frame `QuadTree.update(camera, frame)` chooses the chunks to draw and
lists the chunks it is missing. A node splits into its four children when
the camera is within `split_factor` times the node's edge length of its
bounding sphere, and stays split until the camera is farther than
`merge_factor` times that. A node is drawn until all four of its children
have a chunk, so there are no holes while chunks are built.

The tree does not touch the engine: `make(node)` builds a node's chunk and
`release(node)` destroys it, both given by the caller.
"""

import math

import cube_sphere


class Node:
    __slots__ = ("face", "level", "x", "y", "centre", "bound_radius", "edge", "children",
                 "chunk", "last_used")

    def __init__(self, face, level, x, y, radius, height_margin):
        self.face, self.level, self.x, self.y = face, level, x, y
        self.centre = cube_sphere.chunk_centre(face, level, x, y, radius)
        u0, v0, size = cube_sphere.chunk_bounds(level, x, y)
        farthest = 0.0
        for su in (0.0, 0.5, 1.0):
            for sv in (0.0, 0.5, 1.0):
                d = cube_sphere.direction(face, u0 + su * size, v0 + sv * size)
                farthest = max(farthest, _distance(self.centre, (d[0] * radius, d[1] * radius, d[2] * radius)))
        self.bound_radius = farthest + height_margin
        self.edge = radius * (math.pi / 2.0) / (1 << level)     # metres along one side
        self.children = None
        self.chunk = None         # what `make` returned; None until built
        self.last_used = 0

    @property
    def ready(self):
        return self.chunk is not None

    def key(self):
        return (self.face, self.level, self.x, self.y)


def _distance(a, b):
    return math.sqrt((a[0] - b[0]) ** 2 + (a[1] - b[1]) ** 2 + (a[2] - b[2]) ** 2)


class QuadTree:
    def __init__(self, radius, make, release, max_level=12, split_factor=2.0, merge_ratio=1.25,
                 height_margin=0.0, evict_frames=120):
        self.radius = radius
        self.make, self.release = make, release
        self.max_level = max_level
        self.split_factor = split_factor
        self.merge_ratio = merge_ratio
        self.height_margin = height_margin
        self.evict_frames = evict_frames
        self.roots = [Node(face, 0, 0, 0, radius, height_margin) for face in range(6)]
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
        distance = max(0.0, _distance(camera, node.centre) - node.bound_radius)
        factor = self.split_factor * (self.merge_ratio if node.key() in self._split else 1.0)
        if node.level < self.max_level and distance < factor * node.edge:
            if node.children is None:
                node.children = [Node(node.face, node.level + 1, 2 * node.x + dx, 2 * node.y + dy,
                                      self.radius, self.height_margin)
                                 for dy in (0, 1) for dx in (0, 1)]
            if all(child.ready for child in node.children):
                split_now.add(node.key())
                for child in node.children:
                    self._select(child, camera, frame, missing, split_now)
                return
            for child in node.children:
                child.last_used = frame
                if not child.ready:
                    missing.append((max(0.0, _distance(camera, child.centre) - child.bound_radius), child))
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
