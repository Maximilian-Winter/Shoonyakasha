"""
What the showroom shows and how it stands each model.

The models themselves, and their credits, come from tools/sketchfab.py by way
of `python tools/fetch_assets.py showroom`; each converted model carries its
credit in assets/showroom/<name>/showroom.json, which is what the showroom
puts on screen.

Sizes are the length the model is shown at, in metres along its longest
horizontal side: the files come in whatever units their authors used.
`yaw` turns a model so its nose points at the front of the stage (+Z, where
the cinematic shots start); Y in the showroom turns the current model by 90
degrees and prints the value to put here.
"""

import json
import math
import re
import struct


class Entry:
    def __init__(self, name, title, year, kind, length, yaw=0.0, hover=0.0, plinth=0.0,
                 paint=None, rig="studio", spin=8.0):
        self.name = name
        self.title = title              # shown large on screen
        self.year = year
        self.kind = kind                # "car", "ship", "vehicle" or "miniature"
        self.length = length            # metres along the longest horizontal side
        self.yaw = yaw                  # degrees about +Y
        self.hover = hover              # metres above the turntable (ships and repulsorlifts)
        self.plinth = plinth            # height of a plinth to stand on; 0 for none
        self.paint = re.compile(paint, re.IGNORECASE) if paint else None   # body paint materials
        self.rig = rig                  # the lighting it opens with
        self.spin = spin                # turntable degrees per second


CAR_PAINT = r"paint|body|carroc|lack|exterior|shell"

CATALOGUE = [
    Entry("alfa_33_stradale_1968", "Alfa Romeo 33 Stradale", "1968", "car", 3.97, paint=CAR_PAINT),
    Entry("alfa_33_stradale_2024", "Alfa Romeo 33 Stradale", "2024", "car", 4.65, paint=CAR_PAINT),
    Entry("alfa_montreal", "Alfa Romeo Montreal", "1970", "car", 4.22, paint=CAR_PAINT),
    Entry("alfa_gtv6", "Alfa Romeo GTV-6", "1986", "car", 4.26, paint=CAR_PAINT),
    Entry("gaz13_chaika", "GAZ-13 Chaika", "1959", "car", 5.60, paint=CAR_PAINT),
    Entry("shelby_cobra", "Shelby Cobra 289", "1963", "car", 3.85, paint=CAR_PAINT),
    Entry("chevrolet_camaro", "Chevrolet Camaro Z/28", "1969", "car", 4.72, paint=CAR_PAINT),
    Entry("porsche_911_turbo", "Porsche 911 Turbo (930)", "1975", "car", 4.29, paint=CAR_PAINT),
    Entry("corvette_c8", "Chevrolet Corvette C8 Stingray", "2020", "car", 4.63, paint=CAR_PAINT),
    Entry("lamborghini_revuelto", "Lamborghini Revuelto", "2023", "car", 4.95, paint=CAR_PAINT),
    Entry("eta2_interceptor", "Eta-2 Actis Interceptor", "Anakin's", "ship", 5.5, hover=1.4,
          rig="neon", spin=12.0),
    Entry("jedi_starfighter", "Delta-7B Jedi Starfighter", "Anakin's", "ship", 7.0, hover=1.6,
          rig="neon", spin=12.0),
    Entry("aat", "AAT Battle Tank", "Trade Federation", "vehicle", 9.0, hover=0.45, rig="hardlight", spin=6.0),
    Entry("alkesh", "Al'kesh", "Goa'uld bomber", "ship", 12.0, hover=2.2, rig="night", spin=6.0),
]

BY_NAME = {e.name: e for e in CATALOGUE}


LICENCES = (
    ("CC Attribution-NonCommercial-ShareAlike", "CC BY-NC-SA 4.0"),
    ("Creative Commons Attribution-NonCommercial-ShareAlike", "CC BY-NC-SA 4.0"),
    ("CC Attribution-NonCommercial", "CC BY-NC 4.0"),
    ("Creative Commons Attribution-NonCommercial", "CC BY-NC 4.0"),
    ("CC Attribution-ShareAlike", "CC BY-SA 4.0"),
    ("Creative Commons Attribution-ShareAlike", "CC BY-SA 4.0"),
    ("Creative Commons Attribution", "CC BY 4.0"),
    ("CC Attribution", "CC BY 4.0"),
)


def short_credit(credit):
    """'"Title" by Author - CC BY 4.0 - skfb.ly/xyz' from a Sketchfab credit
    line, in the ASCII the engine's text can draw."""
    m = re.match(r'"(?P<title>.*)" \((?P<url>[^)]*)\) by (?P<author>.*) is licensed under (?P<licence>.*?) \(', credit)
    if not m:
        return ascii_text(credit)
    licence = m.group("licence")
    for long_name, short in LICENCES:
        if licence.startswith(long_name):
            licence = short
            break
    url = re.sub(r"^https?://", "", m.group("url"))
    return ascii_text('"%s" by %s - %s - %s' % (m.group("title"), m.group("author"), licence, url))


def gltf_bounds(path):
    """World-space bounds of a glTF or GLB's meshes, from their accessors'
    min/max (which glTF requires for positions)."""
    with open(path, "rb") as f:
        data = f.read()
    if data[:4] == b"glTF":
        length = struct.unpack_from("<I", data, 12)[0]
        gltf = json.loads(data[20:20 + length].decode("utf-8"))
    else:
        gltf = json.loads(data.decode("utf-8"))
    nodes = gltf.get("nodes", [])
    lo, hi = [math.inf] * 3, [-math.inf] * 3

    def matmul(a, b):
        return [[sum(a[i][k] * b[k][j] for k in range(4)) for j in range(4)] for i in range(4)]

    def local(node):
        if "matrix" in node:
            m = node["matrix"]
            return [[m[c * 4 + r] for c in range(4)] for r in range(4)]
        t = node.get("translation", [0, 0, 0])
        x, y, z, w = node.get("rotation", [0, 0, 0, 1])
        s = node.get("scale", [1, 1, 1])
        r = [[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
             [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
             [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]]
        return [[r[i][0] * s[0], r[i][1] * s[1], r[i][2] * s[2], t[i]] for i in range(3)] + [[0, 0, 0, 1]]

    def walk(index, parent):
        node = nodes[index]
        m = matmul(parent, local(node))
        if "mesh" in node:
            for primitive in gltf["meshes"][node["mesh"]]["primitives"]:
                a = gltf["accessors"][primitive["attributes"]["POSITION"]]
                if "min" not in a:
                    continue
                for x in (a["min"][0], a["max"][0]):
                    for y in (a["min"][1], a["max"][1]):
                        for z in (a["min"][2], a["max"][2]):
                            p = [sum(m[i][k] * v for k, v in enumerate((x, y, z, 1.0))) for i in range(3)]
                            for i in range(3):
                                lo[i] = min(lo[i], p[i])
                                hi[i] = max(hi[i], p[i])
        for child in node.get("children", []):
            walk(child, m)

    identity = [[1.0 if i == j else 0.0 for j in range(4)] for i in range(4)]
    scene = gltf.get("scenes", [{}])[gltf.get("scene", 0)] if gltf.get("scenes") else {"nodes": list(range(len(nodes)))}
    for root in scene.get("nodes", []):
        walk(root, identity)
    if lo[0] == math.inf:
        return [-0.5, 0.0, -0.5], [0.5, 1.0, 0.5]
    return lo, hi


def ascii_text(text):
    """Text the engine's glyph atlas can draw (ASCII 32-126)."""
    text = text.replace("´", "'").replace("’", "'").replace("‘", "'")
    text = text.replace("“", '"').replace("”", '"').replace("–", "-").replace("—", "-")
    text = text.replace("™", "(TM)")
    return "".join(c if 32 <= ord(c) <= 126 else "?" for c in text)
