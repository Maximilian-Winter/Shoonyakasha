#!/usr/bin/env python3
"""Download the large example assets that are not committed.

The repository ships small versions of everything (see assets/README.md), so all
examples run on a fresh clone without this script. Run it when you want the
full-resolution environment maps, or the Sponza scene.

    python tools/fetch_assets.py --list
    python tools/fetch_assets.py env          # all environment maps
    python tools/fetch_assets.py kloofendal_4k

Sponza is deliberately not downloaded automatically: its licence terms are worth
reading before you accept them, so the script prints the page to get it from.
"""

import argparse
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
    args = parser.parse_args()

    if args.list:
        print("Groups:")
        for group, members in GROUPS.items():
            print("  %-22s %s" % (group, ", ".join(members)))
        print("\nDownloadable:")
        for name, (relative, _, licence, mb) in DOWNLOADS.items():
            here = "present" if os.path.exists(os.path.join(ASSETS, relative)) else "missing"
            print("  %-22s ~%4d MB  %-4s  %s" % (name, mb, licence, here))
        print("\nManual (licence needs reading first):")
        for name, (relative, url, note) in MANUAL.items():
            print("  %-22s %s\n    %s\n    %s" % (name, relative, url, note))
        return 0

    wanted = []
    for item in args.what:
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

    failures = 0
    for name in dict.fromkeys(wanted):
        if not fetch(name):
            failures += 1

    print("\n%d/%d fetched into %s" % (len(wanted) - failures, len(wanted), ASSETS))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
