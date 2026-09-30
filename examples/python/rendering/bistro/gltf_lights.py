"""
The KHR_lights_punctual lights of a glTF file, placed in world space.

The engine's glTF loader brings in meshes and materials but not lights, so
the Bistro demo reads them here and creates them itself. Standard library
only.
"""

import json
import math


def _quat_matrix(q):
    x, y, z, w = q
    return [[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
            [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
            [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]]


def _local(node):
    """A node's local transform as a 4x4 row-major matrix."""
    if "matrix" in node:
        m = node["matrix"]                      # column-major in glTF
        return [[m[c * 4 + r] for c in range(4)] for r in range(4)]
    r = _quat_matrix(node.get("rotation", [0, 0, 0, 1]))
    s = node.get("scale", [1, 1, 1])
    t = node.get("translation", [0, 0, 0])
    return [[r[i][0] * s[0], r[i][1] * s[1], r[i][2] * s[2], t[i]] for i in range(3)] + [[0, 0, 0, 1]]


def _mul(a, b):
    return [[sum(a[i][k] * b[k][j] for k in range(4)) for j in range(4)] for i in range(4)]


class Light:
    def __init__(self, name, kind, position, direction, color, intensity, rng, inner, outer):
        self.name = name
        self.kind = kind                # "point", "spot" or "directional"
        self.position = position
        self.direction = direction      # where it shines (-z of the node)
        self.color = color
        self.intensity = intensity      # candela for point and spot, lux for directional
        self.range = rng
        self.inner_cone = inner
        self.outer_cone = outer


def lights(path):
    """Every light instance in the default scene, in world space."""
    with open(path, encoding="utf-8") as f:
        gltf = json.load(f)
    defs = gltf.get("extensions", {}).get("KHR_lights_punctual", {}).get("lights", [])
    nodes = gltf.get("nodes", [])
    out = []

    def visit(index, parent):
        node = nodes[index]
        world = _mul(parent, _local(node))
        ref = node.get("extensions", {}).get("KHR_lights_punctual")
        if ref is not None:
            d = defs[ref["light"]]
            position = tuple(world[i][3] for i in range(3))
            forward = [-world[i][2] for i in range(3)]
            length = math.sqrt(sum(v * v for v in forward)) or 1.0
            spot = d.get("spot", {})
            out.append(Light(node.get("name") or d.get("name", ""), d["type"], position,
                             tuple(v / length for v in forward), tuple(d.get("color", (1, 1, 1))),
                             d.get("intensity", 1.0), d.get("range"),
                             spot.get("innerConeAngle", 0.0), spot.get("outerConeAngle", math.pi / 4)))
        for child in node.get("children", []):
            visit(child, world)

    identity = [[1.0 if i == j else 0.0 for j in range(4)] for i in range(4)]
    scene = gltf.get("scenes", [{}])[gltf.get("scene", 0)]
    for root in scene.get("nodes", []):
        visit(root, identity)
    return out
