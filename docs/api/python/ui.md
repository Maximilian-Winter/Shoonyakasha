# UI

Access: `engine.ui`, from `on_init` on. [Other language reference](../cpp/ui-api.md).

[Binding implementation](../../../python/shoonyakasha/_shoonyakasha.pyx). Import the package as `import shoonyakasha as sk`.

The canvas UI: screen canvases drawn over the image, world canvases drawn into a texture on a quad in the scene, and the panels, images, text, buttons, toggles and sliders on them. The [canvas UI guide](../../guides/canvas-ui.md) walks through building one.

## Entities and units

Canvases and elements are entity handles, like those `engine.scene` returns. An element is the child of a canvas or of another element. Each one is placed in its parent's rect, so moving a parent moves its children. Later children draw over earlier ones. `engine.scene.destroy_entity` on an element destroys its children too.

- **Units:** positions and sizes are canvas units, measured from the parent rect's top-left with y pointing down.
- **Screen canvases:** created with `CANVAS_SCALE_WITH_SCREEN` (the default), a canvas is `reference_size` units, scaled to fit the window. `match` controls how width and height are balanced. With `CANVAS_CONSTANT_PIXEL`, one unit is `scale_factor` pixels.
- **World canvases:** a world canvas is laid out in its `pixel_size` from creation. Changing its resolution with `set_canvas_pixel_size` keeps that layout.

Colours are `(r, g, b, a)` tuples in sRGB with straight alpha. Text is a `str` and can use any Unicode the font has. Scripts that need shaping, such as Devanagari or Tibetan, are drawn glyph by glyph without it.

## Layout

`set_rect` gives every part of the placement at once. Anchors are fractions of the parent's rect:
- **Equal anchors:** the element has the given `size`, and `position` offsets its pivot from the anchor point.
- **Different anchors:** the element stretches between them, and `size` is added to that span. For example, `anchor_min=(0, 0)`, `anchor_max=(1, 1)`, `size=(-20, -20)` fills the parent with a 10-unit margin.
- **`set_anchor(e, (x, y))`:** a shortcut that sets both anchors and the pivot to one corner or edge, so `set_position` measures from that same corner of the parent.

`get_rect` and `get_canvas_size` report the layout as of the last frame. An element created this frame reports zeros until the next frame lays it out.

Use `set_clip(e, True)` to cut an element's descendants off at its rect. `set_visible(e, False)` hides the element and everything below it.

## Text and fonts

- **Fonts:** `load_font(path)` returns a font id, or 0 if the file can't be read. Text without a font uses `default_font`, which loads `fonts/Roboto-Regular.ttf` on first use.
- **Size:** `size` (or `set_font_size`) is the height from ascent to descent in canvas units. Glyphs are signed distance fields, so text stays sharp at any size.
- **Labels:** the text methods on a button or toggle act on its label.

## Input

The left mouse button drives the UI:
- **Pointer:** the cursor; while the mouse is captured, the centre of the screen.
- **Order of testing:** screen canvases first, the one on top first. Then world canvases, by the nearest quad the camera ray hits, front side only. Geometry in front of a world canvas does not block it.
- **Click:** a release over the element the press started on. `was_clicked` and `value_changed` are true for one frame.
- **Timing:** the state is updated after `on_update` runs, so `on_update` reads the previous frame's.
- **Press capture:** a pressed element keeps the pointer until release, so a slider follows a drag outside it.

`pointer_over_ui` is true while the pointer is over a raycast target or pressing an element. Check it before treating a click as one in the 3D scene. Raycast targets are:
- interactable elements;
- elements whose image or panel has `set_raycast_target(e, True)`, the default.

Text never stops the pointer.

Widgets:
- **Button:** `was_clicked`.
- **Toggle:** a click flips `toggle_value` and shows its checkmark.
- **Slider:** pressing or dragging sets `slider_value` from the pointer's position across it. `set_slider_range(min, max, whole_numbers)` rounds to whole numbers when asked.

Values set from code are shown in the next frame and do not set `value_changed`. `set_interactable(e, False)` disables a widget, and dims a button.

## World canvases

`create_world_canvas(pixel_size, world_size)` makes an entity carrying both the canvas and a quad `world_size` units across, facing +Z. Move and turn it with `engine.scene.set_position` and `set_rotation`.
- **Rendering:** the canvas is drawn into its texture every frame, cleared to `set_canvas_clear_color`.
- **Material:** the quad's material shows the texture as emission over a black base colour, so it glows the same whatever the lighting.
- **Brightness:** `emission` scales the glow. It is still subject to the pipeline's exposure and bloom.
- **Pipeline:** the quad draws in any pipeline that draws glTF materials, the default one included.

## Pipelines

Screen canvases need a pass with `"execution": {"type": "ui_canvas"}` writing the swapchain. The default pipeline ends with one, `UIOverlay`. In a pipeline without one, the engine logs a warning and screen canvases are not drawn. See [pipeline JSON](../../reference/pipeline-json.md#execution).

<!-- BEGIN SOURCE API -->

## Members

Signatures and short descriptions below are extracted from the Cython wrapper; return shapes follow its conversions and the native declarations.

| Member | Returns / property value | Description |
|---|---|---|
| `load_font(path)` | int | Id of the font at path, loading it on first use; 0 if it cannot be loaded. |
| `default_font (read/write property)` | int | The font text uses when it names none: fonts/Roboto-Regular.ttf until set. |
| `create_canvas(reference_size=(1920, 1080), scale_mode=CanvasScaleMode_ScaleWithScreen, sort_order=0)` | int | A canvas over the screen. A higher sort_order draws over, and takes the pointer first. |
| `create_world_canvas(pixel_size, world_size, emission=1.0)` | int | A canvas of pixel_size pixels on a quad of world_size units, facing +Z. |
| `set_canvas_scaling(canvas, mode, reference_size=(1920, 1080), scale_factor=1.0, match=0.5)` | None | CANVAS_CONSTANT_PIXEL: scale_factor pixels per unit. CANVAS_SCALE_WITH_SCREEN: reference_size fitted to the target, match 0 following its width and 1 its height. |
| `set_canvas_sort_order(canvas, sort_order)` | None | Calls native `setCanvasSortOrder`; see the class contract above. |
| `set_canvas_pixel_size(canvas, pixel_size)` | None | World canvases: the size of the texture, in pixels. |
| `set_canvas_clear_color(canvas, color)` | None | World canvases: the colour the texture is cleared to each frame. |
| `get_canvas_size(canvas)` | 2-tuple | (width, height) in canvas units, as last laid out. |
| `create_element(parent, size=(100, 100))` | int | An element with no content, for grouping and placing others. |
| `create_panel(parent, size=(100, 100), color=(1, 1, 1, 1), texture="", border=(0, 0, 0, 0))` | int | A filled rectangle, or a 9-slice of texture keeping border (left, top, right, bottom texture pixels) unstretched. |
| `create_image(parent, size=(100, 100), texture="", color=(1, 1, 1, 1))` | int | texture stretched over the rect times color; without a texture, color alone. |
| `create_text(parent, text, size=24.0, color=(1, 1, 1, 1), font=0)` | int | Text filling its parent. size is the height from ascent to descent; font 0 is the default font. |
| `create_button(parent, label, size=(160, 40))` | int | Calls native `createButton`; see the class contract above. |
| `create_toggle(parent, label, is_on=False, size=(200, 28))` | int | Calls native `createToggle`; see the class contract above. |
| `create_slider(parent, min=0.0, max=1.0, value=0.0, size=(200, 24))` | int | Calls native `createSlider`; see the class contract above. |
| `set_parent(element, parent)` | None | Move element to the end of parent's children. |
| `set_rect(element, anchor_min=(0.5, 0.5), anchor_max=(0.5, 0.5), pivot=(0.5, 0.5), position=(0, 0), size=(100, 100))` | None | Anchors are fractions of the parent's rect, (0, 0) its top-left. With equal anchors size is the size; with different ones it is added to the span between them. position offsets the pivot, a fraction of the element's own rect. |
| `set_anchor(element, anchor)` | None | Both anchors and the pivot: (0, 0) places by the top-left corner from the parent's top-left, (1, 1) by the bottom-right from its bottom-right. |
| `set_position(element, position)` | None | Calls native `setPosition`; see the class contract above. |
| `set_size(element, size)` | None | Calls native `setSize`; see the class contract above. |
| `get_rect(element)` | 4-tuple | (x, y, width, height) in canvas units, as last laid out. |
| `set_visible(element, visible)` | None | Calls native `setVisible`; see the class contract above. |
| `is_visible(element)` | bool | Calls native `isVisible`; see the class contract above. |
| `set_clip(element, clip)` | None | Clip the element's descendants to its rect. |
| `set_text(element, text)` | None | Calls native `setText`; see the class contract above. |
| `get_text(element)` | str | Calls native `getText`; see the class contract above. |
| `set_font(element, font)` | None | Calls native `setFont`; see the class contract above. |
| `set_font_size(element, size)` | None | Calls native `setFontSize`; see the class contract above. |
| `set_text_align(element, horizontal, vertical=TextVAlign_Top)` | None | TEXT_ALIGN_LEFT/CENTER/RIGHT and TEXT_ALIGN_TOP/MIDDLE/BOTTOM. |
| `set_text_wrap(element, wrap)` | None | Calls native `setTextWrap`; see the class contract above. |
| `set_color(element, color)` | None | The colour of the element's own text, image and panel. |
| `set_texture(element, path)` | bool | The texture of the element's image or panel. False if it cannot be loaded. |
| `set_panel_border(element, border, border_scale=1.0)` | None | Calls native `setPanelBorder`; see the class contract above. |
| `set_raycast_target(element, target)` | None | Whether the element's image or panel stops the pointer. |
| `set_interactable(element, enabled=True)` | None | Make the element take the pointer, or disable it. Widgets are interactable. |
| `is_hovered(element)` | bool | Calls native `isHovered`; see the class contract above. |
| `is_pressed(element)` | bool | Calls native `isPressed`; see the class contract above. |
| `was_clicked(element)` | bool | Released over the element after being pressed on it, this frame. |
| `value_changed(element)` | bool | A toggle's or slider's value was changed by the pointer, this frame. |
| `toggle_value(toggle)` | bool | Calls native `getToggle`; see the class contract above. |
| `set_toggle_value(toggle, is_on)` | None | Calls native `setToggle`; see the class contract above. |
| `slider_value(slider)` | float | Calls native `getSliderValue`; see the class contract above. |
| `set_slider_value(slider, value)` | None | Calls native `setSliderValue`; see the class contract above. |
| `set_slider_range(slider, min, max, whole_numbers=False)` | None | Calls native `setSliderRange`; see the class contract above. |
| `pointer_over_ui (read-only property)` | bool | Is the pointer over a UI element, or pressing one? Check it before treating a click as one in the scene. |
| `pointer_canvas (read-only property)` | int | The canvas under the pointer, or NULL_ENTITY. |
| `pointer_position (read-only property)` | 2-tuple | The pointer on pointer_canvas, in canvas units. |
| `hovered_element (read-only property)` | int | The interactable element under the pointer, or NULL_ENTITY. |

<!-- END SOURCE API -->
