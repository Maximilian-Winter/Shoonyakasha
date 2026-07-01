# Low-Level Python ECS Bindings — Design

**Status:** Implemented (full scope: custom components, custom systems,
new `engine.ecs` sub-API, auto-disable with a configurable failure
threshold). Not build-verified — this development sandbox has no Vulkan
SDK/`glslc`/Cython toolchain; compile and exercise `python/examples/
ecs_bindings_demo.py` in a real build to confirm.

**Decisions made (were open questions below, kept for context):**
1. Failure handling: auto-disable after a *customizable* number of
   consecutive failures (`max_consecutive_failures`, default 5,
   `<= 0` disables auto-disable entirely) — implemented as
   `ECS::CallbackSystem` in `include/ECS/Systems.h`.
2. Surface: new `engine.ecs` sub-API (`Facade::EcsAPI` / Cython `Ecs`),
   not folded into `engine.scene`.
3. Scope: full (generic component access + custom systems), in one pass.

**Implementation deviates from the file list originally sketched below in
one way, for a good reason found while implementing:** `PyComponentBag`
and the Python-callback system were originally planned as core-engine
headers (`include/ECS/PyComponentBag.h`, `include/ECS/PyCallbackSystem.h`)
"guarded" against BUILD_PYTHON=OFF. Turns out no guarding is needed and
none of that has to touch Python at all: `entt::registry` pools don't
require the *core engine library* to know a type in advance — only
whichever translation unit actually calls `registry.emplace<T>()` does.
So the generic bag (`ScriptComponentBag`) and the generic callback-driven
system (`CallbackSystem`) are 100% Python-agnostic C++ (`std::shared_ptr
<void>` / `std::function<bool(float)>`), living in core engine headers
with zero Python dependency, exactly like every other engine component/
system. All Python-specific `PyObject*` marshalling is confined to
`python/shoonyakasha/_ecs_bridge.h`, same separation the existing
`_callback_bridge.h` already uses for `on_update`/etc. This is a cleaner
outcome than what's described in "New files" below — see that section's
note.

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

### 1. Component storage: `ScriptComponentBag`

One new C++ component type, holding **all** of an entity's script-defined
components in a name-keyed map — and, as implemented, genuinely
Python-agnostic (the payload is `std::shared_ptr<void>`, not a `PyRef`
directly), so this lives in core engine headers with no Python dependency
at all (see "Implementation deviates..." note above):

```cpp
// include/ECS/ScriptComponentBag.h
struct ScriptComponentBag {
    std::unordered_map<std::string, std::shared_ptr<void>> components;
};
```

The Python binding wraps a `PyObject*` into that `shared_ptr<void>` at the
edge (`python/shoonyakasha/_ecs_bridge.h`'s `wrap_py_object`, incref on
attach, GIL-safe decref via the shared_ptr's deleter on detach/entity
destruction) — the engine core never touches `PyRef` or `Python.h` at all.

This mirrors a pattern the codebase already uses for the same reason —
`MaterialComponentV5::params`/`::textures` are exactly this shape (a
generic name-keyed bag) instead of one entt component type per material
property.

**Why one bag instead of trying to give each Python class its own entt
pool:** entt pools need a concrete C++ type per pool. Faking that per
Python class would mean generating a new C++ type at runtime, which is
what `entt::meta` exists for — real, but heavy, machinery this proposal
explicitly defers (see "Future" section). The bag gets you 90% of the
usefulness (attach/query/mutate arbitrary Python data per entity) for a
fraction of the implementation cost, and it's consistent with an idiom the
engine already uses elsewhere.

**Cost of this choice:** querying "all entities with a script component X"
is `registry.view<ScriptComponentBag>()` filtered by key presence —
O(entities with *any* script component), not an indexed O(1) archetype
lookup. Fine for
gameplay/UI-level scripting (hundreds to low thousands of entities); not
appropriate for per-particle/per-vertex hot loops, which should stay in C++
components as they do today.

### 2. Custom components from Python

No required base class — plain Python objects (dataclasses, plain classes,
even dicts) work, since attachment is just "put this object in the bag
under this entity, under this name". Entity lifecycle stays on
`engine.scene` (unchanged) — `engine.ecs` only handles the components:

```python
class Health:
    def __init__(self, hp=100):
        self.hp = hp
        self.max_hp = hp

e = engine.scene.create_entity()
engine.ecs.set_component(e, "Health", Health(hp=50))

h = engine.ecs.get_component(e, "Health")   # same object identity, mutate in place
h.hp -= 10

engine.ecs.remove_component(e, "Health")
engine.ecs.has_component(e, "Health")       # bool
```

**As implemented, built-in components stay entirely on `engine.scene`** —
`engine.ecs.set_component`/`get_component` are for script-defined
components only, not a fallback path to `TransformComponent` etc. (see
"Decided" section: the originally-proposed live-view accessors below were
dropped as redundant with `SceneAPI`'s existing typed getters/setters).

### 3. ~~Live-view accessors for built-in components~~ (not implemented)

Originally proposed: Cython extension types holding `entt::registry*` +
`entt::entity` for direct in-place field access on built-ins
(`engine.ecs.transform(e).position = (...)`). Dropped — `SceneAPI` already
provides typed accessors for every built-in component's fields
(`get_position`/`set_material_vec3`/etc.), so this would have been a
second, differently-shaped way to reach the same data. `engine.ecs` stays
focused on what doesn't exist elsewhere: script components and systems.

### 4. Queries

```python
for e in engine.ecs.find_entities_with_component("Health"):  # ScriptComponentBag scan, filtered by key
    h = engine.ecs.get_component(e, "Health")
    ...
```

(Named `find_entities_with_component` rather than `view_component` in the
implementation — reads better as "give me a list," which is what it
returns, rather than implying a live/lazy `entt::view`-style iterator.)

### 5. Custom systems

Implemented as `ECS::CallbackSystem` (`include/ECS/Systems.h`), an
`ISystem` subclass wrapping `std::function<bool(float)>` — a `bool`
return rather than `void`, unlike `_callback_bridge.h`'s other callbacks,
specifically so the auto-disable failure counter (see "Decided" above) has
something to count. `python/shoonyakasha/_ecs_bridge.h`'s
`make_system_update_callback` is the one new bridge factory this needed;
everything else (`PyRef`, the GIL-acquire/release/print-traceback
pattern) is reused as-is from `_callback_bridge.h`.

```python
def health_regen_system(dt):
    for e in engine.ecs.find_entities_with_component("Health"):
        h = engine.ecs.get_component(e, "Health")
        h.hp = min(h.max_hp, h.hp + 5.0 * dt)

engine.ecs.add_system("HealthRegen", health_regen_system, priority=50)
# ...
engine.ecs.set_system_enabled("HealthRegen", False)
engine.ecs.remove_system("HealthRegen")
```

`priority` places it in the same ordered list as the built-ins
(`TransformSystem`=0, `CameraSystem`=10, `LifetimeSystem`=100 today — see
`include/ECS/Scene.h:581`), so e.g. `priority=5` runs after transforms are
resolved but before the camera system, `priority=150` runs after
everything built-in. `SystemManager::addSystem` sorts by priority
*immediately* after construction, before the caller gets a pointer back —
so `CallbackSystem` takes `priority` as a constructor argument (not
`->priority = x` set afterward, which is too late to affect that add's
sort) to guarantee correct placement no matter when a system is
registered at runtime (not just at startup, unlike the built-in systems).

`add_system`/`removeSystem`/`findSystem` by name needed a small addition
to `SystemManager` (`include/ECS/Systems.h`) — an `ISystem::name` field
plus linear-scan lookup/removal by name. `addSystem` on `EcsAPI` refuses
to register a duplicate name (call `remove_system` first to replace one).

Execution timing (confirmed in `ApplicationBase::update()`,
`src/App/ApplicationBase.cpp:310`): input → `onUpdate` callback → **ECS
systems (priority order)** → render. So a Python system registered here
runs in the same phase as `TransformSystem` etc., strictly before
rendering, and strictly after `on_update`.

### 6. Entity handles

Kept as bare `uint32_t`, matching the existing Facade convention
(`entt::entity`'s underlying type) — no new wrapper object, no overhead,
consistent with `SceneAPI` today. An optional object-style `Entity` proxy
(`entity.transform.position = ...`) can be layered on top in pure Python
later without needing any new C++/Cython surface, since it'd just wrap the
functional calls above.

## Files (as actually implemented)

Core engine, zero Python dependency:
- `include/ECS/ScriptComponentBag.h` — the generic opaque bag
  (`std::unordered_map<std::string, std::shared_ptr<void>>`), named
  `ScriptComponentBag` rather than `PyComponentBag` since the type itself
  has no idea what a Python object is.
- `include/ECS/Systems.h` — added `ISystem::name`, `SystemManager::
  findSystem`/`removeSystem` (by name), and `ECS::CallbackSystem` (wraps
  `std::function<bool(float)>`, tracks consecutive failures, auto-disables
  past `maxConsecutiveFailures`).
- `include/Facade/EcsAPI.h` + `src/Facade/EcsAPI.cpp` — new PIMPL Facade
  sub-API (same shape as `SceneAPI`), wired into `EngineAPI::getEcs()`.
  Public methods use only `std::shared_ptr<void>`/`std::function`/std
  types — no entt, no Python, matching every other Facade header's rule.

Python bridge (note: NOT declared in a separate `_ecs_api.pxd` — the
existing `_engine_api.pxd` explicitly consolidates every Facade class
into one file because "Cython cannot 'complete' a forward-declared
cppclass from another .pxd," so `CppEcsAPI` was added there instead):
- `python/shoonyakasha/_ecs_bridge.h` — `wrap_py_object`/`unwrap_py_object`
  (PyObject* <-> the generic `shared_ptr<void>` payload) and
  `make_system_update_callback` (PyObject* callable -> `std::function<bool
  (float)>`, reusing `_callback_bridge.h`'s `PyRef`).
- `python/shoonyakasha/_engine_api.pxd` — `CppEcsAPI` declaration,
  `function_bool_float`, the three bridge function declarations,
  `CppEngineAPI::getEcs()`.
- `python/shoonyakasha/_shoonyakasha.pyx` — new `cdef class Ecs`, exposed
  as `Engine.ecs`, mirroring the `Scene`/`Input`/`Physics` wrapper pattern.

Tests: `tests/ecs/SystemsTest.cpp` (SystemManager name lookup,
`CallbackSystem` construction) and `tests/facade/EcsAPITest.cpp` (full
behavioral coverage of components + systems + auto-disable, using the
`SHOONYAKASHA_TESTING` raw-registry pattern).

Example: `python/examples/ecs_bindings_demo.py`.

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

## Decided (previously open questions)

1. **Exception behavior:** auto-disable, threshold customizable per system
   (`max_consecutive_failures` param on `add_system`, default 5). A raised
   exception is still always caught and its traceback printed (same as
   every other Python callback in this engine) — auto-disable only stops
   *future* frames from re-running a system that keeps failing.
2. **Surface:** new `engine.ecs` sub-API, not folded into `engine.scene`.
   `engine.scene` keeps owning entity lifecycle and built-in typed
   accessors; `engine.ecs` is specifically for script-defined components
   and systems.
3. **Scope:** full — component access (set/get/has/remove/list/query) and
   systems (add/remove/enable/failure-tracking), in one pass. Live-view
   accessors for built-ins (the design's original section 3) were dropped
   as redundant: `SceneAPI` already has typed get/set for every built-in
   component's fields (`get_position`/`set_material_vec3`/etc.) — adding a
   second, different-shaped way to read the same data wasn't worth the
   duplication. `engine.ecs` is for script components and queries only;
   built-in components stay on `engine.scene`.

## Not done / still open

- **Docs page.** No `docs/guides/` page written yet for this feature —
  `python/examples/ecs_bindings_demo.py` is the only usage reference so
  far.
- **Live-view accessors for built-ins**, if ever wanted despite the note
  above (e.g. for perf-sensitive per-frame Python loops over `Transform`
  fields specifically) — not implemented, per the scope decision.
- Everything under "Non-goals (v1)" above (serialization, dot-path access,
  archetype-speed custom-component queries) remains out of scope.

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
