"""
The ship's instruments on canvases:
  - head-up readouts on a screen canvas around the middle of the view:
    speed on the left with the throttle, assist and time warp and a
    throttle bar under it, height above the ground on the right, and
    heading at the top (the HUD's lines and markers are drawn by the
    HudSymbols pass);
  - two displays in the cockpit, world canvases on its dashboard: FLIGHT
    and ORBIT.
`Instruments.update(readings)` writes a dict of readings into them.
"""

import shoonyakasha as sk

GREEN = (0.45, 1.0, 0.55, 1.0)
DIM = (0.45, 1.0, 0.55, 0.15)
SCREEN_TEXT = (0.55, 1.0, 0.75, 1.0)
SCREEN_TITLE = (1.0, 0.8, 0.35, 1.0)
DISPLAY_PIXELS = (544, 352)
DISPLAY_EMISSION = 2.5
THROTTLE_WIDTH = 220


class Instruments:
    def __init__(self, engine, screens, reference_size=(1600, 900)):
        """`screens`: (centre, right, up, (width, height)) of each display
        in the camera's space, as cockpit.SCREENS."""
        self.engine = engine
        ui = engine.ui
        self.canvas = ui.create_canvas(reference_size=reference_size)
        self.speed = self._readout((-330, 0), 30)
        self.height = self._readout((330, 0), 30)
        self.heading = self._readout((0, -300), 26)
        self.status = self._readout((-330, 44), 18)
        bar = ui.create_panel(self.canvas, size=(THROTTLE_WIDTH, 8), color=DIM)
        ui.set_rect(bar, position=(-330, 72), size=(THROTTLE_WIDTH, 8))
        self.throttle = ui.create_panel(bar, size=(0, 8), color=GREEN)

        self.screens = screens
        self.displays = []
        for _, _, _, size in screens:
            display = ui.create_world_canvas(pixel_size=DISPLAY_PIXELS, world_size=size, emission=DISPLAY_EMISSION)
            ui.set_canvas_clear_color(display, (0.01, 0.02, 0.025, 1.0))
            engine.scene.set_render_layer_mask(display, 2)
            title = ui.create_text(display, "", size=40, color=SCREEN_TITLE)
            ui.set_rect(title, anchor_min=(0, 0), anchor_max=(1, 0), pivot=(0, 0), position=(24, 14), size=(-48, 48))
            body = ui.create_text(display, "", size=30, color=SCREEN_TEXT)
            ui.set_rect(body, anchor_min=(0, 0), anchor_max=(1, 1), pivot=(0, 0), position=(24, 72), size=(-48, -90))
            self.displays.append((display, title, body))
        self.visible = True
        self.show(False)

    def _readout(self, position, size):
        ui = self.engine.ui
        text = ui.create_text(self.canvas, "", size=size, color=GREEN)
        ui.set_rect(text, position=position, size=(320, size + 12))
        ui.set_text_align(text, sk.TEXT_ALIGN_CENTER, sk.TEXT_ALIGN_MIDDLE)
        return text

    def show(self, on):
        self.visible = on
        self.engine.ui.set_visible(self.canvas, on)
        for display, _, _ in self.displays:
            self.engine.scene.set_visible(display, on)

    def place(self, to_world, euler_from_basis):
        """Put the displays where the cockpit has them: `to_world` turns a
        direction in the camera's space into the world's (the camera sits at
        the origin)."""
        for (display, _, _), (centre, right, up, _) in zip(self.displays, self.screens):
            r, u = to_world(right), to_world(up)
            normal = (r[1] * u[2] - r[2] * u[1], r[2] * u[0] - r[0] * u[2], r[0] * u[1] - r[1] * u[0])
            self.engine.scene.set_position(display, to_world(centre))
            self.engine.scene.set_rotation(display, euler_from_basis(r, u, tuple(-c for c in normal)))

    def update(self, readings):
        ui = self.engine.ui
        r = readings
        ui.set_text(self.speed, "%5.0f m/s" % r["speed"])
        ui.set_text(self.height, "%s" % r["height_text"])
        ui.set_text(self.heading, "%03d" % (int(round(r["heading"])) % 360))
        ui.set_text(self.status, "THR %3.0f%%  ASSIST %s  WARP x%d%s"
                    % (r["throttle"] * 100.0, "ON" if r["assist"] else "OFF", r["warp"],
                       "   " + r["contact"]))
        # The fill spans the bar from its left to the throttle's fraction of its width.
        ui.set_rect(self.throttle, anchor_min=(0, 0), anchor_max=(max(r["throttle"], 0.0), 1), pivot=(0, 0),
                    position=(0, 0), size=(0, 0))
        ui.set_visible(self.throttle, r["throttle"] > 0.0)
        flight, orbit = self.displays
        ui.set_text(flight[1], "FLIGHT")
        ui.set_text(flight[2], "speed       %7.0f m/s\nclimb       %+7.1f m/s\nabove ground %s\n%-12s%s\n"
                               "throttle    %7.0f %%\nassist      %7s"
                    % (r["speed"], r["climb"], r["agl_text"], r["datum"], r["asl_text"], r["throttle"] * 100.0,
                       "on" if r["assist"] else "off"))
        ui.set_text(orbit[1], "ORBIT  " + r["body"].upper())
        ui.set_text(orbit[2], "periapsis   %s\napoapsis    %s\nspeed       %7.0f m/s\ncircular    %7.0f m/s\n"
                              "air         %7.4f kg/m3\npressure    %7.2f kPa\nglow        %7.0f %%"
                    % (r["periapsis_text"], r["apoapsis_text"], r["speed"], r["circular"], r["density"],
                       r["dynamic_pressure"] / 1000.0, r["glow"] * 100.0))
