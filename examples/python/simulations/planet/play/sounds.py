"""
The walker's sound (sk.Audio, engine.audio): the sound table, its clips,
and what plays them.

Each entry of SOUNDS gives its bus, its volume, how far its voices' pitch
is spread at random, and, for a spatial one, its distances (`near`, full
volume within; `far`, where its falloff stops). An entry's clips are the
files sounds/<name>.wav, .flac or .mp3 and sounds/<name>_<anything> with
those endings, beside walker.py; one of them is picked at random for each
voice. An entry without a file plays its placeholder: made by its
"make" function when it has one, else here (`placeholder`). `setup`
takes more tables beside SOUNDS (play/loops.py's).

Each frame (`update`) the listener follows the camera, and the sounds of
what happened since the last frame play: the gravity gun's grab, drop,
punt and charged punt, a prop's hit (its speed falling sharply, louder
the more it fell and the heavier the prop), the player's footsteps (one a
BOB_STRIDE, when the head bob is lowest) and landings, and the slate and
the System screen opening and closing. A toast plays as it shows
(`toast`), a boulder's split as it splits (`split`).

The air carries the world bus (acoustics.air_audio): its gain and
low-pass follow the air's density at the camera, reached over AIR_FADE,
silent in vacuum (on the moon, in orbit). There a hit or a split within
acoustics.CONTACT_DISTANCE of the camera plays "contact" through the suit
instead (`strike`), and one farther off plays nothing.

In air a world sound at a point (`strike`) starts late by the time sound
takes to come from there (acoustics.sound_delay) and is dulled by the
distance (acoustics.distance_cutoff); with the time warp above
WARP_SILENT it is not played. A meteor's impact (`boom`) is such a
sound, and in vacuum, standing on the ground within its reach
(acoustics.rumble_volume), a low rumble through the feet. `alert` plays a
sound for the toast that follows it in place of the toast's own.
"""

import math
import os

import numpy as np

from shoonyakasha.planet.character import BOB_STRIDE
from shoonyakasha.planet.vec import add, length, sub

import acoustics
import hud as hud_module
from play.core import GOOD_TOAST, HERE, engine, foot, settings, state, world
from play import spaceflight, walking

RATE = 48000
SOUND_DIR = os.path.join(HERE, "sounds")
ENDINGS = (".wav", ".flac", ".mp3")

SOUNDS = {
    "grab":          {"bus": "suit", "volume": 0.45},
    "drop":          {"bus": "suit", "volume": 0.35},
    "punt":          {"bus": "suit", "volume": 0.8, "spread": 0.05},
    "punt_charged":  {"bus": "suit", "volume": 1.0},
    "hit":           {"bus": "world", "volume": 1.0, "spread": 0.08, "near": 1.0, "far": 60.0},
    "split":         {"bus": "world", "volume": 1.0, "spread": 0.05, "near": 4.0, "far": 300.0},
    "contact":       {"bus": "suit", "volume": 0.6, "spread": 0.1},
    "meteor_boom":   {"bus": "world", "volume": 0.9, "spread": 0.1, "near": 60.0, "far": 6_000.0},
    "meteor_boom_large": {"bus": "world", "volume": 1.0, "spread": 0.05, "near": 300.0, "far": 15_000.0},
    "footstep":      {"bus": "suit", "volume": 0.3, "spread": 0.1},
    "land":          {"bus": "suit", "volume": 0.5, "spread": 0.05},
    "slate_open":    {"bus": "ui", "volume": 0.4},
    "slate_close":   {"bus": "ui", "volume": 0.35},
    "toast":         {"bus": "ui", "volume": 0.3},
    "toast_good":    {"bus": "ui", "volume": 0.45},
    "toast_warning": {"bus": "ui", "volume": 0.45},
    "pads_closed":   {"bus": "ui", "volume": 0.5},
}

HIT_SPEED = 1.5          # m/s a prop's speed must fall by in a frame to be heard hitting something
HIT_FULL = 12.0          # m/s of fall at which a hit is at full volume
HIT_MASS = 50.0          # kg at which a hit is neither quieter nor lower
HIT_GAP = 0.15           # s between one prop's hits
LAND_AIR = 0.35          # s in the air before touching down is a landing
AIR_FADE = 0.3           # s the world bus takes to follow the air
WARP_SILENT = 2.0        # time warp above which world sounds at a point are not played


def placeholder(name):
    """Mono samples at RATE standing in for the sound `name`: chirps,
    thumps and filtered noise."""
    rng = np.random.default_rng(sum(map(ord, name)))

    def t(seconds):
        return np.arange(int(seconds * RATE)) / RATE

    def chirp(f0, f1, seconds):
        x = t(seconds)
        phase = 2.0 * np.pi * (f0 * x + 0.5 * (f1 - f0) / seconds * x * x)
        return np.sin(phase)

    def decay(x, seconds):
        return np.exp(-t(len(x) / RATE) / seconds)

    def attack(x, seconds=0.004):
        ramp = np.minimum(1.0, t(len(x) / RATE) / seconds)
        return x * ramp

    def noise(seconds, smooth):
        n = rng.standard_normal(int(seconds * RATE))
        return np.convolve(n, np.ones(smooth) / smooth, mode="same") if smooth > 1 else n

    def thump(hz, seconds, fall):
        return chirp(hz * 1.6, hz, seconds) * decay(t(seconds), fall)

    def pad(*parts):
        n = max(len(p) for p in parts)
        return sum(np.pad(p, (0, n - len(p))) for p in parts)

    if name == "grab":
        x = chirp(320.0, 640.0, 0.14) * decay(t(0.14), 0.08)
    elif name == "drop":
        x = chirp(520.0, 260.0, 0.12) * decay(t(0.12), 0.06)
    elif name == "punt":
        x = pad(thump(90.0, 0.25, 0.06), 0.5 * noise(0.12, 6) * decay(t(0.12), 0.03))
    elif name == "punt_charged":
        x = pad(thump(70.0, 0.45, 0.12), 0.6 * noise(0.3, 4) * decay(t(0.3), 0.08),
                0.3 * chirp(900.0, 200.0, 0.3) * decay(t(0.3), 0.1))
    elif name == "hit":
        x = pad(thump(140.0, 0.12, 0.03), 0.6 * noise(0.08, 3) * decay(t(0.08), 0.015))
    elif name == "split":
        crackle = noise(0.6, 2) * (rng.random(int(0.6 * RATE)) < 0.02)
        x = pad(thump(60.0, 0.6, 0.15), 0.5 * noise(0.6, 8) * decay(t(0.6), 0.12),
                2.0 * crackle * decay(t(0.6), 0.2))
    elif name == "contact":
        x = pad(thump(55.0, 0.18, 0.05), 0.5 * noise(0.12, 60) * decay(t(0.12), 0.03))
    elif name in ("meteor_boom", "meteor_boom_large"):
        large = name == "meteor_boom_large"
        length_s = 3.0 if large else 1.8
        x = pad(1.2 * thump(30.0 if large else 45.0, length_s, 0.5 if large else 0.3),
                0.7 * noise(length_s, 40) * decay(t(length_s), 0.8 if large else 0.45),
                0.6 * noise(0.15, 2) * decay(t(0.15), 0.03))
    elif name == "pads_closed":
        beep = t(0.12)
        x = pad(*(np.pad(np.sin(2 * np.pi * hz * beep) * decay(beep, 0.08), (int(k * 0.16 * RATE), 0))
                  for k, hz in enumerate((880.0, 660.0, 440.0))))
    elif name == "footstep":
        x = 0.8 * noise(0.07, 24) * decay(t(0.07), 0.02) + 0.3 * thump(110.0, 0.07, 0.02)
    elif name == "land":
        x = pad(thump(80.0, 0.2, 0.05), 0.6 * noise(0.12, 16) * decay(t(0.12), 0.03))
    elif name == "slate_open":
        x = pad(np.sin(2 * np.pi * 660.0 * t(0.07)) * decay(t(0.07), 0.03),
                np.pad(np.sin(2 * np.pi * 990.0 * t(0.09)) * decay(t(0.09), 0.04), (int(0.05 * RATE), 0)))
    elif name == "slate_close":
        x = pad(np.sin(2 * np.pi * 880.0 * t(0.07)) * decay(t(0.07), 0.03),
                np.pad(np.sin(2 * np.pi * 587.0 * t(0.09)) * decay(t(0.09), 0.04), (int(0.05 * RATE), 0)))
    elif name == "toast_good":
        x = pad(np.sin(2 * np.pi * 880.0 * t(0.35)) * decay(t(0.35), 0.12),
                0.6 * np.pad(np.sin(2 * np.pi * 1320.0 * t(0.3)) * decay(t(0.3), 0.1), (int(0.06 * RATE), 0)))
    elif name == "toast_warning":
        beep = np.sin(2 * np.pi * 330.0 * t(0.1)) * decay(t(0.1), 0.06)
        x = pad(beep, np.pad(beep, (int(0.14 * RATE), 0)))
    else:                                       # "toast" and any entry without a placeholder of its own
        x = np.sin(2 * np.pi * 1200.0 * t(0.05)) * decay(t(0.05), 0.015)
    x = attack(np.asarray(x, dtype=np.float64))
    peak = float(np.max(np.abs(x))) or 1.0
    return (0.8 * x / peak).astype(np.float32)


def sound_files(name):
    """The files in SOUND_DIR that are clips of `name`, sorted."""
    if not os.path.isdir(SOUND_DIR):
        return []
    found = []
    for file in os.listdir(SOUND_DIR):
        stem, ending = os.path.splitext(file)
        if ending.lower() in ENDINGS and (stem == name or stem.startswith(name + "_")):
            found.append(os.path.join(SOUND_DIR, file))
    return sorted(found)


def setup(*tables):
    """The engine's audio, the entries of SOUNDS and of `tables` with
    their clips (their files, or their placeholders), the master volume
    from the settings, and the toasts and splits told to play. Without
    audio (miniaudio could not start) the walker is silent."""
    s = state.sound
    s.entries = dict(SOUNDS)
    for table in tables:
        s.entries.update(table)
    try:
        s.audio = engine.audio
    except RuntimeError as error:
        print("[walker] no sound: %s" % error, flush=True)
        return
    for name, entry in s.entries.items():
        make = entry.get("make", placeholder)
        clips = []
        for path in sound_files(name):
            try:
                clips.append(s.audio.load(path))
            except RuntimeError as error:
                print("[walker] %s" % error, flush=True)
        s.clips[name] = clips or [s.audio.load_samples(make(name), sample_rate=RATE)]
    apply_volume()
    shown = state.ui.hud.on_toast
    state.ui.hud.on_toast = lambda text, colour, detail, kind: (toast(colour), shown and shown(text, colour, detail,
                                                                                               kind))
    split_was = foot.rocks.on_split
    foot.rocks.on_split = lambda prop, chunks: (split(chunks), split_was and split_was(prop, chunks))


def apply_volume():
    if state.sound.audio is not None:
        state.sound.audio.set_bus_volume("master", settings["volume"], fade=0.1)


def play(name, position=None, volume=1.0, pitch=1.0, bus=None, **options):
    """A voice of the entry `name` on its bus (or `bus`), at `position`
    (planet space) when the entry is spatial; `options` go on to
    sk.Audio.play (loop, fade_in, the filter's). Counted in
    state.sound.played."""
    s = state.sound
    if s.audio is None:
        return 0
    entry = s.entries[name]
    spread = entry.get("spread", 0.0)
    pitch *= 1.0 + s.rng.uniform(-spread, spread)
    spatial = "near" in entry and position is not None
    voice = s.audio.play(s.rng.choice(s.clips[name]), position=position if spatial else None,
                         bus=bus or entry["bus"], volume=entry["volume"] * volume, pitch=pitch,
                         min_distance=entry.get("near", 1.0), max_distance=entry.get("far", 500.0), **options)
    s.played[name] += 1
    return voice


def toast(colour):
    if state.sound.hush:
        state.sound.hush = False
    elif colour == GOOD_TOAST:
        play("toast_good")
    elif colour == hud_module.WARNING:
        play("toast_warning")
    else:
        play("toast")


def split(chunks):
    if chunks:
        body, position, _ = foot.prop_state(chunks[0])
        strike("split", add(body.centre(), position))


def strike(name, position, volume=1.0, pitch=1.0):
    """The world sound `name` at `position` (planet space), late by the
    sound's travel and dulled by the distance; in vacuum, "contact" through
    the suit when within acoustics.CONTACT_DISTANCE of the camera, else
    none; none with the time warp above WARP_SILENT."""
    if spaceflight.time_warp() > WARP_SILENT:
        return 0
    distance = length(sub(position, state.view.position))
    if state.sound.density > acoustics.VACUUM:
        return play(name, position, volume, pitch, delay=acoustics.sound_delay(distance),
                    lowpass=acoustics.distance_cutoff(distance))
    near = acoustics.contact_volume(distance)
    return play("contact", volume=volume * near, pitch=pitch) if near > 0.0 else 0


def boom(point, large):
    """A meteor's impact at `point` (planet space): its boom (`strike`),
    or in vacuum, with the player on the ground, its rumble through the
    feet."""
    if state.sound.density > acoustics.VACUUM:
        return strike("meteor_boom_large" if large else "meteor_boom", point)
    felt = acoustics.rumble_volume(length(sub(point, state.view.position)), large)
    if felt > 0.0 and foot.player is not None and foot.player.on_ground:
        return play("contact", volume=felt, pitch=0.5)
    return 0


def alert(name):
    """The sound `name`, the next toast silent."""
    play(name)
    state.sound.hush = True


def air_share(point):
    """The air's density at `point` (planet space) as a share of the
    density at the planet's surface; 0 where the moon is the body under it."""
    if world.body_under(point) is not world.planet:
        return 0.0
    planet = world.physics
    return planet.density(point) / (planet.air_density * planet.density_factor)


def update_air():
    """The world bus's gain and cutoff set for the air at the camera when they changed."""
    s = state.sound
    s.density = air_share(state.view.position)
    gain, cutoff = acoustics.air_audio(s.density)
    if s.air_set is None or abs(gain - s.air_set[0]) > 0.01 or abs(math.log(cutoff / s.air_set[1])) > 0.02:
        s.audio.set_bus_volume("world", gain, fade=AIR_FADE)
        s.audio.set_world_lowpass(cutoff, fade=AIR_FADE)
        s.air_set = (gain, cutoff)


def update(dt):
    """The listener at the camera, and the sounds of this frame (the
    module's docstring)."""
    s = state.sound
    if s.audio is None:
        return
    right, up, forward = walking.view_basis()
    s.audio.set_listener(state.view.position, forward, up)
    s.hush = False
    update_air()
    update_gun()
    update_hits(dt)
    update_steps(dt)
    for name, screen in (("slate", state.ui.slate), ("system", state.ui.system)):
        shown = bool(screen is not None and screen.visible)
        if shown != s.shown.get(name, shown):
            play("slate_open" if shown else "slate_close")
        s.shown[name] = shown


def update_gun():
    s = state.sound
    gun = foot.gun
    if gun is None:
        s.held, s.last_punt = None, None
        return
    if gun.last_punt is not None and gun.last_punt is not s.last_punt:
        charged = gun.since_charged <= gun.since_punt
        play("punt_charged" if charged else "punt")
    elif gun.held is not None and s.held is None:
        play("grab")
    elif gun.held is None and s.held is not None:
        play("drop")
    s.held, s.last_punt = gun.held, gun.last_punt


def update_hits(dt):
    """A hit for each prop whose speed fell by HIT_SPEED or more since the last frame."""
    s = state.sound
    s.clock += dt
    seen = {}
    held = foot.gun.held if foot.gun is not None else None
    for prop in foot.props.all:
        if prop.body is None or prop.body == held:
            continue
        body, position, velocity = foot.prop_state(prop)
        key = id(prop)
        seen[key] = velocity
        before = s.velocities.get(key)
        if before is None or length(velocity) >= length(before):
            continue
        fall = length(sub(before, velocity))
        if fall < HIT_SPEED or s.clock - s.hit_at.get(key, -math.inf) < HIT_GAP:
            continue
        s.hit_at[key] = s.clock
        weight = (prop.mass / HIT_MASS) ** 0.25
        strike("hit", add(body.centre(), position), volume=min(1.0, fall / HIT_FULL) * min(1.5, weight),
               pitch=max(0.6, min(1.6, 1.0 / weight)))
    s.velocities = seen
    s.hit_at = {key: at for key, at in s.hit_at.items() if key in seen}


def update_steps(dt):
    """A footstep each BOB_STRIDE walked, when the head bob is lowest; a landing after LAND_AIR in the air."""
    s = state.sound
    player = foot.player
    if player is None:
        s.stride, s.air = None, 0.0
        return
    stride = math.floor(player.bob_distance / BOB_STRIDE + 0.25)
    if s.stride is not None and stride > s.stride and player.on_ground:
        play("footstep")
    s.stride = stride
    if player.on_ground:
        if s.air >= LAND_AIR:
            play("land")
        s.air = 0.0
    else:
        s.air += dt
