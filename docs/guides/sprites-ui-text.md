# Sprites, UI, and text

Use a pipeline with `execution.type: sprite_geometry`, compatible sprite shaders, and matching descriptor/push-constant layouts. Creating a sprite entity does not add a pass to an ordinary 3D pipeline. Start with [sprite_pipeline.json](../../examples/python/games_2d/sprite_ui_test/sprite_pipeline.json) and its [demo](../../examples/python/games_2d/sprite_ui_test/sprite_ui_demo.py).

## Create panels and labels

Inside init, with that pipeline configured:

```python
panel = engine.create_ui_panel(
    sk.UI_ANCHOR_TOP_LEFT, (110, 40), (200, 60),
    color=(0.1, 0.1, 0.15, 0.85))
label = engine.create_text(
    "Score: 0", sk.UI_ANCHOR_TOP_LEFT, (20, 40),
    "fonts/Roboto-Regular.ttf", font_size=24.0)
engine.scene.set_sort_key(panel, 0)
engine.scene.set_text_sort_key(label, 1)
```

Python uses `import shoonyakasha as sk`. C++ equivalents are `createUIPanel`, `createText`, and `getScene().setTextSortKey`, with `UIAnchor` and GLM vectors. See the [Engine](../api/cpp/engine-api.md) and [Scene](../api/cpp/scene-api.md) references for full declarations/defaults.

Panels use pixel sizes and offsets from one of nine viewport anchors. Their offset locates the panel center; text offsets locate the label's reference point. An empty panel texture gives a flat color. `create_sprite` / `createSprite` instead creates a world-space quad using world-unit position and size, projected through the camera.

## Texture, color, and ordering

Use sprite texture/color/UV setters for runtime changes. UV rectangles are `(u0, v0, u1, v1)`, default `(0, 0, 1, 1)`. Texture paths use the shared asset resolver.

Set distinct entity layer bits and matching pass `renderLayerMask` values to route sprites through alpha, additive, or other blend passes. The entity mask is eight bits, default 255; masks intersect rather than match by equality. The [showcase pipeline](../../examples/python/games_2d/full_showcase/showcase_pipeline.json) demonstrates this.

For `sortMode: sort_key`, lower keys draw first. Set text-specific sort/layer properties so they propagate to the glyph entities.

## Text limitations and lifetime

Fonts are baked into an atlas and rendered as glyph sprites. The current range is ASCII 32–126; a `.ttf`/`.otf` input does not imply Unicode shaping or a complete layout engine. `set_text`, color, font size, and alignment setters update labels.

Use `set_text_visible(label, False)` / `setTextVisible(label, false)` to remove a label from view. Label destruction alone can leave generated glyph entities visible because they are not hierarchy children. Hide it before destroying it; this is a known engine limitation.

UI creation does not supply widgets, hit testing, or a retained interaction toolkit. Build interactions with the [input API](../api/python/input.md), as in [Pong](../../examples/python/games_2d/pong_game/README.md).
