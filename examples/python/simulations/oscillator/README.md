# Driven oscillator lesson

Run with the installed `shoonyakasha` package, like the other Python examples.
From this directory, run `python oscillator.py`. From the repository root:

```sh
python examples/python/simulations/oscillator/oscillator.py
python examples/python/simulations/oscillator/oscillator.py --smoke --screenshot oscillator.png
python examples/python/simulations/oscillator/oscillator.py --video resonance.mp4
python examples/python/simulations/oscillator/oscillator.py --video short.mp4 --duration 2 --width 1280 --height 720
```

The Python model integrates `m*x'' + damping*x' + stiffness*x = sin(phase)` using
RK4 at 120 steps/s. Frequency is in Hz; drive amplitude is 1 N. The plot shows
displacement in metres. The diagram shows spring and driving forces; arrow lengths
are illustrative.

Pause to edit authored parameters, step, reset or seek. Seeking restores defaults
and replays; it does not preserve unscripted edits. Two 12-second chapters sweep
through resonance and increase damping. Previous and Next navigate chapter starts.
The same model, plot and captions drive interactive playback and video export.

Export requires ffmpeg, records a visible window and quits after finalization.
`--fps` defaults to 30. `--smoke` without video exercises pause, step and seek and
exits after 65 frames. See the [toolkit guide](../../../../docs/guides/python-toolkit.md).
