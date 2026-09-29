"""
Building glTF files for the alley demo out of Poly Haven assets.

Poly Haven's modular kits (a facade, a fence) come as one glTF holding every
module side by side. `Composer` writes a new glTF beside a kit that places
chosen modules anywhere: each placement copies the module's node, and every
copy points at the kit's own meshes, materials and textures, so nothing is
duplicated on disk and the engine's loader shares the geometry.

`ground_plane` writes a flat, tiled ground from a Poly Haven texture set
(colour, OpenGL normal, AO/rough/metal).

Only the standard library: this runs before the engine starts.
"""

import copy
import json
import math
import os
import struct


def _quat_yaw(degrees):
    """Rotation about +Y as a glTF quaternion (x, y, z, w)."""
    half = math.radians(degrees) * 0.5
    return [0.0, math.sin(half), 0.0, math.cos(half)]


def _quat_mul(a, b):
    ax, ay, az, aw = a
    bx, by, bz, bw = b
    return [aw * bx + ax * bw + ay * bz - az * by,
            aw * by - ax * bz + ay * bw + az * bx,
            aw * bz + ax * by - ay * bx + az * bw,
            aw * bw - ax * bx - ay * by - az * bz]


def rotate_yaw(x, z, degrees):
    """(x, z) turned about +Y, the way _quat_yaw turns a node."""
    c, s = math.cos(math.radians(degrees)), math.sin(math.radians(degrees))
    return x * c + z * s, -x * s + z * c


class Composer:
    """Place modules of a kit glTF, and write the result beside the kit."""

    def __init__(self, kit_path):
        self.kit_path = kit_path
        with open(kit_path, encoding="utf-8") as f:
            self.kit = json.load(f)
        self.by_name = {n.get("name"): i for i, n in enumerate(self.kit["nodes"])}
        self.nodes = []
        self.roots = []
        self._extra_buffer = bytearray()

    def module_names(self):
        return [self.kit["nodes"][i].get("name") for i in self.kit["scenes"][self.kit.get("scene", 0)]["nodes"]]

    def _copy(self, index):
        node = copy.deepcopy(self.kit["nodes"][index])
        children = node.pop("children", [])
        new_index = len(self.nodes)
        self.nodes.append(node)
        if children:
            node["children"] = [self._copy(c) for c in children]
        return new_index

    def add(self, name, position, yaw=0.0, scale=None):
        """Place module `name` with its origin at `position`, turned `yaw`
        degrees about +Y. The kit's own layout offset is replaced."""
        if name not in self.by_name:
            raise KeyError("%s has no module '%s'" % (os.path.basename(self.kit_path), name))
        index = self._copy(self.by_name[name])
        node = self.nodes[index]
        node.pop("matrix", None)
        node["translation"] = [float(v) for v in position]
        node["rotation"] = _quat_mul(_quat_yaw(yaw), node.get("rotation", [0.0, 0.0, 0.0, 1.0]))
        if scale is not None:
            node["scale"] = [float(v) for v in scale]
        self.roots.append(index)
        return index

    def bake_texture_transforms(self):
        """Apply KHR_texture_transform to the UVs of the primitives that use
        it, for loaders without the extension. Each such primitive gets its
        own transformed copy of TEXCOORD_0, in an extra buffer."""
        g = self.kit
        buffers = [os.path.join(os.path.dirname(self.kit_path), b["uri"]) for b in g["buffers"]]
        data = [open(p, "rb").read() for p in buffers]
        done = {}
        for material_index, material in enumerate(g.get("materials", [])):
            transform = None
            for slot in (material.get("pbrMetallicRoughness", {}).get("baseColorTexture"),
                         material.get("normalTexture")):
                if slot and "KHR_texture_transform" in slot.get("extensions", {}):
                    transform = slot["extensions"]["KHR_texture_transform"]
            if not transform:
                continue
            for mesh in g["meshes"]:
                for primitive in mesh["primitives"]:
                    if primitive.get("material") != material_index:
                        continue
                    source = primitive["attributes"].get("TEXCOORD_0")
                    if source is None:
                        continue
                    if source not in done:
                        done[source] = self._transformed_uvs(source, transform, data)
                    primitive["attributes"]["TEXCOORD_0"] = done[source]
            _strip_transforms(material)
        g.get("extensionsUsed", []) and g.__setitem__(
            "extensionsUsed", [e for e in g["extensionsUsed"] if e != "KHR_texture_transform"])

    def _transformed_uvs(self, accessor_index, t, data):
        g = self.kit
        accessor = g["accessors"][accessor_index]
        view = g["bufferViews"][accessor["bufferView"]]
        if accessor.get("componentType") != 5126:
            raise ValueError("only float UVs can be transformed")
        stride = view.get("byteStride", 8)
        start = view.get("byteOffset", 0) + accessor.get("byteOffset", 0)
        blob = data[view["buffer"]]
        ox, oy = t.get("offset", [0.0, 0.0])
        sx, sy = t.get("scale", [1.0, 1.0])
        r = t.get("rotation", 0.0)
        c, s = math.cos(r), math.sin(r)
        out = bytearray()
        for i in range(accessor["count"]):
            u, v = struct.unpack_from("<2f", blob, start + i * stride)
            u, v = u * sx, v * sy                       # T * R * S * uv, per the extension
            u, v = c * u + s * v, -s * u + c * v
            out += struct.pack("<2f", u + ox, v + oy)
        offset = len(self._extra_buffer)
        self._extra_buffer += out
        g["bufferViews"].append({"buffer": len(g["buffers"]), "byteOffset": offset, "byteLength": len(out)})
        new = dict(accessor, bufferView=len(g["bufferViews"]) - 1, byteOffset=0)
        new.pop("min", None)
        new.pop("max", None)
        g["accessors"].append(new)
        return len(g["accessors"]) - 1

    def override_material(self, name, **fields):
        """Change a kit material's top-level fields, e.g. alphaMode="MASK"."""
        for material in self.kit.get("materials", []):
            if material.get("name") == name:
                material.update(fields)
                return
        raise KeyError("no material '%s'" % name)

    def write(self, name):
        """Write `name`.gltf beside the kit and return its path."""
        directory = os.path.dirname(self.kit_path)
        out = dict(self.kit)
        out["nodes"] = self.nodes
        out["scenes"] = [{"nodes": list(self.roots)}]
        out["scene"] = 0
        if self._extra_buffer:
            bin_name = name + ".bin"
            with open(os.path.join(directory, bin_name), "wb") as f:
                f.write(self._extra_buffer)
            out["buffers"] = list(self.kit["buffers"]) + [{"uri": bin_name, "byteLength": len(self._extra_buffer)}]
        path = os.path.join(directory, name + ".gltf")
        with open(path, "w", encoding="utf-8") as f:
            json.dump(out, f)
        return path


def _strip_transforms(material):
    slots = [material.get("normalTexture"), material.get("occlusionTexture"), material.get("emissiveTexture")]
    pbr = material.get("pbrMetallicRoughness", {})
    slots += [pbr.get("baseColorTexture"), pbr.get("metallicRoughnessTexture")]
    for slot in slots:
        if slot and "extensions" in slot:
            slot["extensions"].pop("KHR_texture_transform", None)
            if not slot["extensions"]:
                del slot["extensions"]


def ground_plane(texture_dir, texture, name, x_range, z_range, tile=2.5, y=0.0):
    """A flat rectangle over x_range by z_range at height y, textured with a
    Poly Haven set repeating every `tile` units. Written into texture_dir."""
    (x0, x1), (z0, z1) = x_range, z_range
    positions = [(x0, y, z0), (x1, y, z0), (x1, y, z1), (x0, y, z1)]
    uvs = [(p[0] / tile, p[2] / tile) for p in positions]
    blob = bytearray()
    for p in positions:
        blob += struct.pack("<3f", *p)
    for _ in positions:
        blob += struct.pack("<3f", 0.0, 1.0, 0.0)
    for uv in uvs:
        blob += struct.pack("<2f", *uv)
    # Counter-clockwise seen from above (+y), so the front faces up: the
    # G-buffer turns the normals of back faces round.
    up = (x1 - x0) * (z1 - z0) < 0.0
    blob += struct.pack("<6H", *((0, 1, 2, 0, 2, 3) if up else (0, 2, 1, 0, 3, 2)))
    views = [(0, 48), (48, 48), (96, 32), (128, 12)]
    gltf = {
        "asset": {"version": "2.0", "generator": "shoonyakasha alley demo"},
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [{"name": name, "mesh": 0}],
        "meshes": [{"name": name, "primitives": [{
            "attributes": {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2}, "indices": 3, "material": 0}]}],
        "materials": [{
            "name": texture,
            "pbrMetallicRoughness": {"baseColorTexture": {"index": 0},
                                     "metallicRoughnessTexture": {"index": 2}},
            "normalTexture": {"index": 1},
            "occlusionTexture": {"index": 2},
        }],
        "textures": [{"source": 0}, {"source": 1}, {"source": 2}],
        "images": [{"uri": "%s_%s.jpg" % (texture, suffix)} for suffix in ("diff", "nor_gl", "arm")],
        "buffers": [{"uri": name + ".bin", "byteLength": len(blob)}],
        "bufferViews": [{"buffer": 0, "byteOffset": o, "byteLength": n} for o, n in views],
        "accessors": [
            {"bufferView": 0, "componentType": 5126, "count": 4, "type": "VEC3",
             "min": [x0, y, min(z0, z1)], "max": [x1, y, max(z0, z1)]},
            {"bufferView": 1, "componentType": 5126, "count": 4, "type": "VEC3"},
            {"bufferView": 2, "componentType": 5126, "count": 4, "type": "VEC2"},
            {"bufferView": 3, "componentType": 5123, "count": 6, "type": "SCALAR"},
        ],
    }
    with open(os.path.join(texture_dir, name + ".bin"), "wb") as f:
        f.write(blob)
    path = os.path.join(texture_dir, name + ".gltf")
    with open(path, "w", encoding="utf-8") as f:
        json.dump(gltf, f)
    return path
