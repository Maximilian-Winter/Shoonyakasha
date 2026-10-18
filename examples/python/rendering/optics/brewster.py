"""
Shoonyakasha - Brewster's angle

A glass slab over a mosaic of coloured tiles, path traced with the wave
optics example's spectral, polarised pipeline. A lamp stands where the glass
reflects it into the camera, and the sky reflects across the rest. A
polariser in front of the camera turns; at Brewster's angle, tan(theta) = n,
the glass reflects no p-polarised light, and a polariser that passes p alone
takes the reflection away. A panel plots the reflectance of s and p light
against the angle of incidence, with the camera's angle and Brewster's angle
marked.

Keys:
    WASD/Q/E + right mouse   fly the camera; moving starts the average over
    Left / Right             the angle of incidence, by 1 degree
    B                        go to Brewster's angle
    T                        the polariser on or off
    Up / Down                turn the polariser by 15 degrees
    R                        start the average over
    F2                       hide or show the panel
    P                        save a screenshot to brewster_captures/

Usage:
    python brewster.py
    python brewster.py --angle brewster --polariser 90   # the reflection gone
    python brewster.py --material water
    python brewster.py --samples 512 --screenshot out.png   # converge, save, quit

Requirements:
    - the shoonyakasha package installed (pip install .)
    - a GPU with ray queries
"""

import argparse
import colorsys
import math
import os
import tempfile

import shoonyakasha as sk
from shoonyakasha import keys
from shoonyakasha.mathutil import look_rotation

import optics_common as oc

MATERIALS = (("water", "Water", 1.33), ("glass", "Glass", 1.50), ("diamond", "Diamond", 2.42))
S_COLOUR = (0.96, 0.62, 0.30)
P_COLOUR = (0.45, 0.70, 1.00)
UNPOLARISED = (0.85, 0.87, 0.90)
CAPTION = ("The glass reflects light through its electrons, which the light inside it sets oscillating. "
           "For p-polarised light they oscillate in the plane of incidence, across the refracted ray. At "
           "Brewster's angle the reflected and refracted rays are at right angles, so the reflected ray would "
           "leave along that oscillation, and an oscillating charge radiates nothing along its own axis. Rp is "
           "zero there, and the reflection is wholly s-polarised: a polariser that passes p alone removes it.")

SLAB = (2.4, 0.04, 1.6)              # the glass's width, thickness and depth in metres
SLAB_Y = 0.05                        # the height of the glass's centre
CENTRE = (0.0, SLAB_Y + 0.5 * SLAB[1], 0.0)
TILES = (11, 7)
TILE = 0.2
DISTANCE = 4.1                      # from the centre of the glass to the camera
LAMP_DISTANCE = 3.0
FOV = 32.0
# The camera looks this far to the right of the glass's centre, per metre
# of distance, so the centre sits left of the panel.
LOOK_RIGHT = 0.14

parser = argparse.ArgumentParser(description="Brewster's angle", formatter_class=argparse.RawDescriptionHelpFormatter,
                                 epilog=__doc__)
parser.add_argument("--angle", default="70",
                    help="angle of incidence at the centre of the glass in degrees (5 to 85), or 'brewster'")
parser.add_argument("--material", choices=[m[0] for m in MATERIALS], default="glass",
                    help="the slab's index of refraction: water 1.33, glass 1.50, diamond 2.42")
parser.add_argument("--polariser", type=float, default=None,
                    help="angle in degrees of a polariser in front of the camera, from the image's horizontal")
parser.add_argument("--roughness", type=float, default=0.01, help="GGX roughness of the glass")
parser.add_argument("--sky", type=float, default=0.8, help="intensity of the sky's light")
parser.add_argument("--exposure", type=float, default=1.0, help="multiplies the image before the tone curve")
parser.add_argument("--samples", type=int, default=0, help="stop averaging at this many paths per pixel (0: never)")
parser.add_argument("--screenshot", metavar="FILE", help="save a screenshot once --samples paths are in, then quit")
parser.add_argument("--width", type=int, default=1600)
parser.add_argument("--height", type=int, default=900)
args = parser.parse_args()
if args.screenshot and not args.samples:
    parser.error("--screenshot needs --samples")


# ── Fresnel ──────────────────────────────────────────────────────────────

def fresnel(n, degrees):
    """Reflectances (Rs, Rp) from air onto a dielectric of index n."""
    ci = math.cos(math.radians(degrees))
    st = math.sin(math.radians(degrees)) / n
    ct = math.sqrt(max(0.0, 1.0 - st * st))
    rs = (ci - n * ct) / (ci + n * ct)
    rp = (n * ci - ct) / (n * ci + ct)
    return rs * rs, rp * rp


def brewster(n):
    return math.degrees(math.atan(n))


# ── Plots ────────────────────────────────────────────────────────────────

PLOT_SIZE = (520.0, 290.0)


def axes():
    return oc.Axes(PLOT_SIZE, (0.0, 90.0), 1.0, [0, 15, 30, 45, 60, 75, 90], [0.25 * i for i in range(5)])


def draw_plots(directory):
    """One reflectance plot per material; returns their paths by name."""
    paths = {}
    for name, _, n in MATERIALS:
        a = axes()
        raster = a.raster()
        raster.curve(a.vertical(brewster(n)), (1.0, 1.0, 1.0), 1.5 * a.scale, dash=5.0 * a.scale, alpha=0.6)
        raster.curve(a.polyline(lambda t: 0.5 * sum(fresnel(n, t)), step=0.25), UNPOLARISED, 2.0 * a.scale,
                     dash=6.0 * a.scale)
        raster.curve(a.polyline(lambda t: fresnel(n, t)[1], step=0.25), P_COLOUR, 2.0 * a.scale)
        raster.curve(a.polyline(lambda t: fresnel(n, t)[0], step=0.25), S_COLOUR, 2.0 * a.scale)
        paths[name] = os.path.join(directory, "reflectance_%s.png" % name)
        raster.save(paths[name])
    return paths


FILES = tempfile.mkdtemp(prefix="brewster_")
PLOTS = draw_plots(FILES)

sk.shaders.compile_dir(oc.SHADERS)
engine = sk.Engine(title="Shoonyakasha - Brewster's angle", width=args.width, height=args.height,
                   hdr_environment_path=oc.ENVIRONMENT, pipeline_json_path=oc.PIPELINE)


# ── The scene ────────────────────────────────────────────────────────────

def tile_colour(i, j):
    """A mosaic of hues across the tiles, lighter and darker in a checker."""
    hue = (i / TILES[0] * 0.85 + j / TILES[1] * 0.15) % 1.0
    value = 0.75 if (i + j) % 2 == 0 else 0.5
    return colorsys.hsv_to_rgb(hue, 0.75, value)


class Scene:
    """The floor, the tiles, the glass, the camera and the lamp."""

    def build(self, n):
        self.camera = engine.create_camera(pos=(0.0, 2.0, 4.0), fov=FOV, speed=2.0, near_plane=0.02,
                                           far_plane=100.0)
        self.lamp = engine.create_point_light(pos=(0.0, 2.0, -2.0), color=(1.0, 1.0, 1.0), intensity=6.0,
                                              range=100.0)
        engine.scene.set_light_source_radius(self.lamp, 0.3)
        oc.place(engine, oc.BOX, (0.0, -0.05, 0.0), (10.0, 0.1, 10.0), color=(0.05, 0.05, 0.055))
        for i in range(TILES[0]):
            for j in range(TILES[1]):
                x = (i - 0.5 * (TILES[0] - 1)) * TILE
                z = (j - 0.5 * (TILES[1] - 1)) * TILE
                oc.place(engine, oc.BOX, (x, 0.005, z), (TILE * 0.96, 0.01, TILE * 0.96), color=tile_colour(i, j))
        self.glass = oc.place(engine, oc.BOX, (0.0, SLAB_Y, 0.0), SLAB, color=(1.0, 1.0, 1.0),
                              roughness=args.roughness)
        for entity in self.glass:
            engine.scene.set_material_float(entity, "transmissionFactor", 1.0)
        self.set_index(n)

    def set_index(self, n):
        for entity in self.glass:
            engine.scene.set_material_float(entity, "ior", n)

    def set_angle(self, degrees):
        """The camera at `degrees` from the glass's normal at its centre,
        looking a little to the right of it, and the lamp where the glass
        reflects it into the camera."""
        t = math.radians(degrees)
        eye = (CENTRE[0], CENTRE[1] + DISTANCE * math.cos(t), CENTRE[2] + DISTANCE * math.sin(t))
        engine.scene.set_position(self.camera, eye)
        engine.scene.set_rotation(self.camera, look_rotation(eye, (CENTRE[0] + LOOK_RIGHT * DISTANCE,) + CENTRE[1:]))
        lamp = (CENTRE[0], CENTRE[1] + LAMP_DISTANCE * math.cos(t), CENTRE[2] - LAMP_DISTANCE * math.sin(t))
        engine.scene.set_position(self.lamp, lamp)

    def camera_angle(self):
        """The angle of incidence at the centre of the glass, seen from the camera."""
        eye = engine.scene.get_position(self.camera)
        v = [e - c for e, c in zip(eye, CENTRE)]
        length = math.sqrt(sum(c * c for c in v)) or 1.0
        return math.degrees(math.acos(max(-1.0, min(1.0, v[1] / length))))


# ── The panel ────────────────────────────────────────────────────────────

class Panel:
    WIDTH = 580

    def __init__(self, scene):
        self.scene = scene
        self.material = [m[0] for m in MATERIALS].index(args.material)
        n = MATERIALS[self.material][2]
        self.angle = brewster(n) if args.angle.lower() == "brewster" else min(max(float(args.angle), 5.0), 85.0)
        self.polariser = args.polariser
        self.polariser_angle = 90.0 if args.polariser is None else args.polariser
        self.visible = True

    @property
    def n(self):
        return MATERIALS[self.material][2]

    def build(self):
        ui = engine.ui
        self.texts = oc.TextCache(ui)
        self.canvas, column = oc.panel_frame(ui, self.WIDTH, "Brewster's angle")
        ui.create_text(column, "Fresnel reflection from air onto the slab. F2 hides this panel.", size=15,
                       color=oc.MUTED)

        self.plot = oc.Plot(ui, column, axes(), PLOTS[MATERIALS[self.material][0]],
                            "Reflectance of <color=%s>s</color> and <color=%s>p</color> light, and of "
                            "unpolarised light (dashed)" % (oc.hex_colour(S_COLOUR), oc.hex_colour(P_COLOUR)),
                            x_format="%g°", y_format="%.2f")
        self.dots = (self.plot.dot(S_COLOUR + (1.0,)), self.plot.dot(P_COLOUR + (1.0,)),
                     self.plot.dot(UNPOLARISED + (1.0,)))
        self.brewster_label = oc.text_at(ui, self.plot.holder, "", (0.0, 0.0), size=14.0, color=oc.TEXT,
                                         width=120.0, align=sk.TEXT_ALIGN_RIGHT)

        self.material_buttons = oc.button_row(ui, column, ["%s  %.2f" % (label, n) for _, label, n in MATERIALS])

        self.angle_label = ui.create_text(column, "", size=18)
        ui.set_rich_text(self.angle_label)
        self.angle_slider = ui.create_slider(column, min=5.0, max=85.0, value=self.angle, size=(0, 22))
        ui.set_accessible(self.angle_slider, "Angle of incidence")
        self.brewster_button, = oc.button_row(ui, column, ["Go to Brewster's angle"], height=32)

        row = ui.create_horizontal_layout(column, size=(0, 32), spacing=10)
        ui.set_layout_child_sizing(row, expand_width=True)
        self.polariser_label = ui.create_text(row, "", size=18)
        ui.set_text_align(self.polariser_label, sk.TEXT_ALIGN_LEFT, sk.TEXT_ALIGN_MIDDLE)
        self.polariser_button = ui.create_button(row, "Polariser", size=(150, 32))
        ui.set_font_size(self.polariser_button, 15)
        ui.set_layout_element(self.polariser_button, min=(150, 32), preferred=(150, 32), flexible=(0, 0))
        self.polariser_slider = ui.create_slider(column, min=-90.0, max=90.0, value=self.polariser_angle,
                                                 size=(0, 22))
        ui.set_accessible(self.polariser_slider, "Polariser angle")

        self.readout = []
        for _ in range(3):
            line = ui.create_text(column, "", size=17, color=oc.TEXT)
            ui.set_rich_text(line)
            self.readout.append(line)
        self.caption = ui.create_text(column, CAPTION, size=16, color=oc.MUTED)
        ui.set_text_wrap(self.caption, True)
        ui.set_layout_element(self.caption, min=(0, 110), preferred=(0, 110))

    def update(self):
        ui = engine.ui
        for index, button in enumerate(self.material_buttons):
            if ui.was_clicked(button):
                self.material = index
                ui.set_texture(self.plot.image, PLOTS[MATERIALS[index][0]])
                self.scene.set_index(self.n)
        if ui.value_changed(self.angle_slider):
            self.set_angle(ui.slider_value(self.angle_slider))
        if ui.was_clicked(self.brewster_button):
            self.set_angle(brewster(self.n))
        if ui.was_clicked(self.polariser_button):
            self.toggle_polariser()
        if ui.value_changed(self.polariser_slider):
            self.polariser_angle = ui.slider_value(self.polariser_slider)
            if self.polariser is not None:
                self.polariser = self.polariser_angle
        ui.set_slider_value(self.angle_slider, self.angle)
        ui.set_slider_value(self.polariser_slider, self.polariser_angle)
        for index, button in enumerate(self.material_buttons):
            ui.set_color(button, oc.ACCENT if index == self.material else oc.SURFACE)
        ui.set_color(self.polariser_button, oc.ACCENT if self.polariser is not None else oc.SURFACE)

        n = self.n
        b = brewster(n)
        theta = self.scene.camera_angle()
        rs, rp = fresnel(n, theta)
        self.plot.place_cursor(theta)
        for dot, value in zip(self.dots, (rs, rp, 0.5 * (rs + rp))):
            self.plot.place_dot(dot, theta, value)
        ui.set_rect(self.brewster_label, anchor_min=(0, 0), anchor_max=(0, 0), pivot=(1.0, 0.0),
                    position=(self.plot.axes.x(b) - 5.0, self.plot.axes.TOP + 4.0), size=(120.0, 20.0))
        self.texts.set(self.brewster_label, "θB %.1f°" % b)

        self.texts.set(self.angle_label, "Angle of incidence   <b>%.1f°</b>     Brewster's angle   %.1f°"
                       % (theta, b))
        self.texts.set(self.polariser_label, "Polariser   off" if self.polariser is None
                       else "Polariser   %.0f° from horizontal" % self.polariser)
        unpolarised = 0.5 * (rs + rp)
        self.texts.set(self.readout[0], "<color=%s>Rs  %.3f</color>     <color=%s>Rp  %.3f</color>     "
                       "unpolarised  %.3f" % (oc.hex_colour(S_COLOUR), rs, oc.hex_colour(P_COLOUR), rp, unpolarised))
        if self.polariser is None:
            self.texts.set(self.readout[1], "Reflected light reaching the camera:  <b>%.3f</b>" % unpolarised)
            self.texts.set(self.readout[2], "Without a polariser, half of it is s and half p: (Rs + Rp) / 2.")
        else:
            phi = math.radians(self.polariser)
            through = rs * math.cos(phi) ** 2 + rp * math.sin(phi) ** 2
            self.texts.set(self.readout[1], "Reflected light through the polariser:  <b>%.3f</b>   (%.3f without)"
                           % (through, unpolarised))
            self.texts.set(self.readout[2], "Rs cos²φ + Rp sin²φ, for s horizontal; the "
                                            "exposure is doubled for the polariser's half.")

    def set_angle(self, degrees):
        self.angle = min(max(degrees, 5.0), 85.0)
        self.scene.set_angle(self.angle)

    def toggle_polariser(self):
        self.polariser = None if self.polariser is not None else self.polariser_angle

    def turn_polariser(self, degrees):
        self.polariser_angle = (self.polariser_angle + degrees + 90.0) % 180.0 - 90.0
        if self.polariser is not None:
            self.polariser = self.polariser_angle

    def toggle(self):
        self.visible = not self.visible
        engine.ui.set_visible(self.canvas, self.visible)


# ── Frames ───────────────────────────────────────────────────────────────

class Controls:
    """Keys, the panel, the pipeline's settings, and the average, started
    over whenever the camera, the slab's index or the polariser changes."""

    def __init__(self):
        self.pressed = keys.KeyEdges(engine.input).pressed
        self.scene = Scene()
        self.panel = Panel(self.scene)
        self.accumulation = oc.Accumulation(engine, args.samples, args.screenshot, "brewster")

    def start(self):
        self.scene.build(self.panel.n)
        self.scene.set_angle(self.panel.angle)
        self.panel.build()

    def update(self, dt):
        panel = self.panel
        if self.pressed(keys.LEFT):
            panel.set_angle(panel.angle - 1.0)
        if self.pressed(keys.RIGHT):
            panel.set_angle(panel.angle + 1.0)
        if self.pressed(keys.B):
            panel.set_angle(brewster(panel.n))
        if self.pressed(keys.T):
            panel.toggle_polariser()
        if self.pressed(keys.UP):
            panel.turn_polariser(15.0)
        if self.pressed(keys.DOWN):
            panel.turn_polariser(-15.0)
        if self.pressed(keys.F2):
            panel.toggle()
        if self.pressed(keys.P):
            self.accumulation.capture()
        panel.update()

        # An ideal polariser passes half of unpolarised light; the exposure
        # doubles with it so that light through it keeps its brightness.
        exposure = args.exposure * (2.0 if panel.polariser is not None else 1.0)
        oc.set_settings(engine, args.samples, args.sky, exposure, polariser=panel.polariser)
        camera = engine.scene.get_main_camera()
        state = (engine.scene.get_position(camera), engine.scene.get_rotation(camera), panel.material,
                 panel.polariser)
        self.accumulation.update(state, restart=self.pressed(keys.R))


controls = Controls()


def on_init():
    if not engine.ray_query_supported():
        print("[brewster] this device has no ray queries, which the path tracer needs.")
        os._exit(1)
    controls.start()


engine.set_on_init(on_init)
engine.set_on_update(controls.update)

if __name__ == "__main__":
    engine.run()
