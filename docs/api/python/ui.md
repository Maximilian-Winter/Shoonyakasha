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

## Layout groups

`create_horizontal_layout`, `create_vertical_layout` and `create_grid_layout` make elements that place their children themselves, in child order. Their anchors and positions are ignored while they are in the group.
- **A row or column** gives each child its *layout sizes* along its axis: at least its min, its preferred size when there is room, and a share of the rest by its flexible size.
- **Across its axis** a child gets its preferred size, up to the group's.
- **`set_layout_child_sizing`:**
  - `expand` along the group's axis shares spare room between all children;
  - `expand` across it makes each child fill the group;
  - `control=False` leaves a child at its own size.
- **A grid** puts children in cells of `cell_size`, row by row. It fits as many columns as the width allows, or uses a fixed `columns`.
- **Spare room** is placed by `set_layout_alignment`.

Layout sizes, without `set_layout_element`:
- **Text:** its measured width; its height wrapped at the width it gets, with a min width of 0.
- **An image with a texture:** the texture's size.
- **Anything else, widgets included:** its own size.
- **A group:** its children's sizes added up along its axis and the largest across it, with spacing and padding.

`set_layout_element(e, min=, preferred=, flexible=)` overrides any of them per axis (-1 keeps its own), and `ignore=True` takes a child out of its group. Hidden children take no space.

`set_size_to_content(e, horizontal, vertical)` sizes an element placed by its anchors to its own layout size, `SIZE_FIT_MIN` or `SIZE_FIT_PREFERRED` per axis. Typical uses are a column that grows with its rows, or a label as wide as its text. `get_preferred_size(e)` reads the measured size back. Text is measured only for children of a group and elements sized to content.

## Scroll views

`create_scroll_view(parent, size)` makes a view that clips a column and scrolls it. Add children to `scroll_content(view)`. They stack top to bottom; when the view scrolls only vertically, they span its width less the scrollbar. The column grows with them, and the view scrolls once it is taller than the view.

- **Wheel:** the wheel over a view scrolls the innermost view under the pointer that can still move, so a list inside a scrolling page scrolls first, then the page. A view that scrolls only sideways takes the vertical wheel too.
- **Dragging:** the content follows the pointer once it has moved 8 canvas units from the press. A press on a button inside that turns into a drag is not a click. Sliders and scrollbars keep their own drags.
- **Momentum:** a released drag keeps moving and slows down, unless `set_scroll_settings(view, inertia=False)`. `wheel_speed` sets the canvas units per wheel step.
- **Scrollbars:** they run inside the right and bottom edges and are hidden while everything fits. Their handle's length is the visible fraction. Drag the handle, or press the track to move a page.

`get_scroll_position` and `set_scroll_position` use 0 at the start (top, left) and 1 at the end, per axis. `scroll_to(view, element)` scrolls the least distance that brings an element fully into view, using the layout of the last frame.

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
| `create_canvas(reference_size=(1920, 1080), scale_mode=CANVAS_SCALE_WITH_SCREEN, sort_order=0)` | int | A canvas over the screen. A higher sort_order draws over, and takes the pointer first. |
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
| `create_horizontal_layout(parent, size=(100, 100), spacing=0.0, padding=(0, 0, 0, 0))` | int | A row of children, left to right. padding is (left, top, right, bottom). |
| `create_vertical_layout(parent, size=(100, 100), spacing=0.0, padding=(0, 0, 0, 0))` | int | A column of children, top to bottom. padding is (left, top, right, bottom). |
| `create_grid_layout(parent, size=(100, 100), cell_size=(100, 100), columns=0, spacing=(0, 0), padding=(0, 0, 0, 0))` | int | Cells of cell_size, row by row; columns 0 fits as many as the width allows. |
| `set_layout_alignment(group, alignment)` | None | Where a group puts children in room they leave: (0, 0) top-left, (0.5, 0.5) centre. |
| `set_layout_child_sizing(group, control_width=True, control_height=True, expand_width=False, expand_height=False)` | None | For a row or column: control sizes children from their layout sizes; expand along the group's axis shares spare room, across it fills the group. |
| `set_layout_element(element, min=(-1, -1), preferred=(-1, -1), flexible=(-1, -1), ignore=False)` | None | Override the layout sizes element reports; -1 keeps its own on that axis. ignore leaves it to its anchors inside a group. |
| `set_size_to_content(element, horizontal=SIZE_FIT_UNCONSTRAINED, vertical=SIZE_FIT_UNCONSTRAINED)` | None | Size an element placed by its anchors to its own layout sizes, per axis: SIZE_FIT_UNCONSTRAINED, SIZE_FIT_MIN or SIZE_FIT_PREFERRED. |
| `get_preferred_size(element)` | 2-tuple | (width, height): the preferred layout size as last measured. |
| `create_scroll_view(parent, size=(300, 200), horizontal=False, vertical=True)` | int | A scroll view; add children to scroll_content(view). |
| `scroll_content(view)` | int | The column inside the view that holds its children: they stack top to bottom, spanning the view's width when it scrolls only vertically. |
| `get_scroll_position(view)` | 2-tuple | (x, y), each 0 at the start and 1 at the end. |
| `set_scroll_position(view, position)` | None | Move the view, stopping any drift from a released drag. |
| `scroll_to(view, element)` | None | Scroll the least distance that brings element fully into view. |
| `set_scroll_settings(view, wheel_speed=48.0, inertia=True)` | None | Canvas units per wheel step, and whether a released drag keeps moving. |
| `set_text(element, text)` | None | Calls native `setText`; see the class contract above. |
| `get_text(element)` | str | Calls native `getText`; see the class contract above. |
| `set_font(element, font)` | None | Calls native `setFont`; see the class contract above. |
| `set_font_size(element, size)` | None | Calls native `setFontSize`; see the class contract above. |
| `set_text_align(element, horizontal, vertical=TEXT_ALIGN_TOP)` | None | TEXT_ALIGN_LEFT/CENTER/RIGHT and TEXT_ALIGN_TOP/MIDDLE/BOTTOM. |
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
