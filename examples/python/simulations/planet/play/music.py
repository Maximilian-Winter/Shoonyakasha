"""
The walker's music: a track for where the game is (`track`), "music_menu"
on the start's screen, "music_orbit" in space mode or on the moon,
"music_outpost" on an outpost's pad, else "music_explore". A track wanted
for SETTLE seconds (the menu's at once) takes over from the last, the
two crossfading over CROSSFADE. Each is an entry of MUSIC (as sounds.SOUNDS's, streamed from
its file in sounds/ when there is one), on the music bus, whose volume
sounds.update_mix sets; its placeholder (`placeholder`) is sparse, two
chords swelling and fading with silence between, over LENGTH seconds.
"""

import numpy as np

from play.core import state, world
from play import economy, sounds

RATE = 12_000           # Hz the placeholders are made at: the chords have nothing above 600 Hz
LENGTH = 32.0            # s, a placeholder track
SETTLE = 2.0             # s a track must be wanted before it plays
CROSSFADE = 4.0          # s two tracks crossfade over

CHORDS = {               # a track: its two chords' notes (Hz)
    "music_explore": ((110.0, 164.8, 220.0, 246.9), (87.3, 130.8, 174.6, 261.6)),
    "music_outpost": ((130.8, 164.8, 196.0, 261.6), (98.0, 146.8, 196.0, 246.9)),
    "music_orbit":   ((73.4, 110.0, 146.8, 220.0, 329.6), (58.3, 116.5, 174.6, 233.1)),
    "music_menu":    ((82.4, 123.5, 164.8, 246.9), (65.4, 130.8, 196.0, 261.6)),
}


def placeholder(name):
    """Mono samples at RATE for the track `name`: its first chord swelling
    over 0 to 14 s, its second over 18 to 30 s, silence between and after,
    each note with a slightly detuned twin and a soft octave."""
    t = np.arange(int(LENGTH * RATE)) / RATE
    x = np.zeros_like(t)
    for chord, start, end in zip(CHORDS[name], (0.0, 18.0), (14.0, 30.0)):
        inside = (t >= start) & (t < end)
        envelope = np.where(inside, np.sin(np.pi * (t - start) / (end - start)), 0.0) ** 2
        tone = np.zeros_like(t)
        for hz in chord:
            tone += (np.sin(2.0 * np.pi * hz * t) + np.sin(2.0 * np.pi * (hz + 0.3) * t)
                     + 0.15 * np.sin(2.0 * np.pi * 2.0 * hz * t))
        x += envelope * tone
    peak = float(np.max(np.abs(x))) or 1.0
    return (0.6 * x / peak).astype(np.float32)


MUSIC = {name: {"bus": "music", "volume": 0.7, "stream": True, "make": placeholder, "rate": RATE, "priority": 250}
         for name in CHORDS}


def track():
    """The track for where the game is (the module's docstring)."""
    if state.session.starting:
        return "music_menu"
    if state.space.ship is not None or world.body_under(state.view.position) is not world.planet:
        return "music_orbit"
    if economy.at_outpost() is not None:
        return "music_outpost"
    return "music_explore"


def update(dt):
    s = state.sound
    if s.audio is None:
        return
    wanted = track()
    if wanted != s.track_wanted:
        s.track_wanted, s.track_since = wanted, 0.0
    s.track_since += dt
    if wanted == s.track or (s.track is not None and wanted != "music_menu" and s.track_since < SETTLE):
        return
    if s.music_voice:
        s.audio.stop(s.music_voice, fade=CROSSFADE)
    s.music_voice = sounds.play(wanted, loop=True, fade_in=CROSSFADE)
    s.track = wanted
