"""
Prepares the scans of scans.SCANS with Blender, once:

    python -m shoonyakasha.planet.scan_export            # the kinds not yet prepared
    python -m shoonyakasha.planet.scan_export --all      # every kind again
    python -m shoonyakasha.planet.scan_export --check    # report what is prepared, exit 1 if anything is wrong

Each scene's archive (scans.SCENES, in scans.source_directory()) is
extracted once into scans.directory()/source/<scene>/, after its entries'
paths are checked to stay inside it; Blender (SHOONYAKASHA_BLENDER, else the
newest under "Blender Foundation" in Program Files, else blender on the
PATH) then runs headless with the scene's own scripts turned off (-b -Y)
and scan_blender.py on that scene's kinds, writing them under
scans.directory()/prepared/. CREDITS.md there lists every prepared kind
with its scene and object.

The check passes when every kind is prepared within its triangle budget
(by TRIANGLE_SLACK), a rock's hull has at least 4 and at most
scans.HULL_POINTS points, every material's textures are there, and
CREDITS.md names every kind.
"""

import argparse
import glob
import json
import os
import shutil
import subprocess
import sys
import tempfile
import zipfile

from . import scans

TEXTURE_SIZE = 1024
TRIANGLE_SLACK = 1.1


def find_blender():
    chosen = os.environ.get("SHOONYAKASHA_BLENDER")
    if chosen:
        return chosen
    found = sorted(glob.glob(os.path.join(os.environ.get("ProgramFiles", r"C:\Program Files"), "Blender Foundation",
                                          "Blender *", "blender.exe")),
                   key=lambda p: [int(x) if x.isdigit() else 0
                                  for x in os.path.basename(os.path.dirname(p)).split()[-1].split(".")])
    return found[-1] if found else shutil.which("blender")


def extracted(scene):
    """The scene's extracted folder, extracting its archive when it is not there."""
    archive, blend = scans.SCENES[scene]
    folder = os.path.join(scans.directory(), "source", scene)
    if os.path.exists(os.path.join(folder, blend)):
        return folder
    path = os.path.join(scans.source_directory(), archive)
    with zipfile.ZipFile(path) as z:
        for name in z.namelist():
            if name.startswith(("/", "\\")) or ".." in name.replace("\\", "/").split("/") or ":" in name:
                raise ValueError("%s holds an entry outside its folder: %s" % (archive, name))
        os.makedirs(folder, exist_ok=True)
        z.extractall(folder)
    return folder


def export(kinds):
    blender = find_blender()
    if blender is None:
        raise SystemExit("Blender not found: set SHOONYAKASHA_BLENDER to blender's executable")
    out = os.path.join(scans.directory(), "prepared")
    os.makedirs(out, exist_ok=True)
    script = os.path.join(os.path.dirname(os.path.abspath(__file__)), "scan_blender.py")
    for scene in sorted({k.scene for k in kinds}):
        folder = extracted(scene)
        job = {"out": out, "texture_size": TEXTURE_SIZE,
               "objects": [{"kind": k.name, "object": k.object, "triangles": k.triangles, "rock": k.rock,
                            "hull_points": scans.HULL_POINTS} for k in kinds if k.scene == scene]}
        with tempfile.NamedTemporaryFile("w", suffix=".json", delete=False, encoding="utf-8") as f:
            json.dump(job, f)
        try:
            result = subprocess.run([blender, "-b", "-Y", os.path.join(folder, scans.SCENES[scene][1]),
                                     "--python-exit-code", "1", "--python", script, "--", f.name],
                                    capture_output=True, text=True, errors="replace")
        finally:
            os.unlink(f.name)
        for line in result.stdout.splitlines():
            if line.startswith("[scans]"):
                print(line, flush=True)
        if result.returncode != 0:
            print(result.stdout[-4000:] + result.stderr[-4000:], file=sys.stderr)
            raise SystemExit("Blender failed on %s" % scene)
    write_credits()


def write_credits():
    lines = ["# Scans", "",
             "CC0 models from Poly Haven (polyhaven.com), prepared from its scenes by "
             "shoonyakasha.planet.scan_export.", ""]
    for kind in scans.SCANS:
        meta = os.path.join(scans.prepared_directory(kind.name), "meta.json")
        if os.path.exists(meta):
            lines.append("- `%s`: %s, object `%s`" % (kind.name, scans.SCENES[kind.scene][1], kind.object))
    with open(os.path.join(scans.directory(), "prepared", "CREDITS.md"), "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")


def check():
    """Lines of what is prepared and whether the check passes."""
    lines, ok = [], True
    credits_path = os.path.join(scans.directory(), "prepared", "CREDITS.md")
    credits = open(credits_path, encoding="utf-8").read() if os.path.exists(credits_path) else ""
    for kind in scans.SCANS:
        scan = scans.prepared(kind)
        if scan is None:
            lines.append("%s: not prepared" % kind.name)
            ok = False
            continue
        triangles = len(scan.indices) // 3
        within = triangles <= kind.triangles * TRIANGLE_SLACK
        hull_ok = not kind.rock or 4 <= len(scan.hull) <= scans.HULL_POINTS
        files = all(os.path.exists(t[key]) for t in scan.textures for key in ("albedo", "normal") if t.get(key))
        textured = all(t.get("albedo") for t in scan.textures)
        credited = ("`%s`" % kind.name) in credits
        good = within and hull_ok and files and textured and credited
        ok = ok and good
        lines.append("%s: %d triangles (budget %d), %.2f m, %d materials%s%s: %s"
                     % (kind.name, triangles, kind.triangles, scan.height, len(scan.textures),
                        ", hull %d points" % len(scan.hull) if kind.rock else "",
                        "" if credited else ", not in CREDITS.md", "ok" if good else "FAILED"))
    return lines, ok


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--all", action="store_true")
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    if args.check:
        lines, ok = check()
        print("\n".join(lines))
        print("scans: %s" % ("passed" if ok else "FAILED"))
        raise SystemExit(0 if ok else 1)
    kinds = [k for k in scans.SCANS if args.all or scans.prepared(k) is None]
    if kinds:
        export(kinds)
    print("scans prepared in %s" % os.path.join(scans.directory(), "prepared"))


if __name__ == "__main__":
    main()
