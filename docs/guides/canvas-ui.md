# Canvas UI

The canvas UI builds interfaces out of entities: a canvas, and panels, images, text, buttons, toggles and sliders below it. There are two kinds of canvas:
- A **screen canvas** is drawn over the finished image.
- A **world canvas** is drawn into a texture shown on a quad in the scene, such as a terminal, a sign or a menu you walk up to. You can click on it in 3D like on the screen.

[ui_canvas_demo.py](../../examples/python/getting_started/ui_canvas_demo/ui_canvas_demo.py) has both in under 150 lines. [ui_showcase.py](../../examples/python/ui/ui_showcase/ui_showcase.py) uses most of the API: tabs, widgets driving the scene, text alignment and fonts, a clipped log built at runtime, a modal dialog, a kiosk in the world and 9-slice frames. The [UI reference](../api/python/ui.md) lists every call.

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

### Rows, columns and grids

Placing every element by hand gets tedious for lists and forms. A layout group places its children for you, in the order they were added:

```python
settings = ui.create_vertical_layout(hud, size=(320, 0), spacing=8, padding=(16, 16, 16, 16))
ui.set_anchor(settings, (0, 0))
ui.set_position(settings, (24, 24))
ui.set_size_to_content(settings, vertical=sk.SIZE_FIT_PREFERRED)   # as tall as its rows

ui.create_toggle(settings, "Shadows")
ui.create_text(settings, "Volume", size=18)
ui.create_slider(settings, size=(288, 24))

buttons = ui.create_horizontal_layout(settings, size=(288, 40), spacing=8)
ui.create_button(buttons, "OK", size=(100, 40))
ui.create_button(buttons, "Cancel", size=(100, 40))
```

Things to know:
- **Sizes:** each child keeps the size it was created with. Text takes the size of its words, wrapping at the width it gets.
- **Stretching:** `set_layout_child_sizing(group, expand_width=True)` stretches a column's children across its width. On a row, it shares out the row's spare width.
- **Overrides:** `set_layout_element` gives a child a different min, preferred or flexible size. A flexible size of 1 on one child of a row pushes its later siblings to the far end.
- **Grids:** `create_grid_layout(parent, size, cell_size)` fills rows of equal cells, for inventories and swatches.
- **Placement:** the group itself is placed like any element, by its anchors, or by a parent group.
- **Size to content:** `set_size_to_content` lets the group size itself to its contents, as above.

`set_clip(e, True)` cuts off whatever overflows an element's rect.

### Scrolling lists

A scroll view holds more than fits:

```python
log = ui.create_scroll_view(panel, size=(320, 200))
for line in history:
    ui.create_text(ui.scroll_content(log), line, size=16)
ui.set_scroll_position(log, (0, 1))   # show the end
```

The content is a column that grows with its children and spans the view's width.
- **Pointer:** the wheel, dragging the content (a phone-like flick, which keeps moving briefly) and the scrollbar all scroll it.
- **Clicks:** a button inside still clicks; a press that turns into a drag does not.
- **Elastic ends:** `set_scroll_elastic(log, True)` lets a drag, or the flick after it, pull the content past either end with growing resistance; it springs back in about 0.1 seconds (`elasticity`). The wheel and the scrollbar still stop at the ends. While past an end, `get_scroll_position` is below 0 or above 1.
- **Bringing into view:** `scroll_to(view, element)` brings a row into view, for example the newest one after adding it. `get_rect(e)` reads back where an element ended up. The layout runs once a frame, so an element created this frame reports zeros until the next one.

## Text

Text is drawn from signed distance fields, so it stays sharp at any size and on a world canvas seen up close.
- **Size:** `size` is the height from ascent to descent, in canvas units.
- **Fonts:** `load_font(path)` loads a `.ttf` or `.otf` and returns its id, for `create_text(..., font=id)` or `set_font`. Text that names no font uses `default_font`, `fonts/Roboto-Regular.ttf` from the asset folder.
- **Wrapping:** text wraps at its rect's width unless `set_text_wrap(e, False)`. `set_text_align` aligns it horizontally and vertically in its rect. Lines break where the Unicode line breaking rules (UAX #14) allow, for example:
  - after spaces;
  - after a hyphen;
  - after the Tibetan tsheg (་) and shad (།);
  - between Chinese and Japanese characters and Korean syllables;
  - between words in Thai, Lao, Khmer and Myanmar, which are written without spaces: the words come from word lists shipped in `assets/text/`.

  A full stop, a closing bracket or a small kana never starts a line, an opening bracket never ends one, and a number keeps its decimal point and percent sign. A word wider than the rect breaks between clusters. U+2028 (line separator) ends a line inside a paragraph, as `\n` does.
- **Unicode and shaping:** text is shaped with HarfBuzz, so scripts that join, reorder and stack glyphs come out as the font intends: Devanagari conjuncts and vowel signs, Tibetan stacks, Arabic letter forms. Latin and others get the font's kerning and ligatures. Lines break between whole clusters, never inside one.
- **Fallback fonts:** text uses one font, and characters it lacks come from its fallbacks. Without fallbacks, a missing character shows as the font's replacement character (U+FFFD), or as `?`.

```python
latin = ui.default_font
deva = ui.load_font("fonts/NotoSansDevanagari-Regular.ttf")
tibetan = ui.load_font("fonts/NotoSerifTibetan.ttf")
ui.set_default_font_fallbacks([deva, tibetan])      # for every font without its own
ui.create_text(panel, "Om · ॐ नमः शिवाय · ཨོཾ་མ་ཎི་པདྨེ་ཧཱུྃ")
```

  `set_font_fallbacks(font, [...])` gives one font its own list. The assets folder ships Noto fonts for Devanagari, Tibetan, Arabic, Hebrew, Thai, Lao, Khmer and Myanmar: `fonts/NotoSansThai-Regular.ttf` and so on, with Tibetan in `fonts/NotoSerifTibetan.ttf`.
- **Right-to-left text:** Arabic and Hebrew read right to left, and a line that mixes directions is put in display order by the Unicode bidirectional algorithm: numbers and Latin words inside Arabic read left to right, and brackets are mirrored.
  - **Paragraph direction:** each paragraph takes its direction from its first letter that has one, or, with none, such as "12:30", from the layout direction (see [Right-to-left layout](#right-to-left-layout)). `set_text_direction(e, sk.TEXT_DIRECTION_RTL)` or `TEXT_DIRECTION_LTR` sets it instead, and `TEXT_DIRECTION_LAYOUT` makes it always the layout direction.
  - **Alignment:** `TEXT_ALIGN_START` and `TEXT_ALIGN_END` follow the paragraph: `START` is the left of a left-to-right paragraph and the right of a right-to-left one. `LEFT` and `RIGHT` stay left and right. Text fields align their text to the start.
  - **Control characters:** the directional formatting characters work, for example U+2067 and U+2069 to isolate a Hebrew name inside an English sentence.

```python
ui.set_default_font_fallbacks([ui.load_font("fonts/NotoSansArabic-Regular.ttf"),
                               ui.load_font("fonts/NotoSansHebrew-Regular.ttf")])
line = ui.create_text(panel, "مرحبا بالعالم 2026")
ui.set_text_align(line, sk.TEXT_ALIGN_START)    # right-aligned: the paragraph is right to left
```

### Right-to-left layout

For a right-to-left interface, mirror the layout too:

```python
ui.set_layout_direction(hud, sk.LAYOUT_DIRECTION_RTL)    # a canvas, or any element
```

The layout direction of a canvas or an element decides how its children are placed:
- **Placement:** with right to left, each child is placed as for left to right and then reflected across its parent. Anchors, pivots and the x of `position` are measured from the right, so an element anchored top-left sits top-right.
- **Groups:** rows and grids run from the right, and their left padding is at the right.
- **Widgets:** a slider runs from the right, and Left raises it; a toggle's box is on the right with its label beside it; a scroll view's vertical scrollbar is on the left, and a horizontal scroll view starts at its right end.
- **Text:** text with no letter that has a direction, and text set to `TEXT_DIRECTION_LAYOUT`, takes the layout direction, so `TEXT_ALIGN_START` aligns it right.
- **Inheritance:** elements inherit their parent's direction, so setting it on the canvas mirrors everything. `set_layout_direction(e, sk.LAYOUT_DIRECTION_LTR)` keeps a part left to right, such as a media player's controls. `is_right_to_left(e)` reads the direction an element was last laid out with.
- **Images:** images are not flipped, except those marked with `set_mirror_in_rtl(e)`, for arrows and other icons that point along the line.

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

`set_interactable(e, False)` disables a widget: it ignores the pointer and is drawn dimmed, label included.

The state colour (hovered, pressed, focused or disabled) multiplies an interactable's own panel and image, and the panel, image and text colours of its tint targets:
- buttons list their label;
- toggles list their box, checkmark and label;
- sliders list their fill and handle.

A widget built by hand can list its parts too:

```python
ui.add_tint_target(row, icon)    # the icon follows the row's hover and disabled colours
```

### Keyboard and gamepad

Menus can be used without a mouse:
- **Focus:** Tab moves focus through the widgets in order, and the arrow keys or a gamepad's D-pad move to the nearest one in that direction.
- **Showing it:** a focused button's panel and a focused toggle's box turn blue, and a focused slider's fill turns lighter.
- **Activating:** Enter, Space or the A button press a focused button or flip a toggle, which reads like a click in `was_clicked` and `value_changed`. Left and Right move a focused slider.
- **Clearing:** Escape or B clears focus. Clicking a widget focuses it too.
- **Dialogs:** navigation stays on the topmost canvas, so a dialog keeps focus inside it.

```python
ui.focus(play)    # start a menu on its first button, for a gamepad player
```

`set_navigable(e, False)` keeps decoration or a rarely used widget out of the order.

### Text fields

```python
name = ui.create_text_field(panel, "Your name", size=(300, 36))
ui.set_text_field_options(name, max_length=24)

def on_update(dt):
    if ui.text_submitted(name):          # Enter, this frame
        greet(ui.get_text(name))
    if ui.text_changed(search):          # the user edited it, this frame
        filter_list(ui.get_text(search))
```

A text field is a single line. Clicking it or tabbing to it focuses it, and while it has focus it takes the keyboard:
- **Typing:** characters go in at the caret, replacing any selection. Pasted line breaks and tabs become spaces.
- **Deleting:** Backspace and Delete remove one cluster, so a Devanagari conjunct or a letter with its accents goes in one step. With Ctrl they remove a word.
- **Moving:** Left and Right move one cluster to the left or right on screen, also through mixed-direction text; with Ctrl they move one word, towards the start of a right-to-left paragraph with Right. Words follow the Unicode word rules, so "don't" and "3.5" are one word each, and the word lists in Thai, Lao, Khmer and Myanmar. Home and End go to the start and end of the text. Holding Shift extends the selection; a selection across a change of direction is drawn in as many pieces as it covers on screen.
- **Clipboard:** Ctrl+A selects everything. Ctrl+C, Ctrl+X and Ctrl+V copy, cut and paste through the system clipboard.
- **Submitting:** Enter sets `text_submitted` for one frame. The field keeps focus.
- **Leaving:** Tab moves on to the next widget, and Escape clears focus.
- **Pointer:** a click places the caret, dragging selects, and a double-click selects a word.

The text scrolls sideways to keep the caret in view, and the caret blinks. While a field has focus, `keyboard_captured` is true: the camera sees no keys but the modifiers, and `on_key_pressed` is not called.

Setting and reading:
- `set_text(field, text)` replaces the text and puts the caret at the end. It does not set `text_changed`, which reports only the user's edits.
- `get_text` reads the text, and `select_all` selects it.
- `set_text_field_options` takes three settings:
  - `max_length` caps the number of characters.
  - `password` draws a dot for each character and stops copying.
  - `read_only` keeps selecting and copying but stops editing.

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
- **Lighting:** by default the quad's material shows the texture as emission over a black base colour, so it reads the same in daylight and in the dark. `emission` scales how bright it is. With `lit=True` the texture is the material's base colour instead, lit by the scene's lights like any surface, and `emission` is not used.
- **Exposure:** an unlit canvas's material has the parameter `exposureCompensation` set to 1, which divides its emission by the exposure before Tonemap multiplies by it again. The canvas then keeps its brightness when the exposure changes, by hand or automatically. The tone curve still applies, so colours come out close to, not exactly, as given: a grey of 0.5 shows as about 0.53. The quad's faint reflection of its surroundings is not compensated. Bloom still adds a glow in proportion to the canvas, and text that changes every frame can smear slightly under TAA. Set the parameter to 0 with `set_material_float` for a canvas that exposure should brighten and darken like the rest of the scene.
- **Pointer:** a world canvas takes it where the camera's ray meets the front of its quad, and the nearest quad wins. Geometry in front of the canvas stops the pointer too, when the pipeline names its depth image (see [Pipelines](#pipelines)).

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

### Geometry in front of world canvases

A top-level `ui` block names the depth image that world-canvas hits are tested against, and the pass after which it is complete:

```json
{ "ui": { "depth": "gDepth", "afterPass": "GBufferSkinnedMasked" } }
```

Each frame the engine copies the one depth texel under the pointer, right after that pass, and reads it once the GPU has finished that frame. A world-canvas hit farther along the pointer's ray than that depth, beyond a small margin, does not count. The pass must write the image, and the image is created so that it can be copied from.

- **Default pipeline:** it has the block, reading `gDepth` after the last G-buffer pass. Blended (transparent) surfaces do not write depth, so they do not stop the pointer.
- **Without the block:** nothing hides a world canvas from the pointer except a nearer canvas.
- **Latency:** the depth is as many frames old as there are frames in flight, two by default. A canvas or a wall that has just moved, or a pointer moving quickly across an edge, can be judged by the scene of a frame or two before.

### Exposure compensation in a copied pipeline

The default pipeline's G-buffer and transparent passes bind `exposureReadSet`, with the settings and the 1×1 `exposure` image, and their shaders apply `exposureCompensation` to emission. A copied pipeline without that set and those shaders ignores the parameter, and its world canvases change with the exposure like the rest of the scene.

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
| Placement | Nine screen anchors with a pixel offset | Anchors, pivot and stretching relative to a parent; rows, columns and grids |
| Text | ASCII bitmap glyphs, one entity per glyph | Unicode signed-distance text, one entity per label |
| Drawing | One draw per element, in a `sprite_geometry` pass | Batched, in a `ui_canvas` pass or into a world texture |
| Clipping, input, widgets | None | Clip rects, pointer hit testing, buttons, toggles, sliders |
| On a surface in the world | World sprites, without input | World canvases, with input |
| Default pipeline | Not drawn | Drawn |

## Limits

- Text fields are single-line, and there is no input method (IME) composition, so text that is typed through an IME, such as Chinese or Japanese, cannot be entered; it can still be pasted.
- Text fields interpret Ctrl, not Cmd, as the word modifier, on every platform.
- The arrow keys move focus and also turn an orbit camera, which uses them; only a focused text field takes keys from the camera.
- Right-to-left text: a paragraph is shaped once, before it is broken into lines, so an Arabic word too wide for its rect and split between clusters keeps the joined letter forms it would have on one line.
- Thai, Lao, Khmer and Myanmar words are found by matching against word lists. A word missing from its list, such as a name, is not split; it stays joined to any unknown characters next to it. Chinese characters and hiragana have no word list, so Ctrl with an arrow moves one character at a time through them.
- Geometry in front of a world canvas stops the pointer only in a pipeline with a `ui` depth block, and only as of two frames before (see [Geometry in front of world canvases](#geometry-in-front-of-world-canvases)).
- The glyph atlas has up to four pages of 2048 by 2048 texels, added as they are needed. When all four are full, the page drawn from least recently is cleared, and its glyphs are rasterised again when next drawn. Text that needs more than the four pages in a single frame, several thousand distinct glyphs, is partly left out.

## Troubleshooting

| Problem | Likely cause |
|---|---|
| Screen canvases do not appear | The pipeline has no `ui_canvas` pass (the log says so), or the canvas is hidden behind another with a higher `sort_order` |
| An element does not appear | It, or a parent, is invisible or outside a clipping parent; or its size is zero. `get_rect` shows where it ended up |
| Text is missing | The font failed to load (`load_font` returned 0), or the text's rect is too small for one line |
| Clicks do nothing | Something above the widget stops the pointer: a panel or image drawn over it. Use `set_raycast_target(e, False)` on decoration |
| A world canvas is too dark or blown out | Raise or lower `emission`. If exposure changes it, the material's `exposureCompensation` is 0, or the pipeline was copied without exposure compensation |
| A world canvas cannot be clicked | The camera sees its back, a screen canvas element is over it, or geometry is in front of it |
