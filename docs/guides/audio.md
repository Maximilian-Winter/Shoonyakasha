# Audio

Shoonyakasha plays sound through [miniaudio](https://miniaud.io). `Audio::AudioEngine` (`include/Audio/AudioEngine.h`) holds the clips, a pool of voices, five buses and one listener; miniaudio mixes on its own thread. In Python it is `sk.Audio`, and the engine's own is `engine.audio`.

## The engine's audio, or one of your own

```python
import shoonyakasha as sk

audio = engine.audio                  # made on first use, on the default playback device
headless = sk.Audio(device=False)     # no device: read() pulls the mix
```

```cpp
// Include Audio/AudioEngine.h.
auto& audio = engine.getAudio();
Shoonyakasha::Audio::AudioConfig config;
config.device = false;
Shoonyakasha::Audio::AudioEngine headless(config);
```

`engine.audio` / `getAudio()` can be used before `run()`. When no playback device can be opened, the engine runs as with `device=False` (`has_device` is then false), so nothing is heard and voices only end when `read` pulls the mix. `sk.Audio` takes `device`, `sample_rate` (48000), `channels` (2) and `voices` (64). Construction raises `RuntimeError` when miniaudio cannot start at all.

Call an `Audio` from one thread.

## Clips

```python
punt = audio.load("sounds/punt.wav")                    # decoded now, shared by its voices
music = audio.load("music/orbit.flac", stream=True)     # each voice streams from disk
beep = audio.load_samples(samples, channels=1, sample_rate=48000)
audio.clip_length(punt)                                 # seconds
audio.unload(punt)                                      # its voices stop at once
```

Files are WAV, FLAC or MP3. `load` raises `RuntimeError` when a file cannot be read. `load_samples` takes interleaved float samples, as a float32 buffer (an `array.array("f")` or a NumPy array) or a sequence of numbers, and copies them. Clips are ints; 0 is no clip.

## Voices

```python
voice = audio.play(punt, position=point, bus="suit", volume=0.9, pitch=1.05)
audio.play(click, bus="ui")                               # no position: not spatial
audio.play(boom, position=impact, delay=distance / 343.0, min_distance=200.0, max_distance=15000.0)
loop = audio.play(hum, position=car, loop=True, fade_in=0.5)
audio.set_voice(loop, position=car, velocity=car_velocity, volume=0.6, pitch=1.2)
audio.stop(loop, fade=0.3)
audio.active(loop)                                        # still holds its slot
```

`play` returns a voice, or 0 when the clip does not exist or its category or the full pool keeps it from playing (see Categories and priority). A voice with a `position` is spatial:

- **Falloff:** its gain is `min_distance / (min_distance + rolloff * (d - min_distance))` beyond `min_distance`, with `d` held at `max_distance` beyond that.
- **Panning:** it is panned around the listener.
- **Doppler:** it is shifted by its `velocity` relative to the listener's, scaled by `doppler` (1 by default, 0 for none).

A voice without a position plays as recorded. `delay` starts the voice that many seconds of mix time after the call. `fade_in` fades it in over that many seconds. `stop` fades a voice out over `fade` seconds.

A filtered voice goes through its own two-pole low-pass and high-pass:

```python
wind = audio.play(noise, loop=True, filtered=True)          # both open
audio.play(rumble, loop=True, lowpass=400.0)                # a lowpass or highpass given: filtered
audio.set_voice(wind, lowpass=2500.0, highpass=120.0, filter_fade=0.1)
```

`set_voice` sets both cutoffs together when either is given (the other open; 0 is open too), reached over `filter_fade` seconds and moving evenly in octaves, held between 10 Hz and 0.45 of the sample rate. A voice played unfiltered cannot be filtered later; `set_voice`'s cutoffs do nothing to it. In C++ these are `PlayParams::filtered`, `lowpass` and `highpass`, and `AudioEngine::setVoiceFilter`.

## Categories and priority

```python
audio.set_category_limit("rocks", 12)
audio.play(clack, position=point, category="rocks")
audio.play(hum, loop=True, priority=200)
audio.voices_of("rocks")
```

A voice's loudness is its volume times its falloff where it is (1 when it is not spatial).

- **Categories:** with a category's limit reached, a new voice of it takes over the quietest of the category's voices if the new one is louder. That voice fades out over 5 ms. If the new voice is not louder, it is not played and `play` returns 0. A limit of 0 is no limit, and a voice with no category has none.
- **Priority:** priority runs from 0 to 255, 128 by default. With every voice taken, a voice that is fading out gives its slot up first. Otherwise the voice of the lowest priority (the quietest of those) gives its slot up to a new voice of higher priority, or of the same priority and louder. If none qualifies, the new voice is not played.
- **Names:** categories are named by strings, at most 15 of them. In C++ they are numbers 1 to 15 (`PlayParams::category`, `AudioEngine::setCategoryLimit`), and 0 is none.

A voice that ends, or has stopped and faded out, gives its slot back; its int then names nothing, so calls with it do nothing and `active` is false. `active` is true while a voice waits for its delay, plays or fades out. `active_voices` counts those voices.

## The listener and positions

```python
audio.set_listener(camera_position, forward, up, velocity=camera_velocity)
```

Positions are in your own frame and in double precision. `set_listener` records the listener's position. `play` and `set_voice` subtract it from a voice's position before the voice is handed to miniaudio, so a planet-sized frame keeps its precision at the listener.

A voice keeps the position it was last given relative to the listener. When the listener moves, call `set_voice` for each spatial voice every frame. The listener's right is `forward × up`.

In C++, positions given to `AudioEngine` are already relative to the listener, which is at the origin.

## Buses

Every voice plays into a bus: `"world"` (the default), `"suit"`, `"ui"` or `"music"`. Those four play into `"master"`. The world bus goes through a two-pole low-pass filter on its way there.

```python
audio.set_bus_volume("world", 0.6, fade=0.22)   # linear, over at least 5 ms
audio.pause_bus("world")                        # silent; its voices hold their place
audio.resume_bus("world")
audio.set_world_lowpass(2500.0, fade=0.2)       # moves evenly in octaves
```

The cutoff starts at, and is held below, 0.45 of the sample rate, and it is never under 10 Hz. A bus name other than these five raises `ValueError`.

## Binaural sound and reverb

Both run on [Steam Audio](https://valvesoftware.github.io/steam-audio/) and need two channels. `binaural_available` and `reverb_available` say whether they can be had.

```python
audio.binaural = True                      # spatial voices played from now on: through the HRTF
audio.set_reverb((1.5, 1.0, 0.6), 0.3)     # RT60 s of the low, mid and high bands, and the wet level
audio.set_reverb(0.8, 0.2)                 # one RT60 for every band
audio.set_reverb(0.5, 0.0)                 # off
```

- **Binaural:** while `binaural` is on, a spatial voice that is played goes mono through Steam Audio's default HRTF for its direction from the listener, in place of stereo panning. It keeps the same falloff and Doppler shift. It also goes through its own filter, like a filtered voice, so `set_voice`'s `lowpass` and `highpass` work on it. A voice keeps the way it was played, so turning `binaural` on or off affects only voices played after the change. It is off at first.
- **Reverb:** the reverb takes the world bus. Each of its channels goes through a parametric reverb with the three bands' RT60. The wet signal, eased to its level over about 0.3 s, is added beside the dry one. The suit, UI and music buses have no reverb.

Both work on blocks of 256 frames, so a binaural voice and the reverb are 256 frames late (5.3 ms at 48 kHz). In C++ these are `AudioEngine::setBinaural`, `binaural`, `binauralAvailable`, `setReverb`, `reverbTimes`, `reverbWet` and `reverbAvailable`.

## Without a device

```python
audio = sk.Audio(device=False)
clip = audio.load_samples(tone)
audio.play(clip, bus="ui")
out = audio.read(4800)       # array.array("f"), 4800 frames x channels, interleaved
audio.time                   # seconds of mix produced
```

Without a device the mix advances only when `read` is called. Delays, fades and the end of a voice are all counted in the frames it has produced. `read` raises `RuntimeError` on an `Audio` that has a device. The engine's own tests (`tests/unit/AudioEngineTest.cpp`, `tests/python/test_audio.py`) run this way.
