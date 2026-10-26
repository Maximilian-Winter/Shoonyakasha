"""
The ground's textures: an albedo and a normal map for each of the
terrain's layers, CC0 scans from Poly Haven and ambientCG.

    python -m shoonyakasha.planet.ground_textures    # download the missing ones

They are kept in DIRECTORY: the SHOONYAKASHA_GROUND_TEXTURES environment
variable's directory, else shoonyakasha/ground_textures in the user's
cache (%LOCALAPPDATA% on Windows, $XDG_CACHE_HOME or ~/.cache elsewhere).
`load(engine)` hands those found there to the pipeline's "groundAlbedo<N>"
and "groundNormal<N>" external images, with mipmaps, and returns each
layer's mean albedo (linear RGB), or None when any file is missing.
"""

import io
import json
import os
import sys
import urllib.request
import zipfile

import numpy as np



def _directory():
    chosen = os.environ.get("SHOONYAKASHA_GROUND_TEXTURES")
    if chosen:
        return chosen
    if os.name == "nt" and os.environ.get("LOCALAPPDATA"):
        cache = os.environ["LOCALAPPDATA"]
    else:
        cache = os.environ.get("XDG_CACHE_HOME") or os.path.join(os.path.expanduser("~"), ".cache")
    return os.path.join(cache, "shoonyakasha", "ground_textures")


DIRECTORY = _directory()

# (layer, source, asset id), in the order of the shader's layers (ground.glsl).
LAYERS = [
    ("sand", "polyhaven", "aerial_beach_01"),
    ("dry", "polyhaven", "aerial_ground_rock"),
    ("grass", "polyhaven", "rocky_terrain_02"),
    ("forest", "ambientcg", "Ground037"),
    ("rock", "polyhaven", "aerial_rocks_02"),
    ("snow", "ambientcg", "Snow006"),
    ("moon", "ambientcg", "Ground054"),
]

USER_AGENT = "Shoonyakasha"


def paths(layer):
    return (os.path.join(DIRECTORY, layer + "_albedo.jpg"), os.path.join(DIRECTORY, layer + "_normal.jpg"))


def _get(url):
    request = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    with urllib.request.urlopen(request, timeout=120) as response:
        return response.read()


def _polyhaven(asset):
    """(albedo, normal) JPEG bytes at 1K; the normal map in OpenGL's convention."""
    files = json.loads(_get("https://api.polyhaven.com/files/" + asset))
    return (_get(files["Diffuse"]["1k"]["jpg"]["url"]), _get(files["nor_gl"]["1k"]["jpg"]["url"]))


def _ambientcg(asset):
    """(albedo, normal) JPEG bytes from the asset's 1K-JPG archive."""
    archive = zipfile.ZipFile(io.BytesIO(_get("https://ambientcg.com/get?file=%s_1K-JPG.zip" % asset)))
    names = archive.namelist()
    pick = lambda suffix: archive.read(next(n for n in names if n.endswith(suffix)))
    return (pick("_Color.jpg"), pick("_NormalGL.jpg"))


def fetch():
    os.makedirs(DIRECTORY, exist_ok=True)
    credits = ["Ground textures, all CC0 (https://creativecommons.org/publicdomain/zero/1.0/):", ""]
    for layer, source, asset in LAYERS:
        albedo_path, normal_path = paths(layer)
        site = ("https://polyhaven.com/a/" + asset) if source == "polyhaven" else (
            "https://ambientcg.com/view?id=" + asset)
        credits.append("- %s: %s (%s)" % (layer, asset, site))
        if os.path.exists(albedo_path) and os.path.exists(normal_path):
            print("have", layer)
            continue
        print("fetching", layer, "from", site, flush=True)
        albedo, normal = (_polyhaven if source == "polyhaven" else _ambientcg)(asset)
        with open(albedo_path, "wb") as f:
            f.write(albedo)
        with open(normal_path, "wb") as f:
            f.write(normal)
    with open(os.path.join(DIRECTORY, "CREDITS.md"), "w", encoding="utf-8") as f:
        f.write("\n".join(credits) + "\n")


def available():
    return all(os.path.exists(p) for layer, _, _ in LAYERS for p in paths(layer))


def _rgba(path):
    from PIL import Image
    return np.ascontiguousarray(np.asarray(Image.open(path).convert("RGBA"), dtype=np.uint8))


def _srgb_to_linear(c):
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)


def load(engine):
    """Hand the textures to the pipeline and return each layer's mean
    albedo in linear RGB, or None (and nothing handed over) when a file is
    missing or Pillow is not installed."""
    if not available():
        return None
    try:
        import PIL  # noqa: F401
    except ImportError:
        print("ground textures: Pillow is needed to read them (pip install pillow)", flush=True)
        return None
    means = []
    for index, (layer, _, _) in enumerate(LAYERS):
        albedo_path, normal_path = paths(layer)
        albedo = _rgba(albedo_path)
        engine.set_pipeline_image_data("groundAlbedo%d" % index, albedo, format="R8G8B8A8_SRGB", mipmaps=True)
        engine.set_pipeline_image_data("groundNormal%d" % index, _rgba(normal_path), format="R8G8B8A8_UNORM",
                                       mipmaps=True)
        means.append(tuple(float(v) for v in _srgb_to_linear(albedo[..., :3] / 255.0).mean(axis=(0, 1))))
    return means


if __name__ == "__main__":
    try:
        fetch()
    except Exception as error:   # network or archive trouble: say what failed
        sys.exit("could not fetch the textures: %s" % error)
    print("textures in", DIRECTORY)
