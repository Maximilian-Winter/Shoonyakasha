# Python application and content toolkit

`shoonyakasha.toolkit` ships reusable canvas components, parameters, drawings,
plots, playback, lessons and video export inside the Python package. It requires
no additional Python dependencies. Rebuild the native extension after updating:
retained geometry, widget styling and `engine.request_quit()` are new native APIs.

The examples import the installed `shoonyakasha` package, like the existing
examples. They run from their own directories or the repository root without
overriding Python's package search path.

## Existing callbacks

```python
import shoonyakasha as sk
from shoonyakasha.toolkit.app import Context
from shoonyakasha.toolkit.ui import Column, ParameterRow, Button
from shoonyakasha.toolkit.state import Parameter

engine = sk.Engine(title="Toolkit", width=1920, height=1080)
state = {}

def initialize():
    engine.create_camera((0, 0, 4))
    context = Context(engine)
    column = Column(context, size=(360, 260)).place(24, 24)
    gain = Parameter("Gain", 1.0, minimum=0.0, maximum=2.0, step=0.1)
    ParameterRow(context, column, parameter=gain)
    Button(context, column, "Reset", on_click=lambda: gain.set(gain.default))
    state["context"] = context

engine.set_on_init(initialize)
engine.set_on_update(lambda dt: state["context"].update(dt))
engine.set_on_cleanup(lambda: state["context"].dispose())
engine.run()
```

Create contexts during engine initialization. `Context(engine, parent=handle)`
composes inside an existing screen or world canvas without owning that parent.
A context without a parent creates and owns a 1920×1080 reference canvas.
Call `update(dt)` once per engine update and `dispose()` before engine destruction.
Native clicks are produced by the previous frame's input pass. Custom JSON
pipelines still need a `ui_canvas` pass to draw screen components.

Components expose `entity`, `content`, `visible`, `enabled`, `place(...)`, and
idempotent `dispose()`. Component parents own their descendants and subscriptions.
Integer parent handles are external: manage Python ownership explicitly when using
them. The toolkit installs no engine callbacks implicitly.

## Application runner

```python
from shoonyakasha.toolkit.app import Application, Screen
from shoonyakasha.toolkit.ui import Label

class Home(Screen):
    def mount(self, context):
        Label(context, text="Hello, toolkit", size=(600, 60)).place(24, 24)

Application(Home(), title="Hello", width=1920, height=1080).run()
```

The runner owns callback registration. Initialization calls `on_init`, screen
`mount`, and `on_post_init`. Each update polls the context, advances the exporter
or playback, calls screen `update`, then `on_update`. Rendering hooks forward to
`on_pre_render` and `on_post_render`. Assign `application.playback` and
`application.exporter` to enable their dispatch. `set_screen(screen)` unmounts and
disposes the previous screen's context before mounting its replacement.

For an existing engine, obtain `application.callbacks(engine)` and explicitly
forward all corresponding native hooks to the returned callbacks. Call
`application.raise_if_failed()` after the native run loop. Attaching twice raises.
Keep the application alive while forwarding its callbacks; callbacks hold weak
references to avoid a native reference cycle at shutdown.
Callback exceptions request orderly shutdown and are re-raised after cleanup.
Screens must release external resources in `unmount`; contexts release components.
`engine.request_quit()` finishes the current frame and finalizes recording before
the native cleanup callback.

## Components, state and themes

| Module | Interfaces |
|---|---|
| `app` | `Context`, `Screen`, `Application` |
| `ui` | `Theme`, `Component`, `Panel`, `Row`, `Column`, `Grid`, `ScrollPanel`, `Section` |
| `ui` controls | `Label`, `Button`, `Toggle`, `Slider`, `TextField`, `NumericField`, `ChoiceSelector` |
| `ui` composites | `ParameterRow`, `Tabs`, `Modal`, `Tooltip`, `Notification`, `StatusReadout`, `PlaybackControls` |
| `state` | `Value`, `Parameter`, `ParameterSet`, `bind_custom`, `bind_scene` |

`context.apply_theme(Theme(...))` restyles semantic colors, fonts and interaction
states of mounted components. Spacing and dimensions are chosen at construction;
theme changes do not rebuild layouts. Explicit custom drawing and label colors
remain application-owned. Native layout, shaping, navigation and clipping remain
available through entity handles, including accessibility and RTL customization.

`Modal.open()` focuses its close button and gates other controls in that context;
`close()` restores prior focus. Applications with multiple contexts coordinate
global modal behavior themselves. Native `set_widget_part_color(entity, part,
color)` supports `surface`, `text`, `track`, `fill`, and `handle`. Missing parts are
ignored; unknown names raise. `set_interaction_colors(...)` supplies multiplicative
normal, hover, pressed, focused and disabled colors.

`Value(value)` exposes `.value`, `set(value)` and synchronous
`subscribe(callback, immediate=False)`, returning an idempotent unsubscribe handle.
Equal assignments do not notify. `Parameter(name, default, type=None, minimum=None,
maximum=None, step=None, unit="", format=None, choices=None)` validates floats,
integers, booleans and string choices. Numbers must be finite; step grids begin at
the minimum or zero. Format accepts a callable or a string such as `"{:.2f}"`.
Numeric fields commit on Enter and display invalid input without changing the model.

`ParameterSet(*parameters)` supports `assign(mapping)`, `reset()`, `values()`,
`save_preset(name)` and `apply_preset(name)`. Bulk assignments validate all values
before notifying; listeners see the complete new state. Listeners should not
perform failing external operations within these transactions.
`bind_custom(value, engine, key, kind="float")` and
`bind_scene(value, scene, entity, property)` explicitly connect values to engine
setters and return unsubscribe handles. They introduce no new JSON conventions.

## Drawing and plotting

```python
from shoonyakasha.toolkit.drawing import Drawing
from shoonyakasha.toolkit.plots import Plot, Series

# Fragment: context is an initialized Context.
diagram = Drawing(context, size=(400, 200)).place(400, 24)
with diagram.redraw() as draw:
    draw.arrow((20, 100), (180, 50), color=(0.3, 0.7, 0.9, 1))
    draw.circle((260, 100), 40, filled=False)
plot = Plot(context, title="Signal", size=(600, 320)).place(400, 260)
signal = plot.add(Series("x(t)", capacity=4096))
signal.append(0.0, 1.0)
plot.refresh()
```

`Drawing` supports `line`, `polyline` with optional dashes, `arrow`, `rectangle`,
`circle`, and simple filled `polygon`. Use `clear()` and `flush()`, or `redraw()`.
Unchanged commands retain tessellation; geometry uses local canvas units, per-vertex
RGBA, inherited clipping and scaling, and a one-unit alpha fringe. Drawings are
decorative. Self-intersecting polygons are unsupported. No PNG files are generated.
Initial geometry submission waits for a nonzero native layout rectangle;
`context.ready` becomes true once visible drawings have passed that stage.

`Plot` supports linear explicit/automatic `x_range` and `y_range`, grids, ticks,
legend, multiple series, `cursor`, and `markers` tuples `(x, y, color)`.
`Series.append`, `extend`, and `clear` maintain bounded histories. Reduction retains
ordered extrema in viewport-sized buckets and keeps gaps for nonfinite samples.
Call `refresh()` after modifying data in a screen update for same-frame display.
Log axes, SVG and mathematical typesetting are deferred.

## Time, timelines and lessons

Implement `Simulation.reset(seed)` and `Simulation.step(dt)` for managed models.
`Playback(model, timeline=None, parameters=None, seed=0)` owns a `Clock`, defaulting
to 1/120 second steps, 12 interactive steps per frame and retained backlog.
Use `play`, `pause`, `single_step`, `restart`, `reset`, `seek(seconds)`,
`cancel_seek`, and `clock.speed`. Seeking restores defaults and replays at most
240 steps per update; `seeking` and `progress` expose state. Seek targets use the
preceding simulation tick, followed by exact-time presentation-track evaluation.
Cancelling keeps the intermediate state.

`Binding.value(value)`, `Binding.attribute(component, name)`,
`Binding.scene(scene, entity, property)`, and `Binding.camera(...)` define explicit
targets. Camera properties are `position`, Euler-radian `rotation` and `camera_fov`.
Do not let a native camera controller compete with a camera track.
`Track(binding, [(time, value), ...], interpolation="linear")` supports `hold`,
`linear` and `ease_in_out`; integers, booleans, strings and stepped parameters hold.
Tuple values interpolate componentwise. `Timeline(*tracks, duration=None)` rejects
duplicate targets and evaluates from absolute time. Authored parameters are read-only
while playing or seeking; resuming reapplies the authored state. Setters must be
repeatable state assignments, not file writes or other external effects.

`Lesson(*chapters, title=label, caption=label)` combines ordered
`Chapter(title, duration, timeline=None, caption="")` instances. Tracks use local
chapter time and hold between authored chapters. `next`, `previous`, and
`navigate(playback, index)` pause and reset/replay to chapter starts.
Only registered models and bindings participate: Bullet, autonomous GPU state and
engine-time shaders require application adapters. Fixed stepping does not promise
bitwise reproducibility across hardware. Branching, audio and visual editing are deferred.

## Overlays

`shoonyakasha.toolkit.overlay` draws on screen canvases with the engine's UI
directly (no `Context`): cards of readings, a bar of button hints, a sheet of the
controls and a settings menu, in one style (a dark panel with a gold hairline,
gold headings over faint rules).

```python
from shoonyakasha.toolkit import overlay

# Fragment: engine is running (on_init or later) and canvas is a screen canvas.
card = overlay.Card(engine.ui, canvas, corner=(0, 0), max_rows=8)
card.update("FREE CAMERA", [("SPEED", "12 m/s"), ("ORBIT",), ("PERIAPSIS", "40 km")])
bar = overlay.HintBar(engine.ui, canvas)
bar.update([(("W", "S"), "Throttle"), (("pad:A",), "Assist")])
sheet = overlay.HelpOverlay(engine, "CONTROLS", [[("Ship", [(("W", "S"), "Throttle")])]])
menu = overlay.SettingsMenu(engine, "Settings", [
    {"heading": "Sky"},
    {"slider": "Exposure", "min": 0.05, "max": 2.0, "get": get_exposure, "set": set_exposure,
     "format": "%.2f", "keys": ("B", "V"), "hint": "The image's brightness."},
])
```

- `Card(ui, canvas, corner, max_rows, margin=(24, 24), width=300)`: `update(title, rows)`
  with rows ("LABEL", "value") or ("HEADING",); the panel fits its rows and only what
  changed is handed to the canvas. `show(on)`.
- `HintBar(ui, canvas)`: `update(hints)`, each hint (caps, label), centred along the
  bottom and redrawn only when they change; `show(on)`. A cap named "pad:X" is a
  gamepad's button or stick, A, B, X and Y in their usual colours. `KeyCaps(ui, parent)`
  lays hints out at a point of any element. Text is measured with Pillow from the
  default font, or estimated without it.
- `HelpOverlay(engine, title, columns, footer="")`: columns of (heading, rows), each
  row (caps, text), centred over everything; `show(on)`, `visible`.
- `SettingsMenu(engine, title, items, close_keys=("F1",))`: headings, sliders, toggles
  and buttons in a scroll view down the right side, with key caps beside an item that
  has `keys` and a footer showing the `hint` of the item under the pointer. Call
  `update()` each frame: it hands the pointer's changes to the setters and moves the
  widgets to what the getters say. Its widgets stay out of keyboard navigation;
  `pointer_over` says whether the pointer is over it.

`shoonyakasha.planet.settings` gives such items for a planet's sky, clouds, ground
and terrain.

## Capture

`Exporter(engine, playback, context=None)` exposes `start(path, duration=None,
options=ExportOptions(...))`, `update(dt)`, `post_render()`, `on_resize(width,
height)`, `screenshot(path)`, and `cancel()`. Forward the hooks or assign the
exporter to an `Application`. Defaults: 1920×1080, 30 fps, quality 18; options
also include `quit_when_done`. Construct the engine window at the requested size;
export does not resize it.
`engine.framebuffer_size` reports actual pixel dimensions; starting an export with
different dimensions raises before locking controls or starting the encoder.

Frames sample `n/fps` from zero for `ceil(duration*fps)` frames. The model advances
only after the previous sample is confirmed written. Long preparation pauses
recording, and missing captures retry the current sample. Export locks controls;
Initial layout preparation is also excluded from recording, preserving frame zero.
`Button(..., allow_when_locked=True)` can provide cancel UI. Failures restore controls
and raise after finalizing the partial clip. Cancellation retains a partial recording.
Screenshots run after presentation. Existing ffmpeg, synchronous readback and visible
window limitations apply; no audio or headless renderer is added.

Complete examples: [gallery](../../examples/python/ui/toolkit_gallery/README.md)
and [oscillator](../../examples/python/simulations/oscillator/README.md).
