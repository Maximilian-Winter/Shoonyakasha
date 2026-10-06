"""
The showroom's studio, written as glTF files and an HDR environment.

Nothing here is downloaded: the stage is generated on first start into
assets/showroom/studio/ and reused after that (delete the directory to
regenerate it).

- stage.gltf: an infinity cove, a round floor that curves up into a round
  wall with no corner, so the backdrop has no horizon from any angle. The
  floor and the cove are separate nodes ("floor", "cove") so the showroom
  can colour them apart.
- turntable.gltf: a disc of radius 1 and 6 cm high, with a light strip
  round its rim ("turntable_top", "turntable_rim").
- softbox.gltf: a 1 x 1 light panel facing its node's forward axis (-Z),
  with a dark frame and back ("softbox_diffuser", "softbox_frame").
- plinth.gltf: a 1 x 1 x 1 box standing on the floor, for small models.
- studio.hdr: an equirectangular studio environment, dark walls and floor
  with bright softboxes where the "studio" light rig has its lights, so
  reflections in paint and glass match the lights that light the car.

Pure Python, no dependencies; numpy makes the environment faster if present.
"""

import json
import math
import os
import struct

VERSION = 3          # bump to regenerate existing studios


# ── glTF writing ─────────────────────────────────────────────────────────

class GltfWriter:
    """A small glTF 2.0 writer: meshes of one primitive each, one buffer."""

    def __init__(self):
        self.gltf = {"asset": {"version": "2.0", "generator": "shoonyakasha showroom studio.py"},
                     "scene": 0, "scenes": [{"nodes": []}], "nodes": [], "meshes": [],
                     "materials": [], "accessors": [], "bufferViews": [], "buffers": []}
        self.blob = bytearray()
        self.material_index = {}

    def material(self, name, color, metallic=0.0, roughness=0.5, emissive=(0.0, 0.0, 0.0), double_sided=False):
        if name not in self.material_index:
            self.material_index[name] = len(self.gltf["materials"])
            self.gltf["materials"].append({
                "name": name,
                "pbrMetallicRoughness": {"baseColorFactor": list(color) + [1.0] if len(color) == 3 else list(color),
                                         "metallicFactor": metallic, "roughnessFactor": roughness},
                "emissiveFactor": list(emissive),
                "doubleSided": double_sided,
            })
        return self.material_index[name]

    def _view(self, data, target):
        while len(self.blob) % 4:
            self.blob.append(0)
        offset = len(self.blob)
        self.blob += data
        self.gltf["bufferViews"].append({"buffer": 0, "byteOffset": offset, "byteLength": len(data), "target": target})
        return len(self.gltf["bufferViews"]) - 1

    def _accessor(self, values, components, kind):
        flat = [c for v in values for c in v] if components > 1 else list(values)
        data = struct.pack("<%df" % len(flat), *flat)
        accessor = {"bufferView": self._view(data, 34962), "componentType": 5126, "count": len(values), "type": kind}
        if kind == "VEC3":
            accessor["min"] = [min(v[i] for v in values) for i in range(3)]
            accessor["max"] = [max(v[i] for v in values) for i in range(3)]
        self.gltf["accessors"].append(accessor)
        return len(self.gltf["accessors"]) - 1

    def mesh(self, name, positions, normals, uvs, indices, material):
        index_data = struct.pack("<%dI" % len(indices), *indices)
        self.gltf["accessors"].append({"bufferView": self._view(index_data, 34963), "componentType": 5125,
                                       "count": len(indices), "type": "SCALAR"})
        index_accessor = len(self.gltf["accessors"]) - 1
        self.gltf["meshes"].append({"name": name, "primitives": [{
            "attributes": {"POSITION": self._accessor(positions, 3, "VEC3"),
                           "NORMAL": self._accessor(normals, 3, "VEC3"),
                           "TEXCOORD_0": self._accessor(uvs, 2, "VEC2")},
            "indices": index_accessor, "material": material}]})
        return len(self.gltf["meshes"]) - 1

    def node(self, name, mesh=None, children=None, root=True, **transform):
        node = {"name": name}
        if mesh is not None:
            node["mesh"] = mesh
        if children:
            node["children"] = children
        node.update(transform)
        self.gltf["nodes"].append(node)
        index = len(self.gltf["nodes"]) - 1
        if root:
            self.gltf["scenes"][0]["nodes"].append(index)
        return index

    def write(self, path):
        base = os.path.splitext(os.path.basename(path))[0]
        self.gltf["buffers"] = [{"uri": base + ".bin", "byteLength": len(self.blob)}]
        with open(os.path.join(os.path.dirname(path), base + ".bin"), "wb") as f:
            f.write(self.blob)
        with open(path, "w", encoding="utf-8") as f:
            json.dump(self.gltf, f, indent=1)


def revolve(profile, segments, uv_scale=1.0):
    """A surface of revolution about +Y. `profile` is a list of
    (radius, height, normal_r, normal_y) from the axis outwards; returns
    positions, normals, uvs and indices facing the side the normals give."""
    positions, normals, uvs, indices = [], [], [], []
    rows = len(profile)
    for s in range(segments + 1):
        a = 2.0 * math.pi * s / segments
        c, n = math.cos(a), math.sin(a)
        for r, y, nr, ny in profile:
            positions.append((r * c, y, r * n))
            normals.append((nr * c, ny, nr * n))
            uvs.append((r * c * uv_scale, r * n * uv_scale))
    for s in range(segments):
        for i in range(rows - 1):
            a = s * rows + i
            b = (s + 1) * rows + i
            # Counter-clockwise seen from the side the normal points to.
            indices += [a, a + 1, b, b, a + 1, b + 1]
    return positions, normals, uvs, indices


def _orient(positions, normals, indices):
    """Flip triangles whose winding disagrees with their normals."""
    out = []
    for t in range(0, len(indices), 3):
        i, j, k = indices[t:t + 3]
        p0, p1, p2 = positions[i], positions[j], positions[k]
        e1 = [p1[m] - p0[m] for m in range(3)]
        e2 = [p2[m] - p0[m] for m in range(3)]
        face = (e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0])
        avg = [normals[i][m] + normals[j][m] + normals[k][m] for m in range(3)]
        out += [i, j, k] if sum(face[m] * avg[m] for m in range(3)) >= 0.0 else [i, k, j]
    return out


def box(sx, sy, sz, y0=0.0):
    """An axis-aligned box from y0 up, centred on x and z."""
    positions, normals, uvs, indices = [], [], [], []
    hx, hz = sx / 2.0, sz / 2.0
    faces = [((1, 0, 0), [(hx, 0, -hz), (hx, 0, hz), (hx, sy, hz), (hx, sy, -hz)]),
             ((-1, 0, 0), [(-hx, 0, hz), (-hx, 0, -hz), (-hx, sy, -hz), (-hx, sy, hz)]),
             ((0, 1, 0), [(-hx, sy, -hz), (hx, sy, -hz), (hx, sy, hz), (-hx, sy, hz)]),
             ((0, -1, 0), [(-hx, 0, hz), (hx, 0, hz), (hx, 0, -hz), (-hx, 0, -hz)]),
             ((0, 0, 1), [(hx, 0, hz), (-hx, 0, hz), (-hx, sy, hz), (hx, sy, hz)]),
             ((0, 0, -1), [(-hx, 0, -hz), (hx, 0, -hz), (hx, sy, -hz), (-hx, sy, -hz)])]
    for normal, corners in faces:
        base = len(positions)
        for k, (x, y, z) in enumerate(corners):
            positions.append((x, y + y0, z))
            normals.append(normal)
            uvs.append(((0, 1, 1, 0)[k], (0, 0, 1, 1)[k]))
        indices += [base, base + 1, base + 2, base, base + 2, base + 3]
    return positions, normals, uvs, _orient(positions, normals, indices)


# ── The stage ────────────────────────────────────────────────────────────

FLOOR_RADIUS = 14.0      # where the floor starts curving up
COVE_RADIUS = 4.0        # radius of the curve between floor and wall
WALL_HEIGHT = 14.0       # wall above the curve


def stage(path):
    w = GltfWriter()
    floor_material = w.material("studio_floor", (0.16, 0.16, 0.17), roughness=0.32)
    cove_material = w.material("studio_cove", (0.5, 0.5, 0.52), roughness=0.85)

    # The floor: rings out to where the cove starts, finer near the middle.
    rings = [0.0] + [FLOOR_RADIUS * (i / 24.0) ** 1.4 for i in range(1, 25)]
    profile = [(r, 0.0, 0.0, 1.0) for r in rings]
    floor = w.mesh("floor", *_revolve_oriented(profile, 160, 0.25), floor_material)

    # The cove: a quarter circle up from the floor's edge, then the wall.
    # Normals point in and up, towards the stage.
    profile = []
    for i in range(17):
        a = (math.pi / 2.0) * i / 16.0
        r = FLOOR_RADIUS + COVE_RADIUS * math.sin(a)
        y = COVE_RADIUS * (1.0 - math.cos(a))
        profile.append((r, y, -math.sin(a), math.cos(a)))
    for i in range(1, 9):
        profile.append((FLOOR_RADIUS + COVE_RADIUS, COVE_RADIUS + WALL_HEIGHT * i / 8.0, -1.0, 0.0))
    cove = w.mesh("cove", *_revolve_oriented(profile, 160, 0.25), cove_material)

    w.node("floor", floor)
    w.node("cove", cove)
    w.write(path)


def _revolve_oriented(profile, segments, uv_scale):
    positions, normals, uvs, indices = revolve(profile, segments, uv_scale)
    return positions, normals, uvs, _orient(positions, normals, indices)


def turntable(path):
    w = GltfWriter()
    top = w.material("turntable_top", (0.05, 0.05, 0.055), metallic=0.85, roughness=0.28)
    rim = w.material("turntable_rim", (0.02, 0.02, 0.02), roughness=0.4, emissive=(0.0, 0.0, 0.0))
    height = 0.06
    bevel = 0.015
    # The top: flat out to the bevel, then round over to the side.
    profile = [(0.0, height, 0.0, 1.0), (0.5, height, 0.0, 1.0), (1.0 - bevel, height, 0.0, 1.0)]
    for i in range(1, 5):
        a = (math.pi / 2.0) * i / 4.0
        profile.append((1.0 - bevel + bevel * math.sin(a), height - bevel + bevel * math.cos(a),
                        math.sin(a), math.cos(a)))
    top_mesh = w.mesh("turntable_top", *_revolve_oriented(profile, 128, 0.5), top)
    # The side: a light strip all the way round.
    profile = [(1.0, height - bevel, 1.0, 0.0), (1.0, 0.0, 1.0, 0.0)]
    rim_mesh = w.mesh("turntable_rim", *_revolve_oriented(profile, 128, 0.5), rim)
    w.node("turntable_top", top_mesh)
    w.node("turntable_rim", rim_mesh)
    w.write(path)


def softbox(path):
    w = GltfWriter()
    diffuser = w.material("softbox_diffuser", (1.0, 1.0, 1.0), roughness=1.0, emissive=(1.0, 1.0, 1.0))
    frame = w.material("softbox_frame", (0.015, 0.015, 0.015), roughness=0.6)
    # The diffuser faces -Z, the node's forward axis, just in front of the box.
    positions = [(-0.5, -0.5, -0.101), (0.5, -0.5, -0.101), (0.5, 0.5, -0.101), (-0.5, 0.5, -0.101)]
    normals = [(0.0, 0.0, -1.0)] * 4
    uvs = [(0, 0), (1, 0), (1, 1), (0, 1)]
    indices = _orient(positions, normals, [0, 1, 2, 0, 2, 3])
    panel = w.mesh("softbox_diffuser", positions, normals, uvs, indices, diffuser)
    # The box behind it, 1.04 x 1.04 x 0.2, open at the front.
    p, n, u, i = box(1.04, 1.04, 0.2, y0=-0.52)
    keep = [k for k in range(0, len(i), 3) if not all(abs(p[v][2] + 0.1) < 1e-6 for v in i[k:k + 3])]
    body = w.mesh("softbox_frame", p, n, u, [v for k in keep for v in i[k:k + 3]], frame)
    w.node("softbox_diffuser", panel)
    w.node("softbox_frame", body)
    w.write(path)


def plinth(path):
    w = GltfWriter()
    material = w.material("plinth", (0.82, 0.82, 0.8), roughness=0.55)
    w.node("plinth", w.mesh("plinth", *box(1.0, 1.0, 1.0), material))
    w.write(path)


# ── The environment ──────────────────────────────────────────────────────

# Softboxes in the environment: (azimuth, elevation, width, height) in
# degrees of the panel's centre and its angular size, and radiance. Azimuth
# 0 is -Z (the front of the stage), 90 is +X. Each is a rectangle facing the
# stage's centre.
ENV_SOFTBOXES = [
    (-52.0, 32.0, 30.0, 20.0, 9.0),     # key, front left
    (68.0, 18.0, 22.0, 15.0, 3.5),      # fill, right
    (180.0, 34.0, 28.0, 12.0, 7.0),     # rim, behind
    (0.0, 88.0, 16.0, 70.0, 12.0),      # overhead strip, front to back
    (125.0, 14.0, 8.0, 28.0, 2.5),      # strip, back right
    (-125.0, 14.0, 8.0, 28.0, 2.5),     # strip, back left
]


def _direction(azimuth, elevation):
    a, e = math.radians(azimuth), math.radians(elevation)
    return (math.sin(a) * math.cos(e), math.sin(e), -math.cos(a) * math.cos(e))


def _panel_frames():
    """Per softbox: centre direction, right and up axes, half tangents."""
    frames = []
    for az, el, w, h, radiance in ENV_SOFTBOXES:
        c = _direction(az, el)
        ref = (0.0, 1.0, 0.0) if abs(c[1]) < 0.95 else (math.sin(math.radians(az)), 0.0, -math.cos(math.radians(az)))
        right = _normalize(_cross(ref, c))
        up = _cross(c, right)
        frames.append((c, right, up, math.tan(math.radians(w / 2.0)), math.tan(math.radians(h / 2.0)), radiance))
    return frames


def _cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def _normalize(v):
    n = math.sqrt(sum(x * x for x in v))
    return tuple(x / n for x in v)


def _base(elevation):
    """Dark floor, grey walls darkening upwards into a black ceiling."""
    if elevation < 0.0:
        return 0.035 + 0.02 * math.exp(elevation / 12.0)
    return 0.06 * math.exp(-elevation / 35.0) + 0.006


def _panel(d, frame):
    """A softbox's radiance in direction d: a soft-edged rectangle, a little
    brighter in the middle, through a gnomonic projection onto its plane."""
    c, right, up, tw, th, radiance = frame
    facing = d[0] * c[0] + d[1] * c[1] + d[2] * c[2]
    if facing <= 0.05:
        return 0.0
    x = (d[0] * right[0] + d[1] * right[1] + d[2] * right[2]) / facing / tw
    y = (d[0] * up[0] + d[1] * up[1] + d[2] * up[2]) / facing / th
    edge = max(abs(x), abs(y))
    inside = min(max((1.06 - edge) / 0.06, 0.0), 1.0)
    return radiance * inside * (0.85 + 0.15 * (1.0 - min(edge, 1.0) ** 2))


def environment(path, width=1024, height=512):
    frames = _panel_frames()
    rows = []
    try:
        import numpy as np
        x = (np.arange(width) + 0.5) / width
        y = (np.arange(height) + 0.5) / height
        # The engine samples u = atan(z, x) / 2pi + 0.5 and v = 0.5 - asin(y) / pi.
        phi = (x - 0.5) * 2.0 * np.pi
        theta = (0.5 - y) * np.pi
        PHI, THETA = np.meshgrid(phi, theta)
        D = np.stack([np.cos(THETA) * np.cos(PHI), np.sin(THETA), np.cos(THETA) * np.sin(PHI)], axis=-1)
        E = np.degrees(THETA)
        value = np.where(E < 0.0, 0.035 + 0.02 * np.exp(E / 12.0), 0.06 * np.exp(-E / 35.0) + 0.006)
        for c, right, up, tw, th, radiance in frames:
            facing = D @ np.array(c)
            safe = np.maximum(facing, 1e-3)
            px = (D @ np.array(right)) / safe / tw
            py = (D @ np.array(up)) / safe / th
            edge = np.maximum(np.abs(px), np.abs(py))
            inside = np.clip((1.06 - edge) / 0.06, 0.0, 1.0) * (facing > 0.05)
            value = value + radiance * inside * (0.85 + 0.15 * (1.0 - np.minimum(edge, 1.0) ** 2))
        tint = np.stack([value, value * 0.985, value * 0.96], axis=-1)
        rows = [row.tolist() for row in tint]
    except ImportError:
        for j in range(height):
            theta = (0.5 - (j + 0.5) / height) * math.pi
            row = []
            for i in range(width):
                phi = ((i + 0.5) / width - 0.5) * 2.0 * math.pi
                d = (math.cos(theta) * math.cos(phi), math.sin(theta), math.cos(theta) * math.sin(phi))
                v = _base(math.degrees(theta)) + sum(_panel(d, f) for f in frames)
                row.append((v, v * 0.985, v * 0.96))
            rows.append(row)
    _write_hdr(path, rows, width, height)


def _rgbe(r, g, b):
    m = max(r, g, b)
    if m < 1e-32:
        return b"\0\0\0\0"
    mantissa, exponent = math.frexp(m)
    scale = mantissa * 256.0 / m
    return bytes((int(r * scale), int(g * scale), int(b * scale), exponent + 128))


def _write_hdr(path, rows, width, height):
    with open(path, "wb") as f:
        f.write(b"#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n")
        f.write(("-Y %d +X %d\n" % (height, width)).encode("ascii"))
        for row in rows:
            f.write(b"".join(_rgbe(*pixel) for pixel in row))


# ── Entry point ──────────────────────────────────────────────────────────

FILES = ("stage.gltf", "turntable.gltf", "softbox.gltf", "plinth.gltf", "studio.hdr")


def ensure(directory):
    """Write the studio into `directory` unless it is already there, at this
    VERSION. Returns the directory."""
    marker = os.path.join(directory, "version.txt")
    try:
        with open(marker, encoding="utf-8") as f:
            current = f.read().strip() == str(VERSION)
    except OSError:
        current = False
    if current and all(os.path.exists(os.path.join(directory, name)) for name in FILES):
        return directory
    os.makedirs(directory, exist_ok=True)
    print("[showroom] writing the studio into %s" % directory)
    stage(os.path.join(directory, "stage.gltf"))
    turntable(os.path.join(directory, "turntable.gltf"))
    softbox(os.path.join(directory, "softbox.gltf"))
    plinth(os.path.join(directory, "plinth.gltf"))
    environment(os.path.join(directory, "studio.hdr"))
    with open(marker, "w", encoding="utf-8") as f:
        f.write(str(VERSION))
    return directory


if __name__ == "__main__":
    import sys
    ensure(sys.argv[1] if len(sys.argv) > 1 else "studio")
