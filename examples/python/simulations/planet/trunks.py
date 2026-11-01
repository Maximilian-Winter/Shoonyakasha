"""
Tree trunks in the physics bubble (shoonyakasha.planet.bubble): a static
capsule for each tree and cactus near the camera that
shoonyakasha.planet.nearby_trees lists.

The scatter lists only the plants on chunks drawn that frame, so a list
adds trunks and does not remove them: `Trunks.update` gives each listed
plant not yet in the bubble, or whose base or kind changed (by more than
MOVED), a capsule of its trunk's radius standing along the body's up from
SINK below its base to its trunk's height, and removes the trunks more
than KEEP beyond the list's radius from the camera. The plants are the
planet's, so the bubble must be on the planet. `clear` removes them all
(when the terrain or the vegetation changes).
"""

from dataclasses import dataclass

from shoonyakasha.planet.vec import add, cross, length, local_frame, scale, sub

from flight import quat_from_basis

SINK = 0.5                       # m a trunk reaches below its base, into the ground
MOVED = 0.05                     # m a base may move and still be the same trunk
KEEP = 10.0                      # m beyond the list's radius a trunk is kept


@dataclass
class Trunk:
    body: int
    kind: str
    base: tuple                  # relative to the body's centre
    radius: float                # m


class Trunks:
    def __init__(self):
        self.trunks = {}             # key: Trunk

    def update(self, bubble, trees, camera, radius):
        """The trunks of `trees` (nearby_trees.NearbyTree, planet space) in
        `bubble`; those farther than `radius` + KEEP from `camera` (planet
        space) removed."""
        physics, body = bubble.physics, bubble.body
        for tree in trees:
            base = body.relative(tree.base)
            trunk = self.trunks.get(tree.key)
            if trunk is not None and trunk.kind == tree.kind and length(sub(trunk.base, base)) <= MOVED:
                continue
            if trunk is not None:
                physics.remove(trunk.body)
            trunk_radius, height = tree.trunk
            p = bubble.to_bubble(tree.base)
            up = bubble.up(p)
            east, _ = local_frame(up)
            rotation = quat_from_basis(east, up, cross(east, up))
            half = max(0.5 * (height + SINK) - trunk_radius, 0.01)
            centre = add(p, scale(up, 0.5 * (height - SINK)))
            capsule = physics.create_capsule(trunk_radius, half, centre, rotation, motion="static", friction=0.8)
            self.trunks[tree.key] = Trunk(capsule, tree.kind, base, trunk_radius)
        here = body.relative(camera)
        for key in [k for k, t in self.trunks.items() if length(sub(t.base, here)) > radius + KEEP]:
            physics.remove(self.trunks.pop(key).body)

    def clear(self, bubble):
        for trunk in self.trunks.values():
            if bubble is not None and bubble.physics.contains(trunk.body):
                bubble.physics.remove(trunk.body)
        self.trunks.clear()
