"""
A patch of a body's surface from terrain heights measured at a square
grid of directions, for contact points against the ground.

The grid lies in the plane tangent to the sphere at the patch's centre
direction, SIZE x SIZE points `spacing` metres apart along its east and
north axes; each point's direction is from the body's centre through it.
On a body with a sea, heights below sea level count as sea level, so the
sea is a floor; with `water`, heights below the water level it gives for a
point's direction count as that level, so lakes are floors too.

`GroundPatch.query` answers for a point relative to the body's centre: the
surface under it, interpolated bilinearly between the four grid heights
around it (in the tangent plane's coordinates, where the point's direction
meets the plane), and the surface's normal from the same cell. Points outside the
grid take the nearest cell's edge; points further outside than half the
patch's width have no answer.
"""

import math

from shoonyakasha.planet.vec import add, cross, dot, normalize, scale, sub

SIZE = 13                       # grid points along each side


def _lerp(a, b, t):
    return add(a, scale(sub(b, a), t))


def tangent_axes(centre):
    """(east, north) at the unit direction `centre`; east x north == centre."""
    east = cross((0.0, 1.0, 0.0), centre)
    if dot(east, east) < 1e-18:
        east = (1.0, 0.0, 0.0)
    east = normalize(east)
    return east, cross(centre, east)


class GroundPatch:
    def __init__(self, radius, centre, spacing, sea=True, water=None):
        """A grid around the unit direction `centre` of a sphere of `radius`
        metres, its points `spacing` metres apart; heights come with
        `set_heights`. `sea`: heights below 0 count as 0. `water`: a
        function of a unit direction giving the water level (m) over it, or
        None where there is none; heights below it count as it."""
        self.radius = radius
        self.sea = sea
        self.centre = normalize(centre)
        self.spacing = spacing
        self.east, self.north = tangent_axes(self.centre)
        mid = (SIZE - 1) * 0.5
        origin = scale(self.centre, radius)
        # Row-major: index j * SIZE + i, i along east, j along north.
        self.directions = [
            normalize(add(origin, add(scale(self.east, (i - mid) * spacing),
                                         scale(self.north, (j - mid) * spacing))))
            for j in range(SIZE) for i in range(SIZE)]
        self.floors = [water(d) if water is not None else None for d in self.directions]
        self.heights = None
        self.points = None

    def set_heights(self, heights):
        """Terrain heights (m) at `directions`, in their order."""
        heights = [max(float(h), 0.0) if self.sea else float(h) for h in heights]
        self.heights = [h if floor is None else max(h, floor) for h, floor in zip(heights, self.floors)]
        self.points = [scale(d, self.radius + h) for d, h in zip(self.directions, self.heights)]

    def grid_coordinates(self, point):
        """(x, y): where `point`'s direction meets the tangent plane, in grid
        steps from the grid's first point; None on the far side."""
        d = normalize(point)
        along = dot(d, self.centre)
        if along <= 0.0:
            return None
        on_plane = sub(scale(d, self.radius / along), scale(self.centre, self.radius))
        mid = (SIZE - 1) * 0.5
        return (dot(on_plane, self.east) / self.spacing + mid, dot(on_plane, self.north) / self.spacing + mid)

    def query(self, point):
        """(depth, normal) of the surface under `point`: how far `point` is
        below the surface along its normal (negative above it), and the
        unit normal. None when the point is too far outside the patch."""
        xy = self.grid_coordinates(point)
        if xy is None:
            return None
        x, y = xy
        last = SIZE - 1
        reach = last * 0.5
        if x < -reach or y < -reach or x > last + reach or y > last + reach:
            return None
        x, y = min(max(x, 0.0), float(last)), min(max(y, 0.0), float(last))
        i, j = min(int(x), last - 1), min(int(y), last - 1)
        fx, fy = x - i, y - j
        h = self.heights
        p = self.points
        k = j * SIZE + i
        height = ((h[k] * (1.0 - fx) + h[k + 1] * fx) * (1.0 - fy)
                  + (h[k + SIZE] * (1.0 - fx) + h[k + SIZE + 1] * fx) * fy)
        along_x = _lerp(sub(p[k + 1], p[k]), sub(p[k + SIZE + 1], p[k + SIZE]), fy)
        along_y = _lerp(sub(p[k + SIZE], p[k]), sub(p[k + SIZE + 1], p[k + 1]), fx)
        normal = normalize(cross(along_x, along_y))
        r = math.sqrt(dot(point, point))
        radial = scale(point, 1.0 / r)
        return (self.radius + height - r) * dot(radial, normal), normal
