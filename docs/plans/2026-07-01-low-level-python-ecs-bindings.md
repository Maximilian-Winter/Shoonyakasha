# Low-Level Python ECS Bindings — Design

**Status:** Proposal, not yet implemented.

## Context

The Python bindings today (`python/shoonyakasha/`) only expose the high-level
`Facade` layer: `EngineAPI`/`SceneAPI` with typed convenience methods
(`create_camera`, `set_material_vec3`, `create_sprite`, ...). Every new piece
of engine data needs a hand-written C++ method + Cython wrapper on both
sides. There's no way for a Python script to define its own component data
or its own per-frame logic that the engine's `SystemManager` runs alongside
`TransformSystem`/`CameraSystem`/etc.

This doc proposes a lower-level binding layer, additive to the existing
Facade (nothing here replaces `SceneAPI`), that exposes:

1. Generic entity/component access for the engine's built-in component types,
   without a new typed method per field.
2. **Custom Python-defined components** — arbitrary Python objects attached
   to entities, readable/writable by both Python and (for iteration
   purposes) C++.
3. **Custom Python-defined systems** — Python callables registered with the
   engine's `SystemManager`, run every frame in priority order alongside the
   built-in systems, with the same ordering guarantees.

## What's already there (and what isn't)

Investigated directly in the engine source before designing this:

- **`ECS::ComponentRegistry`** (`include/ECS/Core.h:212`) is a type-erased
  dispatch table over a **fixed set of C++ types**, registered via
  `registerComponent<T>(name)` at compile time. It's the mechanism behind
  `SceneAPI::addComponent(entity, "Transform")` today. It cannot register a
  brand-new type at runtime — `entt::registry::emplace<T>` needs `T` known
  to the C++ compiler. **New component types from Python cannot use this
  path.**
- **`entt`** is used via its standard templated API only
  (`emplace<T>`/`view<T,U>()`/`try_get<T>`/`all_of<T>`) — no
  `entt::meta`/`entt::any`/`entt::runtime_view` anywhere in the codebase.
  True dynamic per-type storage pools (proper archetype queries for
  Python-defined types) would require introducing entt's runtime reflection,
  which is a much larger undertaking than this proposal covers (see
  "Future: entt::meta" below).
- **`ISystem`/`SystemManager`** (`include/ECS/Systems.h:22`) is a plain
  virtual-dispatch interface, `addSystem<T>()` template-instantiated. There
  is no existing `std::function`-based "system from a callback" hook — it
  needs to be added, but the shape is simple (one new `ISystem` subclass).
- **The Python callback bridge already solves the hard part.**
  `python/shoonyakasha/_callback_bridge.h` wraps a `PyObject*` in a
  ref-counted `PyRef`, and every `make_*_callback` factory returns a
  `std::function` that does:
  ```cpp
  PyGILState_STATE gstate = PyGILState_Ensure();
  PyObject* result = PyObject_CallNoArgs(ref.obj);
  Py_XDECREF(result);
  if (PyErr_Occurred()) PyErr_Print();
  PyGILState_Release(gstate);
  ```
  `PyGILState_Ensure` is safe to call whether or not the calling thread
  already holds the GIL, so this pattern works regardless of the engine's
  threading model — it's exactly the mechanism a Python-defined system's
  per-frame callback needs, and it's already proven (used for
  `on_update`/`on_init`/etc.). Exceptions are caught and printed via
  `PyErr_Print()`, not propagated — silent-but-safe. `Engine._callback_refs`
  (a Python list on the Cython `Engine` class) keeps callables alive against
  GC while C++ holds the raw `PyObject*`; the same pattern needs to extend
  to system callbacks and stored component objects.
- **Serialization and the JSON dot-path resolver are both hand-written per
  component/field** (`ECS/Scene.h` save/load, `FrameGraph/DotPathResolver.cpp`).
  Both are out of scope for v1 — see "Non-goals" below.

## Design

### 1. Component storage: `PyComponentBag`

One new C++ component type, holding **all** of an entity's Python-defined
components in a name-keyed map:

```cpp
// New: include/ECS/PyComponentBag.h
struct PyComponentBag {
    std::unordered_map<std::string, PyRef> components;  // typeName/label -> owned PyObject*
};
```

This mirrors a pattern the codebase already uses for the same reason —
`MaterialComponentV5::params`/`::textures` are exactly this shape (a
generic name-keyed bag) instead of one entt component type per material
property. `PyRef` is `_callback_bridge.h`'s existing ref-counted wrapper,
reused verbatim.

**Why one bag instead of trying to give each Python class its own entt
pool:** entt pools need a concrete C++ type per pool. Faking that per
Python class would mean generating a new C++ type at runtime, which is
what `entt::meta` exists for — real, but heavy, machinery this proposal
explicitly defers (see "Future" section). The bag gets you 90% of the
usefulness (attach/query/mutate arbitrary Python data per entity) for a
fraction of the implementation cost, and it's consistent with an idiom the
engine already uses elsewhere.

**Cost of this choice:** querying "all entities with Python component X" is
`registry.view<PyComponentBag>()` filtered by key presence — O(entities with
*any* Python component), not an indexed O(1) archetype lookup. Fine for
gameplay/UI-level scripting (hundreds to low thousands of entities); not
appropriate for per-particle/per-vertex hot loops, which should stay in C++
components as they do today.

### 2. Custom components from Python

No required base class — plain Python objects (dataclasses, plain classes,
even dicts) work, since attachment is just "put this object in the bag
under this entity, under this name":

```python
class Health:
    def __init__(self, hp=100):
        self.hp = hp
        self.max_hp = hp

e = engine.ecs.create_entity()
engine.ecs.set_component(e, "Health", Health(hp=50))

h = engine.ecs.get_component(e, "Health")   # same object identity, mutate in place
h.hp -= 10

engine.ecs.remove_component(e, "Health")
engine.ecs.has_component(e, "Health")       # bool
```

`set_component`/`get_component` work for *both* custom Python components
and (for convenience) as a fallback name-based path to built-in components
that don't already have a fast typed accessor — but built-ins should
prefer the typed live-views below for performance and ergonomics.

### 3. Live-view accessors for built-in components

Rather than growing more `get_x`/`set_x` string-keyed pairs
(`SceneAPI`'s current style), expose small Cython extension types that hold
`entt::registry*` + `entt::entity` and read/write the real component
in place through property getters/setters — no copy in, no copy back out:

```python
t = engine.ecs.transform(e)      # None if entity has no TransformComponent
t.position = (1.0, 2.0, 3.0)     # writes straight into TransformComponent
t.position                        # reads straight out
t.mark_dirty()                    # if needed, mirrors TransformComponent::isDirty
```

This is genuinely "lower level" than `SceneAPI.set_position(entity, pos)`:
it's the same underlying field access, but as a reusable view object
instead of one function per field, and it composes naturally with a
`view()` query (below) instead of requiring N separate calls per entity.

### 4. Queries

```python
for e, t in engine.ecs.view_transform():           # entt::view<TransformComponent> under the hood
    ...

for e in engine.ecs.view_component("Health"):        # PyComponentBag scan, filtered by key
    h = engine.ecs.get_component(e, "Health")
    ...
```

Built-in typed views (`view_transform`, `view_renderable`, ...) are thin
wrappers per component type the engine already ships (small, finite list,
same "one method per type" cost as the live-view accessors — acceptable
since these are compile-time-known types, unlike custom components).

### 5. Custom systems

A new `ISystem` subclass, `ECS::PyCallbackSystem`, holding a
`std::function<void(float)>` built from `_callback_bridge.h`'s existing
`make_update_callback` — **no new bridge machinery needed for this part**.
The Python callback doesn't take a registry parameter; it closes over
`engine.ecs` (or whatever object it already has) to query/mutate, exactly
like `on_update(dt)` does today:

```python
def health_regen_system(dt):
    for e in engine.ecs.view_component("Health"):
        h = engine.ecs.get_component(e, "Health")
        h.hp = min(h.max_hp, h.hp + 5.0 * dt)

engine.ecs.add_system(health_regen_system, priority=50, name="HealthRegen")
# ...
engine.ecs.set_system_enabled("HealthRegen", False)
engine.ecs.remove_system("HealthRegen")
```

`priority` places it in the same ordered list as the built-ins
(`TransformSystem`=0, `CameraSystem`=10, `LifetimeSystem`=100 today — see
`include/ECS/Scene.h:581`), so e.g. `priority=5` runs after transforms are
resolved but before the camera system, `priority=150` runs after
everything built-in. This reuses `SystemManager`'s existing priority-sort
(`include/ECS/Systems.h:167`) untouched.

`add_system` returns/accepts a `name` so systems are individually
enable/disable/remove-able — `SystemManager` doesn't support lookup-by-name
today, so this needs a small addition (a `unordered_map<string, ISystem*>`
alongside the existing vector, or a lookup helper).

Execution timing (confirmed in `ApplicationBase::update()`,
`src/App/ApplicationBase.cpp:310`): input → `onUpdate` callback → **ECS
systems (priority order)** → render. So a Python system registered here
runs in the same phase as `TransformSystem` etc., strictly before
rendering, and strictly after `on_update`. Existing exception handling
(catch + `PyErr_Print`) applies per-system-per-frame — an exception in one
Python system doesn't take down the frame or other systems, but also
doesn't halt that system's future frames automatically; that's worth a
follow-up (see below).

### 6. Entity handles

Kept as bare `uint32_t`, matching the existing Facade convention
(`entt::entity`'s underlying type) — no new wrapper object, no overhead,
consistent with `SceneAPI` today. An optional object-style `Entity` proxy
(`entity.transform.position = ...`) can be layered on top in pure Python
later without needing any new C++/Cython surface, since it'd just wrap the
functional calls above.

## New files

- `python/shoonyakasha/_ecs_bridge.h` — `PyComponentBag` C++ definition +
  `make_system_callback` (thin reuse of `_callback_bridge.h`'s existing
  factory, just under a clearer name) + component get/set/has helpers
  operating on `PyComponentBag`. Auto-discovered by CMake's existing glob
  (`python/CMakeLists.txt:30`) — no build changes needed.
- `include/ECS/PyComponentBag.h` — the component struct itself, engine-side
  (only meaningful in builds with `BUILD_PYTHON=ON`; guard accordingly so a
  pure-C++ build doesn't need Python headers).
- `include/ECS/PyCallbackSystem.h` — the `ISystem` subclass.
- `python/shoonyakasha/_ecs_api.pxd` + additions to `_shoonyakasha.pyx` —
  `EcsAPI`/`Ecs` Cython wrapper class exposing everything in sections 2-5
  above. Likely reachable as `engine.ecs` alongside today's `engine.scene`.
- Small addition to `ECS::SystemManager` — name-keyed lookup for
  enable/disable/remove by name (currently only iterates a plain vector).

## Non-goals (v1)

- **Serialization.** `Scene::saveToFile`/`loadFromFile` are hand-written
  per component (`ECS/Scene.h:179-388`); a `PyComponentBag` entry can't
  serialize generically without either (a) requiring Python components to
  implement a `to_dict`/`from_dict` protocol the C++ save/load path calls
  back into (adds a GIL-crossing dependency to file I/O), or (b) skipping
  Python components on save/load entirely. Proposed v1 behavior: **skip
  silently** (documented), revisit once there's a concrete need.
- **JSON pipeline / dot-path access to Python component fields.** Shaders
  can't read Python objects; this was never in scope.
- **Archetype-speed queries for custom components.** Documented O(n) scan
  cost above; fine for the intended use (gameplay/UI scripting), not for
  hot per-frame loops over large entity counts.

## Open questions for you

1. **Exception behavior for systems.** Keep the existing catch-and-print
   silent-continue behavior (consistent with `on_update` etc.), or should a
   Python system that raises get auto-disabled after N consecutive failures
   so a broken script doesn't spam stderr every frame forever?
2. **Naming/surface.** `engine.ecs.*` as a new sub-API alongside
   `engine.scene`, or fold this directly into the existing `Scene`
   Cython class (`engine.scene.create_entity`, `engine.scene.add_system`,
   etc.)? The existing `SceneAPI` already owns entity lifecycle
   (`create_entity`/`destroy_entity`), so there's a reasonable case for one
   extended class rather than a parallel one.
3. **Scope for the first implementation pass** — all of sections 2-5, or
   start with just custom systems (section 5, smallest/lowest-risk, reuses
   the most existing infrastructure) and add generic component
   access/live-views in a follow-up?

## Suggested implementation order

1. `PyComponentBag` + `PyCallbackSystem` (C++ only, no Python-visible
   surface yet) + unit tests using the `SHOONYAKASHA_TESTING` pattern
   (`tests/facade/SceneAPITest.cpp` shows the raw-registry test-mode
   constructor to reuse).
2. `SystemManager` name-keyed lookup addition.
3. `_ecs_bridge.h` + Cython `EcsAPI`: `create_entity`/`destroy_entity`
   (if not just reusing `SceneAPI`'s), `set_component`/`get_component`/
   `has_component`/`remove_component`, `add_system`/`remove_system`/
   `set_system_enabled`.
4. Live-view accessors for the most-used built-ins first (`transform`,
   `renderable`), expand as needed.
5. `view_component`/`view_transform` query helpers.
6. Example script + docs page under `docs/guides/`.

## Future: `entt::meta`

If custom-component query performance ever becomes a real bottleneck, the
upgrade path is registering Python component types with `entt::meta` at
first use (reflect a Python class's declared fields once, back it with a
real per-type entt storage pool instead of the bag). That's a materially
bigger project — new dependency on entt's runtime reflection, per-field
marshalling instead of "hand a PyObject* through," and migration of
whatever's built on the bag API. Worth flagging now so the bag-based v1
API surface (`get_component`/`set_component`/`view_component` by name)
is designed to *keep working* if the storage underneath ever changes,
rather than leaking bag-specific details into the Python API.
