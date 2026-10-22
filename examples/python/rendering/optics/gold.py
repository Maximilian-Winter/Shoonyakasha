"""
Shoonyakasha - Why gold is gold

Four polished spheres of gold, copper, silver and aluminium, path traced with
the wave optics example's spectral pipeline from each metal's measured
complex refractive index n + ik. A panel plots each metal's reflectance at
normal incidence, and the n and k of one of them, from the same tables the
renderer reads, with a wavelength cursor that reads off n, k and the
reflectance of each metal there. The scene is lit by white light, or by
light of the cursor's wavelength alone, shown in its colour; moving the
cursor switches to the latter.

Keys:
    WASD/Q/E + right mouse   fly the camera; moving starts the average over
    Left / Right             move the wavelength cursor by 5 nm, in light of that wavelength
    V                        white light / light of the cursor's wavelength
    M                        plot n and k of the next metal
    R                        start the average over
    F2                       hide or show the panel
    P                        save a screenshot to gold_captures/

Usage:
    python gold.py
    python gold.py --metal copper --wavelength 560
    python gold.py --single --wavelength 450           # light of 450 nm alone
    python gold.py --samples 512 --screenshot out.png   # converge, save, quit

Requirements:
    - the shoonyakasha package installed (pip install .)
    - a GPU with ray queries
"""

import argparse
import math
import os
import re

import shoonyakasha as sk
from shoonyakasha import keys
from shoonyakasha.mathutil import look_rotation

import optics_common as oc

# The metals in the order of metals.glsl's tables, with their names, the
# colour of their curves in the plots, and a line on what the plot shows.
METALS = ("GOLD", "SILVER", "COPPER", "ALUMINIUM")
NAMES = {"GOLD": "Gold", "SILVER": "Silver", "COPPER": "Copper", "ALUMINIUM": "Aluminium"}
CURVE = {"GOLD": (0.96, 0.76, 0.26), "COPPER": (0.92, 0.46, 0.30), "SILVER": (0.88, 0.90, 0.95),
         "ALUMINIUM": (0.50, 0.68, 0.98)}
CAPTION = {
    "GOLD": "Below about 500 nm, light lifts electrons out of gold's filled d band into its conduction band and "
            "is absorbed: reflectance falls to about 0.4. Above it, the free electrons reflect nearly all of it. "
            "Relativistic effects on gold's 6s electrons bring that band edge down into the visible.",
    "COPPER": "Copper's d band edge lies lower in energy than gold's: absorption sets in below about 590 nm. "
              "More of the green is lost, and copper looks redder than gold.",
    "SILVER": "Silver's d band edge lies in the ultraviolet, near 320 nm. Across the visible range its free "
              "electrons reflect about 98% at every wavelength, so it looks white.",
    "ALUMINIUM": "Aluminium has an absorption band near 800 nm, just past the red end. Its reflectance falls "
                 "slightly towards the red, from 0.92 to 0.88, so it looks faintly bluish white.",
}
# Left to right in the scene.
ORDER = ("GOLD", "COPPER", "SILVER", "ALUMINIUM")
LAMBDA_MIN, LAMBDA_MAX = oc.LAMBDA_MIN, oc.LAMBDA_MAX

RADIUS = 0.28
SPACING = 0.72
EYE = (0.82, 0.78, 3.7)
TARGET = (0.82, 0.30, 0.0)
FOV = 40.0
LAMP = (1.6, 2.6, 2.4)
SINGLE_EXPOSURE = 0.5

parser = argparse.ArgumentParser(description="Why gold is gold", formatter_class=argparse.RawDescriptionHelpFormatter,
                                 epilog=__doc__)
parser.add_argument("--metal", choices=[m.lower() for m in METALS], default="gold",
                    help="the metal whose n and k the panel plots")
parser.add_argument("--wavelength", type=float, default=450.0, help="the cursor's wavelength in nm (380 to 780)")
parser.add_argument("--single", action="store_true",
                    help="start in light of the cursor's wavelength alone, shown in its colour")
parser.add_argument("--roughness", type=float, default=0.04, help="GGX roughness of the spheres")
parser.add_argument("--sky", type=float, default=0.4, help="intensity of the sky's light")
parser.add_argument("--exposure", type=float, default=1.0, help="multiplies the image before the tone curve")
parser.add_argument("--samples", type=int, default=0, help="stop averaging at this many paths per pixel (0: never)")
parser.add_argument("--screenshot", metavar="FILE", help="save a screenshot once --samples paths are in, then quit")
parser.add_argument("--width", type=int, default=1600)
parser.add_argument("--height", type=int, default=900)
args = parser.parse_args()
if args.screenshot and not args.samples:
    parser.error("--screenshot needs --samples")


# ── The metals' data ─────────────────────────────────────────────────────

def read_metals():
    """{name: [(n, k)] at 380, 400, ... 780 nm}, from metals.glsl."""
    with open(os.path.join(oc.SHADERS, "metals.glsl"), encoding="utf-8") as f:
        source = f.read()
    tables = {}
    for name, body in re.findall(r"const vec2 (\w+)_IOR\[21\] = vec2\[21\]\((.*?)\);", source, re.S):
        tables[name] = [(float(n), float(k)) for n, k in re.findall(r"vec2\(([-\d.]+), ([-\d.]+)\)", body)]
    return tables


TABLES = read_metals()


def ior(metal, wavelength):
    """n, k at a wavelength, interpolated linearly as the shader does."""
    x = (min(max(wavelength, LAMBDA_MIN), LAMBDA_MAX) - LAMBDA_MIN) / 20.0
    i = min(int(x), 19)
    t = x - i
    (n0, k0), (n1, k1) = TABLES[metal][i], TABLES[metal][i + 1]
    return n0 + (n1 - n0) * t, k0 + (k1 - k0) * t


def reflectance(n, k):
    """Reflectance at normal incidence from air onto n + ik."""
    return ((n - 1.0) ** 2 + k * k) / ((n + 1.0) ** 2 + k * k)


def metal_colour(metal):
    """sRGB of the metal's normal reflectance under an equal-energy light,
    with the flat spectrum white, as the renderer's colour conversion has it."""
    total, white = [0.0, 0.0, 0.0], [0.0, 0.0, 0.0]
    for step in range(401):
        wavelength = LAMBDA_MIN + step
        r = reflectance(*ior(metal, wavelength))
        rgb = oc.xyz_to_linear_rgb(*oc.cie_xyz(wavelength))
        for c in range(3):
            total[c] += r * rgb[c]
            white[c] += rgb[c]
    return tuple(oc.encode_srgb(t / w) for t, w in zip(total, white))


# ── Plots ────────────────────────────────────────────────────────────────

REFLECTANCE_SIZE = (600.0, 250.0)
NK_SIZE = (600.0, 210.0)
WAVELENGTH_TICKS = [400, 500, 600, 700]


def reflectance_axes():
    return oc.Axes(REFLECTANCE_SIZE, (LAMBDA_MIN, LAMBDA_MAX), 1.0, WAVELENGTH_TICKS, [0.25 * i for i in range(5)],
                   spectrum=True)


def nk_axes(metal):
    top = float(math.ceil(max(max(n, k) for n, k in TABLES[metal]) + 0.2))
    step = 1.0 if top <= 6.0 else 2.0
    return oc.Axes(NK_SIZE, (LAMBDA_MIN, LAMBDA_MAX), top, WAVELENGTH_TICKS,
                   [step * i for i in range(int(top / step) + 1)], spectrum=True)


def draw_plots():
    """The reflectance plot and one n, k plot per metal."""
    plots = {}
    axes = reflectance_axes()
    raster = axes.raster()
    for metal in ("ALUMINIUM", "SILVER", "COPPER", "GOLD"):
        raster.curve(axes.polyline(lambda w, m=metal: reflectance(*ior(m, w))), CURVE[metal], 2.0 * axes.scale)
    plots["reflectance"] = raster
    for metal in METALS:
        axes = nk_axes(metal)
        raster = axes.raster()
        raster.curve(axes.polyline(lambda w, m=metal: ior(m, w)[0]), CURVE[metal], 2.0 * axes.scale)
        raster.curve(axes.polyline(lambda w, m=metal: ior(m, w)[1]), CURVE[metal], 2.0 * axes.scale,
                     dash=7.0 * axes.scale)
        plots[metal] = raster
    return plots


SPHERE = oc.sphere()
PLOTS = draw_plots()

sk.shaders.compile_dir(oc.SHADERS)
engine = sk.Engine(title="Shoonyakasha - Why gold is gold", width=args.width, height=args.height,
                   hdr_environment_path=oc.ENVIRONMENT, pipeline_json_path=oc.PIPELINE)


# ── The scene ────────────────────────────────────────────────────────────

def sphere_centre(index):
    return ((index - 1.5) * SPACING, RADIUS, 0.0)


def build_scene():
    camera = engine.create_camera(pos=EYE, fov=FOV, speed=2.0, near_plane=0.02, far_plane=100.0)
    engine.scene.set_rotation(camera, look_rotation(EYE, TARGET))
    lamp = engine.create_point_light(pos=LAMP, color=(1.0, 1.0, 1.0), intensity=12.0, range=100.0)
    engine.scene.set_light_source_radius(lamp, 0.4)
    oc.place(engine, oc.BOX, (0.0, -0.05, 0.0), (10.0, 0.1, 10.0), color=(0.06, 0.06, 0.065))
    slots = []
    for index, metal in enumerate(ORDER):
        for entity in oc.place_mesh(engine, SPHERE, sphere_centre(index), (RADIUS,) * 3):
            slots.append(((entity, oc.CONDUCTOR, METALS.index(metal) + 1, args.roughness), (1.0, 0.0, 1.0, 0.0),
                          (0.0, 0.0, 0.0, 1.0)))
    oc.set_material_slots(engine, slots)
    oc.set_settings(engine, args.samples, args.sky, args.exposure)


# ── The panel ────────────────────────────────────────────────────────────

class Panel:
    WIDTH = 660

    def __init__(self):
        self.metal = args.metal.upper()
        self.wavelength = min(max(args.wavelength, LAMBDA_MIN), LAMBDA_MAX)
        self.single = args.single
        self.visible = True

    def build(self):
        ui = engine.ui
        self.texts = oc.TextCache(ui)
        self.canvas, column = oc.panel_frame(ui, self.WIDTH, "Why gold is gold")
        ui.create_text(column, "From each metal's measured refractive index n + ik. F2 hides this panel.",
                       size=15, color=oc.MUTED)

        self.reflectance = oc.Plot(ui, column, reflectance_axes(), PLOTS["reflectance"],
                                   "Reflectance at normal incidence", y_format="%.2f")
        self.reflectance_dots = {metal: self.reflectance.dot(CURVE[metal] + (1.0,)) for metal in METALS}

        self.buttons = dict(zip(ORDER, oc.button_row(ui, column, [NAMES[m] for m in ORDER])))
        for metal, button in self.buttons.items():
            swatch = ui.create_image(button, size=(16, 16), color=metal_colour(metal) + (1.0,))
            ui.set_rect(swatch, anchor_min=(0, 0.5), anchor_max=(0, 0.5), pivot=(0, 0.5), position=(9, 0),
                        size=(16, 16))

        self.nk = {}
        self.nk_dots = {}
        for metal in METALS:
            self.nk[metal] = oc.Plot(ui, column, nk_axes(metal), PLOTS[metal],
                                     "Refractive index of %s: n (solid), k (dashed)" % NAMES[metal].lower())
            self.nk_dots[metal] = (self.nk[metal].dot(CURVE[metal] + (1.0,)), self.nk[metal].dot((1, 1, 1, 1)))
        ui.create_text(column, "R = ((n - 1)² + k²) / ((n + 1)² + k²)", size=16, color=oc.TEXT)

        self.white_button, self.single_button = oc.button_row(ui, column, ["White light", "One wavelength"])
        self.wavelength_label = ui.create_text(column, "", size=18)
        ui.set_rich_text(self.wavelength_label)
        self.slider = ui.create_slider(column, min=LAMBDA_MIN, max=LAMBDA_MAX, value=self.wavelength, size=(0, 22))
        ui.set_accessible(self.slider, "Wavelength")
        self.readout = {}
        for metal in ORDER:
            row = ui.create_horizontal_layout(column, size=(0, 22))
            cells = []
            for width in (150, 100, 100, 100):
                cell = ui.create_text(row, "", size=17, color=CURVE[metal] + (1.0,))
                ui.set_rich_text(cell)
                ui.set_layout_element(cell, min=(width, 22), preferred=(width, 22))
                cells.append(cell)
            self.readout[metal] = cells
        self.caption = ui.create_text(column, "", size=17, color=oc.TEXT)
        ui.set_text_wrap(self.caption, True)
        ui.set_layout_element(self.caption, min=(0, 90), preferred=(0, 90))

        # Names under the spheres, where the starting camera sees them.
        self.labels = []
        for index, metal in enumerate(ORDER):
            x, y = oc.project(EYE, TARGET, FOV,
                              tuple(c + d for c, d in zip(sphere_centre(index), (0.0, -RADIUS, 0.0))))
            self.labels.append(oc.text_at(ui, self.canvas, NAMES[metal], (x, y + 40.0), size=22.0,
                                          color=(0.93, 0.93, 0.95, 1.0), width=200.0))

    def update(self, camera_moved):
        ui = engine.ui
        if ui.value_changed(self.slider):
            self.wavelength = ui.slider_value(self.slider)
            self.single = True
        for metal, button in self.buttons.items():
            if ui.was_clicked(button):
                self.metal = metal
        if ui.was_clicked(self.white_button):
            self.single = False
        if ui.was_clicked(self.single_button):
            self.single = True
        w = self.wavelength
        ui.set_slider_value(self.slider, w)
        ui.set_color(self.white_button, oc.SURFACE if self.single else oc.ACCENT)
        ui.set_color(self.single_button, oc.ACCENT if self.single else oc.SURFACE)

        hue = oc.wavelength_colour(w) + (1.0,)
        self.reflectance.place_cursor(w, hue)
        for metal, dot in self.reflectance_dots.items():
            self.reflectance.place_dot(dot, w, reflectance(*ior(metal, w)))
        for metal, plot in self.nk.items():
            ui.set_visible(plot.holder, metal == self.metal)
            plot.place_cursor(w, hue)
            n, k = ior(metal, w)
            plot.place_dot(self.nk_dots[metal][0], w, n)
            plot.place_dot(self.nk_dots[metal][1], w, k)
        for metal, button in self.buttons.items():
            ui.set_color(button, oc.ACCENT if metal == self.metal else oc.SURFACE)

        self.texts.set(self.wavelength_label, "Wavelength   <b>%.0f nm</b>   %.2f eV%s"
                       % (w, 1239.84 / w, "   the scene in this light alone" if self.single else ""))
        for metal, cells in self.readout.items():
            n, k = ior(metal, w)
            texts = (NAMES[metal], "n  %.2f" % n, "k  %.2f" % k, "R  %.2f" % reflectance(n, k))
            for cell, text in zip(cells, texts):
                self.texts.set(cell, "<b>%s</b>" % text if metal == self.metal else text)
        self.texts.set(self.caption, CAPTION[self.metal])
        for label in self.labels:
            ui.set_visible(label, not camera_moved)

    def toggle_single(self):
        self.single = not self.single

    def key(self):
        """What the image depends on: the wavelength while it is shown alone."""
        return (self.single, self.wavelength) if self.single else (False,)

    def step(self, nanometres):
        self.wavelength = min(max(self.wavelength + nanometres, LAMBDA_MIN), LAMBDA_MAX)
        self.single = True

    def next_metal(self):
        self.metal = ORDER[(ORDER.index(self.metal) + 1) % len(ORDER)]

    def toggle(self):
        self.visible = not self.visible
        engine.ui.set_visible(self.canvas, self.visible)


# ── Frames ───────────────────────────────────────────────────────────────

class Controls:
    """Keys, the panel, and the average, started over whenever the camera
    moves or the single wavelength view's wavelength changes."""

    def __init__(self):
        self.pressed = keys.KeyEdges(engine.input).pressed
        self.panel = Panel()
        self.accumulation = oc.Accumulation(engine, args.samples, args.screenshot, "gold")
        self.start_state = None

    def start(self):
        build_scene()
        self.panel.build()

    def update(self, dt):
        if self.pressed(keys.LEFT):
            self.panel.step(-5.0)
        if self.pressed(keys.RIGHT):
            self.panel.step(5.0)
        if self.pressed(keys.M):
            self.panel.next_metal()
        if self.pressed(keys.V):
            self.panel.toggle_single()
        if self.pressed(keys.F2):
            self.panel.toggle()
        if self.pressed(keys.P):
            self.accumulation.capture()

        camera = engine.scene.get_main_camera()
        state = (engine.scene.get_position(camera), engine.scene.get_rotation(camera))
        if self.start_state is None:
            self.start_state = state
        self.panel.update(state != self.start_state)
        engine.set_custom_float("wo.wavelength", self.panel.wavelength if self.panel.single else 0.0)
        # In light of one wavelength the exposure halves, which keeps the
        # sky's reflections below the tone curve's shoulder.
        engine.set_custom_float("wo.exposure", args.exposure * (SINGLE_EXPOSURE if self.panel.single else 1.0))
        self.accumulation.update(state + self.panel.key(), restart=self.pressed(keys.R))


controls = Controls()


def on_init():
    if not engine.ray_query_supported():
        print("[gold] this device has no ray queries, which the path tracer needs.")
        os._exit(1)
    controls.start()


engine.set_on_init(on_init)
engine.set_on_update(controls.update)

if __name__ == "__main__":
    engine.run()
