# Audio with miniaudio — Implementation Guide

Fourth guide for Shoonyakasha. Adds an audio engine feature to the C++ engine, wires it into the ECS and the Python API, and uses it for the planet game: spatial one-shots, music, procedural loops (wind, hover car, gravity gun, reentry), and world rules (air density, the moon's vacuum, the speed of sound, time warp, the slate and the pause menu).

miniaudio's function names below are from memory of its 0.11 API. The structure matters more than the exact signatures; check names against the header you vendor.

---

## 0. Overview

| Piece | Approach |
|---|---|
| Library | miniaudio (single header, public domain / MIT-0), `ma_engine` on top of its node graph |
| Threads | miniaudio mixes on its own audio thread; the ECS only sends parameter updates once per frame |
| Precision | Listener at the origin; sources get `position − camera` subtracted in double, then cast, exactly like rendering |
| Mix structure | Buses as `ma_sound_group`s: master → world, ui, music, suit; world has its own low-pass for air and the slate |
| ECS | `AudioListener`, `AudioSource`, `AudioEmitterProcedural` components; `AudioSystem` after the transform system |
| Procedural sound | Custom `ma_node`s: wind, hover car, gravity-gun hum, reentry, suit breathing |
| World rules | Air density drives the world bus's gain and cutoff; vacuum keeps only contact and suit sound; delayed starts by distance / 340 m/s; world bus fades out above 2× warp |
| Later | Steam Audio for HRTF and occlusion as a node in the same graph |

### Why not PortAudio

PortAudio gives you a device and a callback asking for samples. A game also needs decoding, streaming, resampling, a mixer, voice management, 3D panning, distance falloff and doppler. miniaudio has all of these and still exposes the low-level device and the node graph, so nothing is lost by starting higher.

### Module layout

```
include/Audio/
  AudioEngine.h/.cpp        # owns ma_engine, buses, clip cache, voice pool
  AudioNodes.h/.cpp         # custom procedural nodes
  AudioSystem.h/.cpp        # ECS system: components -> engine, per frame
third_party/miniaudio/
  miniaudio.h               # MINIAUDIO_IMPLEMENTATION in exactly one .cpp
  extras/stb_vorbis.c       # Ogg Vorbis decoding (optional)
python/shoonyakasha/
  audio.py                  # thin helpers over the bound API (sound banks, presets)
examples/planet/
  sounds.py                 # the game's sound table and world rules
  sounds/                   # wav/ogg/flac files
```

---

## 1. The engine wrapper

### 1.1 Build

- Vendor `miniaudio.h`. Define `MINIAUDIO_IMPLEMENTATION` in `AudioEngine.cpp` only.
- For Ogg Vorbis, include `stb_vorbis.c` before miniaudio with `STB_VORBIS_HEADER_ONLY`, and again after the implementation (the pattern in miniaudio's examples). Or keep to WAV/FLAC for effects and Ogg only for music.
- Turn off backends you don't need with `MA_NO_*` defines to shorten builds.

### 1.2 `AudioEngine`

```cpp
class AudioEngine {
public:
    bool init(uint32_t sampleRate = 48000, uint32_t channels = 2);
    void shutdown();

    // Buses
    ma_sound_group* bus(BusId id);
    void setBusVolume(BusId id, float linear);          // smoothed, see 4.3
    void setWorldLowpass(float cutoffHz);               // smoothed

    // Clips
    ClipId loadClip(const std::string& path, ClipFlags flags);  // Decode | Stream | Async
    void   unloadClip(ClipId);

    // Voices
    VoiceId play(ClipId, const PlayParams&);            // one-shot or loop
    void    stop(VoiceId, float fadeSeconds = 0.0f);
    void    setVoice(VoiceId, const VoiceParams&);      // position, velocity, gain, pitch...

    // Listener
    void setListener(const glm::vec3& forward, const glm::vec3& up, const glm::vec3& velocity);

    // Clock
    uint64_t timeFrames() const;                        // ma_engine_get_time_in_pcm_frames

private:
    ma_engine engine_;
    ma_sound_group buses_[BusCount];
    ma_lpf_node worldLowpass_;
    // clip cache, voice pool...
};
```

`init` in outline:

```cpp
ma_engine_config cfg = ma_engine_config_init();
cfg.sampleRate = sampleRate;
cfg.channels   = channels;
cfg.listenerCount = 1;
ma_engine_init(&cfg, &engine_);

// Buses: master is the engine's endpoint; groups attach to it.
ma_sound_group_init(&engine_, 0, nullptr, &buses_[Bus::World]);
ma_sound_group_init(&engine_, 0, nullptr, &buses_[Bus::UI]);
ma_sound_group_init(&engine_, 0, nullptr, &buses_[Bus::Music]);
ma_sound_group_init(&engine_, 0, nullptr, &buses_[Bus::Suit]);

// The world bus goes through a low-pass before the endpoint.
ma_node_graph* graph = ma_engine_get_node_graph(&engine_);
ma_lpf_node_config lpf = ma_lpf_node_config_init(channels, sampleRate, 20000.0, 2);
ma_lpf_node_init(graph, &lpf, nullptr, &worldLowpass_);
ma_node_attach_output_bus(&worldLowpass_, 0, ma_engine_get_endpoint(&engine_), 0);
ma_node_attach_output_bus(&buses_[Bus::World], 0, &worldLowpass_, 0);
```

### 1.3 Threads

- miniaudio's device callback runs the node graph on its own thread. `ma_sound_set_*` calls are safe from the main thread.
- Custom nodes read their parameters from atomics (or a small lock-free struct written once per frame) and smooth them inside `onProcess`, so a parameter jump never clicks.
- Decoding: short clips are decoded fully at load (`MA_SOUND_FLAG_DECODE`). Music and long ambiences stream (`MA_SOUND_FLAG_STREAM`). Big loads use `MA_SOUND_FLAG_ASYNC` so the main thread never waits on disk.

### 1.4 Voices

- A fixed pool of `ma_sound` objects (e.g. 64) created from cached data sources with `ma_sound_init_copy` or `ma_sound_init_from_file` over the resource manager, so a clip's decoded data is shared between voices.
- `VoiceId` = slot index + generation counter, so a stale handle from Python never touches a reused slot.
- One-shots return their slot to the pool when `ma_sound_at_end` is true (checked by `AudioSystem` each frame, not in the callback).

---

## 2. Precision and the listener

The planet game keeps positions in doubles with the planet's centre at the origin, and renders with the camera at the origin. Audio does the same:

- The listener sits at the origin, its forward and up set from the camera's basis each frame (`ma_engine_listener_set_direction`, `ma_engine_listener_set_world_up`).
- Each spatial source gets `float3(source_world − camera_world)`, subtracted in double on the CPU, every frame (`ma_sound_set_position`).
- Velocities for doppler are relative to the body under the listener: a crate resting on the moon has zero velocity relative to a player standing on the moon, though both move at 288 m/s around the planet. Set `ma_engine_listener_set_velocity` to the listener's velocity relative to that body, and each source's velocity relative to the same body.
- Doppler: on, with a modest factor (0.5) so fast fly-bys bend in pitch without the hover car's own engine wobbling as you steer.

```cpp
void AudioSystem::update(const Camera& cam, const BodyFrame& frame, double dt) {
    engine_.setListener(cam.forward, cam.up, frame.relativeVelocity(cam.velocityW));
    for (auto [e, src, tf] : registry.view<AudioSource, Transform>()) {
        if (!src.spatial || !src.voice) continue;
        glm::vec3 rel = glm::vec3(tf.positionW - cam.positionW);        // double -> float
        glm::vec3 vel = glm::vec3(frame.relativeVelocity(tf.velocityW));
        engine_.setVoice(src.voice, {.position = rel, .velocity = vel});
    }
}
```

---

## 3. ECS

### 3.1 Components

```cpp
struct AudioListener { };                              // on the camera entity, at most one

struct AudioSource {
    ClipId   clip;
    BusId    bus = Bus::World;
    float    volume = 1.0f;
    float    pitch = 1.0f;
    bool     loop = false;
    bool     spatial = true;
    bool     autoplay = false;
    float    minDistance = 1.0f;                       // full volume inside
    float    maxDistance = 500.0f;                     // culled beyond
    float    rolloff = 1.0f;
    uint8_t  priority = 128;                           // higher wins when voices run out
    VoiceId  voice{};                                  // runtime
};

struct AudioEmitterProcedural {
    ProcKind kind;                                     // Wind, HoverCar, GunHum, Reentry, Breathing
    BusId    bus = Bus::World;
    std::array<float, 4> params{};                     // meaning per kind, see section 5
    VoiceId  voice{};                                  // runtime
};
```

### 3.2 `AudioSystem` per frame

Runs after the transform system (priority 1, say), so positions are this frame's:

1. Listener from the `AudioListener` entity's camera basis.
2. Sources with `autoplay` and no voice start, if within `maxDistance`.
3. Sources beyond `maxDistance × 1.1` release their voice (hysteresis); looping ones restart when back in range.
4. Positions and velocities updated for spatial voices (section 2).
5. Finished one-shots release their slots.
6. Procedural emitters push their `params` to their nodes.
7. Voice limiting (section 6).

### 3.3 Python API

```python
audio = engine.audio

clip = audio.load("sounds/punt.wav")                          # decoded, cached by path
music = audio.load("music/orbit.ogg", stream=True)

# One-shots: positioned in world space (doubles), or not spatial
audio.play(clip, position=world_point, bus="world", volume=0.9, pitch=1.05)
audio.play(audio.load("ui/click.wav"), bus="ui", spatial=False)
audio.play(clip, position=impact, delay=distance / 340.0)     # scheduled start

# Components on entities
engine.scene.add_audio_source(entity, clip, loop=True, autoplay=True,
                              min_distance=3.0, max_distance=300.0)
engine.scene.add_procedural_audio(car_entity, "hover_car", bus="world")
engine.scene.set_procedural_param(car_entity, "throttle", 0.6)

# Buses
audio.set_bus_volume("world", 0.8, fade=0.3)
audio.set_world_lowpass(4000.0, fade=0.2)
audio.pause_bus("world")                                      # the System menu
audio.resume_bus("world")
```

Positions given to Python's `play` are planet-space doubles; the binding subtracts the camera itself, so callers never think about it.

---

## 4. Mix structure

### 4.1 Buses

```
voices ─┬─ world ── world low-pass ─┐
        ├─ suit ────────────────────┤
        ├─ ui ──────────────────────┼── master ── device
        └─ music ───────────────────┘
```

| Bus | What | Pauses with Esc | Affected by air |
|---|---|---|---|
| world | everything outside the player: wind, the car, impacts, rocks, meteors, other props | yes | yes |
| suit | breathing, servos, footsteps as heard through the suit, gun hum, contact sounds on the moon | yes | no |
| ui | slate, toasts, menus, key clicks | no | no |
| music | streams | no (ducks) | no |

Footsteps and the gun go on suit rather than world because they're heard through your own body; on the moon that's the only way you hear them at all.

### 4.2 States

| State | world | suit | music | low-pass |
|---|---|---|---|---|
| Playing | 1.0 | 1.0 | 0.6 | from air (5.1) |
| Slate open | 0.6 | 0.9 | 0.5 | min(air, 2,500 Hz) |
| System menu (Esc) | paused | paused | 0.4 | — |
| Main menu | stopped | stopped | 0.8 | — |
| Warp > 2× | fades to 0 over 0.5 s | 0.6 | 0.6 | — |

### 4.3 Smoothing

Every bus volume and cutoff change goes through a one-pole smoother (time constant 50–300 ms) in the bus's own node or via `ma_sound_group_set_fade_in_milliseconds`. Instant jumps click and sound cheap; the slate opening with a 220 ms duck matches its 220 ms visual.

---

## 5. World rules

### 5.1 Air density

`world.physics.density(position)` already exists. Use the density at the listener, normalised to sea level, `d = ρ / ρ0`:

```python
def air_audio(d):
    if d <= 1e-4:                       # vacuum: moon, orbit
        return 0.0, 200.0
    gain = min(1.0, 0.25 + 0.75 * d ** 0.5)
    cutoff = 400.0 + 19_600.0 * min(1.0, d) ** 0.7
    return gain, cutoff
```

- The world bus gain and low-pass cutoff follow it each frame (smoothed).
- At 0 the world bus is silent. That's the moon and orbit: you hear the suit bus, and contact sounds routed to it.
- **Contact sounds in vacuum:** a rock landing near you on the moon plays on the suit bus, low-passed hard and at low volume, only if within a few metres. Something you hold with the gun or stand on transmits; something 30 m away doesn't.

### 5.2 Speed of sound

Impacts, meteor booms and distant crashes start late by `distance / 343` seconds:

```python
audio.play(boom, position=impact, delay=length(sub(impact, camera)) / 343.0)
```

In the engine, `delay` becomes `ma_sound_set_start_time_in_pcm_frames(engine_time + delay × sample_rate)` on a voice started now. Only in air; in vacuum the event has no sound unless it's a contact sound.

### 5.3 Distance and loudness

- Attenuation model: inverse, with `minDistance` as the reference. A meteor boom has a reference of 200 m and a max of 15 km; a rock click 0.5 m and 40 m.
- Air absorbs highs over distance. Give each spatial voice a per-voice low-pass with cutoff falling with distance (e.g. 18 kHz at 10 m to 2 kHz at 5 km). This sells scale more than any other trick: a far meteor is a rumble, a near one a crack.

### 5.4 Time warp

- Above 2× the world bus fades to silence and procedural nodes freeze their state, so nothing plays back at a silly speed.
- Scheduled starts (5.2) are in real time, not world time. A boom from a meteor during warp is dropped rather than delayed.

### 5.5 Wind

The same `wind_at(position)` that pushes the car drives the wind node (5.6). Inside a storm, its gain and gustiness rise; rain is a looping bed on the world bus whose gain follows the storm's coverage at the listener.

### 5.6 Procedural nodes

Each is a custom `ma_node` with a `ma_node_vtable` whose `onProcess` fills the output from a few smoothed parameters. They're cheap (a few filtered noise and oscillator voices each).

| Node | Parameters | Sound |
|---|---|---|
| Wind | speed (m/s), gustiness 0..1, air density | pink noise through a band-pass whose centre and gain rise with speed; slow LFOs on both for gusts; a second, higher whistle band above 15 m/s |
| Hover car | throttle 0..1, height above ground (m), boost 0..1, fuel empty | two detuned saw oscillators low-passed (hum) with pitch from throttle; noise band for the cushion that rises as height falls below 2 m; boost adds a brighter layer; empty tank drops the hum to a sputter |
| Gravity gun | state (idle, target, holding, charging), charge 0..1, held mass | quiet idle hum; a tone that rises with charge; holding adds a slow wobble whose depth grows with mass; punt is a one-shot, not this node |
| Reentry | glow 0..1 (from `flight.reentry_glow`) | brown noise rumble plus crackle (sparse random clicks through a band-pass), gain from glow |
| Suit breathing | exertion 0..1, reserve 0..1 | a looped breath sample with rate and gain from exertion; below 20 % reserve the breath gets shorter and a filtered heartbeat joins |

```cpp
struct WindNode {
    ma_node_base base;
    std::atomic<float> speed{0}, gust{0}, density{1};
    float smoothSpeed = 0, smoothGust = 0;
    PinkNoise noise;
    Biquad band, whistle;
    float lfoPhase = 0;
};

static void windProcess(ma_node* node, const float** in, ma_uint32* inFrames,
                        float** out, ma_uint32* outFrames) {
    auto* w = reinterpret_cast<WindNode*>(node);
    const float target = w->speed.load(std::memory_order_relaxed);
    float* o = out[0];
    for (ma_uint32 i = 0; i < *outFrames; ++i) {
        w->smoothSpeed += (target - w->smoothSpeed) * 0.0005f;          // ~40 ms at 48 kHz
        float gust = 1.0f + w->smoothGust * std::sin(w->lfoPhase);
        w->lfoPhase += 0.7f * 6.2831853f / 48000.0f;
        float n = w->noise.next();
        w->band.setBandPass(150.0f + 25.0f * w->smoothSpeed * gust, 0.7f);
        float s = w->band.process(n) * std::min(1.0f, w->smoothSpeed / 25.0f);
        o[2 * i] = o[2 * i + 1] = s * w->density.load(std::memory_order_relaxed);
    }
}
```

(Set filter coefficients per block, not per sample, in the real version.)

Procedural nodes for spatial things (the hover car seen from outside, a storm's wind) attach to a spatializer: either output into an `ma_sound` set up as a data-source wrapper, or run miniaudio's spatializer struct (`ma_spatializer`) inside the node with the source's relative position. Non-spatial ones (wind at the listener, breathing, the gun) attach straight to their bus.

---

## 6. Voice management

- Pool of 64 voices. Categories with caps: rocks and chunks 12, footsteps 4, UI 8, impacts 8, ambience 6, everything else shares the rest.
- When a category is full, the new sound takes the slot of the quietest voice in it (estimated loudness = volume × attenuation at its current distance) if the new one is louder; otherwise it's dropped.
- Rock splitting spawns many contacts at once. Collapse strikes on the same body within 50 ms into one sound, with volume from the largest impulse.
- `watch_strikes` already reports speed and momentum. Map momentum to volume and speed to pitch and sample choice (soft, medium, hard), with a small random pitch spread (±5 %) so repeats don't sound identical.

---

## 7. The game's sound table

Keep it data, like `game_data.py`:

```python
SOUNDS = {
    "punt":        {"files": ["gun_punt_1.wav", "gun_punt_2.wav"], "bus": "suit", "volume": 0.9, "pitch_spread": 0.05},
    "punt_charged":{"files": ["gun_charged.wav"], "bus": "suit", "volume": 1.0},
    "grab":        {"files": ["gun_grab.wav"], "bus": "suit", "volume": 0.6},
    "rock_hit":    {"files": ["rock_hit_soft.wav", "rock_hit_hard.wav"], "bus": "world", "category": "rocks",
                    "min_distance": 1.0, "max_distance": 60.0},
    "boulder_split": {"files": ["boulder_split.wav"], "bus": "world", "category": "impacts",
                      "min_distance": 4.0, "max_distance": 300.0},
    "meteor_boom": {"files": ["boom_far.wav", "boom_near.wav"], "bus": "world", "category": "impacts",
                    "min_distance": 200.0, "max_distance": 15_000.0, "delay_by_distance": True},
    "footstep":    {"files": {"grass": [...], "rock": [...], "sand": [...], "snow": [...], "moon": [...]},
                    "bus": "suit", "category": "footsteps"},
    "slate_open":  {"files": ["slate_open.wav"], "bus": "ui"},
    "slate_close": {"files": ["slate_close.wav"], "bus": "ui"},
    "toast":       {"files": ["toast.wav"], "bus": "ui", "volume": 0.5},
    "sell":        {"files": ["credits.wav"], "bus": "ui"},
    "pads_closed": {"files": ["warning.wav"], "bus": "ui"},
}
```

- **Footsteps** choose their set from the biome layer under the player (the probe already returns climate; the dominant ground layer is one lookup away). One step per 1.6 m walked, the same stride as the head bob.
- **Music** in a few stems: an exploration bed, a calmer outpost track, an orbit track. Crossfade over 4 s by state. Keep it sparse; the world's own sound is the point.
- Free sources to start with: freesound.org (check each licence), Sonniss's yearly GDC bundles, and your own Suno corpus for music if you want something with your own character.

---

## 8. Steam Audio later

When you want HRTF (direction on headphones), occlusion by terrain and reverb in craters:

- Steam Audio (Apache 2.0) processes a mono input into binaural stereo given a direction. Wrap `iplBinauralEffectApply` in a custom miniaudio node that replaces miniaudio's own panning for spatial voices on the world bus.
- Occlusion: one ray from the listener to the source through Jolt's `cast_ray` or the terrain probes, a few times a second per audible voice; lower the voice's gain and cutoff when blocked.
- Reverb: a crater or a canyon is a good place for a cheap convolution or a parametric reverb send with size from a few probe distances around the listener.

Nothing in sections 1–7 changes for this; it's an extra node in the same graph.

---

## 9. Checks

In the style of the existing `--*-check` flags:

- `--audio-check`: start the engine with a null backend (`ma_backend_null`), play a 1 kHz tone at 10 m and 100 m with inverse attenuation, capture the output through a capture node, and check the RMS ratio within 1 dB; check a voice scheduled with a 0.5 s delay starts within 1 ms of that; check that pausing the world bus silences it while ui keeps playing.
- `--vacuum-check`: on the moon, drop a crate 20 m away and one 2 m away; the world bus stays silent, and only the near one is heard on the suit bus.
- `--voice-check`: split 20 boulders in one frame; at most the rocks category's cap of voices plays, the loudest ones, and the frame time stays within budget.
- Unit tests in Python: `air_audio` at sea level, 10 km and vacuum; the delay for 3.4 km is 10 s; sound table entries load and refer to existing files.

---

## 10. Milestones

1. **Engine.** miniaudio vendored, `AudioEngine` with buses, clip cache and voice pool; a test tone on the null backend; `--audio-check`.
2. **ECS and Python.** Components, `AudioSystem`, listener at the origin with camera-relative sources, the Python API with `play(..., position, delay)`.
3. **First sounds.** Gravity gun (grab, punt, charged), rock hits from `watch_strikes`, footsteps by ground layer, slate and toast UI sounds.
4. **Air.** Density-driven world bus, vacuum on the moon with contact sounds on the suit bus; `--vacuum-check`.
5. **Procedural nodes.** Wind, hover car, gun hum, reentry, breathing.
6. **Events.** Meteor booms with delay by distance and distance low-pass, storms with rain and stronger wind, pads-closed warnings.
7. **States.** Slate duck, Esc pause, main menu music, warp fade, music crossfades.
8. **Voices.** Categories, caps, collapsing strikes; `--voice-check`.
9. **Later.** Steam Audio HRTF and occlusion.

---

## 11. Pitfalls

- **Clicks** from unsmoothed parameter jumps or voices stopped mid-sample. Fade every stop over at least 5 ms; smooth every parameter in the node.
- **Allocating or locking in the audio thread.** `onProcess` must not allocate, log, or take a mutex. Parameters in atomics, buffers preallocated.
- **Large positions.** Never hand miniaudio a planet-space position. Subtract the camera in double first, every frame, for every voice.
- **Doppler on the moon** from using absolute velocities: the whole moon moves at 288 m/s. Velocities relative to the listener's body.
- **Stale handles** from Python after a voice slot is reused: generation counters in `VoiceId`.
- **Hundreds of contact sounds** when rocks split or settle: collapse strikes per body and per 50 ms, and cap the category.
- **Device changes** (headphones unplugged): handle miniaudio's device notification and reinitialise the device without rebuilding the graph.
- **Time warp** speeding up procedural nodes that integrate time: they should use real time, or freeze while the world bus is faded out.

---

## 12. References

- miniaudio — https://miniaud.io and https://github.com/mackron/miniaudio (the single header holds the full documentation; `examples/` has node graph and custom node samples)
- Steam Audio — https://valvesoftware.github.io/steam-audio/
- OpenAL Soft (alternative with HRTF) — https://openal-soft.org
- Andy Farnell, *Designing Sound* (MIT Press) — procedural wind, engines and impacts
- Previous guides: `planet_flight_mvp.md`, `on_foot_and_gravity_gun.md`, `game_and_world_mechanics.md`
