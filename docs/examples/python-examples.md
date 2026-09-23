# Python examples

Install the native package using [BUILDING.md](../../BUILDING.md#python-bindings). Each example compiles its own shaders, so `glslc` must be discoverable. Run each from the directory containing its script, JSON, and `shaders/`.

| Directory | Run | Demonstrates |
|---|---|---|
| [getting_started/demo](../../examples/python/getting_started/demo) | `python demo.py` | PBR/IBL scene and facade use |
| [getting_started/ecs_bindings_demo](../../examples/python/getting_started/ecs_bindings_demo) | `python ecs_bindings_demo.py` | Custom Python components and systems |
| [animation/skinned_fox_demo](../../examples/python/animation/skinned_fox_demo) | `python skinned_fox_demo.py` | Skeletal animation |
| [games_2d/sprite_ui_test](../../examples/python/games_2d/sprite_ui_test) | `python sprite_ui_demo.py` | Sprites, panels, and text |
| [games_2d/full_showcase](../../examples/python/games_2d/full_showcase) | `python showcase_demo.py` | Layer masks, blend modes, and script ECS |
| [games_2d/dakini_temple](../../examples/python/games_2d/dakini_temple) | `python temple.py` | Procedural shader layers driven by material parameters and custom scene values |
| [games_2d/pong_game](../../examples/python/games_2d/pong_game) | `python pong.py` | Complete 2D game and capture controls |

Pong's third-party artwork must be obtained separately; follow its [README](../../examples/python/games_2d/pong_game/README.md). Other optional assets and fallback behavior are described in the [shared asset guide](../../assets/README.md). The Fox script has an older comment suggesting the C++ directory; use its own Python directory as listed here.

For a minimal first project, prefer the [generated starter](../getting-started/python-quickstart.md). For pipeline authoring, see [JSON walkthrough](../guides/json-render-pipeline.md).

## Runtime previews

Native frame captures using bundled assets. See [capture recipes and asset choices](../images/examples/README.md) for refresh instructions. Click an image to view it at full resolution.

### Facade demo (`demo`)

<a href="../images/examples/python/demo.png"><img src="../images/examples/python/demo.png" alt="Red box near the bottom of a multicolored particle field" width="720"></a>

The Python facade demo with its bundled `Box.gltf` fallback and active compute particles; optional Sponza is not installed in the shared asset root.

### ECS bindings (`ecs_bindings_demo`)

<a href="../images/examples/python/ecs_bindings_demo.png"><img src="../images/examples/python/ecs_bindings_demo.png" alt="Uniform dark empty viewport from the ECS bindings example" width="720"></a>

This example intentionally creates no meshes or sprites. Its custom components and systems run without visible objects; the deliberately failing system reports its auto-disable in the console. The empty viewport is expected, not a loading failure.

### Animated Fox (`skinned_fox_demo`)

<a href="../images/examples/python/skinned_fox_demo.png"><img src="../images/examples/python/skinned_fox_demo.png" alt="Low-poly orange and white fox in an animated pose" width="720"></a>

Bundled `Fox.glb` during playback of the first animation clip, driven through the Python bindings.

### Sprites and UI (`sprite_ui_test`)

<a href="../images/examples/python/sprite_ui_test.png"><img src="../images/examples/python/sprite_ui_test.png" alt="Purple orb in the center with a score label and a panel in opposite corners" width="720"></a>

Bundled orb and panel textures with a screen-space score label. The checkerboard around the orb is part of the supplied texture, not an added screenshot background.

### Full showcase (`full_showcase`)

<a href="../images/examples/python/full_showcase.png"><img src="../images/examples/python/full_showcase.png" alt="Five orbiting orbs with colored halos, a health HUD and system-status text" width="720"></a>

Bundled sprites, layered blend passes, text, and Python ECS systems in motion. The status confirms that the deliberately failing system has auto-disabled.

### Dakini temple (`dakini_temple`)

<a href="../images/examples/python/dakini_temple.png"><img src="../images/examples/python/dakini_temple.png" alt="Top-down mandala palace with a fire ring, vajra fence, four coloured quarters, a red lotus and a red double triangle at the centre" width="720"></a>

A mandala palace with no textures. `shaders/mandala.frag` draws each layer onto its quad from a `shape` material parameter, and Python systems turn the rings, make the lotus breathe, flicker the lamps and drift the embers. **Left**/**Right** (or **Space**) dim the palace and bring it back for another deity: Vajrayogini, Green Tara, White Tara or Vajrapani. Each has its own palette, centre symbol and mantra, chosen in the shader through the `scene.custom.deity` value. The preview shows Vajrayogini, the first deity.

### Pong (`pong_game`) — preview unavailable

Pong uses separately obtained **Simple Ping Pong Assets by Esoe B.Studios**. The local artwork README provides attribution but no license grant, and the [publisher page](https://myebstudios.itch.io/simple-ping-pong-assets) does not establish permission to publish gameplay captures. The bundled font licenses do not cover that artwork. A screenshot is omitted pending permission; see the [capture record](../images/examples/README.md#pong-artwork-permission).
