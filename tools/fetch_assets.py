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

    python tools/fetch_assets.py bistro       # Amazon Lumberyard Bistro, ~2 GB, then converted

    python tools/fetch_assets.py showroom     # the showroom demo's Sketchfab models (needs an API token)
    python tools/fetch_assets.py showroom/alfa_gtv6 showroom/aat

Sponza is deliberately not downloaded automatically: its licence terms are worth
reading before you accept them, so the script prints the page to get it from.

Poly Haven sets (models and textures, all CC0) go into assets/polyhaven/<id>/,
one directory per asset, at the texture resolution asked for (1k by default).

The Bistro (CC BY 4.0) is downloaded into assets/bistro/source/ and converted
for the engine into assets/bistro/, its textures at most 2k by default
(`--resolution` 1k or 4k to change that). Converting needs Pillow and numpy;
see tools/bistro.py for what it changes.

The showroom's models come from Sketchfab, which hands out downloads only to a
signed-in account: set SKETCHFAB_API_TOKEN to the API token from
https://sketchfab.com/settings/password (or pass --sketchfab-token). They are
converted into assets/showroom/<name>/ (Pillow and numpy again; see
tools/sketchfab.py). Most are CC BY-NC-SA 4.0, a few CC BY 4.0: credit them
wherever they are shown, and keep NC models out of commercial use.
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


def fetch_showroom(names, resolution, token):
    """Download the showroom's Sketchfab models and convert them; see
    tools/sketchfab.py."""
    sys.path.insert(0, HERE)
    import sketchfab
    print("Showroom models from Sketchfab, into assets/showroom/ (textures at most %s):" % resolution)
    return sketchfab.fetch(names, ASSETS, token,
                           max_size={"1k": 1024, "2k": 2048, "4k": 4096}[resolution]) == 0


def fetch_bistro(resolution):
    """Download the Bistro and convert it; see tools/bistro.py."""
    sys.path.insert(0, HERE)
    import bistro
    directory = os.path.join(ASSETS, "bistro")
    print("Amazon Lumberyard Bistro (CC BY 4.0, https://developer.nvidia.com/orca/amazon-lumberyard-bistro)")
    print("  downloading into assets/bistro/source/ (~2 GB) ...")
    try:
        bistro.fetch(os.path.join(directory, "source"))
    except Exception as exc:                      # noqa: BLE001 - report and stop
        print("  FAILED: %s (run again to resume)" % exc)
        return False
    print("  converting at %s ..." % resolution)
    path = bistro.convert(os.path.join(directory, "source"), directory,
                          max_size={"1k": 1024, "2k": 2048, "4k": 4096}[resolution])
    print("  -> assets/%s" % os.path.relpath(path, ASSETS).replace(os.sep, "/"))
    return True


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
    parser.add_argument("--resolution", choices=["1k", "2k", "4k"],
                        help="texture resolution of Poly Haven sets (default: 1k), "
                             "the Bistro and the showroom models (default: 2k)")
    parser.add_argument("--sketchfab-token", default=os.environ.get("SKETCHFAB_API_TOKEN"),
                        help="Sketchfab API token for the showroom models "
                             "(default: $SKETCHFAB_API_TOKEN)")
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
        print("\nConverted (CC BY 4.0, into assets/bistro/):")
        here = "present" if os.path.exists(os.path.join(ASSETS, "bistro", "bistro.gltf")) else "missing"
        print("  %-22s ~2000 MB  Amazon Lumberyard Bistro  %s" % ("bistro", here))
        sys.path.insert(0, HERE)
        import sketchfab
        print("\nShowroom models (Sketchfab, needs an API token; into assets/showroom/):")
        for name, (_, credit) in sketchfab.MODELS.items():
            here = "present" if os.path.exists(os.path.join(ASSETS, "showroom", name, "model.gltf")) else "missing"
            print("  %-22s %s\n    %s" % ("showroom/" + name, here, credit))
        print("\nManual (licence needs reading first):")
        for name, (relative, url, note) in MANUAL.items():
            print("  %-22s %s\n    %s\n    %s" % (name, relative, url, note))
        return 0

    wanted = []
    showroom = []
    for item in args.what:
        if item == "showroom" or item.startswith("showroom/"):
            sys.path.insert(0, HERE)
            import sketchfab
            names = list(sketchfab.MODELS) if item == "showroom" else [item.split("/", 1)[1]]
            unknown = [n for n in names if n not in sketchfab.MODELS]
            if unknown:
                print("Unknown showroom model: %s (try --list)" % ", ".join(unknown))
                return 1
            showroom += names
            continue
        if item == "bistro":
            if not fetch_bistro(args.resolution or "2k"):
                return 1
            continue
        if item in POLYHAVEN_SETS:
            models, textures = POLYHAVEN_SETS[item]
            resolution = args.resolution or "1k"
            print("Poly Haven set '%s' at %s (CC0, https://polyhaven.com):" % (item, resolution))
            failures = sum(not fetch_polyhaven(m, "model", resolution) for m in models)
            failures += sum(not fetch_polyhaven(t, "texture", resolution) for t in textures)
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

    if showroom and not fetch_showroom(list(dict.fromkeys(showroom)), args.resolution or "2k",
                                       args.sketchfab_token):
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
