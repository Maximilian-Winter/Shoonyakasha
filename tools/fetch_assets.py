#!/usr/bin/env python3
"""Download the large example assets that are not committed.

The repository ships small versions of everything (see assets/README.md), so all
examples run on a fresh clone without this script. Run it when you want the
full-resolution environment maps, or the Sponza scene.

    python tools/fetch_assets.py --list
    python tools/fetch_assets.py env          # all environment maps
    python tools/fetch_assets.py kloofendal_4k

    python tools/fetch_assets.py alley        # the alley demo's Poly Haven models
    python tools/fetch_assets.py alley --resolution 2k

Sponza is deliberately not downloaded automatically: its licence terms are worth
reading before you accept them, so the script prints the page to get it from.

Poly Haven sets (models and textures, all CC0) go into assets/polyhaven/<id>/,
one directory per asset, at the texture resolution asked for (1k by default).
"""

import argparse
import json
import os
import sys
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
ASSETS = os.path.normpath(os.path.join(HERE, "..", "assets"))

# name -> (destination relative to assets/, url, licence, approx MB)
DOWNLOADS = {
    "kloofendal_4k": (
        "env/kloofendal_28d_misty_4k.hdr",
        "https://dl.polyhaven.org/file/ph-assets/HDRIs/hdr/4k/kloofendal_28d_misty_4k.hdr",
        "CC0", 25),
    "kloofendal_8k": (
        "env/kloofendal_28d_misty_8k.hdr",
        "https://dl.polyhaven.org/file/ph-assets/HDRIs/hdr/8k/kloofendal_28d_misty_8k.hdr",
        "CC0", 99),
    "farm_sunset_4k": (
        "env/farm_sunset_4k.hdr",
        "https://dl.polyhaven.org/file/ph-assets/HDRIs/hdr/4k/farm_sunset_4k.hdr",
        "CC0", 26),
    "charolettenbrunn_4k": (
        "env/charolettenbrunn_park_4k.hdr",
        "https://dl.polyhaven.org/file/ph-assets/HDRIs/hdr/4k/charolettenbrunn_park_4k.hdr",
        "CC0", 27),
}

GROUPS = {
    "env": ["kloofendal_4k", "farm_sunset_4k", "charolettenbrunn_4k"],
    "all": list(DOWNLOADS),
}

# Poly Haven sets: name -> (models, textures). Models come as glTF with their
# textures; textures as the colour, OpenGL normal and AO/rough/metal maps.
POLYHAVEN_API = "https://api.polyhaven.com"
POLYHAVEN_SETS = {
    "alley": ([
        "modular_urban_apartments_facade", "modular_factory_facade", "modular_fire_escape",
        "street_lamp_01", "street_lamp_02", "barrel_stove", "covered_car", "modular_chainlink_fence",
        "metal_trash_can", "concrete_road_barrier", "old_tyre", "utility_box_01", "exterior_aircon_unit",
        "rollershutter_door", "security_light", "fire_hydrant", "water_manhole_cover",
        "cardboard_box_01", "Barrel_02", "plastic_monobloc_chair_01", "potted_plant_02", "weed_plant_02",
        "wooden_crate_01",
    ], ["asphalt_02"]),
}
TEXTURE_MAPS = {"Diffuse": "diff", "nor_gl": "nor_gl", "arm": "arm"}


def _json(url):
    request = urllib.request.Request(url, headers={"User-Agent": "shoonyakasha-fetch-assets"})
    with urllib.request.urlopen(request) as response:
        return json.load(response)


def _download(url, destination):
    if os.path.exists(destination):
        return
    os.makedirs(os.path.dirname(destination), exist_ok=True)
    partial = destination + ".part"
    urllib.request.urlretrieve(url, partial)
    os.replace(partial, destination)


def fetch_polyhaven(asset, kind, resolution):
    """One Poly Haven model or texture into assets/polyhaven/<asset>/."""
    directory = os.path.join(ASSETS, "polyhaven", asset)
    try:
        files = _json("%s/files/%s" % (POLYHAVEN_API, asset))
        if kind == "model":
            entry = files["gltf"][resolution]["gltf"]
            downloads = {asset + ".gltf": entry["url"]}
            downloads.update({path: item["url"] for path, item in entry.get("include", {}).items()})
        else:
            downloads = {}
            for key, suffix in TEXTURE_MAPS.items():
                item = files[key][resolution]["jpg"]
                downloads["%s_%s.jpg" % (asset, suffix)] = item["url"]
        for relative, url in downloads.items():
            _download(url, os.path.join(directory, relative))
        if kind == "model":
            _alpha_textures(files, resolution, os.path.join(directory, asset + ".gltf"))
    except Exception as exc:                      # noqa: BLE001 - report and continue
        print("  %-34s FAILED: %s" % (asset, exc))
        return False
    print("  %-34s -> assets/polyhaven/%s" % (asset, asset))
    return True


def _alpha_textures(files, resolution, gltf_path):
    """Poly Haven's glTF downloads carry JPEG colour maps, which have no alpha,
    so alpha-tested and blended materials (leaves, wire mesh, glass) come out
    opaque. Fetch the PNG of those maps, which keeps the alpha, and point the
    glTF at it."""
    with open(gltf_path, encoding="utf-8") as f:
        gltf = json.load(f)
    wanted = set()
    for material in gltf.get("materials", []):
        if material.get("alphaMode", "OPAQUE") == "OPAQUE":
            continue
        slot = material.get("pbrMetallicRoughness", {}).get("baseColorTexture")
        if slot is not None:
            wanted.add(gltf["textures"][slot["index"]]["source"])
    changed = False
    for index in wanted:
        image = gltf["images"][index]
        uri = image.get("uri", "")
        if not uri.endswith(".jpg"):
            continue
        name = os.path.basename(uri)
        for maps in files.values():
            entry = maps.get(resolution, {}) if isinstance(maps, dict) else {}
            if "jpg" in entry and "png" in entry and os.path.basename(entry["jpg"]["url"]) == name:
                png = uri[:-4] + ".png"
                _download(entry["png"]["url"], os.path.join(os.path.dirname(gltf_path), png))
                image["uri"] = png
                image["mimeType"] = "image/png"
                changed = True
                break
    if changed:
        with open(gltf_path, "w", encoding="utf-8") as f:
            json.dump(gltf, f)


MANUAL = {
    "sponza": (
        "models/NewSponza_Main_glTF_003.gltf",
        "https://www.intel.com/content/www/us/en/developer/topic-technology/graphics-processing-research/samples.html",
        "See assets/README.md -- the bundled licence file states both "
        "'personal and educational use' terms and the full CC BY 4.0 text, so "
        "read it and decide before redistributing anything built on it."),
}


def report(done, total, block):
    if total <= 0:
        return
    pct = min(100, done * block * 100 // total)
    sys.stdout.write("\r    %3d%%" % pct)
    sys.stdout.flush()


def fetch(name):
    relative, url, licence, mb = DOWNLOADS[name]
    destination = os.path.join(ASSETS, relative)

    if os.path.exists(destination):
        print("  %-22s already present" % name)
        return True

    os.makedirs(os.path.dirname(destination), exist_ok=True)
    print("  %-22s %s  (~%d MB, %s)" % (name, url.rsplit("/", 1)[-1], mb, licence))

    partial = destination + ".part"
    try:
        urllib.request.urlretrieve(url, partial, report)
        sys.stdout.write("\r")
        os.replace(partial, destination)
    except Exception as exc:                      # noqa: BLE001 - report and continue
        if os.path.exists(partial):
            os.remove(partial)
        print("\r  %-22s FAILED: %s" % (name, exc))
        return False

    print("  %-22s -> assets/%s" % (name, relative))
    return True


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("what", nargs="*", default=["env"],
                        help="asset or group name (default: env)")
    parser.add_argument("--list", action="store_true", help="show what is available")
    parser.add_argument("--resolution", default="1k", choices=["1k", "2k", "4k"],
                        help="texture resolution of Poly Haven sets (default: 1k)")
    args = parser.parse_args()

    if args.list:
        print("Groups:")
        for group, members in GROUPS.items():
            print("  %-22s %s" % (group, ", ".join(members)))
        print("\nDownloadable:")
        for name, (relative, _, licence, mb) in DOWNLOADS.items():
            here = "present" if os.path.exists(os.path.join(ASSETS, relative)) else "missing"
            print("  %-22s ~%4d MB  %-4s  %s" % (name, mb, licence, here))
        print("\nPoly Haven sets (CC0, into assets/polyhaven/):")
        for name, (models, textures) in POLYHAVEN_SETS.items():
            print("  %-22s %d models, %d textures" % (name, len(models), len(textures)))
        print("\nManual (licence needs reading first):")
        for name, (relative, url, note) in MANUAL.items():
            print("  %-22s %s\n    %s\n    %s" % (name, relative, url, note))
        return 0

    wanted = []
    for item in args.what:
        if item in POLYHAVEN_SETS:
            models, textures = POLYHAVEN_SETS[item]
            print("Poly Haven set '%s' at %s (CC0, https://polyhaven.com):" % (item, args.resolution))
            failures = sum(not fetch_polyhaven(m, "model", args.resolution) for m in models)
            failures += sum(not fetch_polyhaven(t, "texture", args.resolution) for t in textures)
            if failures:
                print("%d asset(s) failed" % failures)
                return 1
            continue
        if item in GROUPS:
            wanted.extend(GROUPS[item])
        elif item in DOWNLOADS:
            wanted.append(item)
        elif item in MANUAL:
            relative, url, note = MANUAL[item]
            print("%s is not downloaded automatically." % item)
            print("  get it from : %s" % url)
            print("  place it at : assets/%s" % relative)
            print("  %s" % note)
            return 0
        else:
            print("Unknown: %s (try --list)" % item)
            return 1

    if not wanted:
        return 0
    failures = 0
    for name in dict.fromkeys(wanted):
        if not fetch(name):
            failures += 1

    print("\n%d/%d fetched into %s" % (len(wanted) - failures, len(wanted), ASSETS))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
