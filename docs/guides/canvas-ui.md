# Canvas UI

The canvas UI builds interfaces out of entities: a canvas, and panels, images, text, buttons, toggles and sliders below it. There are two kinds of canvas:
- A **screen canvas** is drawn over the finished image.
- A **world canvas** is drawn into a texture shown on a quad in the scene, such as a terminal, a sign or a menu you walk up to. You can click on it in 3D like on the screen.

[ui_canvas_demo.py](../../examples/python/getting_started/ui_canvas_demo/ui_canvas_demo.py) has both. The [UI reference](../api/python/ui.md) lists every call.

## A first HUD

```python
import shoonyakasha as sk

engine = sk.Engine(title="HUD", width=1280, height=720)   # the default pipeline draws the UI


def on_init():
    ui = engine.ui
    engine.create_camera(pos=(0.0, 1.0, 4.0))

    hud = ui.create_canvas(reference_size=(1280, 720))
    panel = ui.create_panel(hud, size=(320, 140), color=(0.08, 0.09, 0.12, 0.85))
    ui.set_anchor(panel, (0, 0))          # place it by its top-left corner...
    ui.set_position(panel, (24, 24))      # ...24 units from the canvas's top-left

    title = ui.create_text(panel, "Hello, canvas", size=28)
    ui.set_rect(title, anchor_min=(0, 0), anchor_max=(1, 0), pivot=(0, 0), position=(16, 12), size=(-32, 36))

    play = ui.create_button(panel, "Play")
    ui.set_anchor(play, (0, 1))
    ui.set_position(play, (16, -16))
    state["play"] = play


def on_update(dt):
    if engine.ui.was_clicked(state["play"]):
        print("Play")


state = {}
engine.set_on_init(on_init)
engine.set_on_update(on_update)
engine.run()
```

A canvas made with `reference_size=(1280, 720)` is laid out in 1280 by 720 units at any window size. The UI grows and shrinks with the window and keeps its proportions.

## Placing elements

Each element sits in its parent's rect: the canvas, or another element. Units are canvas units, measured from the parent rect's top-left with y down. Moving or hiding a parent moves or hides its children, and later children draw on top of earlier ones.

`set_rect` takes:
- **Anchors:** where in the parent the element hangs, as fractions of the parent's rect.
- **A pivot:** which point of the element is placed there, as a fraction of its own rect.
- **A position:** an offset of the pivot from the anchor point.
- **A size.**

Common placements:

| Placement | Call |
|---|---|
| Fixed size, centred (the default for new elements) | nothing, or `set_rect(e, size=(w, h))` |
| Fixed size, `(x, y)` from the parent's top-left | `set_anchor(e, (0, 0))`, `set_position(e, (x, y))` |
| Fixed size, `(x, y)` in from the bottom-right | `set_anchor(e, (1, 1))`, `set_position(e, (-x, -y))` |
| Fill the parent with a margin `m` | `set_rect(e, anchor_min=(0, 0), anchor_max=(1, 1), size=(-2 * m, -2 * m))` |
| A strip along the top, `h` high | `set_rect(e, anchor_min=(0, 0), anchor_max=(1, 0), pivot=(0.5, 0), size=(0, h))` |

When the anchors differ, the element stretches with its parent, and `size` is added to the span between them.

`set_clip(e, True)` cuts off whatever overflows an element's rect, for example a scrolling log. `get_rect(e)` reads back where an element ended up. The layout runs once a frame, so an element created this frame reports zeros until the next one.

## Text

Text is drawn from signed distance fields, so it stays sharp at any size and on a world canvas seen up close.
- **Size:** `size` is the height from ascent to descent, in canvas units.
- **Fonts:** `load_font(path)` loads a `.ttf` or `.otf` and returns its id, for `create_text(..., font=id)` or `set_font`. Text that names no font uses `default_font`, `fonts/Roboto-Regular.ttf` from the asset folder.
- **Wrapping:** text wraps at its rect's width unless `set_text_wrap(e, False)`. `set_text_align` aligns it horizontally and vertically in its rect.
- **Unicode:** any character the font has is drawn, so Latin with diacritics, Cyrillic and Greek all work. There is no shaping, which scripts such as Devanagari and Tibetan need to join and reorder glyphs. A character missing from the font shows as the font's replacement character (U+FFFD), or as `?` if it has none.

## Widgets and the pointer

`create_button`, `create_toggle` and `create_slider` build ready-made widgets. Read them in `on_update`:
- `was_clicked(button)` is true for one frame after a click.
- `value_changed(e)` is true for one frame after the pointer changed a toggle or slider.
- `toggle_value(e)` and `slider_value(e)` read the current values.

The input runs after `on_update`, so `on_update` sees the previous frame's clicks.

The left mouse button is the pointer's button:
- **Click:** a press and a release on the same widget. Releasing elsewhere cancels it.
- **Slider drags:** a slider being dragged follows the mouse even outside its rect.
- **Captured mouse:** while the mouse is captured for camera control (ESC), the pointer is the centre of the screen. That serves a crosshair in a first-person game.

`pointer_over_ui` says whether the pointer is over the UI. Check it before acting on a click in the 3D scene:

```python
def on_mouse_button(button, pressed):
    if button == sk.keys.MOUSE_LEFT and pressed and not engine.ui.pointer_over_ui:
        shoot()

engine.input.set_on_mouse_button(on_mouse_button)
```

Any element can be made clickable with `set_interactable(e, True)` and read with `was_clicked`.

What stops the pointer:
- **Raycast targets:** panels and images stop it. `set_raycast_target(e, False)` lets it through, for decoration over a button.
- **Text:** never stops it.
- **Children:** a click on a child counts for the nearest interactable above it, so a button's label and icon act as the button.

`set_interactable(e, False)` disables a widget: it ignores the pointer, and a disabled button is drawn dimmed.

## World canvases

```python
terminal = ui.create_world_canvas(pixel_size=(1024, 512), world_size=(1.6, 0.8), emission=2.0)
engine.scene.set_position(terminal, (-1.5, 0.9, 0.3))
engine.scene.set_rotation(terminal, (0.0, 0.45, 0.0))
ui.set_canvas_clear_color(terminal, (0.03, 0.06, 0.1, 1.0))
ui.create_button(terminal, "Open the gate", size=(400, 100))
```

`create_world_canvas` returns an entity that is both the canvas and a quad of `world_size` units facing +Z.
- **Layout and resolution:** the canvas is laid out in `pixel_size` units. `set_canvas_pixel_size` changes the texture's resolution without moving anything.
- **Drawing:** it is drawn into its texture every frame, so the quad shows the canvas as it is now.
- **Lighting:** the quad's material shows the texture as emission over a black base colour, so it reads the same in daylight and in the dark. `emission` scales how bright it is.
- **Exposure:** world canvases are part of the 3D image, so automatic exposure and bloom apply to them. Raise `emission` in a bright scene. Text that changes every frame can smear slightly under TAA.
- **Pointer:** a world canvas takes it where the camera's ray meets the front of its quad. The nearest quad wins, but the ray passes through other geometry, so a wall in front of a terminal does not stop clicks on it.

## Pipelines

Screen canvases are drawn by a pass whose execution type is `ui_canvas`. The engine supplies its shaders and pipeline:
- **Default pipeline:** it ends with one, `UIOverlay`. It runs after tonemapping, so screen colours come out as given, untouched by exposure or bloom.
- **Your own pipeline:** add the pass after the one that writes the swapchain, and move that pass's `present` to it:

```json
{ "name": "UIOverlay", "type": "graphics", "execution": { "type": "ui_canvas" },
  "outputs": [ { "resource": "swapchain", "usage": "color_blend", "present": true } ] }
```

Without such a pass, screen canvases are not drawn and the log says so every few seconds. World canvases need no pass of their own; their quads are drawn like any other material.

The UI shaders ship beside the default pipeline in `shaders/ui/`. A copied pipeline can carry its own `shaders/ui/`, which is used first.

## From C++

`EngineAPI::getUI()` returns the same API as `UIAPI` ([reference](../api/cpp/ui-api.md)). A program that derives from `ApplicationBase` can also work with the components underneath:
- `UI/UIComponents.h`: `UICanvas`, `UIRect`, `UIPanel`, `UIImage`, `UIText`, `UIInteractable` and the widget components.
- `UI/UIWidgets.h`: functions that build widgets into a registry.
- `getUIContext()`: the fonts, world-canvas creation and the pointer state.

[examples/cpp/rendering/canvas_ui](../../examples/cpp/rendering/canvas_ui/main.cpp) works this way.

## Compared with sprites and labels

The older 2D layer, [sprites, UI and text](sprites-ui-text.md), is unchanged and still works. The two can be used side by side.

| | Sprite UI (`create_ui_panel`, `create_text`) | Canvas UI (`engine.ui`) |
|---|---|---|
| Placement | Nine screen anchors with a pixel offset | Anchors, pivot and stretching, relative to a parent |
| Text | ASCII bitmap glyphs, one entity per glyph | Unicode signed-distance text, one entity per label |
| Drawing | One draw per element, in a `sprite_geometry` pass | Batched, in a `ui_canvas` pass or into a world texture |
| Clipping, input, widgets | None | Clip rects, pointer hit testing, buttons, toggles, sliders |
| On a surface in the world | World sprites, without input | World canvases, with input |
| Default pipeline | Not drawn | Drawn |

## Limits

- No layout groups, scroll views or text input fields yet.
- No text shaping, so complex scripts are drawn glyph by glyph.
- World-canvas clicks ignore geometry in front of the canvas.
- The glyph atlas is 2048 by 2048 texels. When it fills up, it is cleared and the text on screen is rasterised again; this is rare unless a very large number of distinct characters is in use at once.

## Troubleshooting

| Problem | Likely cause |
|---|---|
| Screen canvases do not appear | The pipeline has no `ui_canvas` pass (the log says so), or the canvas is hidden behind another with a higher `sort_order` |
| An element does not appear | It, or a parent, is invisible or outside a clipping parent; or its size is zero. `get_rect` shows where it ended up |
| Text is missing | The font failed to load (`load_font` returned 0), or the text's rect is too small for one line |
| Clicks do nothing | Something above the widget stops the pointer: a panel or image drawn over it. Use `set_raycast_target(e, False)` on decoration |
| A world canvas is too dark or blown out | Raise or lower `emission`; automatic exposure applies to it |
| A world canvas cannot be clicked | The camera sees its back, or a screen canvas element is over it |
