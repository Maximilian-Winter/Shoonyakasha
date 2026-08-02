"""
Pong — a complete small game on the Shoonyakasha Python bindings

क्रीडा — play, the lightest of the six activities.

Everything here is a screen-space sprite: no 3D geometry, no lighting, no IBL.
That makes it the smallest interesting thing the engine can be asked to do, and
a decent read of what the sprite/UI half of the API actually feels like to use.

Controls
    W / S, or Up / Down   move your paddle (left, orange)
    Space                 serve
    R                     restart the match
    P                     screenshot
    V                     start/stop recording (needs ffmpeg)

Requirements
    - the extension built for the Python you are running (see BUILDING.md)
    - the art pack in assets/, see pong-game-assets-link.txt

Run it from this directory:

    python pong.py
"""

import math
import os
import random
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "python"))

import shoonyakasha as sk
from shoonyakasha import keys


# ═══════════════════════════════════════════════════════════════
# The game is laid out in a fixed virtual canvas
# ═══════════════════════════════════════════════════════════════
#
# Every position and size below is in these units, matching the art pack's own
# preview image. A single mapping at the end turns them into window pixels, so
# resizing the window letterboxes the game instead of distorting it — and the
# game logic never has to know the window size.

CANVAS_W, CANVAS_H = 800.0, 500.0

BAR_H = 47.0                       # the magenta score bar along the top
FIELD_TOP = BAR_H
FIELD_BOTTOM = CANVAS_H
FIELD_H = FIELD_BOTTOM - FIELD_TOP

PADDLE_W, PADDLE_H = 17.0, 120.0
PADDLE_INSET = 30.0                # from the side wall to the paddle's centre
PADDLE_SPEED = 430.0               # units per second

BALL_SIZE = 30.0
BALL_SPEED_START = 400.0
BALL_SPEED_GAIN = 46.0             # added on every paddle hit
BALL_SPEED_MAX = 1050.0
MAX_BOUNCE_ANGLE = math.radians(52.0)

AI_SPEED = 330.0                   # deliberately below the ball's top speed
AI_DEAD_ZONE = 16.0                # ignores small errors, so it can be beaten
AI_REACTION = 0.11                 # seconds of lag before it tracks a new ball

SERVE_DELAY = 0.9                  # pause after a point, before the next serve
MATCH_POINTS = 7
MATCH_SECONDS = 180.0

# Draw order: lower draws first. Explicit numbers beat implicit ordering, and
# text defaults to 0, which would put the score behind the bar it sits on.
Z_BOARD = 0
Z_TRAIL = 10
Z_PLAY = 20
Z_BAR = 30
Z_TEXT = 40

ART = "assets/arts/"
FONT = "assets/fonts/Roboto/static/Roboto-Bold.ttf"

# The art pack's Player.png is the orange paddle and Computer.png the blue one,
# so the win banner takes its colour from the paddle, not from the side.
PLAYER_COLOR = (0.85, 0.48, 0.32, 1.0)
AI_COLOR = (0.36, 0.55, 0.95, 1.0)
WHITE = (1.0, 1.0, 1.0, 1.0)
DARK = (0.16, 0.04, 0.13, 1.0)     # reads as near-black on the magenta bar

# Board.png carries a one-pixel lighter border. Stretched to the full field it
# shows up as a bright hairline down the right edge, so sample just inside it.
BOARD_UV = (0.0015, 0.0025, 0.9985, 0.9975)

# BallMotion.png streaks from its head, up and to the right, down to a tail at
# the lower left. Both numbers are in the art's own pixel space (y downward).
TRAIL_TAIL_ANGLE = math.radians(135.0)
TRAIL_HEAD_OFFSET = (0.21, -0.21)  # head, as a fraction of the sprite's size


# The C++ examples get their .spv from CMake; a Python example has no build
# step, so compile here rather than shipping binaries that can drift.
sk.shaders.compile_dir("shaders")

engine = sk.Engine(
    title="Shoonyakasha Pong",
    width=800, height=500,
    pipeline_json_path="pong_pipeline.json",
    log_file="pong.log",
    log_level=2,
)


class Pong:
    def __init__(self):
        self.scene = None
        self.input = None

        # Window size, kept current by the resize callback.
        self.window = (800.0, 500.0)

        self.score = [0, 0]
        self.time_left = MATCH_SECONDS
        self.over = False

        middle = FIELD_TOP + FIELD_H * 0.5
        self.paddle_y = [middle, middle]
        self.ai_target = middle
        self.ai_timer = 0.0
        self._held = {}

        self.ball = [CANVAS_W * 0.5, CANVAS_H * 0.5]
        self.ball_vel = [0.0, 0.0]
        self.serve_timer = SERVE_DELAY
        self.serve_to = random.choice((-1.0, 1.0))
        self.waiting_to_serve = True
        self.needs_input = True

        self.entities = {}

    # ── Setup ──────────────────────────────────────────────────

    def build(self):
        self.scene = engine.scene
        self.input = engine.input

        # The sprite shader's scene UBO reads the camera's view-projection even
        # for screen-space draws, so one has to exist. It is otherwise unused.
        engine.create_camera(pos=(0.0, 0.0, 5.0), fov=60.0)

        panel = engine.create_ui_panel
        e = self.entities

        e["board"] = panel(sk.UI_ANCHOR_TOP_LEFT, (0, 0), (1, 1),
                           ART + "Board.png", WHITE)
        self.scene.set_sprite_uv_rect(e["board"], BOARD_UV)

        # ScoreBar.png is the left half, its right edge cut on a diagonal.
        # Mirroring it with a flipped UV rect gives the right half, and the two
        # diagonals leave the notch the art was designed around.
        e["bar_l"] = panel(sk.UI_ANCHOR_TOP_LEFT, (0, 0), (1, 1),
                           ART + "ScoreBar.png", WHITE)
        e["bar_r"] = panel(sk.UI_ANCHOR_TOP_LEFT, (0, 0), (1, 1),
                           ART + "ScoreBar.png", WHITE)
        self.scene.set_sprite_uv_rect(e["bar_r"], (1.0, 0.0, 0.0, 1.0))

        e["trail"] = panel(sk.UI_ANCHOR_TOP_LEFT, (0, 0), (1, 1),
                           ART + "BallMotion.png", WHITE)
        e["ball"] = panel(sk.UI_ANCHOR_TOP_LEFT, (0, 0), (1, 1),
                          ART + "Ball.png", WHITE)
        e["player"] = panel(sk.UI_ANCHOR_TOP_LEFT, (0, 0), (1, 1),
                            ART + "Player.png", WHITE)
        e["ai"] = panel(sk.UI_ANCHOR_TOP_LEFT, (0, 0), (1, 1),
                        ART + "Computer.png", WHITE)

        for name, z in (("board", Z_BOARD), ("bar_l", Z_BAR), ("bar_r", Z_BAR),
                        ("trail", Z_TRAIL), ("ball", Z_PLAY),
                        ("player", Z_PLAY), ("ai", Z_PLAY)):
            self.scene.set_sort_key(e[name], z)

        text = engine.create_text
        e["score_l"] = text("0", sk.UI_ANCHOR_TOP_LEFT, (0, 0), FONT, 32.0, DARK)
        e["score_r"] = text("0", sk.UI_ANCHOR_TOP_LEFT, (0, 0), FONT, 32.0, DARK)
        e["clock_label"] = text("TIME", sk.UI_ANCHOR_TOP_LEFT, (0, 0), FONT, 11.0,
                                (0.58, 0.58, 0.66, 1.0))
        e["clock"] = text("3:00", sk.UI_ANCHOR_TOP_LEFT, (0, 0), FONT, 17.0, WHITE)
        e["banner"] = text("", sk.UI_ANCHOR_TOP_LEFT, (0, 0), FONT, 34.0, WHITE)
        e["hint"] = text("", sk.UI_ANCHOR_TOP_LEFT, (0, 0), FONT, 17.0,
                         (0.86, 0.86, 0.92, 1.0))

        for name in ("score_l", "score_r", "clock_label", "clock", "banner", "hint"):
            self.scene.set_text_sort_key(e[name], Z_TEXT)
            self.scene.set_text_align(e[name], sk.TEXT_ALIGN_CENTER)

        self.reset_ball(random.choice((-1.0, 1.0)), needs_input=True)
        self.refresh_labels()
        self.layout()

    # ── Virtual canvas → window pixels ─────────────────────────

    def _mapping(self):
        """Scale and origin that fit the canvas inside the window, centred."""
        width, height = self.window
        scale = min(width / CANVAS_W, height / CANVAS_H)
        return (scale,
                (width - CANVAS_W * scale) * 0.5,
                (height - CANVAS_H * scale) * 0.5)

    def place(self, name, cx, cy, w, h):
        """Put an element at a canvas-space centre with a canvas-space size."""
        scale, ox, oy = self._mapping()
        entity = self.entities[name]
        self.scene.set_ui_anchor(entity, sk.UI_ANCHOR_TOP_LEFT,
                                 (ox + cx * scale, oy + cy * scale))
        self.scene.set_scale(entity, (w * scale, h * scale, 1.0))

    def place_text(self, name, cx, baseline_y, size):
        """Anchor a label. Unlike sprites, cy is the text baseline, not the
        centre — glyph bearings are measured from it."""
        scale, ox, oy = self._mapping()
        entity = self.entities[name]
        self.scene.set_ui_anchor(entity, sk.UI_ANCHOR_TOP_LEFT,
                                 (ox + cx * scale, oy + baseline_y * scale))
        self.scene.set_text_font_size(entity, size * scale)

    def layout(self):
        """Position everything that does not move. Re-run on resize."""
        self.place("board", CANVAS_W * 0.5, FIELD_TOP + FIELD_H * 0.5,
                   CANVAS_W, FIELD_H)
        self.place("bar_l", CANVAS_W * 0.25, BAR_H * 0.5, CANVAS_W * 0.5, BAR_H)
        self.place("bar_r", CANVAS_W * 0.75, BAR_H * 0.5, CANVAS_W * 0.5, BAR_H)

        self.place_text("score_l", 250.0, 35.0, 32.0)
        self.place_text("score_r", 550.0, 35.0, 32.0)
        self.place_text("clock_label", CANVAS_W * 0.5, 16.0, 11.0)
        self.place_text("clock", CANVAS_W * 0.5, 35.0, 17.0)
        self.place_text("banner", CANVAS_W * 0.5, CANVAS_H * 0.38, 34.0)
        self.place_text("hint", CANVAS_W * 0.5, CANVAS_H * 0.46, 17.0)

        self.follow()

    def follow(self):
        """Position the things that move. Every frame."""
        self.place("player", PADDLE_INSET, self.paddle_y[0], PADDLE_W, PADDLE_H)
        self.place("ai", CANVAS_W - PADDLE_INSET, self.paddle_y[1],
                   PADDLE_W, PADDLE_H)
        self.place("ball", self.ball[0], self.ball[1], BALL_SIZE, BALL_SIZE)

        # The trail sits behind the ball, turned to face along its travel, and
        # fades out when the ball is slow or waiting to be served.
        speed = math.hypot(*self.ball_vel)
        trail = self.entities["trail"]
        if speed < 1.0:
            self.scene.set_sprite_color(trail, (1.0, 1.0, 1.0, 0.0))
            return

        nx, ny = self.ball_vel[0] / speed, self.ball_vel[1] / speed
        size = BALL_SIZE * 1.55

        # Turn the art's own tail direction to point opposite the travel...
        angle = math.atan2(-ny, -nx) - TRAIL_TAIL_ANGLE
        self.scene.set_rotation(trail, (0.0, 0.0, angle))

        # ...then shift the sprite so its head, not its centre, is on the ball.
        hx, hy = TRAIL_HEAD_OFFSET[0] * size, TRAIL_HEAD_OFFSET[1] * size
        cos_a, sin_a = math.cos(angle), math.sin(angle)
        self.place("trail",
                   self.ball[0] - (hx * cos_a - hy * sin_a),
                   self.ball[1] - (hx * sin_a + hy * cos_a),
                   size, size)

        fade = min(1.0, (speed - 120.0) / 420.0) * 0.85
        self.scene.set_sprite_color(trail, (1.0, 1.0, 1.0, max(0.0, fade)))

    # ── Game state ─────────────────────────────────────────────

    def reset_ball(self, towards, needs_input=False):
        """Park the ball at the centre, aimed at `towards`.

        Between points it serves itself after a beat — being made to press a
        key after every rally is tiresome. The first serve of a match waits for
        one, so the game does not start moving before you are looking at it.
        """
        self.ball = [CANVAS_W * 0.5, FIELD_TOP + FIELD_H * 0.5]
        self.ball_vel = [0.0, 0.0]
        self.serve_to = towards
        self.serve_timer = SERVE_DELAY
        self.waiting_to_serve = True
        self.needs_input = needs_input

    def serve(self):
        # Never straight along the axis — a horizontal serve is dull and a
        # near-vertical one takes forever to come back.
        angle = random.uniform(-0.38, 0.38)
        self.ball_vel = [math.cos(angle) * BALL_SPEED_START * self.serve_to,
                         math.sin(angle) * BALL_SPEED_START]
        self.waiting_to_serve = False

    def restart(self):
        self.score = [0, 0]
        self.time_left = MATCH_SECONDS
        self.over = False
        self.paddle_y = [FIELD_TOP + FIELD_H * 0.5, FIELD_TOP + FIELD_H * 0.5]
        self.reset_ball(random.choice((-1.0, 1.0)), needs_input=True)
        self.refresh_labels()

    def refresh_labels(self):
        self.scene.set_text(self.entities["score_l"], str(self.score[0]))
        self.scene.set_text(self.entities["score_r"], str(self.score[1]))

        seconds = max(0, int(math.ceil(self.time_left)))
        self.scene.set_text(self.entities["clock"],
                            "%d:%02d" % (seconds // 60, seconds % 60))

        if self.over:
            won = self.score[0] > self.score[1]
            drawn = self.score[0] == self.score[1]
            self.scene.set_text(self.entities["banner"],
                                "DRAW" if drawn else
                                ("YOU WIN" if won else "COMPUTER WINS"))
            self.scene.set_text_color(self.entities["banner"],
                                      WHITE if drawn else
                                      (PLAYER_COLOR if won else AI_COLOR))
            self.scene.set_text(self.entities["hint"], "press R to play again")
        elif self.waiting_to_serve and self.needs_input:
            self.scene.set_text(self.entities["banner"], "")
            self.scene.set_text(self.entities["hint"], "press SPACE to serve")
        else:
            self.scene.set_text(self.entities["banner"], "")
            self.scene.set_text(self.entities["hint"], "")

    # ── Simulation ─────────────────────────────────────────────

    def move_player(self, dt):
        direction = 0.0
        if self.input.is_key_down(keys.W) or self.input.is_key_down(keys.UP):
            direction -= 1.0
        if self.input.is_key_down(keys.S) or self.input.is_key_down(keys.DOWN):
            direction += 1.0

        self.paddle_y[0] = self.clamp_paddle(
            self.paddle_y[0] + direction * PADDLE_SPEED * dt)

    def move_ai(self, dt):
        # Track the ball, but only after a short delay, only while it is coming
        # this way, and only to within a dead zone. Each of those is a way for
        # the player to win a rally; a perfect tracker cannot be beaten at all.
        self.ai_timer -= dt
        if self.ai_timer <= 0.0:
            self.ai_timer = AI_REACTION
            if self.ball_vel[0] > 0.0:
                self.ai_target = self.ball[1]
            else:
                self.ai_target = FIELD_TOP + FIELD_H * 0.5

        error = self.ai_target - self.paddle_y[1]
        if abs(error) > AI_DEAD_ZONE:
            step = AI_SPEED * dt
            self.paddle_y[1] = self.clamp_paddle(
                self.paddle_y[1] + math.copysign(min(step, abs(error)), error))

    @staticmethod
    def clamp_paddle(y):
        half = PADDLE_H * 0.5
        return max(FIELD_TOP + half, min(FIELD_BOTTOM - half, y))

    def bounce_off(self, paddle_index, direction):
        """Reflect the ball off a paddle, angled by where it struck."""
        offset = (self.ball[1] - self.paddle_y[paddle_index]) / (PADDLE_H * 0.5)
        offset = max(-1.0, min(1.0, offset))

        speed = min(BALL_SPEED_MAX,
                    math.hypot(*self.ball_vel) + BALL_SPEED_GAIN)
        angle = offset * MAX_BOUNCE_ANGLE

        self.ball_vel = [math.cos(angle) * speed * direction,
                         math.sin(angle) * speed]

    def step_ball(self, dt):
        half = BALL_SIZE * 0.5
        self.ball[0] += self.ball_vel[0] * dt
        self.ball[1] += self.ball_vel[1] * dt

        # Walls
        if self.ball[1] - half < FIELD_TOP:
            self.ball[1] = FIELD_TOP + half
            self.ball_vel[1] = abs(self.ball_vel[1])
        elif self.ball[1] + half > FIELD_BOTTOM:
            self.ball[1] = FIELD_BOTTOM - half
            self.ball_vel[1] = -abs(self.ball_vel[1])

        # Paddles. Testing the velocity sign as well as the overlap keeps a
        # ball that clipped the edge from being caught inside the paddle and
        # reflected on every following frame.
        left_face = PADDLE_INSET + PADDLE_W * 0.5
        if (self.ball_vel[0] < 0.0 and self.ball[0] - half <= left_face
                and self.ball[0] > PADDLE_INSET - PADDLE_W
                and abs(self.ball[1] - self.paddle_y[0]) <= PADDLE_H * 0.5 + half):
            self.ball[0] = left_face + half
            self.bounce_off(0, 1.0)

        right_face = CANVAS_W - PADDLE_INSET - PADDLE_W * 0.5
        if (self.ball_vel[0] > 0.0 and self.ball[0] + half >= right_face
                and self.ball[0] < CANVAS_W - PADDLE_INSET + PADDLE_W
                and abs(self.ball[1] - self.paddle_y[1]) <= PADDLE_H * 0.5 + half):
            self.ball[0] = right_face - half
            self.bounce_off(1, -1.0)

        # Points
        if self.ball[0] < -BALL_SIZE:
            self.score[1] += 1
            self.reset_ball(-1.0)
            self.finish_if_done()
            self.refresh_labels()
        elif self.ball[0] > CANVAS_W + BALL_SIZE:
            self.score[0] += 1
            self.reset_ball(1.0)
            self.finish_if_done()
            self.refresh_labels()

    def finish_if_done(self):
        if max(self.score) >= MATCH_POINTS:
            self.over = True

    # ── Per-frame ──────────────────────────────────────────────

    def update(self, dt):
        # A frame that took a quarter second — a window drag, a stall — must not
        # teleport the ball through a paddle.
        dt = min(dt, 1.0 / 30.0)

        self.handle_keys()

        if not self.over:
            self.move_player(dt)

            if self.waiting_to_serve:
                self.serve_timer -= dt
                if self.serve_timer <= 0.0 and not self.needs_input:
                    self.serve()
                    self.refresh_labels()
            else:
                self.move_ai(dt)
                self.step_ball(dt)

            previous = int(math.ceil(self.time_left))
            self.time_left = max(0.0, self.time_left - dt)
            if int(math.ceil(self.time_left)) != previous:
                if self.time_left <= 0.0:
                    self.over = True
                self.refresh_labels()

        self.follow()

    def handle_keys(self):
        # Edge-triggered: is_key_down is a level, so remember what was held.
        for code, action in ((keys.SPACE, self.on_space), (keys.R, self.restart),
                             (keys.P, self.on_screenshot), (keys.V, self.on_record)):
            held = self.input.is_key_down(code)
            if held and not self._held.get(code, False):
                action()
            self._held[code] = held

    def on_space(self):
        if not self.over and self.waiting_to_serve and self.serve_timer <= 0.0:
            self.serve()
            self.refresh_labels()

    def on_screenshot(self):
        path = "pong_screenshot.png"
        print("screenshot ->", path if engine.capture_screenshot(path) else "failed")

    def on_record(self):
        if engine.is_recording():
            engine.stop_recording()
            print("recording stopped after %d frames" % engine.recorded_frame_count())
        elif not sk.video_recording_available():
            print("no ffmpeg found — put one on PATH or in $FFMPEG to record")
        elif engine.start_recording("pong_clip.mkv", fps=60):
            print("recording to pong_clip.mkv")

    def on_resize(self, width, height):
        self.window = (float(width), float(height))
        self.layout()


game = Pong()
engine.set_on_init(game.build)
engine.set_on_update(game.update)
engine.set_on_resize(game.on_resize)

if __name__ == "__main__":
    engine.run()
