# Pong

A complete small game written against the Python bindings — no C++, no build
step beyond the extension itself.

```
cd examples/pong_game
python pong.py
```

| key | |
|---|---|
| `W` / `S`, or `↑` / `↓` | move your paddle (left, orange) |
| `Space` | serve the first ball of a match |
| `R` | restart |
| `P` | screenshot |
| `V` | start/stop recording (needs ffmpeg) |

First to 7 wins, or whoever leads when the three minutes are up.

## Assets

The art is **Simple ping pong 2d game assets** by Esoe B.Studios, from
[itch.io](https://myebstudios.itch.io/simple-ping-pong-assets). It is not in
this repository — download it and extract it into `assets/`, so that
`assets/arts/Ball.png` exists. See `pong-game-assets-link.txt`.

The font is Roboto (SIL Open Font License), which ships with that pack.

## What it shows

Everything on screen is a screen-space sprite drawn by one pass — the same
`sprite_geometry` pass `sprite_ui_test` uses, in `pong_pipeline.json`. There is
no 3D geometry, no lighting and no IBL, which makes this close to the smallest
useful thing the engine can be asked to do.

A few things in it are worth stealing:

**One virtual canvas, mapped once.** Every coordinate in the file is in a fixed
800×500 space. `_mapping()` turns that into window pixels, so resizing the
window letterboxes the game rather than distorting it, and the game logic never
learns the window size. Only `place()` and `place_text()` know about pixels.

**Text baselines are not sprite centres.** A sprite's anchor offset is its
centre; a label's is its baseline, because glyph bearings are measured from
there. `place_text()` names its parameter `baseline_y` so the difference is
visible at the call site rather than discovered by nudging numbers.

**Draw order is explicit.** `Z_BOARD` through `Z_TEXT` — sprites default to 0
and so does text, so anything overlapping needs a number. Text needs
`set_text_sort_key`; `set_sort_key` does not reach a label's glyphs.

**One texture, mirrored.** `ScoreBar.png` is the left half of the bar with its
right edge cut on a diagonal. The right half is the same texture with a flipped
UV rect, which is what leaves the notch the art was drawn around.

**The opponent is deliberately flawed.** It reacts on a delay, ignores errors
inside a dead zone, and moves slower than the ball's top speed. A tracker
without those is unbeatable, which is not the same thing as difficult.
