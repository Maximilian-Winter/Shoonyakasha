"""Tests for sk.Audio (include/Audio/AudioEngine.h) without a device: clips
from samples and from WAV files, spatial voices placed relative to the
listener in double precision, delays, buses by name, pausing and the
world bus's low-pass, voices ending and stopping, and read(). Where this
interpreter's shoonyakasha has no extension (another test put the source
tree first on sys.path), the tests run in a fresh interpreter, which
imports the installed package.

    python -m unittest discover -s tests/python
"""

import array
import math
import os
import pathlib
import struct
import subprocess
import sys
import tempfile
import unittest

import shoonyakasha as sk

RATE = 48000


def tone(hz, seconds, amplitude=0.5):
    return [amplitude * math.cos(2.0 * math.pi * hz * i / RATE) for i in range(int(seconds * RATE))]


def rms(samples, channel=None):
    picked = samples if channel is None else samples[channel::2]
    return math.sqrt(sum(s * s for s in picked) / len(picked)) if picked else 0.0


def write_wav(path, samples):
    """A mono 16-bit WAV file of the samples at RATE."""
    data = struct.pack("<%dh" % len(samples), *(int(round(s * 32767)) for s in samples))
    with open(path, "wb") as f:
        f.write(b"RIFF" + struct.pack("<I", 36 + len(data)) + b"WAVEfmt ")
        f.write(struct.pack("<IHHIIHH", 16, 1, 1, RATE, RATE * 2, 2, 16))
        f.write(b"data" + struct.pack("<I", len(data)) + data)


class AudioTest(unittest.TestCase):
    def setUp(self):
        if not sk.extension_available():
            if __name__ == "__main__":
                self.fail("no engine extension in a fresh interpreter")
            self.skipTest("runs in a fresh interpreter (test_in_fresh_interpreter)")
        self.audio = sk.Audio(device=False)
        self.clip = self.audio.load_samples(tone(1000.0, 1.0))

    def test_without_a_device(self):
        self.assertFalse(self.audio.has_device)
        self.assertEqual(self.audio.sample_rate, RATE)
        self.assertEqual(self.audio.channels, 2)
        out = self.audio.read(480)
        self.assertIsInstance(out, array.array)
        self.assertEqual(len(out), 960)
        self.assertAlmostEqual(self.audio.time, 0.01)

    def test_samples_from_a_buffer_or_a_list(self):
        buffer = array.array("f", tone(1000.0, 0.5))
        clip = self.audio.load_samples(buffer)
        self.assertAlmostEqual(self.audio.clip_length(clip), 0.5)
        stereo = self.audio.load_samples([0.1, -0.1] * 4800, channels=2)
        self.assertAlmostEqual(self.audio.clip_length(stereo), 0.1)
        with self.assertRaises(ValueError):
            self.audio.load_samples([0.1, 0.2, 0.3], channels=2)

    def test_plain_voice_plays_and_ends(self):
        clip = self.audio.load_samples(tone(1000.0, 0.05))
        voice = self.audio.play(clip, bus="ui")
        self.assertNotEqual(voice, 0)
        self.assertTrue(self.audio.active(voice))
        self.assertGreater(rms(self.audio.read(2400)), 0.1)
        self.audio.read(4800)
        self.assertFalse(self.audio.active(voice))
        self.assertEqual(self.audio.active_voices, 0)

    def test_positions_are_taken_relative_to_the_listener_in_double_precision(self):
        far = 1.0e8          # float32 spacing there is 8 m
        self.audio.set_listener((far, 0.0, 0.0), (0.0, 0.0, -1.0), (0.0, 1.0, 0.0))
        self.audio.play(self.clip, position=(far + 3.0, 0.0, 0.0), loop=True)
        self.audio.read(4800)
        out = self.audio.read(4800)
        self.assertGreater(rms(out, 1), 2.0 * rms(out, 0))

    def test_set_voice_moves_it_to_the_other_side(self):
        voice = self.audio.play(self.clip, position=(3.0, 0.0, 0.0), loop=True)
        self.audio.read(4800)
        self.audio.set_voice(voice, position=(-3.0, 0.0, 0.0))
        self.audio.read(4800)
        out = self.audio.read(4800)
        self.assertGreater(rms(out, 0), 2.0 * rms(out, 1))

    def test_level_falls_off_with_distance(self):
        def level(distance):
            audio = sk.Audio(device=False)
            clip = audio.load_samples(tone(1000.0, 1.0))
            audio.play(clip, position=(0.0, 0.0, -distance), loop=True, max_distance=1000.0)
            audio.read(4800)
            return rms(audio.read(9600))
        self.assertAlmostEqual(20.0 * math.log10(level(10.0) / level(100.0)), 20.0, delta=1.0)

    def test_delay(self):
        self.audio.play(self.clip, bus="ui", delay=0.25)
        out = self.audio.read(RATE // 2)
        first = next(i for i, s in enumerate(out) if abs(s) > 1e-6) // 2
        self.assertAlmostEqual(first, RATE // 4, delta=RATE // 1000)

    def test_buses(self):
        world = self.audio.play(self.clip, bus="world", loop=True)
        self.audio.pause_bus("world")
        self.assertTrue(self.audio.bus_paused("world"))
        self.audio.read(480)
        self.assertEqual(rms(self.audio.read(4800)), 0.0)
        self.assertTrue(self.audio.active(world))
        self.audio.resume_bus("world")
        self.audio.set_bus_volume("world", 0.5)
        self.assertAlmostEqual(self.audio.bus_volume("world"), 0.5)
        self.audio.read(960)
        self.assertAlmostEqual(rms(self.audio.read(4800)), 0.5 * 0.5 / math.sqrt(2.0), delta=0.01)
        with self.assertRaises(ValueError):
            self.audio.play(self.clip, bus="drums")

    def test_world_lowpass(self):
        high = self.audio.load_samples(tone(8000.0, 1.0))
        self.audio.play(high, bus="world", loop=True)
        self.audio.read(480)
        open_level = rms(self.audio.read(4800))
        self.audio.set_world_lowpass(500.0)
        self.assertAlmostEqual(self.audio.world_lowpass, 500.0)
        self.audio.read(960)
        self.assertLess(rms(self.audio.read(4800)), 0.05 * open_level)

    def test_voice_filter(self):
        high = self.audio.load_samples(tone(8000.0, 1.0))
        voice = self.audio.play(high, bus="ui", loop=True, filtered=True)
        self.audio.read(480)
        open_level = rms(self.audio.read(4800))
        self.audio.set_voice(voice, lowpass=500.0)
        self.audio.read(960)
        self.assertLess(rms(self.audio.read(4800)), 0.05 * open_level)
        self.audio.set_voice(voice, highpass=2000.0)
        self.audio.read(960)
        self.assertAlmostEqual(rms(self.audio.read(4800)) / open_level, 1.0, delta=0.05)
        self.audio.stop(voice)
        low = self.audio.load_samples(tone(200.0, 1.0))
        voice = self.audio.play(low, bus="ui", loop=True, highpass=3000.0)
        self.audio.read(480)
        self.assertLess(rms(self.audio.read(4800)), 0.05 * open_level)
        self.audio.stop(voice)
        plain = self.audio.play(high, bus="ui", loop=True)
        self.audio.set_voice(plain, lowpass=500.0)
        self.audio.read(960)
        self.assertAlmostEqual(rms(self.audio.read(4800)) / open_level, 1.0, delta=0.05)

    def test_categories_and_priority(self):
        self.audio.set_category_limit("rocks", 2)
        self.assertEqual(self.audio.category_limit("rocks"), 2)
        quiet = self.audio.play(self.clip, bus="ui", loop=True, volume=0.2, category="rocks")
        self.audio.play(self.clip, bus="ui", loop=True, volume=0.5, category="rocks")
        self.assertEqual(self.audio.play(self.clip, bus="ui", loop=True, volume=0.1, category="rocks"), 0)
        self.assertNotEqual(self.audio.play(self.clip, bus="ui", loop=True, volume=1.0, category="rocks"), 0)
        self.audio.read(480)
        self.assertFalse(self.audio.active(quiet))
        self.assertEqual(self.audio.voices_of("rocks"), 2)
        self.assertEqual(self.audio.voices_of("ui"), 0)
        audio = sk.Audio(device=False, voices=1)
        clip = audio.load_samples(tone(1000.0, 1.0))
        low = audio.play(clip, loop=True, priority=10)
        self.assertEqual(audio.play(clip, loop=True, priority=5), 0)
        self.assertNotEqual(audio.play(clip, loop=True, priority=200), 0)
        self.assertFalse(audio.active(low))
        for k in range(15):
            audio.set_category_limit("c%d" % k, 1)
        with self.assertRaises(ValueError):
            audio.set_category_limit("one too many", 1)

    def test_binaural(self):
        self.assertTrue(self.audio.binaural_available)
        self.assertFalse(self.audio.binaural)
        self.audio.binaural = True
        self.assertTrue(self.audio.binaural)
        noise = self.audio.load_samples([0.5 * math.sin(i * i * 0.37) for i in range(RATE)])
        self.audio.set_listener((0.0, 0.0, 0.0), (0.0, 0.0, -1.0), (0.0, 1.0, 0.0))
        self.audio.play(noise, position=(5.0, 0.0, 0.0), loop=True)
        self.audio.read(4800)
        out = self.audio.read(9600)
        self.assertGreater(rms(out, 1), 1.5 * rms(out, 0))

    def test_reverb(self):
        self.assertTrue(self.audio.reverb_available)
        self.assertEqual(self.audio.reverb_wet, 0.0)
        self.audio.set_reverb(1.2, 0.5)
        for t in self.audio.reverb_times:
            self.assertAlmostEqual(t, 1.2, places=5)
        self.audio.set_reverb((1.5, 1.0, 0.5), 0.6)
        self.assertAlmostEqual(self.audio.reverb_times[2], 0.5)
        self.assertAlmostEqual(self.audio.reverb_wet, 0.6)
        self.audio.read(RATE // 2)
        click = self.audio.load_samples([0.5 * math.sin(i * i * 0.37) for i in range(480)])
        self.audio.play(click, bus="world")
        self.audio.read(RATE // 5)
        self.assertGreater(rms(self.audio.read(RATE // 5)), 1e-4)

    def test_stop_with_a_fade_and_a_stale_voice(self):
        audio = sk.Audio(device=False, voices=1)
        clip = audio.load_samples(tone(1000.0, 1.0))
        first = audio.play(clip, loop=True)
        self.assertEqual(audio.play(clip), 0)
        audio.stop(first, fade=0.05)
        audio.read(RATE // 10)
        self.assertFalse(audio.active(first))
        second = audio.play(clip, loop=True)
        self.assertNotIn(second, (0, first))
        audio.stop(first)
        self.assertTrue(audio.active(second))

    def test_wav_file_decoded_and_streamed(self):
        with tempfile.TemporaryDirectory() as folder:
            path = pathlib.Path(folder) / "tone.wav"
            write_wav(path, tone(1000.0, 0.5))
            for stream in (False, True):
                clip = self.audio.load(path, stream=stream)
                self.assertAlmostEqual(self.audio.clip_length(clip), 0.5, delta=1e-3)
                voice = self.audio.play(clip, bus="ui")
                self.audio.read(480)
                self.assertGreater(rms(self.audio.read(4800)), 0.1)
                self.audio.stop(voice)
                self.audio.unload(clip)
            del self.audio
        with self.assertRaises(RuntimeError):
            sk.Audio(device=False).load(os.path.join(folder, "missing.wav"))


class FreshInterpreterTest(unittest.TestCase):
    def test_in_fresh_interpreter(self):
        if sk.extension_available() or __name__ == "__main__":
            self.skipTest("ran in this interpreter")
        here = os.path.dirname(os.path.abspath(__file__))
        result = subprocess.run([sys.executable, os.path.abspath(__file__)], cwd=here, capture_output=True,
                                text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
