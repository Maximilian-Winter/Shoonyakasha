"""
The walker's looping sounds: each an entry of LOOPS (as sounds.SOUNDS's,
with its placeholder made here, `placeholder`) played as a filtered loop
while it is wanted, its volume, pitch and filter set every frame from the
game (`update`), fading in and out over LOOP_FADE.

- **wind**: the air moving past the listener (the wind there less the
  player's or the car's velocity): louder, brighter and gustier the
  faster, and gustier the more of a storm is here; above WHISTLE_FROM m/s
  a whistle joins. Inside the car, half as loud and darker. On the world
  bus, so the air carries it.
- **rain**: as loud as how much of a storm is here (`storm_here`), drumming
  duller on the car's roof while driving.
- **car_hum** and **car_cushion**: the hover car's engines, pitch and
  brightness rising with the throttle and the boost, stuttering with the
  tank empty; and the hiss of its cushion as it comes within CUSHION_FROM
  of the ground. Heard through the suit while driving, else from where the
  car is.
- **gun_hum**: the gravity gun, quiet while idle, louder with a target,
  wobbling as it holds (more for a heavier body), rising in pitch as a
  punt charges.
- **reentry**: the roar of the car in space mode as the air ahead of it
  glows (flight.reentry_glow).
- **breath** and **heartbeat**: the player's breathing, faster and louder
  with exertion (the speed walked, the jet), and below STRAIN_RESERVE of
  the suit's reserve shorter, with a heartbeat.
"""

import math

import numpy as np

from shoonyakasha.planet.character import RUN
from shoonyakasha.planet.vec import length, normalize, sub

import acoustics
import flight
from play.core import foot, planet, state, world
from play import sounds, spaceflight, vehicle

RATE = sounds.RATE
LOOP_FADE = 0.4          # s a loop takes to fade in or out
FILTER_EASE = 0.1        # s a loop's filter takes to follow
WHISTLE_FROM = 15.0      # m/s of air past the listener at which the whistle starts
CUSHION_FROM = 2.0       # m above the ground below which the car's cushion is heard
STRAIN_RESERVE = 0.2     # the suit's reserve below which the breath strains and the heart is heard
EXERTION_EASE = 2.0      # s exertion takes to follow the effort
OCCLUSION_EVERY = 0.25   # s between a spatial loop's occlusion rays
CULL_SHARE = 1.1         # of its far distance beyond which a spatial loop stops
CAR_CLEARANCE = 3.5      # m short of the car an occlusion ray stops, so as not to hit the car


def placeholder(name):
    """Mono samples at RATE for the loop `name`, periodic over their
    length, so they loop without a seam."""
    rng = np.random.default_rng(sum(map(ord, name)) + 7)

    def t(seconds):
        return np.arange(int(round(seconds * RATE))) / RATE

    def coloured(seconds, power, low=20.0, high=16_000.0):
        """Noise falling as 1 / f^(power / 2) between `low` and `high` Hz."""
        n = int(round(seconds * RATE))
        f = np.fft.rfftfreq(n, 1.0 / RATE)
        gain = np.where((f >= low) & (f <= high), 1.0 / np.maximum(f, 1.0) ** (0.5 * power), 0.0)
        spectrum = (rng.standard_normal(len(f)) + 1j * rng.standard_normal(len(f))) * gain
        return np.fft.irfft(spectrum, n)

    def saw(hz, x):
        return 2.0 * ((hz * x) % 1.0) - 1.0

    if name == "wind":
        x = coloured(6.0, 1.0)
    elif name == "wind_whistle":
        x = coloured(4.0, 0.3, 300.0, 9_000.0)
    elif name == "rain":
        x = coloured(5.0, 0.2, 400.0, 12_000.0)
        x /= np.max(np.abs(x))
        drops = np.zeros_like(x)
        where = rng.random(len(x)) < 0.004
        drops[where] = rng.uniform(0.3, 1.0, int(where.sum()))
        x = 0.6 * x + np.convolve(np.pad(drops, (24, 0), mode="wrap"), np.hanning(24), mode="valid")[:len(x)]
    elif name == "car_hum":
        x0 = t(1.0)
        x = saw(110.0, x0) + 0.8 * saw(111.0, x0) + 0.6 * np.sin(2.0 * np.pi * 55.0 * x0)
    elif name == "car_cushion":
        x = coloured(3.0, 0.4, 100.0, 9_000.0)
    elif name == "gun_hum":
        x0 = t(2.0)
        x = (np.sin(2.0 * np.pi * 220.0 * x0) + 0.4 * np.sin(2.0 * np.pi * 331.0 * x0)
             + 0.2 * np.sin(2.0 * np.pi * 440.0 * x0))
    elif name == "reentry":
        x = coloured(4.0, 2.0, 20.0, 3_000.0)
        x /= np.max(np.abs(x))
        clicks = np.zeros_like(x)
        where = rng.random(len(x)) < 0.0015
        clicks[where] = rng.uniform(-1.0, 1.0, int(where.sum()))
        x = x + 0.8 * np.convolve(np.pad(clicks, (40, 0), mode="wrap"), np.hanning(40), mode="valid")[:len(x)]
    elif name == "breath":
        x0 = t(3.2)
        noise = coloured(3.2, 0.3, 300.0, 5_000.0)
        noise /= np.max(np.abs(noise))
        inhale = np.where(x0 < 1.1, np.sin(np.pi * x0 / 1.1), 0.0) ** 2
        exhale = np.where((x0 >= 1.4) & (x0 < 2.9), np.sin(np.pi * (x0 - 1.4) / 1.5), 0.0) ** 2
        x = noise * (0.6 * inhale + exhale)
    elif name == "heartbeat":
        x0 = t(0.9)
        beat = np.zeros_like(x0)
        for at, weight in ((0.0, 1.0), (0.28, 0.7)):
            d = x0 - at
            beat += np.where(d >= 0.0, weight * np.sin(2.0 * np.pi * 48.0 * d) * np.exp(-d / 0.04), 0.0)
        x = beat
    else:
        x = coloured(2.0, 1.0)
    x = np.asarray(x, dtype=np.float64)
    peak = float(np.max(np.abs(x))) or 1.0
    return (0.8 * x / peak).astype(np.float32)


LOOP = {"category": "ambience", "priority": 200, "make": placeholder}
LOOPS = {
    "wind":         {"bus": "world", "volume": 0.6, **LOOP},
    "wind_whistle": {"bus": "world", "volume": 0.35, **LOOP},
    "rain":         {"bus": "world", "volume": 0.6, **LOOP},
    "car_hum":      {"bus": "world", "volume": 0.5, "near": 3.0, "far": 250.0, **LOOP},
    "car_cushion":  {"bus": "world", "volume": 0.4, "near": 3.0, "far": 120.0, **LOOP},
    "gun_hum":      {"bus": "suit", "volume": 0.3, **LOOP},
    "reentry":      {"bus": "suit", "volume": 0.9, **LOOP},
    "breath":       {"bus": "suit", "volume": 0.35, **LOOP},
    "heartbeat":    {"bus": "suit", "volume": 0.6, **LOOP},
}


def hold(name, level, pitch=1.0, position=None, bus=None, lowpass=0.0, highpass=0.0, velocity=(0.0, 0.0, 0.0)):
    """The loop `name` playing at `level` (times its volume; 0 or less
    stops it, fading out), `pitch`, at `position` (planet space) when given
    and the entry is spatial, moving at `velocity` (for the Doppler shift),
    on `bus` (its own when None), through its filter at `lowpass` and
    `highpass` Hz (0 open). A spatial loop stops beyond CULL_SHARE times
    its entry's far distance and starts again within it. A spatial loop with
    something between it and the camera (sounds.occluded, looked for every
    OCCLUSION_EVERY seconds, CAR_CLEARANCE short of it) is quieter and
    duller (acoustics.occluded_audio). A loop whose bus or being spatial
    changes starts again."""
    s = state.sound
    if s.audio is None:
        return
    entry = s.entries[name]
    bus = bus or entry["bus"]
    spatial = position is not None and "near" in entry
    if spatial:
        distance = length(sub(position, state.view.position))
        far = entry.get("far", 500.0)
        if distance > CULL_SHARE * far or (name not in s.loops and distance > far):
            level = 0.0
    if spatial and level > 0.0:
        due, blocked = s.occlusion.get(name, (0.0, False))
        if s.clock >= due:
            blocked = sounds.occluded(position, CAR_CLEARANCE)
            s.occlusion[name] = (s.clock + OCCLUSION_EVERY, blocked)
        gain, cutoff = acoustics.occluded_audio(blocked)
        level *= gain
        if cutoff:
            lowpass = min(lowpass, cutoff) if lowpass > 0.0 else cutoff
    running = s.loops.get(name)
    if running is not None and (level <= 0.0 or running[1] != bus or running[2] != spatial
                                or not s.audio.active(running[0])):
        s.audio.stop(running[0], fade=LOOP_FADE)
        del s.loops[name]
        running = None
    if level <= 0.0:
        return
    if running is None:
        voice = sounds.play(name, position, volume=level, pitch=pitch, bus=bus, loop=True, fade_in=LOOP_FADE,
                            filtered=True, lowpass=lowpass, highpass=highpass, velocity=velocity)
        if voice:
            s.loops[name] = (voice, bus, spatial)
        return
    s.audio.set_voice(running[0], position=position if spatial else None, velocity=velocity,
                      volume=entry["volume"] * level,
                      pitch=pitch, lowpass=lowpass, highpass=highpass, filter_fade=FILTER_EASE)


def update(dt):
    """Every loop's level, pitch and filter for this frame (the module's docstring)."""
    if state.sound.audio is None:
        return
    update_wind()
    update_rain()
    update_car()
    update_gun()
    update_reentry()
    update_breath(dt)


def airflow():
    """m/s of air moving past the listener: on foot the wind less the
    player's velocity, driving the wind less the car's; none in space mode
    or off the planet."""
    camera = state.view.position
    if state.space.ship is not None or world.body_under(camera) is not planet:
        return 0.0
    if state.vehicle.driving and state.vehicle.car is not None:
        car = state.vehicle.car
        wind = world.wind_at(spaceflight.car_body(), car.position, car.height)
        return length(sub(wind, car.velocity))
    wind = world.wind_at(planet, planet.relative(camera))
    if foot.player is None:
        return length(wind)
    return length(sub(wind, foot.physics.character_velocity(foot.player.character)))


def storm_here():
    """How much of the storm is at the camera, 0 to 1 (wind.Storm.at); 0 off the planet."""
    camera = state.view.position
    if world.storm is None or world.body_under(camera) is not planet:
        return 0.0
    return world.storm.at(normalize(planet.relative(camera)), world.time)


def update_wind():
    speed = min(airflow(), 80.0)
    clock = state.sound.clock
    gust = 1.0 + (0.25 + 0.35 * storm_here()) * math.sin(0.8 * clock) * math.sin(0.23 * clock + 1.7)
    level = smoothstep(1.0, 25.0, speed) * gust
    lowpass = min(8_000.0, 250.0 + 90.0 * speed * gust)
    inside = state.vehicle.driving
    if inside:
        level, lowpass = 0.5 * level, 0.5 * lowpass
    hold("wind", level, lowpass=lowpass, highpass=60.0 + 4.0 * speed)
    whistle = smoothstep(WHISTLE_FROM, 35.0, speed) * gust * (0.5 if inside else 1.0)
    hold("wind_whistle", whistle, pitch=0.8 + 0.01 * speed, lowpass=1_800.0 + 60.0 * speed,
         highpass=1_200.0 + 40.0 * speed)


def update_rain():
    level = storm_here()
    if state.vehicle.driving:
        hold("rain", 0.8 * level, pitch=0.8, lowpass=1_500.0)
    else:
        hold("rain", level, lowpass=8_000.0, highpass=300.0)


def update_car():
    car = state.vehicle.car
    if car is None:
        hold("car_hum", 0.0)
        hold("car_cushion", 0.0)
        return
    driving = state.vehicle.driving
    if state.space.ship is not None:
        throttle, boost = state.space.controls.throttle, False
    else:
        throttle, boost = abs(state.vehicle.controls.throttle), state.vehicle.controls.boost and driving
    throttle = throttle if driving else 0.0
    level = 0.35 + 0.45 * throttle + (0.2 if boost else 0.0)
    pitch = 0.7 + 0.5 * throttle + (0.15 if boost else 0.0)
    if car.dry():
        level *= 0.3 + 0.7 * (state.sound.rng.random() < 0.3)
        pitch = 0.55
    position = None if driving else vehicle.car_position()
    bus = "suit" if driving else None
    hold("car_hum", level * (0.8 if driving else 1.0), pitch=pitch, position=position, bus=bus,
         lowpass=700.0 + 3_000.0 * throttle + (1_500.0 if boost else 0.0), velocity=tuple(car.velocity))
    height = car.height
    cushion = 0.0
    if state.space.ship is None and height is not None and height < CUSHION_FROM:
        cushion = (1.0 - height / CUSHION_FROM) ** 2 * (0.5 + 0.5 * min(1.0, car.speed() / 20.0))
    hold("car_cushion", cushion, position=position, bus=bus, lowpass=900.0 + 40.0 * car.speed(),
         velocity=tuple(car.velocity))


def update_gun():
    gun = foot.gun
    if gun is None or foot.player is None or state.vehicle.driving:
        hold("gun_hum", 0.0)
        return
    status = gun.status
    charge = gun.charge
    pitch = 1.0
    if status == "holding":
        mass = foot.physics.mass(gun.held) if foot.physics.contains(gun.held) else 0.0
        level = 0.6
        pitch = 1.0 + 0.03 * min(1.0, mass / 200.0) * math.sin(2.0 * math.pi * 3.0 * state.sound.clock)
    elif status == "can_grab":
        level = 0.35
    elif status in ("too_heavy", "too_hot"):
        level = 0.25
    else:
        level = 0.12
    if gun.charging is not None:
        level, pitch = 0.6 + 0.4 * charge, pitch * (1.0 + 1.2 * charge)
    hold("gun_hum", level, pitch=pitch, lowpass=1_500.0 + 4_000.0 * charge)


def update_reentry():
    ship = state.space.ship
    glow = flight.reentry_glow(world.physics, ship.position, ship.velocity) if ship is not None else 0.0
    hold("reentry", glow, pitch=0.8 + 0.4 * glow, lowpass=400.0 + 2_000.0 * glow)


def update_breath(dt):
    s = state.sound
    player = foot.player
    if player is None or state.vehicle.driving:
        hold("breath", 0.0)
        hold("heartbeat", 0.0)
        return
    speed = length(foot.physics.character_velocity(player.character))
    effort = min(1.0, max(0.0, (speed - 1.0) / (RUN - 1.0)))
    if not player.on_ground and foot.controls.jet:
        effort = max(effort, 0.6)
    s.exertion += (effort - s.exertion) * min(1.0, dt / EXERTION_EASE)
    strain = max(0.0, 1.0 - state.suit.state.reserve / STRAIN_RESERVE)
    hold("breath", 0.35 + 0.65 * s.exertion + 0.4 * strain, pitch=0.8 + 0.5 * s.exertion + 0.3 * strain,
         lowpass=2_500.0 + 3_000.0 * s.exertion)
    hold("heartbeat", strain, pitch=0.9 + 0.5 * strain)


def smoothstep(low, high, x):
    u = min(1.0, max(0.0, (x - low) / (high - low)))
    return u * u * (3.0 - 2.0 * u)
