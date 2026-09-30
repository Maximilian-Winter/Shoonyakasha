"""
Fetch the Amazon Lumberyard Bistro exterior and convert it for the engine.

Used by `python tools/fetch_assets.py bistro`. The scene comes from NVIDIA's
glTF edit of the Bistro (by way of zeux/niagara_bistro, which keeps it in plain
git rather than Git LFS). Its materials use two things the engine's loader
doesn't read, so this writes a converted copy:

- Textures are BC7 DDS files (MSFT_texture_dds). They are decoded and saved
  as JPEG, or PNG where the alpha matters, at most `max_size` on a side.
- Materials are specular-glossiness (KHR_materials_pbrSpecularGlossiness).
  They are turned into metallic-roughness with the conversion the Khronos
  glTF tools use, per pixel where a material has a specular map.

Also:
- Normal maps are CryEngine "ddna" maps: DirectX-style (green points down)
  with gloss in alpha. Green is flipped, and the gloss goes into roughness.
- Glass (KHR_materials_transmission) becomes a blended, mostly clear material.
- Emissive textures get an emissiveFactor, which the export left out.

Source files go to assets/bistro/source/ (about 2 GB), the converted scene
to assets/bistro/bistro.gltf. Converting needs Pillow and numpy.

Amazon Lumberyard Bistro, Open Research Content Archive (ORCA),
https://developer.nvidia.com/orca/amazon-lumberyard-bistro, CC BY 4.0.
glTF edit: NVIDIA RTXDI assets (MIT), https://github.com/NVIDIAGameWorks/rtxdi-assets
"""

import concurrent.futures
import hashlib
import json
import os
import sys
import urllib.request

REPO = "https://raw.githubusercontent.com/zeux/niagara_bistro/2bdb6a410f8ebd475d3737e7c8e038ed2b00b02e/"
SCENE = "bistro.gltf"
BUFFERS = ["bistro.bin", "bistro-anim.bin"]


def _get(url, destination):
    if os.path.exists(destination):
        return False
    os.makedirs(os.path.dirname(destination), exist_ok=True)
    partial = destination + ".part"
    request = urllib.request.Request(url, headers={"User-Agent": "shoonyakasha-fetch-assets"})
    with urllib.request.urlopen(request) as response, open(partial, "wb") as out:
        while True:
            block = response.read(1 << 20)
            if not block:
                break
            out.write(block)
    os.replace(partial, destination)
    return True


def _dds_images(gltf):
    """The DDS file of every texture the materials use."""
    used = set()

    def walk(value):
        if isinstance(value, dict):
            if isinstance(value.get("index"), int) and set(value) <= {"index", "texCoord", "scale", "strength", "extensions"}:
                used.add(value["index"])
            for item in value.values():
                walk(item)
        elif isinstance(value, list):
            for item in value:
                walk(item)

    walk(gltf.get("materials", []))
    uris = set()
    for index in used:
        texture = gltf["textures"][index]
        source = texture.get("extensions", {}).get("MSFT_texture_dds", {}).get("source", texture.get("source"))
        uris.add(gltf["images"][source]["uri"])
    return sorted(uris)


def fetch(source_dir):
    """Download the scene and its DDS textures into source_dir."""
    for name in [SCENE] + BUFFERS:
        if _get(REPO + name, os.path.join(source_dir, name)):
            print("  %s" % name)
    with open(os.path.join(source_dir, SCENE), encoding="utf-8") as f:
        gltf = json.load(f)
    uris = _dds_images(gltf)
    done = [0]

    def one(uri):
        _get(REPO + uri, os.path.join(source_dir, uri))
        done[0] += 1
        sys.stdout.write("\r  textures %d/%d" % (done[0], len(uris)))
        sys.stdout.flush()

    with concurrent.futures.ThreadPoolExecutor(8) as pool:
        list(pool.map(one, uris))
    print()


# ---------------------------------------------------------------- conversion

DIELECTRIC = 0.04


def _require_imaging():
    try:
        import numpy  # noqa: F401
        from PIL import Image  # noqa: F401
    except ImportError:
        raise SystemExit("Converting the Bistro needs Pillow and numpy: pip install pillow numpy")


def _to_linear(c):
    import numpy as np
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)


def _to_srgb(c):
    import numpy as np
    c = np.clip(c, 0.0, 1.0)
    return np.where(c <= 0.0031308, c * 12.92, 1.055 * np.power(c, 1.0 / 2.4) - 0.055)


def _brightness(c):
    import numpy as np
    return np.sqrt(0.299 * c[..., 0] ** 2 + 0.587 * c[..., 1] ** 2 + 0.114 * c[..., 2] ** 2)


def _solve_metallic(diffuse, specular, one_minus_specular):
    """Metalness that reproduces a diffuse/specular pair (Khronos' conversion)."""
    import numpy as np
    specular = np.maximum(specular, DIELECTRIC)
    a = DIELECTRIC
    b = diffuse * one_minus_specular / (1.0 - DIELECTRIC) + specular - 2.0 * DIELECTRIC
    c = DIELECTRIC - specular
    d = np.maximum(b * b - 4.0 * a * c, 0.0)
    return np.clip((-b + np.sqrt(d)) / (2.0 * a), 0.0, 1.0)


def spec_gloss_to_metal_rough(diffuse, diffuse_alpha, specular, gloss):
    """Per-pixel conversion, all inputs linear floats in [0, 1].
    Returns (base colour linear RGB, alpha, metallic, roughness)."""
    import numpy as np
    one_minus = 1.0 - specular.max(axis=-1)
    metallic = _solve_metallic(_brightness(diffuse), _brightness(specular), one_minus)
    m = metallic[..., None]
    from_diffuse = diffuse * (one_minus[..., None] / (1.0 - DIELECTRIC) / np.maximum(1.0 - m, 1e-4))
    from_specular = (specular - DIELECTRIC * (1.0 - m)) / np.maximum(m, 1e-4)
    base = from_diffuse + (from_specular - from_diffuse) * (m * m)
    return np.clip(base, 0.0, 1.0), diffuse_alpha, metallic, 1.0 - gloss


class _Textures:
    """Decoded DDS files, resized to at most max_size, cached by path."""

    def __init__(self, source_dir, max_size):
        self.source_dir = source_dir
        self.max_size = max_size
        self.cache = {}

    def load(self, uri, size=None):
        """RGBA floats in [0, 1], optionally resized to `size` (w, h)."""
        import numpy as np
        from PIL import Image
        key = (uri, size)
        if key not in self.cache:
            image = Image.open(os.path.join(self.source_dir, uri)).convert("RGBA")
            w, h = image.size
            scale = min(1.0, self.max_size / max(w, h))
            target = size or (max(1, int(w * scale)), max(1, int(h * scale)))
            if image.size != target:
                image = image.resize(target, Image.LANCZOS)
            self.cache[key] = np.asarray(image, dtype=np.float32) / 255.0
        return self.cache[key]


def _save(pixels, path, alpha):
    """Save an array of floats in [0, 1] as PNG (with alpha) or JPEG."""
    import numpy as np
    from PIL import Image
    data = (np.clip(pixels, 0.0, 1.0) * 255.0 + 0.5).astype(np.uint8)
    partial = "%s.%d.part" % (path, os.getpid())    # materials sharing a map may write it at once
    if alpha:
        Image.fromarray(data, "RGBA").save(partial, format="PNG", compress_level=6)
    else:
        Image.fromarray(data[..., :3], "RGB").save(partial, format="JPEG", quality=92, subsampling=0)
    os.replace(partial, path)


def _texture_uri(gltf, slot):
    """The DDS behind a material texture slot, or None."""
    if not slot:
        return None
    texture = gltf["textures"][slot["index"]]
    source = texture.get("extensions", {}).get("MSFT_texture_dds", {}).get("source", texture.get("source"))
    return gltf["images"][source]["uri"]


def _material_job(material, gltf):
    """What one material needs converted: a dict of inputs, hashable by key."""
    sg = material.get("extensions", {}).get("KHR_materials_pbrSpecularGlossiness")
    if sg is None:
        # Already metallic-roughness (glass, some metals): only its maps need converting.
        return {
            "name": material.get("name", ""),
            "specGloss": False,
            "normal": _texture_uri(gltf, material.get("normalTexture")),
            "emissive": _texture_uri(gltf, material.get("emissiveTexture")),
        }
    return {
        "name": material.get("name", ""),
        "diffuse": _texture_uri(gltf, sg.get("diffuseTexture")),
        "diffuseFactor": sg.get("diffuseFactor", [1.0, 1.0, 1.0, 1.0]),
        "specular": _texture_uri(gltf, sg.get("specularGlossinessTexture")),
        "specularFactor": sg.get("specularFactor", [1.0, 1.0, 1.0]),
        "glossiness": sg.get("glossinessFactor", 1.0),
        "normal": _texture_uri(gltf, material.get("normalTexture")),
        "emissive": _texture_uri(gltf, material.get("emissiveTexture")),
        "alpha": material.get("alphaMode", "OPAQUE") != "OPAQUE",
    }


def _stem(uri):
    return os.path.splitext(uri.replace("/", "_"))[0]


def _convert_material(job, textures, out_dir):
    """Write a material's textures. Returns the metallic-roughness fields."""
    out = {}
    tag = hashlib.sha1(json.dumps([job, textures.max_size], sort_keys=True).encode()).hexdigest()[:8]
    # Converting again (after an interrupted run, say) reuses finished materials.
    record = os.path.join(out_dir, "converted", tag + ".json")
    if os.path.exists(record):
        with open(record, encoding="utf-8") as f:
            return json.load(f)
    if job.get("specGloss", True):
        out["pbr"] = _convert_spec_gloss(job, textures, out_dir, tag)
        size = out["pbr"].pop("size", None)
    else:
        size = None
    n = textures.load(job["normal"], size) if job["normal"] else None
    if n is not None:
        normal = n[..., :3].copy()
        normal[..., 1] = 1.0 - normal[..., 1]          # DirectX (green down) to glTF (green up)
        name = "%s_%d_normal.jpg" % (_stem(job["normal"]), textures.max_size)
        path = os.path.join(out_dir, name)
        if not os.path.exists(path):
            _save(normal, path, False)
        out["normal"] = name
    if job["emissive"]:
        name = "%s_%d_emissive.jpg" % (_stem(job["emissive"]), textures.max_size)
        path = os.path.join(out_dir, name)
        if not os.path.exists(path):
            _save(textures.load(job["emissive"]), path, False)
        out["emissive"] = name
    os.makedirs(os.path.dirname(record), exist_ok=True)
    with open(record, "w", encoding="utf-8") as f:
        json.dump(out, f)
    return out


def _convert_spec_gloss(job, textures, out_dir, tag):
    """Base colour, metalness and roughness for a specular-glossiness material."""
    import numpy as np
    diffuse_factor = np.array(job["diffuseFactor"], dtype=np.float32)
    specular_factor = np.array(job["specularFactor"], dtype=np.float32)

    # Base colour, metalness and roughness, at the size of the diffuse map, or of
    # the specular map when there is only that. The gloss is the factor times the
    # alpha of the specular map and of ddna.
    size = None
    for uri in (job["diffuse"], job["specular"]):
        if uri:
            first = textures.load(uri)
            size = (first.shape[1], first.shape[0])
            break
    shape = (size[1], size[0]) if size else (1, 1)
    if job["diffuse"]:
        d = textures.load(job["diffuse"], size)
        diffuse = _to_linear(d[..., :3]) * diffuse_factor[:3]
        diffuse_alpha = d[..., 3] * diffuse_factor[3]
    else:
        diffuse = np.broadcast_to(diffuse_factor[:3], shape + (3,))
        diffuse_alpha = np.full(shape, diffuse_factor[3], dtype=np.float32)
    gloss = np.full(shape, job["glossiness"], dtype=np.float32)
    n = textures.load(job["normal"], size) if job["normal"] else None
    if n is not None and size:
        gloss = gloss * n[..., 3]
    if job["specular"]:
        s = textures.load(job["specular"], size)
        specular = _to_linear(s[..., :3]) * specular_factor
        gloss = gloss * s[..., 3]
    else:
        specular = np.broadcast_to(specular_factor, shape + (3,)).astype(np.float32)

    base, alpha, metallic, roughness = spec_gloss_to_metal_rough(diffuse, diffuse_alpha, specular, gloss)
    pbr = {"size": size}
    if size:
        stem = _stem(job["diffuse"] or job["specular"])
        name = "%s_%s_base.%s" % (stem, tag, "png" if job["alpha"] else "jpg")
        rgba = np.concatenate([_to_srgb(base), np.broadcast_to(alpha, base.shape[:2])[..., None]], axis=-1)
        _save(rgba, os.path.join(out_dir, name), job["alpha"])
        pbr["baseColorTexture"] = name
        orm = np.stack([np.ones(base.shape[:2]), roughness, metallic], axis=-1)
        name = "%s_%s_orm.jpg" % (stem, tag)
        _save(orm, os.path.join(out_dir, name), False)
        pbr["metallicRoughnessTexture"] = name
        pbr["baseColorFactor"] = [1.0, 1.0, 1.0, 1.0]
    else:
        # glTF colour factors are linear, like `base`.
        pbr["baseColorFactor"] = [float(v) for v in base.reshape(-1, 3)[0]] + [float(alpha.flat[0])]
        pbr["metallicFactor"] = float(metallic.flat[0])
        pbr["roughnessFactor"] = float(roughness.flat[0])
    return pbr


def _worker(args):
    job, source_dir, out_dir, max_size = args
    try:
        return _convert_material(job, _Textures(source_dir, max_size), out_dir)
    except Exception as exc:                      # noqa: BLE001 - report the material
        return {"error": "%s: %s" % (job["name"], exc)}


def convert(source_dir, out_dir, max_size=2048, workers=None):
    """Write out_dir/bistro.gltf with metallic-roughness materials and
    JPEG/PNG textures. Returns the path."""
    _require_imaging()
    with open(os.path.join(source_dir, SCENE), encoding="utf-8") as f:
        gltf = json.load(f)
    os.makedirs(out_dir, exist_ok=True)
    jobs = [_material_job(m, gltf) for m in gltf["materials"]]
    args = [(job, source_dir, out_dir, max_size) for job in jobs]
    results = [None] * len(jobs)
    workers = workers or max(1, min(8, (os.cpu_count() or 2)))
    with concurrent.futures.ProcessPoolExecutor(workers) as pool:
        for i, result in enumerate(pool.map(_worker, args)):
            results[i] = result
            sys.stdout.write("\r  materials %d/%d" % (i + 1, len(jobs)))
            sys.stdout.flush()
    print()
    errors = [r["error"] for r in results if "error" in r]
    if errors:
        raise SystemExit("Bistro conversion failed:\n  " + "\n  ".join(errors))

    images, textures, by_uri = [], [], {}

    def texture(uri):
        if uri not in by_uri:
            images.append({"uri": uri})
            textures.append({"source": len(images) - 1, "sampler": 0})
            by_uri[uri] = len(textures) - 1
        return {"index": by_uri[uri]}

    for material, job, result in zip(gltf["materials"], jobs, results):
        extensions = material.pop("extensions", {})
        extensions.pop("KHR_materials_pbrSpecularGlossiness", None)
        transmission = extensions.pop("KHR_materials_transmission", None)
        if transmission is not None:
            # Glass: blended and nearly clear, since the engine has no transmission.
            material["alphaMode"] = "BLEND"
            material["pbrMetallicRoughness"] = {
                "baseColorFactor": [1.0, 1.0, 1.0, 1.0 - 0.85 * transmission.get("transmissionFactor", 1.0)],
                "metallicFactor": 0.0, "roughnessFactor": 0.05}
        if extensions:
            material["extensions"] = extensions
        if "pbr" not in result:
            pass                                   # keeps its own factors
        elif "baseColorTexture" in result["pbr"]:
            material["pbrMetallicRoughness"] = {
                "baseColorTexture": texture(result["pbr"]["baseColorTexture"]),
                "metallicRoughnessTexture": texture(result["pbr"]["metallicRoughnessTexture"])}
        else:
            material["pbrMetallicRoughness"] = {k: result["pbr"][k] for k in
                                                ("baseColorFactor", "metallicFactor", "roughnessFactor")}
        if "normal" in result:
            material["normalTexture"] = texture(result["normal"])
        else:
            material.pop("normalTexture", None)
        if "emissive" in result:
            material["emissiveTexture"] = texture(result["emissive"])
            material.setdefault("emissiveFactor", [1.0, 1.0, 1.0])
        else:
            material.pop("emissiveTexture", None)

    gltf["images"] = images
    gltf["textures"] = textures
    gltf["samplers"] = [{"magFilter": 9729, "minFilter": 9987, "wrapS": 10497, "wrapT": 10497}]
    gltf["extensionsUsed"] = [e for e in gltf.get("extensionsUsed", [])
                              if e not in ("KHR_materials_pbrSpecularGlossiness", "MSFT_texture_dds",
                                           "KHR_materials_transmission")]
    gltf.pop("extensionsRequired", None)
    # The export names mesh nodes subset_0, subset_1...; name them after their
    # material instead, so a scene can find its bulbs and glass by name.
    for index, node in enumerate(gltf["nodes"]):
        if "mesh" in node:
            primitive = gltf["meshes"][node["mesh"]]["primitives"][0]
            material = gltf["materials"][primitive["material"]]["name"] if "material" in primitive else "none"
            node["name"] = "%s#%d" % (material, index)
    for buffer in gltf["buffers"]:
        buffer["uri"] = "source/" + buffer["uri"]
    path = os.path.join(out_dir, "bistro.gltf")
    with open(path, "w", encoding="utf-8") as f:
        json.dump(gltf, f)
    return path
