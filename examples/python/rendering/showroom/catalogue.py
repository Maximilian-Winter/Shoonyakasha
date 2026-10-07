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

import re


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
    ("CC Attribution-ShareAlike", "CC BY-SA 4.0"),
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


def ascii_text(text):
    """Text the engine's glyph atlas can draw (ASCII 32-126)."""
    text = text.replace("´", "'").replace("’", "'").replace("‘", "'")
    text = text.replace("“", '"').replace("”", '"').replace("–", "-").replace("—", "-")
    return "".join(c if 32 <= ord(c) <= 126 else "?" for c in text)
