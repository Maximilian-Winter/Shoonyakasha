"""
Fetch the showroom's models from Sketchfab and convert them for the engine.

Used by `python tools/fetch_assets.py showroom`. Sketchfab only hands out
downloads to a signed-in account, so this needs your API token: find it at
https://sketchfab.com/settings/password ("API token") and put it in the
SKETCHFAB_API_TOKEN environment variable (or pass --sketchfab-token).
Without a token, download each model's glTF archive from its page yourself
and put the zip at assets/showroom/source/<name>.zip; converting picks it up.

Downloads go to assets/showroom/source/ (the zips and their unpacked files);
each converted model to assets/showroom/<name>/model.gltf, with a
showroom.json beside it that records its credit, size and materials.

Converting writes a copy the engine's loader reads completely. The loader
takes core metallic-roughness glTF with one UV set, so:
- KHR_texture_transform is baked into the UVs.
- KHR_materials_emissive_strength is folded into emissiveFactor.
- KHR_materials_transmission (glass) becomes a blended, mostly clear material.
- KHR_materials_pbrSpecularGlossiness becomes metallic-roughness, per pixel
  where there are maps (the Bistro converter's conversion).
- KHR_materials_unlit becomes emission (screens, lamps, painted-on light).
- KHR_materials_clearcoat is kept with its amount and roughness maps; its
  normal map is dropped.
- Maps on TEXCOORD_1 move the UV set to TEXCOORD_0 when a material uses only
  that set, and are dropped when it mixes sets (usually baked occlusion).
- Textures larger than --resolution are scaled down; WebP and other formats
  stb_image cannot read are saved as PNG or JPEG.
- Every mesh primitive gets its own node named "<material>#<n>", so the
  showroom can find a model's paint or glass by material name.

The models keep their licences: CC BY 4.0, CC BY-NC-SA 4.0 or CC BY-NC 4.0
(see MODELS). Converted files are adaptations under the same licence, and
anything that shows them, screenshots and videos included, needs the credit.
Converting needs Pillow and numpy.
"""

import hashlib
import io
import json
import math
import os
import re
import shutil
import struct
import sys
import urllib.error
import urllib.request
import zipfile

API = "https://api.sketchfab.com/v3"
USER_AGENT = "shoonyakasha-fetch-assets"

# name -> (Sketchfab uid, credit line). The credit is the attribution the
# author asks for, as Sketchfab generates it.
MODELS = {
    "alfa_gtv6": ("b34de8434a7d469d930da0d01bb78c8a",
                  '"1986 Alfa Romeo GTV-6" (https://skfb.ly/pLNop) by OUTPISTON is licensed under '
                  'CC Attribution-NonCommercial-ShareAlike (http://creativecommons.org/licenses/by-nc-sa/4.0/).'),
    "alfa_montreal": ("4eabb7931ecd4ede82deb76a11976e5e",
                      '"1970 Alfa Romeo Montreal" (https://skfb.ly/pLKx8) by OUTPISTON is licensed under '
                      'CC Attribution-NonCommercial-ShareAlike (http://creativecommons.org/licenses/by-nc-sa/4.0/).'),
    "alfa_33_stradale_1968": ("b8694795cdc6497fb8937f66457505a9",
                              '"1968 Alfa Romeo 33 Stradale" (https://skfb.ly/pLFPr) by OUTPISTON is licensed under '
                              'CC Attribution-NonCommercial-ShareAlike (http://creativecommons.org/licenses/by-nc-sa/4.0/).'),
    "alfa_33_stradale_2024": ("9211893859074f0cb471d5c2929b9948",
                              '"2024 Alfa Romeo 33 Stradale ICE" (https://skfb.ly/pNXHu) by Ddiaz Design is licensed under '
                              'CC Attribution-NonCommercial-ShareAlike (http://creativecommons.org/licenses/by-nc-sa/4.0/).'),
    "eta2_interceptor": ("b76f59276bc444458d5faaa9e6321cfc",
                         '"SWBF2(Custom) - Anakin\'s Eta-2 Actis Interceptor" (https://skfb.ly/pxvCx) by Zorg_Sinister '
                         'is licensed under Creative Commons Attribution (http://creativecommons.org/licenses/by/4.0/).'),
    "jedi_starfighter": ("27c14b58ffd343edabea8bea7b595005",
                         '"Anakin\'s Jedi Starfighter - Star Wars" (https://skfb.ly/6RSHr) by Quiznos323 is licensed under '
                         'CC Attribution-NonCommercial-ShareAlike (http://creativecommons.org/licenses/by-nc-sa/4.0/).'),
    "aat": ("9ed126f4616a482fabee52b0e0d46e52",
            '"-Star Wars- AAT" (https://skfb.ly/owYtF) by ARKON MAREK is licensed under '
            'Creative Commons Attribution (http://creativecommons.org/licenses/by/4.0/).'),
    "alkesh": ("a2c2858c553f42d7a1d0b11af4196411",
               '"Inspired By Stargate SG-1: Goa´Uld Alkesh" (https://skfb.ly/pNuWv) by Ska-Ara is licensed under '
               'CC Attribution-NonCommercial-ShareAlike (http://creativecommons.org/licenses/by-nc-sa/4.0/).'),
    "gaz13_chaika": ("21a409682dd24f309ae1ada885f4e1ba",
                     '"GAZ 13 Chaika" (https://skfb.ly/oIoSW) by panderlaike_design is licensed under '
                     'Creative Commons Attribution (http://creativecommons.org/licenses/by/4.0/).'),
    "shelby_cobra": ("a9024d1c23f1490f8f40468946c14b0c",
                     '"Shelby Cobra | www.vecarz.com" (https://skfb.ly/psAAX) by vecarz is licensed under '
                     'Creative Commons Attribution (http://creativecommons.org/licenses/by/4.0/).'),
    "chevrolet_camaro": ("789b0af67d994306b967facf75ab2e01",
                         '"1970 Chevrolet Camaro" (https://skfb.ly/pryVB) by DisneyCars is licensed under '
                         'Creative Commons Attribution (http://creativecommons.org/licenses/by/4.0/).'),
    "porsche_911_turbo": ("8568d9d14a994b9cae59499f0dbed21e",
                          '"FREE 1975 Porsche 911 (930) Turbo" (https://skfb.ly/6WZyV) by Lionsharp Studios is '
                          'licensed under Creative Commons Attribution (http://creativecommons.org/licenses/by/4.0/).'),
    "corvette_c8": ("01d63aa7013347acbfa62bc00e0b2df6",
                    '"2020 Chevrolet Corvette C8 Stingray Convertible" (https://skfb.ly/pr7JI) by Ddiaz Design is '
                    'licensed under Creative Commons Attribution (http://creativecommons.org/licenses/by/4.0/).'),
    "lamborghini_revuelto": ("4258ff5b559c45f2a470344f0e04c8cd",
                             '"Lamborghini Revuelto" (https://skfb.ly/prqBx) by Outlaw Games™ is licensed under '
                             'Creative Commons Attribution-NonCommercial (http://creativecommons.org/licenses/by-nc/4.0/).'),
}

DIELECTRIC = 0.04


# ── Download ─────────────────────────────────────────────────────────────

class TokenError(Exception):
    pass


def _request(url, token=None):
    headers = {"User-Agent": USER_AGENT}
    if token:
        headers["Authorization"] = "Token " + token
    return urllib.request.Request(url, headers=headers)


def download(name, source_dir, token):
    """The model's glTF archive into source_dir/<name>.zip, unless it is
    there already. Returns its path."""
    uid = MODELS[name][0]
    destination = os.path.join(source_dir, name + ".zip")
    if os.path.exists(destination):
        return destination
    if not token:
        raise TokenError("no Sketchfab API token")
    try:
        with urllib.request.urlopen(_request("%s/models/%s/download" % (API, uid), token)) as response:
            info = json.load(response)
    except urllib.error.HTTPError as exc:
        if exc.code in (401, 403):
            raise TokenError("Sketchfab refused the token (HTTP %d)" % exc.code)
        raise
    archive = info.get("gltf") or {}
    if not archive.get("url"):
        raise RuntimeError("Sketchfab offers no glTF download for this model")
    os.makedirs(source_dir, exist_ok=True)
    partial = destination + ".part"
    size = archive.get("size") or 0
    done = 0
    # The archive URL is pre-signed; sending the token to it would be refused.
    with urllib.request.urlopen(_request(archive["url"])) as response, open(partial, "wb") as out:
        while True:
            block = response.read(1 << 20)
            if not block:
                break
            out.write(block)
            done += len(block)
            if size:
                sys.stdout.write("\r    %3d%% of %d MB" % (done * 100 // size, size >> 20))
                sys.stdout.flush()
    sys.stdout.write("\r")
    os.replace(partial, destination)
    return destination


def unpack(archive, directory):
    """Unpack a zip, refusing paths that would land outside `directory`."""
    if os.path.isdir(directory):
        return directory
    partial = directory + ".part"
    shutil.rmtree(partial, ignore_errors=True)
    root = os.path.realpath(partial)
    with zipfile.ZipFile(archive) as z:
        for member in z.infolist():
            target = os.path.realpath(os.path.join(partial, member.filename))
            if not target.startswith(root + os.sep) and target != root:
                raise RuntimeError("unsafe path in archive: %s" % member.filename)
        z.extractall(partial)
    os.replace(partial, directory)
    return directory


def find_gltf(directory):
    """The glTF or GLB in an unpacked archive (Sketchfab's is scene.gltf)."""
    found = []
    for base, _, files in os.walk(directory):
        for f in files:
            if f.lower().endswith((".gltf", ".glb")):
                found.append(os.path.join(base, f))
    if not found:
        raise RuntimeError("no .gltf or .glb in %s" % directory)
    found.sort(key=lambda p: (os.path.basename(p).lower() != "scene.gltf", len(p)))
    return found[0]


# ── Reading glTF ─────────────────────────────────────────────────────────

COMPONENTS = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT4": 16}
DTYPES = {5120: "i1", 5121: "u1", 5122: "<i2", 5123: "<u2", 5125: "<u4", 5126: "<f4"}
NORMALIZE = {5120: 127.0, 5121: 255.0, 5122: 32767.0, 5123: 65535.0}


def load(path):
    """(gltf dict, list of buffer bytes, directory) from a .gltf or .glb."""
    import base64
    directory = os.path.dirname(os.path.abspath(path))
    with open(path, "rb") as f:
        data = f.read()
    glb_bin = None
    if data[:4] == b"glTF":
        length = struct.unpack_from("<I", data, 8)[0]
        offset = 12
        gltf = None
        while offset < length:
            chunk_length, chunk_type = struct.unpack_from("<II", data, offset)
            chunk = data[offset + 8:offset + 8 + chunk_length]
            if chunk_type == 0x4E4F534A:
                gltf = json.loads(chunk.decode("utf-8"))
            elif chunk_type == 0x004E4942:
                glb_bin = chunk
            offset += 8 + chunk_length
    else:
        gltf = json.loads(data.decode("utf-8"))
    buffers = []
    for b in gltf.get("buffers", []):
        uri = b.get("uri")
        if uri is None:
            buffers.append(glb_bin)
        elif uri.startswith("data:"):
            buffers.append(base64.b64decode(uri.split(",", 1)[1]))
        else:
            with open(os.path.join(directory, urllib.request.url2pathname(uri)), "rb") as f:
                buffers.append(f.read())
    return gltf, buffers, directory


def read_accessor(gltf, buffers, index):
    """An accessor as a float or int numpy array of shape (count, components)."""
    import numpy as np
    a = gltf["accessors"][index]
    n = COMPONENTS[a["type"]]
    dtype = np.dtype(DTYPES[a["componentType"]])
    count = a["count"]
    if "bufferView" in a:
        view = gltf["bufferViews"][a["bufferView"]]
        blob = buffers[view["buffer"]]
        start = view.get("byteOffset", 0) + a.get("byteOffset", 0)
        stride = view.get("byteStride") or dtype.itemsize * n
        raw = np.lib.stride_tricks.as_strided(
            np.frombuffer(blob, dtype=np.uint8, count=len(blob) - start, offset=start),
            shape=(count, dtype.itemsize * n), strides=(stride, 1)) if count else np.zeros((0, dtype.itemsize * n), np.uint8)
        values = np.ascontiguousarray(raw).view(dtype).reshape(count, n)
    else:
        values = np.zeros((count, n), dtype=dtype)
    sparse = a.get("sparse")
    if sparse:
        values = values.copy()
        idx = sparse["indices"]
        view = gltf["bufferViews"][idx["bufferView"]]
        ib = np.frombuffer(buffers[view["buffer"]], dtype=np.dtype(DTYPES[idx["componentType"]]),
                           count=sparse["count"], offset=view.get("byteOffset", 0) + idx.get("byteOffset", 0))
        val = sparse["values"]
        view = gltf["bufferViews"][val["bufferView"]]
        vb = np.frombuffer(buffers[view["buffer"]], dtype=dtype, count=sparse["count"] * n,
                           offset=view.get("byteOffset", 0) + val.get("byteOffset", 0)).reshape(-1, n)
        values[ib] = vb
    if a.get("normalized") and a["componentType"] in NORMALIZE:
        values = np.maximum(values.astype(np.float32) / NORMALIZE[a["componentType"]], -1.0)
    return values


def node_matrices(gltf):
    """World matrix (numpy 4x4) of every node reachable from the scene."""
    import numpy as np
    world = {}

    def local(node):
        if "matrix" in node:
            return np.array(node["matrix"], dtype=np.float64).reshape(4, 4).T
        t = node.get("translation", [0, 0, 0])
        x, y, z, w = node.get("rotation", [0, 0, 0, 1])
        s = node.get("scale", [1, 1, 1])
        r = np.array([[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
                      [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
                      [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]])
        m = np.eye(4)
        m[:3, :3] = r * np.array(s)[None, :]
        m[:3, 3] = t
        return m

    def walk(index, parent):
        m = parent @ local(gltf["nodes"][index])
        world[index] = m
        for child in gltf["nodes"][index].get("children", []):
            walk(child, m)

    scene = gltf.get("scenes", [{}])[gltf.get("scene", 0)] if gltf.get("scenes") else {
        "nodes": [i for i in range(len(gltf.get("nodes", [])))
                  if not any(i in n.get("children", []) for n in gltf["nodes"])]}
    for root in scene.get("nodes", []):
        walk(root, np.eye(4))
    return world


# ── Converting ───────────────────────────────────────────────────────────

class Converter:
    """One model: reads the source glTF, writes model.gltf, model.bin and
    textures/ into out_dir."""

    def __init__(self, source_path, out_dir, max_size):
        self.gltf, self.buffers, self.source_dir = load(source_path)
        self.out_dir = out_dir
        self.max_size = max_size
        self.extra = bytearray()      # appended buffer data
        self.notes = []
        self.textures_written = {}

    # Buffers ------------------------------------------------------------

    def append_accessor(self, values, kind, component_type=5126):
        """Append an array as a new accessor in the extra buffer."""
        import numpy as np
        data = np.ascontiguousarray(values, dtype={5126: np.float32, 5125: np.uint32}[component_type]).tobytes()
        while len(self.extra) % 4:
            self.extra.append(0)
        offset = len(self.extra)
        self.extra += data
        g = self.gltf
        g["bufferViews"].append({"buffer": -1, "byteOffset": offset, "byteLength": len(data)})
        accessor = {"bufferView": len(g["bufferViews"]) - 1, "componentType": component_type,
                    "count": int(len(values)), "type": kind}
        if kind == "VEC3":
            accessor["min"] = [float(v) for v in values.min(axis=0)]
            accessor["max"] = [float(v) for v in values.max(axis=0)]
        g["accessors"].append(accessor)
        return len(g["accessors"]) - 1

    # Materials ----------------------------------------------------------

    def _texture_slots(self, material):
        """(slot dict, srgb) for every texture a material uses."""
        pbr = material.get("pbrMetallicRoughness", {})
        coat = material.get("extensions", {}).get("KHR_materials_clearcoat", {})
        slots = [(pbr.get("baseColorTexture"), True), (pbr.get("metallicRoughnessTexture"), False),
                 (material.get("normalTexture"), False), (material.get("occlusionTexture"), False),
                 (material.get("emissiveTexture"), True),
                 (coat.get("clearcoatTexture"), False), (coat.get("clearcoatRoughnessTexture"), False)]
        return [(s, srgb) for s, srgb in slots if s]

    def convert_materials(self):
        g = self.gltf
        for i, m in enumerate(g.get("materials", [])):
            m.setdefault("name", "material%d" % i)
            ext = m.get("extensions", {})
            if "KHR_materials_pbrSpecularGlossiness" in ext:
                self._spec_gloss(m, ext.pop("KHR_materials_pbrSpecularGlossiness"))
            pbr = m.setdefault("pbrMetallicRoughness", {})
            strength = ext.pop("KHR_materials_emissive_strength", None)
            if strength:
                s = strength.get("emissiveStrength", 1.0)
                m["emissiveFactor"] = [c * s for c in m.get("emissiveFactor", [0.0, 0.0, 0.0])]
            transmission = ext.pop("KHR_materials_transmission", None)
            if transmission and transmission.get("transmissionFactor", 0.0) > 0.0:
                # Glass: blended and nearly clear, since the engine has no
                # transmission. Smooth, so it carries reflections.
                factor = transmission.get("transmissionFactor", 1.0)
                color = pbr.get("baseColorFactor", [1.0, 1.0, 1.0, 1.0])
                pbr["baseColorFactor"] = color[:3] + [min(color[3], 1.0 - 0.85 * factor)]
                m["alphaMode"] = "BLEND"
            if "KHR_materials_unlit" in ext:
                ext.pop("KHR_materials_unlit")
                color = pbr.get("baseColorFactor", [1.0, 1.0, 1.0, 1.0])
                m["emissiveFactor"] = color[:3]
                if "baseColorTexture" in pbr:
                    m["emissiveTexture"] = dict(pbr["baseColorTexture"])
                pbr["baseColorFactor"] = [0.0, 0.0, 0.0, color[3]]
                pbr["metallicFactor"] = 0.0
                pbr["roughnessFactor"] = 1.0
            coat = ext.get("KHR_materials_clearcoat")
            if coat is not None and coat.pop("clearcoatNormalTexture", None) is not None:
                self.notes.append("%s: clear coat normal map dropped (the coat takes the base normal)" % m["name"])
            for dropped in ("KHR_materials_specular", "KHR_materials_ior",
                            "KHR_materials_volume", "KHR_materials_sheen", "KHR_materials_iridescence",
                            "KHR_materials_anisotropy", "KHR_materials_dispersion"):
                if ext.pop(dropped, None) is not None:
                    self.notes.append("%s: %s dropped" % (m["name"], dropped))
            if not ext:
                m.pop("extensions", None)
        g.pop("extensionsRequired", None)

    def _spec_gloss(self, material, sg):
        """Specular-glossiness to metallic-roughness: per pixel with maps
        (the Khronos conversion, as in tools/bistro.py), from the factors
        otherwise."""
        import numpy as np
        sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
        import bistro
        diffuse_f = sg.get("diffuseFactor", [1.0, 1.0, 1.0, 1.0])
        specular_f = sg.get("specularFactor", [1.0, 1.0, 1.0])
        gloss_f = sg.get("glossinessFactor", 1.0)
        pbr = material.setdefault("pbrMetallicRoughness", {})
        if sg.get("diffuseTexture") or sg.get("specularGlossinessTexture"):
            d_img = self._image_array(sg.get("diffuseTexture"))
            s_img = self._image_array(sg.get("specularGlossinessTexture"))
            size = (d_img if d_img is not None else s_img).shape[:2]
            if d_img is None:
                d_img = np.ones(size + (4,), np.float32)
            if s_img is None:
                s_img = np.ones(size + (4,), np.float32)
            if s_img.shape[:2] != d_img.shape[:2]:
                from PIL import Image
                s_img = np.asarray(Image.fromarray((s_img * 255).astype(np.uint8)).resize(
                    (d_img.shape[1], d_img.shape[0]), Image.LANCZOS), np.float32) / 255.0
            diffuse = bistro._to_linear(d_img[..., :3]) * np.array(diffuse_f[:3])
            specular = bistro._to_linear(s_img[..., :3]) * np.array(specular_f)
            gloss = s_img[..., 3] * gloss_f
            base, alpha, metallic, roughness = bistro.spec_gloss_to_metal_rough(
                diffuse, d_img[..., 3] * diffuse_f[3], specular, gloss)
            stem = hashlib.sha1(json.dumps(sg, sort_keys=True).encode()).hexdigest()[:10]
            has_alpha = material.get("alphaMode", "OPAQUE") != "OPAQUE"
            base_name = self._save_array(np.dstack([bistro._to_srgb(base), alpha]), "sg_%s_base" % stem, has_alpha)
            mr = np.dstack([np.ones_like(metallic), roughness, metallic])
            mr_name = self._save_array(mr, "sg_%s_mr" % stem, False)
            uv = (sg.get("diffuseTexture") or sg.get("specularGlossinessTexture")).get("texCoord", 0)
            pbr["baseColorTexture"] = {"index": self._new_texture(base_name), "texCoord": uv}
            pbr["metallicRoughnessTexture"] = {"index": self._new_texture(mr_name), "texCoord": uv}
            pbr["baseColorFactor"] = [1.0, 1.0, 1.0, 1.0]
            pbr["metallicFactor"] = 1.0
            pbr["roughnessFactor"] = 1.0
        else:
            diffuse = np.array([[diffuse_f[:3]]], np.float32)
            specular = np.array([[specular_f]], np.float32)
            base, _, metallic, roughness = bistro.spec_gloss_to_metal_rough(
                diffuse, np.array([[diffuse_f[3]]]), specular, np.array([[gloss_f]], np.float32))
            pbr["baseColorFactor"] = [float(c) for c in base[0, 0]] + [diffuse_f[3]]
            pbr["metallicFactor"] = float(metallic[0, 0])
            pbr["roughnessFactor"] = float(roughness[0, 0])

    # UVs ----------------------------------------------------------------

    def fix_uvs(self):
        """Bake texture transforms, and make every material's maps read
        TEXCOORD_0."""
        import numpy as np
        g = self.gltf
        materials = g.get("materials", [])
        baked = {}
        for mesh in g.get("meshes", []):
            for primitive in mesh.get("primitives", []):
                index = primitive.get("material")
                if index is None:
                    continue
                material = materials[index]
                slots = self._texture_slots(material)
                if not slots:
                    continue
                sets = {s.get("extensions", {}).get("KHR_texture_transform", {}).get("texCoord", s.get("texCoord", 0))
                        for s, _ in slots}
                attributes = primitive["attributes"]
                if sets == {1} and "TEXCOORD_1" in attributes:
                    source = attributes["TEXCOORD_1"]
                elif "TEXCOORD_0" in attributes:
                    source = attributes["TEXCOORD_0"]
                else:
                    continue
                transforms = [s.get("extensions", {}).get("KHR_texture_transform") for s, _ in slots]
                transform = next((t for t in transforms if t), None)
                if transform:
                    key = (source, json.dumps(transform, sort_keys=True))
                    if key not in baked:
                        uv = read_accessor(g, self.buffers, source).astype(np.float32)
                        ox, oy = transform.get("offset", [0.0, 0.0])
                        sx, sy = transform.get("scale", [1.0, 1.0])
                        r = transform.get("rotation", 0.0)
                        c, s = math.cos(r), math.sin(r)
                        u, v = uv[:, 0] * sx, uv[:, 1] * sy        # T * R * S * uv, per the extension
                        uv = np.stack([c * u + s * v + ox, -s * u + c * v + oy], axis=1)
                        baked[key] = self.append_accessor(uv, "VEC2")
                    attributes["TEXCOORD_0"] = baked[key]
                else:
                    attributes["TEXCOORD_0"] = source
        for material in materials:
            used = []
            for s, _ in self._texture_slots(material):
                used.append(s.get("extensions", {}).get("KHR_texture_transform", {}).get("texCoord", s.get("texCoord", 0)))
            only_one = len(set(used)) == 1
            for holder, key in self._slot_holders(material):
                slot = holder[key]
                uvset = slot.get("extensions", {}).get("KHR_texture_transform", {}).get("texCoord", slot.get("texCoord", 0))
                if uvset != 0 and not only_one:
                    del holder[key]
                    self.notes.append("%s: %s on TEXCOORD_%d dropped" % (material["name"], key, uvset))
                    continue
                slot.pop("texCoord", None)
                ext = slot.get("extensions", {})
                ext.pop("KHR_texture_transform", None)
                if not ext:
                    slot.pop("extensions", None)

    def _slot_holders(self, material):
        pbr = material.get("pbrMetallicRoughness", {})
        out = [(pbr, k) for k in ("baseColorTexture", "metallicRoughnessTexture") if k in pbr]
        out += [(material, k) for k in ("normalTexture", "occlusionTexture", "emissiveTexture") if k in material]
        coat = material.get("extensions", {}).get("KHR_materials_clearcoat", {})
        out += [(coat, k) for k in ("clearcoatTexture", "clearcoatRoughnessTexture") if k in coat]
        return out

    # Textures -----------------------------------------------------------

    def _image_path(self, texture_index):
        g = self.gltf
        texture = g["textures"][texture_index]
        ext = texture.get("extensions", {})
        source = texture.get("source")
        for key in ("EXT_texture_webp", "KHR_texture_basisu", "MSFT_texture_dds"):
            if source is None and key in ext:
                source = ext[key]["source"]
        image = g["images"][source]
        return image, source

    def _image_bytes(self, image):
        if "uri" in image:
            uri = image["uri"]
            if uri.startswith("data:"):
                import base64
                return base64.b64decode(uri.split(",", 1)[1])
            with open(os.path.join(self.source_dir, urllib.request.url2pathname(uri)), "rb") as f:
                return f.read()
        view = self.gltf["bufferViews"][image["bufferView"]]
        start = view.get("byteOffset", 0)
        return bytes(self.buffers[view["buffer"]][start:start + view["byteLength"]])

    def _image_array(self, slot):
        import numpy as np
        from PIL import Image
        if not slot:
            return None
        image, _ = self._image_path(slot["index"])
        picture = Image.open(io.BytesIO(self._image_bytes(image))).convert("RGBA")
        w, h = picture.size
        scale = min(1.0, self.max_size / max(w, h))
        if scale < 1.0:
            picture = picture.resize((max(1, int(w * scale)), max(1, int(h * scale))), Image.LANCZOS)
        return np.asarray(picture, np.float32) / 255.0

    def _save_array(self, pixels, stem, alpha):
        import numpy as np
        from PIL import Image
        os.makedirs(os.path.join(self.out_dir, "textures"), exist_ok=True)
        data = (np.clip(pixels, 0.0, 1.0) * 255.0 + 0.5).astype(np.uint8)
        name = "textures/%s.%s" % (stem, "png" if alpha else "jpg")
        if alpha:
            Image.fromarray(data, "RGBA").save(os.path.join(self.out_dir, name), compress_level=6)
        else:
            Image.fromarray(data[..., :3], "RGB").save(os.path.join(self.out_dir, name), quality=92, subsampling=0)
        return name

    def _new_texture(self, uri):
        g = self.gltf
        g.setdefault("images", []).append({"uri": uri})
        g.setdefault("textures", []).append({"source": len(g["images"]) - 1})
        return len(g["textures"]) - 1

    def write_textures(self):
        """Copy or re-encode every image a texture uses: at most max_size,
        PNG or JPEG only."""
        from PIL import Image
        g = self.gltf
        used_alpha = {}
        for material in g.get("materials", []):
            blended = material.get("alphaMode", "OPAQUE") != "OPAQUE"
            for holder, key in self._slot_holders(material):
                used_alpha[holder[key]["index"]] = used_alpha.get(holder[key]["index"], False) or (
                    blended and key == "baseColorTexture")
        os.makedirs(os.path.join(self.out_dir, "textures"), exist_ok=True)
        for t_index, texture in enumerate(g.get("textures", [])):
            image, source = self._image_path(t_index)
            texture["source"] = source
            texture.pop("extensions", None)
            if source in self.textures_written:
                continue
            if image.get("uri", "").startswith("textures/") and os.path.exists(os.path.join(self.out_dir, image["uri"])):
                self.textures_written[source] = image["uri"]      # written by the spec-gloss conversion
                continue
            raw = self._image_bytes(image)
            picture = Image.open(io.BytesIO(raw))
            fmt = picture.format
            w, h = picture.size
            scale = min(1.0, self.max_size / max(w, h))
            stem = re.sub(r"[^A-Za-z0-9_.-]", "_", os.path.splitext(os.path.basename(image.get("uri", "") or
                                                                                   "image%d" % source))[0])
            stem = "%d_%s" % (source, stem)
            keep_alpha = picture.mode in ("RGBA", "LA", "P") and (used_alpha.get(t_index, False) or fmt == "PNG")
            if scale >= 1.0 and fmt in ("PNG", "JPEG"):
                name = "textures/%s.%s" % (stem, "png" if fmt == "PNG" else "jpg")
                with open(os.path.join(self.out_dir, name), "wb") as f:
                    f.write(raw)
            else:
                picture = picture.convert("RGBA" if keep_alpha else "RGB")
                if scale < 1.0:
                    picture = picture.resize((max(1, int(w * scale)), max(1, int(h * scale))), Image.LANCZOS)
                name = "textures/%s.%s" % (stem, "png" if keep_alpha else "jpg")
                if keep_alpha:
                    picture.save(os.path.join(self.out_dir, name), compress_level=6)
                else:
                    picture.save(os.path.join(self.out_dir, name), quality=92, subsampling=0)
            self.textures_written[source] = name
        # Keep only the images textures use, each pointing at its written file.
        order = sorted(self.textures_written)
        renumber = {old: new for new, old in enumerate(order)}
        g["images"] = [{"uri": self.textures_written[old]} for old in order]
        for texture in g.get("textures", []):
            texture["source"] = renumber[texture["source"]]
        for key in ("EXT_texture_webp", "KHR_texture_basisu", "MSFT_texture_dds", "KHR_texture_transform",
                    "KHR_materials_emissive_strength", "KHR_materials_transmission", "KHR_materials_unlit",
                    "KHR_materials_pbrSpecularGlossiness"):
            if key in g.get("extensionsUsed", []):
                g["extensionsUsed"].remove(key)

    # Nodes --------------------------------------------------------------

    def split_and_name(self):
        """Give every primitive its own node "<material>#<n>" (a child of the
        node that held the mesh), and put the whole scene under one root
        node "model"."""
        g = self.gltf
        materials = g.get("materials", [])
        split = {}           # (mesh, primitive) -> new mesh
        counter = {}

        def name_for(material_index):
            base = materials[material_index]["name"] if material_index is not None else "default"
            base = re.sub(r"[#\s]+", "_", base).strip("_") or "material"
            counter[base] = counter.get(base, 0) + 1
            return "%s#%d" % (base, counter[base])

        for node_index in range(len(g.get("nodes", []))):
            node = g["nodes"][node_index]
            if "mesh" not in node or "skin" in node:
                continue
            mesh_index = node.pop("mesh")
            mesh = g["meshes"][mesh_index]
            children = node.setdefault("children", [])
            for p_index, primitive in enumerate(mesh["primitives"]):
                key = (mesh_index, p_index)
                if key not in split:
                    g["meshes"].append({"name": mesh.get("name", "mesh"), "primitives": [primitive]})
                    split[key] = len(g["meshes"]) - 1
                g["nodes"].append({"name": name_for(primitive.get("material")), "mesh": split[key]})
                children.append(len(g["nodes"]) - 1)
        # Drop the meshes nothing uses any more, renumbering the rest.
        used = sorted({n["mesh"] for n in g["nodes"] if "mesh" in n})
        remap = {old: new for new, old in enumerate(used)}
        g["meshes"] = [g["meshes"][i] for i in used]
        for n in g["nodes"]:
            if "mesh" in n:
                n["mesh"] = remap[n["mesh"]]

        scene_index = g.get("scene", 0)
        scenes = g.setdefault("scenes", [{"nodes": []}])
        roots = scenes[scene_index].get("nodes", [])
        g["nodes"].append({"name": "model", "children": roots})
        scenes[scene_index]["nodes"] = [len(g["nodes"]) - 1]
        g["scene"] = scene_index

    # Bounds -------------------------------------------------------------

    def bounds(self):
        """World-space bounds of the model, and of every mesh in full.

        The first ignore the outermost 0.05% of vertices on each axis: some
        files carry a large ground plane or a stray part far away, a handful
        of vertices that would otherwise make the model look many times its
        size. The full bounds come from the accessors' min/max."""
        import numpy as np
        g = self.gltf
        world = node_matrices(g)
        lo = np.full(3, np.inf)
        hi = np.full(3, -np.inf)
        samples = []
        triangles = 0
        for node_index, matrix in world.items():
            node = g["nodes"][node_index]
            if "mesh" not in node:
                continue
            for primitive in g["meshes"][node["mesh"]]["primitives"]:
                accessor = g["accessors"][primitive["attributes"]["POSITION"]]
                amin, amax = accessor.get("min"), accessor.get("max")
                if amin is None or amax is None:
                    p = read_accessor(g, self.buffers, primitive["attributes"]["POSITION"]).astype(np.float64)
                    amin, amax = p.min(axis=0), p.max(axis=0)
                corners = np.array([[x, y, z, 1.0] for x in (amin[0], amax[0]) for y in (amin[1], amax[1])
                                    for z in (amin[2], amax[2])])
                w = (matrix @ corners.T).T[:, :3]
                lo = np.minimum(lo, w.min(axis=0))
                hi = np.maximum(hi, w.max(axis=0))
                count = (g["accessors"][primitive["indices"]]["count"] if "indices" in primitive
                         else accessor["count"])
                triangles += count // 3
                p = read_accessor(g, self.buffers, primitive["attributes"]["POSITION"]).astype(np.float64)
                p = p[::max(1, len(p) // 50000)]
                samples.append(p @ matrix[:3, :3].T + matrix[:3, 3])
        full = ([float(v) for v in lo], [float(v) for v in hi])
        if samples:
            points = np.concatenate(samples)
            lo = np.percentile(points, 0.05, axis=0)
            hi = np.percentile(points, 99.95, axis=0)
        return [float(v) for v in lo], [float(v) for v in hi], int(triangles), full

    # Writing ------------------------------------------------------------

    def write(self):
        """Merge the buffers and the extra data into model.bin, and write
        model.gltf."""
        g = self.gltf
        os.makedirs(self.out_dir, exist_ok=True)
        offsets = []
        total = 0
        partial = os.path.join(self.out_dir, "model.bin.part")
        with open(partial, "wb") as out:
            for blob in self.buffers + [bytes(self.extra)]:
                pad = (-total) % 8
                out.write(b"\0" * pad)
                total += pad
                offsets.append(total)
                out.write(blob or b"")
                total += len(blob or b"")
        os.replace(partial, os.path.join(self.out_dir, "model.bin"))
        extra = len(self.buffers)
        for view in g.get("bufferViews", []):
            source = extra if view["buffer"] == -1 else view["buffer"]
            view["byteOffset"] = view.get("byteOffset", 0) + offsets[source]
            view["buffer"] = 0
        g["buffers"] = [{"uri": "model.bin", "byteLength": total}]
        with open(os.path.join(self.out_dir, "model.gltf"), "w", encoding="utf-8") as f:
            json.dump(g, f)


def convert(name, source_path, out_dir, max_size=2048):
    """Convert one model; returns the summary written to showroom.json."""
    required = set(json.load(open(source_path, encoding="utf-8")).get("extensionsRequired", [])) \
        if source_path.endswith(".gltf") else set()
    unsupported = required & {"KHR_draco_mesh_compression", "EXT_meshopt_compression", "KHR_texture_basisu"}
    if unsupported:
        raise RuntimeError("needs %s, which this converter cannot decode" % ", ".join(sorted(unsupported)))
    shutil.rmtree(out_dir, ignore_errors=True)
    c = Converter(source_path, out_dir, max_size)
    c.convert_materials()
    c.fix_uvs()
    c.write_textures()
    c.split_and_name()
    lo, hi, triangles, full = c.bounds()
    c.write()
    materials = []
    for m in c.gltf.get("materials", []):
        materials.append({"name": m["name"], "alphaMode": m.get("alphaMode", "OPAQUE"),
                          "emissive": max(m.get("emissiveFactor", [0, 0, 0])) > 0.0})
    uid, credit = MODELS.get(name, ("", ""))
    summary = {"name": name, "uid": uid, "credit": credit,
               "boundsMin": lo, "boundsMax": hi, "fullBoundsMin": full[0], "fullBoundsMax": full[1],
               "triangles": triangles,
               "maxTextureSize": max_size, "materials": materials, "notes": c.notes}
    with open(os.path.join(out_dir, "showroom.json"), "w", encoding="utf-8") as f:
        json.dump(summary, f, indent=1)
    return summary


def fetch(names, assets_dir, token, max_size=2048):
    """Download and convert `names` into assets_dir/showroom/. Returns the
    number that failed."""
    root = os.path.join(assets_dir, "showroom")
    source_dir = os.path.join(root, "source")
    failures = 0
    for name in names:
        print("  %s" % MODELS[name][1])
        try:
            archive = download(name, source_dir, token)
        except TokenError as exc:
            print("    skipped: %s. Set SKETCHFAB_API_TOKEN (https://sketchfab.com/settings/password),\n"
                  "    or download the glTF archive from the model's page to assets/showroom/source/%s.zip"
                  % (exc, name))
            failures += 1
            continue
        except Exception as exc:                      # noqa: BLE001 - report and continue
            print("    download FAILED: %s" % exc)
            failures += 1
            continue
        try:
            unpacked = unpack(archive, os.path.join(source_dir, name))
            summary = convert(name, find_gltf(unpacked), os.path.join(root, name), max_size)
        except Exception as exc:                      # noqa: BLE001 - report and continue
            print("    convert FAILED: %s" % exc)
            failures += 1
            continue
        size = [b - a for a, b in zip(summary["boundsMin"], summary["boundsMax"])]
        print("    -> assets/showroom/%s/model.gltf  (%d triangles, %.2f x %.2f x %.2f, %d materials%s)" % (
            name, summary["triangles"], size[0], size[1], size[2], len(summary["materials"]),
            ", %d notes" % len(summary["notes"]) if summary["notes"] else ""))
    return failures
