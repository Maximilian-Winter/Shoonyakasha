# Canvas UI, phase 3: bidi, line breaking, IME, rich text and accessibility

Status: proposed. Follows [phase 2](2026-10-10-canvas-ui-phase-2.md), whose milestones 9 to 16 are done.

Phase 2 ended with a list of things still out of scope. This plan covers all of them, in nine milestones:
- full bidirectional reordering (UAX #9);
- full line breaking (UAX #14), including Thai, Lao, Khmer and Myanmar;
- right-to-left layout mirroring;
- multi-line text fields;
- input method (IME) composition;
- rich text;
- vertical text;
- accessibility for screen readers;
- elastic scrolling.

Decided with the user:
- every item is in scope;
- bidi comes from SheenBidi (Apache-2.0) and line and word breaking from libunibreak (zlib), both through vcpkg, rather than ICU;
- IME and accessibility target Windows first, behind interfaces that Linux and macOS can implement later.

## What the code has today (checked in source)

- **Direction.** `shapeText` (`UI/TextShaper.h`) splits text into runs of one script and one font. Each run's direction comes from `hb_script_get_horizontal_direction`, so an Arabic or Hebrew run is shaped right to left, with its glyphs in visual order. `layoutText` places the runs in text order, left to right. So a mixed line keeps its words in typing order, and the paragraph direction is always left to right.
- **Line breaking.** `UI/LineBreak.cpp` is a small table covering spaces, the Tibetan tsheg and shad, hyphens, CJK ideographs, kana and Hangul, and opening and closing punctuation. Thai, Lao, Khmer and Myanmar break only at spaces.
- **Words.** `TextEditBuffer::previousWord` and `nextWord` (`UI/TextEditing.h`) treat runs of letters and digits, or of punctuation, separated by spaces as words.
- **Text fields.**
  - `UITextField` holds one line. It scrolls sideways to keep the caret in view.
  - `TextEditBuffer` keeps a caret and an anchor, and moves between `caretStops` (cluster starts).
  - `caretX` and `caretAt` work on a single laid-out line.
- **Typed text.** `VulkanWindow` sets `glfwSetCharCallback`, which publishes `CharEvent`. `InputStateComponent::textInput` collects the characters. GLFW 3.4 has no API for the text being composed in an IME, and the engine never touches the native window handle.
- **Layout direction.** Anchors, pivots, the layout groups and the slider all assume left to right. `TextAlignH` is `Left`, `Center` or `Right`, with no start or end.
- **Text styling.**
  - A `UIText` has one font chain, one size and one colour.
  - `UIVertex::mode` uses its low byte for the draw mode and its second byte for the atlas page. The upper two bytes are free.
  - `SdfFont` shapes through HarfBuzz's own OpenType functions (`hb_font_create` on the font file), which can also read vertical metrics.
- **Fonts shipped.**
  - Roboto, Playfair Display, Noto Sans Devanagari and Noto Serif Tibetan.
  - No CJK, Mongolian, Arabic, Hebrew or Thai font.
- **Scrolling.**
  - `CanvasLayoutSystem` clamps `UIScrollView::scrollPosition` to 0..1 (`CanvasLayout.cpp`).
  - `CanvasInputSystem` coasts after a drag, with `decelerationRate`.
  - Nothing moves past the edges.
- **Accessibility.** There is none: no names, roles or platform provider.

## Order

| # | Milestone | Depends on |
|---|---|---|
| 17 | Elastic scrolling | – |
| 18 | Bidirectional text (SheenBidi) | – |
| 19 | Line and word breaking (libunibreak, word lists) | 18 |
| 20 | Right-to-left layout mirroring | 18 |
| 21 | Multi-line text fields | 18, 19 |
| 22 | Native window hook and IME composition (Windows) | 21 |
| 23 | Rich text | 18 |
| 24 | Vertical text | 19 |
| 25 | Accessibility through UI Automation (Windows) | 22 |

Reasons for the order:
- Bidi comes before line breaking because line breaking is done on the logical text and then reordered per line, and the caret code needs both.
- Multi-line fields come before the IME, so composition is drawn in the final field.
- Accessibility reuses the window hook built for the IME.

Each milestone is finished when it has:
- the C++ components and systems;
- the `UIAPI` methods;
- the Cython bindings;
- tests;
- the reference and guide updated;
- the showcase extended;
- a validation-enabled run.

### Needs approval before code

Under the project's rules, two parts of this plan add a new abstraction or dependency. Each is presented again with its final shape before it is written:
- **The native window hook (milestone 22).** It replaces GLFW's window procedure with one that handles IME and accessibility messages and passes everything else on.
- **Shipped data (milestones 19 and 24):**
  - dictionary word lists for Thai, Lao, Khmer and Myanmar;
  - Arabic, Hebrew, Thai, Mongolian and CJK fonts or font subsets.

  Their sizes are measured and reported before they are added.

## 17. Elastic scrolling

- **New fields.** `UIScrollView` gains `movement` (`Clamped`, the current behaviour, or `Elastic`) and `elasticity`, the spring time in seconds.
- **Drag.** While dragging, an elastic view moves past its edges with growing resistance: the overshoot is `view * (1 - 1 / (d / view * k + 1))` for a drag `d` past the edge, where `view` is the viewport size and `k` is 0.55.
- **Release.** On release, or when coasting reaches an edge, the view springs back with a critically damped spring.
- **Wheel.** The wheel never overshoots.
- **Position past the edges.** `scrollPosition` may leave 0..1 while overshooting, so `CanvasLayoutSystem` clamps only clamped views. Scrollbars show the overshoot by shrinking the handle.

**Tests:**
- the resistance curve;
- the spring settling within `elasticity` and not oscillating;
- a clamped view unchanged;
- the wheel staying in range;
- the scrollbar handle during overshoot.

## 18. Bidirectional text

**Dependency.** `sheenbidi` in `vcpkg.json`.

**Reordering:**
- `layoutText` runs the UAX #9 algorithm per paragraph.
- The paragraph direction is either detected from the first strong character, or set by a new `TextStyle::direction` (`Auto`, `LeftToRight` or `RightToLeft`).
- Lines are still broken on the logical text. Each line's runs are then reordered visually, with SheenBidi's line API.
- Shaping uses the level's direction rather than only the script's, so neutral characters between right-to-left words take the right direction.
- Mirrored characters (brackets) are mirrored by HarfBuzz from the run direction.

**Results.** `TextCluster::x` stays the visual position, and clusters gain `level` and `rightToLeft`.

**Caret and selection:**
- `caretX` uses the cluster's edge on its own side.
- At a direction boundary, the caret is drawn at the edge of the more deeply embedded run (the higher bidi level); only one caret is drawn. That gives every position on screen one caret index, which is what lets the arrows move visually.
- Left and Right move visually. Home and End go to the start and end of the text, in typing order.
- A selection in mixed text is drawn as one rectangle per visual run, so the selection element becomes a list of rects drawn by the batch builder.

**Alignment.** `TextAlignH` gains `Start` and `End`, resolved against the paragraph direction. The default stays `Left`, so existing text is unchanged.

**Fonts.** Noto Sans Arabic and Noto Sans Hebrew are added, after approval of their size, and join the default fallback chain.

**Tests** (golden):
- an Arabic sentence with Latin and numbers;
- Hebrew with brackets;
- nested embeddings with explicit isolates (U+2066 to U+2069);
- caret positions and visual movement across a boundary;
- selection rects;
- `Start` and `End` alignment in both paragraph directions.

## 19. Line and word breaking

**Dependency.** `libunibreak` in `vcpkg.json`.

**Line breaking:**
- `LineBreak.cpp`'s table is replaced by libunibreak's UAX #14 line breaking (`set_linebreaks_utf32`).
- UAX #14 looks at more than two characters, so the pairwise `canBreakBetween` is replaced by `lineBreaks(text, begin, end)`, which gives the break after each code point of a paragraph; `layoutText` asks it once per paragraph.
- Mandatory breaks (`\n`, U+2028, U+2029, CR LF) come from the same source.

**Word breaking:**
- `previousWord`, `nextWord` and `wordAt` use UAX #29 word boundaries (`set_wordbreaks_utf32`).
- So Ctrl+arrow and double-click work in scripts without spaces as far as UAX #29 goes.

**Dictionary scripts:**
- libunibreak does not find word boundaries in Thai, Lao, Khmer or Myanmar.
- A small dictionary breaker does: longest match over a word list, with a fallback to cluster breaks for unknown words. It marks breaks inside runs of those scripts, and both the line and word breaks use them.
- The word lists are ICU's dictionary sources (Unicode license). They are loaded on first use from `assets/text/`.
- Their size is measured and approved before they are added (see above). A Thai font, Noto Sans Thai, comes with them.

**Tests:**
- UAX #14 conformance on a subset of `LineBreakTest.txt`, a few hundred lines kept in `tests/data/`;
- UAX #29 on a subset of `WordBreakTest.txt`;
- Thai and Khmer sentences breaking at their words;
- the existing Tibetan, CJK and punctuation tests passing unchanged.

## 20. Right-to-left layout mirroring

**The setting.** `UIRect` gains `layoutDirection` (`Inherit`, `LeftToRight` or `RightToLeft`), and `UICanvas` a default.

**What a right-to-left element mirrors:**
- anchors and pivot on x, and `anchoredPosition.x`;
- the order of horizontal and grid layout groups, and which side `padding.left` applies to;
- the paragraph direction of text set to `Auto` that has no letter with a direction, and of text set to the new `FromLayout` (which a toggle's label uses), and so where `Start` and `End` align them; `Auto` text with letters keeps the direction of its first one;
- the slider (its minimum on the right), the toggle (box on the right), and the side of a vertical scrollbar;
- a horizontal scroll view's starting position.

**What it does not mirror:** images and nine-slice panels are not flipped. A `mirrorImage` flag on `UIImage` lets an arrow icon flip.

Mirroring happens in `CanvasLayoutSystem` when rects are resolved, so input, hit testing and drawing need no change.

**Tests:**
- every anchor preset mirrored;
- nested direction changes;
- layout groups;
- a slider dragged in a right-to-left canvas;
- `Start` alignment following the canvas.

## 21. Multi-line text fields

**The field:**
- `UITextField` gains `multiLine`, plus `submitOnEnter` (default true for one line; for many lines, Enter inserts a line break and Ctrl+Enter submits).
- A multi-line field wraps, and scrolls vertically to keep the caret in view, and with the wheel. It scrolls itself, as a single line scrolls sideways, rather than putting a scroll view in the viewport: a scroll view would take drags that select text and would need its content sized to the text each frame.
- `TextEditBuffer` works on the whole text.

**Movement:**
- Up and Down move to the caret stop nearest to a remembered goal x on the next line, and PageUp and PageDown move by the visible height.
- Home and End go to the visual line's ends, and Ctrl+Home and Ctrl+End to the text's ends.

**Selection** spans lines as one rect per line segment, reusing milestone 18's rect list.

**Code changes.** `caretX` and `caretAt` take a line, and a new `caretPosition(layout, index)` returns the caret's x, line and baseline.

**Tests:**
- vertical movement with the goal x across short lines;
- wrapping while typing;
- selection rects across three lines;
- PageDown;
- Enter and Ctrl+Enter;
- `maxLength` with line breaks;
- a mixed-direction line inside a multi-line field.

## 22. Native window hook and IME composition (Windows)

**The hook (presented for approval first):**
- `Platform/NativeWindow.h` defines the interfaces below:
  - `setTextInputActive(bool)`;
  - `setTextInputRect(rect)`, where the candidate window goes;
  - the IME events: `composition started`, `composition changed`, `composition ended`, `committed`;
  - a message hook that the accessibility milestone reuses.
- The Windows implementation takes the `HWND` with `glfwGetWin32Window`. It subclasses GLFW's window procedure (`SetWindowLongPtr(GWLP_WNDPROC)`), handles the messages it needs and calls GLFW's procedure for everything else.
- Other platforms get a stub that does nothing, so builds and behaviour there are unchanged.

**IME (through IMM32, which Windows bridges to the Text Services Framework):**
- **The context.** The IME is attached only while a text field has focus (`ImmAssociateContextEx`), so game keys never open a candidate window.
- **Composition.** `WM_IME_COMPOSITION` reads the composition string, its cursor and its clause attributes with `ImmGetCompositionString`. The composition is drawn inline in the field at the caret, underlined (the active clause thicker), without being inserted into the text.
- **Committing.** The result string is inserted as typed text, once. Whether GLFW's `WM_CHAR` path also delivers it is checked first, so it is not inserted twice.
- **The candidate window** is placed below the caret (`ImmSetCandidateWindow`) in window pixels. For a screen canvas the caret is mapped through the canvas scale. For a world canvas it is projected through the camera, and set only while it is in view.
- **Cancelling.** Escape cancels a composition rather than clearing focus.
- **New state.** `UITextField` gains `composition` and `compositionCaret`, and `changed` is not set until text is committed.

**Tests:**
- the composition state machine fed with synthetic events (start, change with clauses, commit, cancel);
- the field drawing the composition without changing `text`;
- the candidate rect for screen and world canvases.

**End to end:** typing Japanese with the Microsoft IME in the showcase's name field, with screenshots of the inline composition and the candidate window. This needs a Japanese IME installed on the test machine, and is reported as not run if there is none.

**As built** (the shape approved before the code, with these differences):
- The four IME events became one: `TextCompositionEvent` carries the whole composition each time it changes, empty when it ends. Committed text is published as `CharEvent`s by the hook, and `WM_IME_COMPOSITION` is not passed on, so Windows never turns it into `WM_CHAR` as well.
- Keys the IME takes arrive as `WM_KEYDOWN` with `VK_PROCESSKEY`, which GLFW 3.4 already drops, so the hook does not filter them.
- `UITextField` keeps the composition as one `TextComposition` (text, caret and clauses) rather than separate fields.
- The candidate rect is computed by `UI::textInputRect` from the caret element's rect; `ApplicationBase` sets it, and turns text input off and on again when focus moves, so a composition does not follow focus into another field.
- The Microsoft IME end-to-end run was not done: no Japanese IME is installed on the test machine. The inline drawing was checked in a real run instead, with the C++ canvas example's script setting a composition for one frame.

## 23. Rich text

**Markup** (opt-in with `UIText::richText`, off by default so existing text containing `<` is unchanged):

| Tag | Effect |
|---|---|
| `<color=#RRGGBB[AA]>…</color>` | colour |
| `<b>…</b>`, `<i>…</i>` | weight and slant |
| `<size=N>…</size>` | font size in canvas units |
| `<font=name>…</font>` | another loaded font chain |
| `<link=id>…</link>` | clickable span |
| `<u>`, `<s>` | underline and strikethrough |

`&lt;` and `&gt;` escape the brackets. Malformed tags are drawn as text.

**Layout.**
- Parsing yields the plain text and attribute runs.
- Shaping breaks runs where the font or size changes.
- Line height follows the largest size on the line.
- The runs survive bidi reordering, because attributes are looked up by code point.

**Bold and italic:**
- A font registered as a family member (`ui.set_font_style(regular, bold=…, italic=…)`) is used when present.
- Otherwise bold is synthesised by lowering the distance-field threshold, through a weight value in `UIVertex::mode`'s third byte, and italic by shearing the glyph quad.
- The vertex stays 24 bytes.

**Links.**
- A click on a link span sets `UIText` link state for one frame, read with `ui.clicked_link(text)`.
- The text element becomes a raycast target only when it has links.
- Hover over a link uses the link colour.

Underline and strikethrough are drawn as solid quads from the font's metrics.

**Tests:**
- parsing (nesting, unclosed and malformed tags, escapes);
- colours on the right glyphs;
- size changes and line height;
- runs in an Arabic line with an embedded Latin link;
- faux bold and italic vertex data;
- link hit testing by cluster.

**As built:**
- `UI/RichText.h` parses the markup and `prepareText` resolves it against the fonts. `layoutText` takes `TextSpan`s, each with its own font chain and size; `shapeText` takes a font chain per code point. Glyphs record their cluster's first code point and their line, and the batch builder looks attributes up by code point.
- `FontLibrary` gained names (a font's file name by default, or `setName`) and bold, italic and bold-italic fonts (`setStyles`); the Python call is `set_font_style(regular, bold=…, italic=…, bold_italic=…)`.
- Link state is on `UIText`: `hoveredLink` and `clickedLink` hold the link's id. The whole rect of a text with links is a raycast target.
- Links are drawn in `linkColor` only while hovered; they get no other style unless the markup gives one.
- Faux bold moves the outline out by `kFauxBoldWeight` atlas units and does not change advances; faux italic slants by `kFauxItalicSlant` about the line's baseline.
- Underline and strikethrough positions come from the text's first font (post and OS/2 tables, through HarfBuzz) at each run's size.
- A text field's own text never reads markup.

## 24. Vertical text

**Orientation.** `TextStyle` gains `orientation` (`Horizontal`, `VerticalRightToLeft` or `VerticalLeftToRight`).

**Writing direction:**
- CJK vertical text runs in columns from right to left.
- Mongolian runs in columns from left to right, its glyphs turned 90 degrees, as the script is written.

**Shaping:**
- Runs are shaped with `HB_DIRECTION_TTB`, which applies the font's `vert` and `vrt2` features and reads `vmtx` advances.
- Latin and digits inside CJK vertical text are turned sideways by default; a `TextStyle` option keeps short runs of digits upright (tate-chū-yoko) for up to two digits.

**Lines become columns:**
- Wrapping uses the box height.
- Alignment and line breaking reuse milestone 19's breaks, applied down the column.
- Layout sizes swap axes for layout groups and size-to-content.

**Not covered:** caret and selection in vertical text, so text fields stay horizontal.

**Fonts.** A CJK font and Noto Sans Mongolian are needed. Full Noto CJK fonts are many megabytes, so the engine ships a subset for its tests and examples (generated with `pyftsubset`, which the OFL allows) and documents how to load a full font.

**Tests** (golden):
- a Japanese column with punctuation (vertical forms from `vert`);
- a sideways Latin word;
- tate-chū-yoko digits;
- a Mongolian line;
- column wrapping and alignment.

## 25. Accessibility through UI Automation (Windows)

**A snapshot.**
- Each frame, after layout, `AccessibilitySystem` builds a snapshot of the screen canvases: a tree of nodes holding role, name, value, state, bounds in window pixels, and the actions supported.
- Screen readers read the snapshot, never the registry.
- Their actions (invoke, toggle, set value, focus, scroll) are queued and applied at the start of the next frame.
- UI Automation calls the provider on the window's thread inside the message pump, between frames, so the snapshot is never read while it is built. This is checked in the milestone, and guarded with a mutex if it does not hold.

**Roles and patterns from components:**

| Component | Role | Pattern |
|---|---|---|
| button | Button | Invoke |
| toggle | CheckBox | Toggle |
| slider | Slider | RangeValue |
| text field | Edit | Value, and Text for selection and caret |
| scroll view | Pane | Scroll |
| text | Text | – |
| canvas | Window or Group | – |

- Names come from a button's or toggle's label, or a new `UIAccessible` component that also sets descriptions, hides decoration, and overrides the role.

**Events.** Focus changes, value changes, toggles and text changes raise UI Automation events, so Narrator follows keyboard navigation.

**The provider.** It answers `WM_GETOBJECT` through milestone 22's window hook, with a fragment root for the window and fragments for the nodes.

**World canvases.** These are not exposed. Their bounds move with the camera, and they are often off screen. This is documented as a limit.

**Tests:**
- snapshot building (roles, names, states, hidden decoration, bounds);
- the action queue applied next frame;
- the provider tree answering COM calls in-process through `IUIAutomation` from a test (Windows only, skipped elsewhere).

**End to end:** Narrator reading and operating the showcase with Tab, Space and arrows, and the tree inspected with Accessibility Insights. Both are manual checks, reported as such.

## Verification

As in the earlier phases:
- run the full C++ (ctest) and Python suites before and after each milestone and report both counts;
- run the C++ `canvas_ui` example and the Python showcase with `SHOONYAKASHA_VALIDATION=1`, with screenshots;
- re-run the earlier text tests unchanged where shared code changes.

Text milestones get golden tests: glyph ids, positions and break positions for fixed strings with the shipped fonts.

## Still out of scope afterwards

- IME and accessibility on Linux and macOS (the interfaces exist; the implementations do not);
- caret and editing in vertical text;
- accessibility for world canvases;
- hyphenation;
- justified text;
- emoji in colour (COLR/CBDT fonts), since the distance-field atlas is single-channel.
