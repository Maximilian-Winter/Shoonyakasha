# Example screenshot capture record

These PNGs are authentic native frame captures, made on Windows on 2026-09-07. They contain the rendered client area only: no desktop, window borders, terminal, image generation, or compositing. Each example has its own file; the galleries reuse those files. Frames are captured at 1280×720 and displayed at 720 px in the guides and 400 px in the galleries, without stretching or duplicate thumbnails.

## Refresh

A Vulkan-capable desktop, the [C++ build dependencies](../../../BUILDING.md), and an installed native Python package are required. Run commands below from the repository root. Keep `assets/` limited to the bundled assets for this capture pass: if optional `models/NewSponza_Main_glTF_003.gltf` is installed there, the facade, particle-flow, and Python demo choose it instead of their box fallback.

The opt-in [preparation script](../../../tools/docs_capture/prepare.py) writes modified **copies** into ignored `build/docs-capture/`. A CMake project hook substitutes those copies in a separate build. Normal engine sources, APIs, examples, and builds are untouched. The copies add native frame saving and automatic exit, set a 1280×720 viewport, and apply the camera/asset choices below. Do not distribute these capture executables as the normal examples.

In a compiler-enabled PowerShell (for example, the Visual Studio Developer PowerShell):

```powershell
python tools/docs_capture/prepare.py
$repo = (Get-Location).Path.Replace('\', '/')
cmake -S . -B build/docs-capture/native -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_EXAMPLES=ON -DBUILD_TESTS=OFF -DBUILD_PYTHON=OFF -DSHOONYAKASHA_INSTALL=OFF "-DCMAKE_PROJECT_Shoonyakasha_INCLUDE=$repo/build/docs-capture/capture.cmake"
cmake --build build/docs-capture/native --config Release
python tools/docs_capture/run.py cpp
python tools/docs_capture/run.py python --python .venv/Scripts/python.exe
```

Supply the same toolchain/dependency options used by your normal build to the configure command; see [BUILDING.md](../../../BUILDING.md). For this capture, Ninja/MSVC 14.40 reused `cmake-build-release/vcpkg_installed` with `-DVCPKG_MANIFEST_INSTALL=OFF` and the vcpkg toolchain. `glslc` was on `PATH`. Python 3.12 used the locally built extension and its runtime DLLs via a staged package on `PYTHONPATH`.

To refresh just one preview:

```powershell
python tools/docs_capture/run.py cpp --only physics_test
python tools/docs_capture/run.py python --python .venv/Scripts/python.exe --only skinned_fox_demo
```

The [runner](../../../tools/docs_capture/run.py) changes into each documented source directory and invokes its executable or script. It sets `SHOONYAKASHA_ASSET_DIR` to the repository's `assets/`, retains logs and temporary PNGs in `build/docs-capture/`, and replaces a published image only after successful capture. A failed run leaves the existing image intact and returns a failure status. Python uses a [wrapper](../../../tools/docs_capture/python_capture.py) that chains the example's post-render callback and calls `engine.capture_screenshot()`. Since Python has no public quit method, the runner terminates only its own Python child after the screenshot callback confirms success.

C++ uses `ApplicationBase::captureScreenshot()` after rendering; the older standalone bloom and particle examples call `RenderTargetSaver` on the presented swapchain image. The isolated C++ copies exit through their normal cleanup path. No keyboard or mouse automation is needed. The physics copies call their existing **P** key handler once before the loop.

## Capture choices

The delay starts after initialization (Python: after the first rendered frame). Timing and randomized particles can vary with hardware; these are illustrative active frames, not image-comparison baselines.

| Language / file basename | Assets and interaction | Delay |
|---|---|---|
| C++ `facade_test` | Automatic bundled `Box.gltf` fallback. Camera position `(1.6, 1, 3.2)`, rotation `(-0.27, 0.46, 0)` radians for a closer three-quarter view. | 3 s |
| C++ `instancing_test` | Bundled `instanced_boxes.gltf`; normal run, without `--selftest`. | 3 s |
| C++ `declarative_sponza_test` | Replace the Sponza filename with `models/Box.gltf` **only in the generated source copy**. This is not an automatic fallback in the normal example. Bundled HDR environment; default camera and particles. | 3 s |
| C++ `bloom_test` | Procedural animated light orbs; no external models. | 3 s |
| C++ `particle_test` | Procedural 100,000-particle simulation; viewport reduced from its normal 4K request. | 3 s |
| C++ `particle_flow_example` | Automatic bundled-box fallback, bundled environment, active attractors. | 3 s |
| C++ `ssbo_data_flow_example` | Bundled box and environment; default buffer initialization, no `--load` argument. | 3 s |
| C++ `physics_test` | Procedural boxes, spheres, ground; **P** starts physics. | 1 s |
| C++ `pbr_physics_particles` | Procedural bodies and particles, bundled environment; **P** starts simulation. | 1 s |
| C++ `skinned_mesh_test` | Bundled `Fox.glb` and environment, first animation clip playing, default camera. | 3 s |
| Python `demo` | Automatic bundled-box fallback, environment, and active particles. | 3 s |
| Python `ecs_bindings_demo` | No renderable objects by design; expected empty viewport, with ECS activity in the console. | 3 s |
| Python `skinned_fox_demo` | Bundled `Fox.glb` and environment, first animation clip playing. | 3 s |
| Python `sprite_ui_test` | Bundled orb/panel textures and Roboto font; no input. Checkerboard pixels are part of the orb texture. | 3 s |
| Python `full_showcase` | Bundled orb/panel textures and Roboto font; automatic motion and system-failure demonstration. | 3 s |
| Python `dakini_temple` | Procedural shader layers and bundled Roboto font; no textures, no input. | 3 s |
| Python `japanese_shrine` | Hand-downloaded `models/japanese_shrine.glb` (CC BY 4.0, aumiella; see the asset README) with the bundled sunset environment; automatic orbit, no input. | 3 s |
| Python `pong_game` | **Not captured:** separately obtained artwork; permission unresolved below. | — |

The shared [asset guide](../../../assets/README.md) records asset locations and attribution. These captures do not redistribute model or texture source files.

## Pong artwork permission

Pong requires **Simple Ping Pong Assets by Esoe B.Studios**, obtained separately from the [publisher page](https://myebstudios.itch.io/simple-ping-pong-assets). On the capture machine, `examples/python/games_2d/pong_game/assets/README.txt` contained attribution and social links, but no license grant for the artwork. The publisher page checked on 2026-09-07 likewise did not establish screenshot-publication permission. Roboto/Teko OFL files cover fonts, not the game art.

**Missing preview: `python/pong_game.png`.** No gameplay image is included until permission is established. After obtaining permission, record its source here, start a rally in the normal game, and press **P** to save its native `pong_screenshot.png`. Move that image here as `python/pong_game.png`, add its caption to the Python guide, and update the coverage count. Follow the [Pong README](../../../examples/python/games_2d/pong_game/README.md) for artwork installation and game controls.

## Validation

The published set contains **17 PNGs for 18 examples**, with Pong explicitly blocked. Inspect every full-size capture for correct content and framing before replacing images. The ECS viewport is intentionally empty; bloom's diffuse light shapes and the UI texture's checkerboard are actual example output.

```powershell
python tools/check_docs.py
python tools/generate_api_docs.py --check
git diff --check
git status --short -- docs/images/examples
```

Keep images under this directory rather than any directory named `screenshots`: the latter is ignored by the repository. Preserve PNG proportions, avoid duplicate gallery files, and review the PNG byte sizes before committing.
