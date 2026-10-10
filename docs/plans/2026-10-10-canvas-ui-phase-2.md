# Canvas UI, phase 2: layout, scrolling, shaping, text input and polish

Status: proposed. Follows [the canvas UI plan](2026-10-10-canvas-ui.md), whose eight milestones are done.

The first phase left out the items below. It listed them as limits in the [canvas UI guide](../guides/canvas-ui.md#limits), or they turned up while building it:
- layout groups;
- scroll views;
- text input fields;
- shaping for complex scripts;
- world-canvas clicks that ignore geometry in front of the canvas;
- world canvases being affected by exposure;
- a glyph atlas that is cleared when it fills;
- state colours that do not reach a widget's label.

This plan covers all of them, in eight milestones. Decided with the user: every item is in scope, and HarfBuzz is added as a vcpkg dependency.

## What the code has today (checked in source)

- **Keyboard.** `VulkanWindow::keyCallback` publishes `KeyEvent{key, pressed}` with `pressed = action != GLFW_RELEASE`, so a repeat looks like a press and the modifiers are dropped. There is no `glfwSetCharCallback` and no clipboard access.
- **Keys reach the camera.** `CameraControllerSystem` reads `InputStateComponent::keys` (WASD, Q/E, sprint). `StandaloneInputHandler` toggles mouse capture on ESC (`ECS/InputSystem.h`). `ApplicationBase` switches camera modes on 1/2/3 and forwards every key to `onKeyPressed`. Typing into a field would do all of these.
- **Scrolling.** `InputStateComponent::scrollDelta` already carries the wheel per frame.
- **No gamepad input** anywhere.
- **No raycasting** in the engine, physics included. World-canvas picking (`UIHitTest`) tests only canvas quads.
- **Exposure.** It is applied once, in `Tonemap`, from `settings.exposure` times the adapted value in the 1×1 `exposure` image (`R32_SFLOAT`, written by `exposure.comp`). World canvases are emissive materials, so it applies to them.
- **Text.** `layoutText(font, u32string, style, box)` maps one code point to one glyph with stb_truetype pair kerning. `GlyphCache` is keyed by (font id, glyph id), and `SdfFont::rasterize` takes a glyph id. HarfBuzz returns glyph ids of the same font file, so shaped output feeds the existing cache and rasteriser unchanged.
- **Atlas.** One 2048² `R8_UNORM` texture; when full, `GlyphCache::reset()` clears everything. `UIVertex::mode` uses only its low byte.
- **Tint.** `UIInteractable::stateColor()` multiplies only the element's own `UIPanel`/`UIImage`. A button's label is a child, so a disabled button keeps a bright label. `ui_showcase.py` works around this by dimming the label itself.
- **Dependencies.** `vcpkg.json` has vulkan, nlohmann-json, entt, bullet3, glfw3, glm and spirv-reflect.

## Order

| # | Milestone | Depends on |
|---|---|---|
| 9 | Layout groups and size-to-content | – |
| 10 | Scroll views and scrollbars | 9 |
| 11 | HarfBuzz shaping and font fallback | – |
| 12 | Script-aware line breaking | 11 |
| 13 | Keyboard plumbing, focus and navigation | – |
| 14 | Text input fields | 11, 13 |
| 15 | World-canvas occlusion and exposure-independent canvases | – |
| 16 | Glyph atlas pages with LRU eviction; tint targets | – |

Text fields come after shaping. A caret moves by shaped cluster, and a field built first would have to be reworked.

Each milestone is finished when it has:
- the C++ components and systems;
- the `UIAPI` methods;
- the Cython bindings;
- tests;
- the reference and guide updated;
- the showcase example extended;
- a validation-enabled run.

## 9. Layout groups and size-to-content

**Components** (`UI/UIComponents.h`):
- `UILayoutElement`: `minSize`, `preferredSize` and `flexibleSize` per axis (−1 = not set), and `ignoreLayout`. An element without one reports its content: text measures itself; images report their texture size.
- `UIHorizontalLayout` and `UIVerticalLayout`, with:
  - `padding` (left, top, right, bottom);
  - `spacing`;
  - `childAlignment` (nine-way);
  - `controlChildWidth` and `controlChildHeight`;
  - `childForceExpand` per axis.
- `UIGridLayout`: `cellSize`, `spacing`, `padding`, `startCorner`, and a column count or `flexible`.
- `UIContentSizeFitter`: `horizontal` and `vertical`, each `unconstrained`, `min` or `preferred`.

**Algorithm** (in `CanvasLayoutSystem`, which runs first among the UI systems):
1. **Bottom up:** compute min, preferred and flexible sizes. A layout group sums its children's sizes along its axis and takes the largest across it.
2. **Top down:** resolve rects as now. A layout group then writes its children's rects directly; a child placed this way is marked `drivenByLayout` and its anchors are ignored.
3. **Size fitters:** they set their element's size from step 1 before step 2 reaches it.

**Text measurement.** Text needs measuring before layout, but today `CanvasTextSystem` runs after layout. The layout system gets a `const FontLibrary*` and measures through a `measureText(font, text, style, maxWidth)` helper. That helper reuses `UITextCache` when its inputs match, so text is not laid out twice per frame.

**Tests:**
- packing with spacing and padding;
- expand versus fixed children;
- alignment;
- grid wrapping;
- nested groups;
- a fitter around a wrapped label;
- `ignoreLayout`;
- hidden children taking no space.

**Python:**
- `create_horizontal_layout`, `create_vertical_layout` and `create_grid_layout`;
- `set_layout_element(e, min=, preferred=, flexible=)`;
- `set_size_to_content(e, horizontal, vertical)`.

The showcase sidebar pages are rebuilt with vertical layouts instead of hand-placed rows.

## 10. Scroll views and scrollbars

**`UIScrollView`:**
- `content` (an entity), `horizontal` and `vertical`;
- `scrollPosition` (0..1 per axis) and `wheelSpeed`;
- `inertia` with `decelerationRate`, and `clamp` (no elastic overshoot in this phase).

The view clips its content (`UIClip`). The content is usually a vertical layout with a size fitter.

**`UIScrollbar`:** a track and a handle. The handle's size shows the visible fraction and its position the scroll position. It can be dragged, and a click on the track pages.

**Input** (`CanvasInputSystem`):
- **Wheel:** scrolls the innermost scroll view under the pointer, on screen and world canvases.
- **Dragging the content:** scrolls it once the pointer has moved more than a threshold, 8 canvas units, from the press. The press on a child button is then cancelled, so dragging a list of buttons scrolls rather than clicks.
- **Capture handover:** this needs the pointer capture to pass from the pressed element to its scroll view.

**Tests:**
- wheel scrolling;
- clamping;
- the drag threshold cancelling a child press;
- the scrollbar handle matching the visible fraction;
- inertia decaying to rest;
- nested scroll views (the inner one first).

**Python:**
- `create_scroll_view(parent, size, horizontal=False, vertical=True)`, returning the view;
- `scroll_content(view)`;
- `get_scroll_position` and `set_scroll_position`;
- `scroll_to(view, element)`.

The showcase log becomes a scroll view.

## 11. HarfBuzz shaping and font fallback

**Dependency.** Add `harfbuzz` to `vcpkg.json` and `find_package(harfbuzz CONFIG REQUIRED)`; link it privately into the engine. The Python wheel builds with `x64-windows-static-md`, which links HarfBuzz statically.

**`UI/TextShaper` (pimpl, in front of HarfBuzz):**
- **Itemization.** Split the text into runs of one script, using HarfBuzz's Unicode functions. Common and Inherited characters (spaces, punctuation, combining marks) join the run next to them.
- **Font choice.** Split runs further by font, taking the first font in the element's fallback chain that has the character (`stbtt_FindGlyphIndex`).
- **Shaping.** Shape each run with an `hb_face` made from the same font bytes `SdfFont` holds. Glyph ids therefore match `GlyphCache` and `SdfFont::rasterize` as they are.
- **Output.** Per glyph: font id, glyph id, cluster (index of the first code point), advance and offset, scaled from the bake size to the font size.

**Layout.** `TextLayout` takes shaped runs in place of code points:
- lines break between clusters, never inside one;
- glyphs are placed with HarfBuzz's offsets, so marks sit on their bases;
- stb_truetype pair kerning is dropped, since HarfBuzz applies `kern` and GPOS.

`TextLayoutResult` gains `clusters`: for each cluster, its code-point range and its x position on its line. Text fields need this for carets and selection.

**Font fallback:**
- `FontLibrary::setFallbacks(font, {ids})`;
- a library-wide default chain, used when a font has none;
- `UIText::font` stays the primary font.

**Direction.** Each run is shaped in its own direction, so Arabic and Hebrew words are shaped correctly. A line still orders its runs left to right; full bidirectional reordering (UAX #9) stays out of this phase and is documented.

**Assets.** Add a Tibetan font under the OFL, with its licence file; Noto Serif Tibetan is the candidate. Noto Sans Devanagari is already in `assets/fonts`.

**Tests** (with the shipped fonts):
- **Devanagari:** क्ष forms one conjunct glyph; in कि the vowel sign ि is drawn before क; reph on र्क; the cluster ranges.
- **Tibetan:** the stack སྒྲ draws its subjoined letters below the base.
- **Latin:** ligatures do not split clusters, and kerning matches HarfBuzz's.
- **Fallback:** mixed Latin and Devanagari in one label picks the right font per run.

**Python:**
- `set_font_fallbacks(font, [fonts])`;
- `set_default_font_fallbacks([fonts])`.

**Follow-up.** The dakini temple can draw its Devanagari labels with engine text instead of the Pillow/raqm PNGs it renders now.

## 12. Script-aware line breaking

A table-driven subset of UAX #14, enough for the scripts above. Break opportunities:
- after spaces;
- after the Tibetan tsheg (U+0F0B) and shad (U+0F0D);
- between CJK ideographs;
- after a hyphen;
- never inside a cluster.

`wrap` uses these. A word wider than the box still breaks between clusters, as today it breaks between characters. Full UAX #14 through libunibreak can come later if a language needs it.

**Tests:** Tibetan wrapping only at tsheg and shad, Devanagari keeping clusters whole, CJK breaking per ideograph, and English as now.

## 13. Keyboard plumbing, focus and navigation

**Events** (`VulkanWindow`, `EventSystem`):
- `KeyEvent` gains `action` (press, repeat, release) and `mods`. `pressed` keeps its meaning, so existing code is unchanged.
- A new `CharEvent{codepoint}` comes from `glfwSetCharCallback`.
- Clipboard access goes through `VulkanWindow`: `glfwGetClipboardString` and `glfwSetClipboardString`.

**Input state.** `InputStateComponent` gains, for each frame:
- `textInput` (a UTF-32 string);
- `keyEvents` (key, action, mods);
- gamepad state from `glfwGetGamepadState` for the first connected pad.

**Focus.** `UIFocusState` is owned by `UIContext`, next to the pointer state. It holds `focused` (an entity) and `keyboardCaptured`, which is true while a text field has focus.
- Clicking an interactable focuses it; clicking elsewhere clears focus.
- `UIInteractable` gains `focusedColor` and `navigable`.

**Keyboard capture.** While a text field holds focus, `ApplicationBase`:
- gives the camera and `InputStateComponent::keys` an empty key state;
- skips the 1/2/3 camera-mode keys and `onKeyPressed`;
- makes ESC clear the focus instead of toggling mouse capture (`StandaloneInputHandler` asks the UI first).

Python sees this as `ui.keyboard_captured`.

**Navigation:**
- Tab and Shift+Tab cycle through navigable interactables in draw order.
- Arrow keys and the gamepad D-pad move to the nearest one in that direction, by rect centres.
- Enter, Space or gamepad A activates the focused widget: a button clicks, a toggle flips.
- Left/Right, or the D-pad on a focused slider, steps the slider.
- B or ESC clears focus.

Navigation works on the canvas with the highest sort order that has a navigable element, so an open dialog keeps focus to itself.

**Tests:**
- char events reaching `textInput`;
- repeats being reported;
- focus moving by Tab and by direction;
- activation by Enter;
- capture blocking camera keys;
- ESC clearing focus rather than toggling capture.

**Python:**
- `focus(e)`, `focused_element` and `keyboard_captured`;
- `set_navigable(e, bool)`;
- `engine.input.set_on_char(callback)`.

## 14. Text input fields

**`UITextField`:**
- `text` (UTF-8) and `placeholder`;
- `caret` (a code-point index) and `selectionAnchor`;
- `maxLength`, `password` (draws a mask character), `readOnly`;
- `submitted` and `changed` edges, one frame each.

Single-line only in this phase; multi-line comes later.

**Editing** works on code points and moves by cluster (milestone 11):
- inserting typed text;
- Backspace and Delete;
- Left/Right, with Ctrl jumping by word;
- Home and End;
- Shift extends the selection;
- Ctrl+A, C, X and V, using the clipboard;
- Enter submits.

**Pointer:**
- a click places the caret, using the cluster x positions;
- dragging selects;
- a double-click selects a word.

**Drawing:**
- the field clips its text and scrolls it horizontally to keep the caret in view;
- the caret blinks;
- the selection is drawn as solid quads behind the text, in the same batches.

**Not in this phase:** IME composition (the input method for CJK and similar) and multi-line editing. Both are documented.

**Tests:**
- editing operations on mixed scripts, including a Devanagari conjunct deleted as one cluster;
- selection and clipboard;
- `maxLength`;
- the caret kept in view;
- click-to-caret positions;
- the `submitted` and `changed` edges.

**Python:**
- `create_text_field(parent, placeholder="", size=...)`;
- `get_text` and `set_text`;
- `text_submitted`, `text_changed` and `select_all`.

The showcase gains a name field and a search box that filters the log.

## 15. World-canvas occlusion and exposure-independent canvases

**Occlusion.** Read back the depth under the pointer, one texel a frame:
- a new `RenderGraph::requestPixelReadback(resource, afterPass, pixel)` records a 1×1 `vkCmdCopyImageToBuffer` with its barriers after the named pass;
- the result is ready one frame later, once the fence has been waited on;
- `CanvasInputSystem` turns that depth into a distance along the pointer ray, using the camera's projection, and ignores world-canvas hits farther than it, with a small tolerance.

Pipeline support:
- **Default pipeline:** reads `gDepth` after the G-buffer pass.
- **Configuration:** a pipeline names its depth resource through a top-level `"ui": {"depth": "gDepth", "afterPass": "GBuffer"}` block.
- **Fallback:** without that block, picking behaves as now.

One frame of latency, documented.

Testing a ray against mesh bounds was considered and set aside. It would get boxes wrong for any non-box mesh, and meshes without bounds would never occlude.

**Exposure-independent canvases.** A material parameter, `exposureCompensation` (0 by default, 1 for UI), scales a material's emission by `1 / exposure` before lighting:
- `exposure` is `settings.exposure` times last frame's adapted value from the 1×1 image;
- this needs that image bound in the G-buffer and forward passes: one more binding in the default pipeline and its shaders.

A compensated canvas then reaches the screen at its own brightness. Bloom still adds a halo in proportion to it, and TAA still applies.

- `createWorldCanvas` gets a `lit` mode (albedo slot, no compensation) and an `unlit` mode (emissive slot, compensation 1), with `unlit` the default.
- Copied pipelines without the binding ignore the parameter.

**Tests:**
- the readback's barriers and its one-frame latency;
- a hit hidden behind nearer depth being rejected;
- the pipeline JSON `ui` block parsing;
- exposure compensation turning emission into the expected screen value, using the shipped-shader interface test with the new binding.

## 16. Glyph atlas pages with LRU eviction; tint targets

**Atlas pages:**
- The atlas becomes an `R8_UNORM` 2D array: one 2048² page at first, growing to at most four.
- `GlyphCache` records the frame each glyph and each page was last used.
- When every page is full, the least recently used page is cleared and its glyphs are forgotten, instead of the whole atlas. Its glyphs are rasterized again when next drawn.
- The page index goes in the second byte of `UIVertex::mode`, so the vertex stays 24 bytes. `ui.frag` samples a `sampler2DArray`.
- Reusing a page while an earlier frame still samples it is ordered by the same barrier the atlas uses now.

**Tint targets:**
- `UIInteractable` gains `tintTargets`, a list of entities whose panel, image and text colours also take the state colour.
- The widget helpers fill it in: a button's label, a toggle's box and checkmark, a slider's handle and fill.
- A disabled button then dims its label, and toggles and sliders show hover. The showcase's workaround is removed.

**Tests:**
- page growth;
- LRU eviction choosing the oldest page;
- glyphs from an evicted page coming back;
- the vertex page index;
- tint reaching the targets for every state.

**Related fix found while writing the showcase.** `load_gltf_scene` sets a mesh entity's parent but `SceneAPI::getChildren(root)` returns nothing. The glTF loader appears to set `HierarchyComponent::parent` without adding the child to its parent's `children`. It is outside the UI and gets checked and fixed on its own.

## Verification

As in the first phase:
- run the full C++ (ctest) and Python suites before and after each milestone and report both counts;
- run the C++ `canvas_ui` example and the Python showcase with `SHOONYAKASHA_VALIDATION=1`, with screenshots;
- re-run the existing 2D examples where shared code changes.

Shaping and line breaking get golden tests: glyph ids and positions for fixed strings with the shipped fonts.

## Still out of scope afterwards

- IME composition;
- full bidirectional reordering (UAX #9) and full UAX #14 line breaking;
- multi-line text fields;
- vertical text;
- rich text (inline colour, weight and links);
- accessibility (screen readers);
- elastic scrolling;
- right-to-left layout mirroring.
