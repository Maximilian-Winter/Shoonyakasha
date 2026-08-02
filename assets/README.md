# Shared example assets

One copy of everything the examples and Python demos load, found at runtime by
walking up from the working directory or the executable (`Core/AssetPaths.h`).
Examples name paths relative to this directory:

```cpp
config.hdrEnvironmentPath = "env/kloofendal_misty_1k.hdr";
loadGltfScene("models/Box.gltf");
```

Set `SHOONYAKASHA_ASSET_DIR` to point somewhere else. `.shoonyakasha-assets` is
the marker that identifies this directory; do not delete it.

## Everything here is committed, and every example runs on a fresh clone

The environment maps are 1024×512 downsamples, ~2 MB each rather than ~99 MB.
They are enough to see IBL working. For the full-resolution originals:

```
python tools/fetch_assets.py --list
python tools/fetch_assets.py env
```

then point the example at `env/kloofendal_28d_misty_4k.hdr` instead.

The small versions were produced from the originals with
`tools/downsample_hdr.py`, which is dependency-free so anyone can regenerate or
re-scale them.

## Provenance and licence

| Asset | Source | Licence | Notes |
|---|---|---|---|
| `env/kloofendal_misty_1k.hdr` | [Poly Haven — Kloofendal 28d Misty](https://polyhaven.com/a/kloofendal_28d_misty) | CC0 | Downsampled from the 8k original |
| `env/farm_sunset_1k.hdr` | [Poly Haven — Farm Sunset](https://polyhaven.com/a/farm_sunset) | CC0 | Downsampled from the 8k original |
| `env/charolettenbrunn_park_1k.hdr` | [Poly Haven — Charolettenbrunn Park](https://polyhaven.com/a/charolettenbrunn_park) | CC0 | Downsampled from the 4k original |
| `models/Box.gltf`, `Box0.bin` | [Khronos glTF-Sample-Assets](https://github.com/KhronosGroup/glTF-Sample-Assets) | CC0 (per that repo's `LICENSE.md`) | |
| `models/instanced_boxes.gltf` | Written for `examples/cpp/api/instancing_test`, shares `Box0.bin` | Same as `Box0.bin` | 19 nodes, one mesh |
| `models/Fox.glb` | [Khronos glTF-Sample-Assets](https://github.com/KhronosGroup/glTF-Sample-Assets) | CC0 | Skinned, animated |
| `textures/*.png` | Authored for `examples/python/games_2d/full_showcase` | Project licence (MIT) | |
| `fonts/Roboto-Regular.ttf` | [Roboto](https://github.com/googlefonts/roboto-classic) | SIL Open Font License 1.1 — see `fonts/OFL.txt` | |

**The Poly Haven attributions are inferred from the filenames**, which match
their assets, and Poly Haven publishes everything CC0. No provenance file
travelled with the copies that were in this repository, so this table records
what is believed rather than what is documented. Worth confirming once against
the pages linked above.

## Not committed

| Asset | Why | How to get it |
|---|---|---|
| Full-resolution HDRs (4k/8k, 25–99 MB each) | Size | `python tools/fetch_assets.py env` |
| Intel Sponza (`NewSponza_*`, ~450 MB with textures) | Size **and** licence | `python tools/fetch_assets.py sponza` prints the page |

**Read Sponza's licence before redistributing anything built on it.** The
`credits_license.txt` that ships with the add-on package says two different
things: a preamble limiting use to "personal use and educational use. Limited
commercial use for marketing and print purposes", followed by the full text of
CC BY 4.0, which permits commercial use and redistribution with attribution.
Those readings do not agree. Intel's own page is the authority. Whichever
applies, attribution is required:

```
Sponza 2022 Scene, commissioned by Frank Meinl, sponsored by Anton Kaplanyan.
Intel Sample Library.
```

Examples that want Sponza should keep degrading to `models/Box.gltf` when it is
absent, as `declarative_sponza_test` does, so a fresh clone still runs.
