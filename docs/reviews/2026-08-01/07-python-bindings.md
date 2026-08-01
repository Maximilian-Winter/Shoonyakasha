# Python / Cython Binding Layer Review

## Scope

Files read in full:

- `python/shoonyakasha/_shoonyakasha.pyx` (1239 lines)
- `python/shoonyakasha/_engine_api.pxd`, `_facade_types.pxd`
- `python/shoonyakasha/_callback_bridge.h`, `_ecs_bridge.h`, `_glm_bridge.h`
- `python/shoonyakasha/__init__.py`
- `python/CMakeLists.txt`, `pyproject.toml`, `.gitignore`
- `python/examples/demo.py`, `ecs_bindings_demo.py`, `skinned_fox_demo.py`
- `examples/full_showcase/showcase_demo.py` (+ spot-check of `examples/sprite_ui_test/sprite_ui_demo.py`)
- `docs/architecture/cython-bridge.md`, `docs/api/python/{constants,engine,gltf-result,input,physics,scene}.md`
- C++ side: `include/Facade/{EngineAPI,SceneAPI,InputAPI,PhysicsAPI,EcsAPI,FacadeTypes}.h`, plus targeted reads of `src/Facade/EngineAPI.cpp`, `src/Facade/SceneAPI.cpp`, `src/Facade/EcsAPI.cpp`

Read-only. Nothing was edited.

---

## Exposed API inventory

Five Python types, all in one `.pyx` (deliberately, to dodge circular `cimport` — `_shoonyakasha.pyx:8`).

| Python type | Wraps | Kind |
|---|---|---|
| `Engine` | `Facade::EngineAPI` | `cdef class`, owns the C++ object (`_shoonyakasha.pyx:918`, `:968`) |
| `Scene` | `Facade::SceneAPI` | `cdef class`, borrowed pointer (`:188-189`) |
| `Input` | `Facade::InputAPI` | `cdef class`, borrowed pointer (`:604-605`) |
| `Physics` | `Facade::PhysicsAPI` | `cdef class`, borrowed pointer (`:670-671`) |
| `Ecs` | `Facade::EcsAPI` | `cdef class`, borrowed pointer (`:786-787`) |
| `GltfResult` | `Facade::GltfResult` | plain Python class with `__slots__` (`:130-155`) |

Method counts (all snake_case, all with at least a one-line docstring except the bulk of the Scene getters/setters):

- `Engine`: 8 callback setters (`:987-1025`), 4 sub-API properties (`:1029-1063`), 7 creation helpers (`:1067-1206`), 2 read-only properties, 5 `set_custom_*` (`:1220-1238`), `run()`.
- `Scene`: ~78 methods spanning entity lifecycle, queries, string-keyed components, name/tag/active, transform, camera, light, material, sprite/UI, text, renderable flags, hierarchy, animation, serialization (`:199-591`).
- `Input`: 6 polling + 4 event-callback setters (`:617-657`).
- `Physics`: 4 properties + 3 force methods + 4 velocity methods + `rebuild_body` + `body_count` (`:683-760`).
- `Ecs`: 6 script-component methods + 10 system-management methods (`:799-896`).

Module constants (`:55-92`): `NULL_ENTITY`, `CAMERA_*` (2), `LIGHT_*` (3), `RIGIDBODY_*` (3), `COLLIDER_*` (5), `UI_ANCHOR_*` (9), `TEXT_ALIGN_*` (3).

---

## Coverage gaps vs C++ facade

**Against the facade itself, coverage is effectively 100%.** I diffed every public method in the five facade headers against `_engine_api.pxd` and `_shoonyakasha.pyx`. The only unexposed public member is `SceneAPI::wireSprite2DManager` (`include/Facade/SceneAPI.h:318`), which the header itself marks as internal wiring. There are no missing `EngineAPI`, `InputAPI`, `PhysicsAPI`, or `EcsAPI` methods. That is unusual and worth stating plainly — the binding tracks the facade closely.

The real gaps are one layer down: things the *facade* doesn't expose, so Python can't reach them either.

1. **Physics body configuration is unreachable.** `RIGIDBODY_*` and `COLLIDER_*` are exported to Python (`_shoonyakasha.pyx:67-76`) but **no facade method anywhere accepts a `RigidBodyType` or `ColliderShape`** — I grepped `include/Facade/` and `src/Facade/` and the only hits are the enum definitions in `FacadeTypes.h`. From Python you can do `scene.add_component(e, "RigidBody")` and get whatever the default-constructed component is; you cannot set mass, body type, collider shape, half-extents, radius, friction, or restitution. These 8 constants are dead API. See Findings F7.
2. **Spot lights** are creatable by type (`set_light_type(e, LIGHT_SPOT)`) but cone inner/outer angle has no accessor.
3. **No mesh assignment.** You can create an entity and give it a `Transform`, but there's no way to attach geometry except by loading a glTF. No primitive helpers (`create_cube` etc.), no mesh-from-buffer path.
4. **No render-graph introspection or control** from Python: no resource read-back, no screenshot, no pass enable/disable, no reload. `set_custom_*` (5 scalar setters) is the entire data path into the graph — no arrays, no buffers, no images.
5. **No window control**: no close/quit, no title change, no fullscreen, no vsync, no cursor mode. There is no programmatic way to end `run()` from Python at all — see F5.
6. **No collision/trigger events** surfaced to Python despite `PhysicsAPI` being present.
7. **No raycast / picking**.
8. **`Ecs` is not exported from the package** — `__init__.py:13-44` omits it, as it omits `UI_ANCHOR_*` and `TEXT_ALIGN_*`. `engine.ecs` works (it's a property), but `sk.Ecs` and `sk.UI_ANCHOR_TOP_LEFT` are `AttributeError`. See F1.

---

## Features

Things this layer genuinely gets right, and I want to be specific because several of them are the parts people usually get wrong:

- **GIL handoff for `run()` is correct.** `Engine.run` releases the GIL (`_shoonyakasha.pyx:982-983`) and the pxd declares `void run() except + nogil` (`_engine_api.pxd:346`). Callbacks re-acquire via `PyGILState_Ensure`/`Release` (`_callback_bridge.h:47,51`). Because the main thread's tstate is still registered in the GILState TSS after Cython's `PyEval_SaveThread`, `PyGILState_Ensure` correctly finds it and does `PyEval_RestoreThread` rather than creating a new one. Nesting is counter-based, so a callback that calls back into a binding method that triggers another callback is safe. I found no GIL bug in the acquire/release logic itself.
- **`wrap_py_object`'s deleter acquires the GIL before `Py_XDECREF`** (`_ecs_bridge.h:42-44`). This is the correct and non-obvious thing to do, since the last `shared_ptr` owner may be the engine (e.g. `destroyEntity` from C++), not Python.
- **`unwrap_py_object` returning a new reference, declared as `object` in the pxd**, is the correct Cython ownership idiom, and the pxd comment (`_engine_api.pxd:108-112`) documents exactly why — including the null-guard requirement, which `Ecs.get_component` honours (`_shoonyakasha.pyx:816-818`).
- **The `EcsAPI` split is well designed.** Keeping `shared_ptr<void>` / `std::function<bool(float)>` in the engine and all `PyObject*` marshalling in `python/shoonyakasha/_ecs_bridge.h` means the engine core has zero Python dependency. The failure-counting/auto-disable contract (`include/Facade/EcsAPI.h:88-96`) is a sensible answer to "a script system raised" and the Python side surfaces it fully.
- **Docstrings are present and accurate** where they exist, including the non-obvious ones: `Ecs.find_entities_with_component` documents its O(n) scan (`:836-841`), `set_render_layer_mask`/`set_sort_key` explain the JSON pass interaction (`:503-514`).
- **Naming is idiomatic**: consistent snake_case, `is_`/`get_`/`set_` prefixes match Python convention, properties used where the C++ side is a getter/setter pair (`physics.gravity`, `scene.entity_count`, `engine.delta_time`).
- `GltfResult` as a plain Python class with `__slots__` and a useful `__repr__` (`:148-155`) is the right call over a `cdef class`.

---

## Limitations

- **Single-threaded by contract, unenforced.** Only `run()` is `nogil`. Every other binding call holds the GIL for its whole duration, and none of them are safe to call from a Python thread while the render loop is running — nothing in the engine synchronizes ECS access. Nothing in the bindings or docs says so.
- **Callbacks never propagate exceptions.** Every bridge swallows via `PyErr_Print()` (`_callback_bridge.h:50,66,82,98,115,131`; `_ecs_bridge.h:69`). There is no opt-in strict mode.
- **No type stubs.** No `.pyi` anywhere in the repo (`git ls-files | grep .pyi` is empty). Because these are `cdef class` extension types, IDEs and mypy get nothing — not even signatures.
- **No Python tests.** `tests/` has `ecs/`, `facade/`, `framegraph/`, `unit/` — all C++. There is no `conftest.py`, no `test_*.py` for the bindings. The `sk.UI_ANCHOR_*` bug (F1) survives precisely because nothing imports the package in CI.
- **`load_gltf_scene` uses `**kwargs`**, so misspelled options are silently ignored (`:1094-1114`).
- **No numpy / buffer-protocol interop.** All vector traffic is Python tuples, one call per value.

---

## Findings

### F1 — CRITICAL: `sk.UI_ANCHOR_*` does not exist; two shipped examples crash on startup

`__init__.py:13-44` re-exports an explicit list that omits `UI_ANCHOR_*`, `TEXT_ALIGN_*`, and `Ecs`. The constants *are* defined at module level in the extension (`_shoonyakasha.pyx:79-92`), but only reachable as `shoonyakasha._shoonyakasha.UI_ANCHOR_TOP_LEFT`.

Both UI examples use the package-level name:

- `examples/full_showcase/showcase_demo.py:132` — `anchor=sk.UI_ANCHOR_MIDDLE_CENTER`, also `:155`, `:165`
- `examples/sprite_ui_test/sprite_ui_demo.py:53,62,72` — `sk.UI_ANCHOR_TOP_LEFT`, `sk.UI_ANCHOR_BOTTOM_RIGHT`

Both raise `AttributeError: module 'shoonyakasha' has no attribute 'UI_ANCHOR_MIDDLE_CENTER'` inside `on_init`. Because `on_init` runs through `make_void_callback`, the traceback is printed by `PyErr_Print()` (`_callback_bridge.h:50`) and **the engine continues running with a half-built scene** — so it doesn't even fail loudly. Fix: add `UI_ANCHOR_*`, `TEXT_ALIGN_*`, and `Ecs` to the import list and `__all__` in `__init__.py`.

### F2 — CRITICAL: sub-API wrappers hold a raw pointer with no reference to the owning `Engine`

`Scene`, `Input`, `Physics`, `Ecs` each store only `_ptr` and `_owned` (`_shoonyakasha.pyx:188-189`, `:604-605`, `:670-671`, `:786-787`). The `Engine` holds strong references to them (`:921-924`), but **not the reverse**. `Engine.__dealloc__` does `del self._ptr` (`:970-972`), destroying the `EngineAPI` and every sub-API it owns.

```python
scene = sk.Engine(pipeline_json_path="p.json").scene   # Engine is a temporary
scene.create_entity("boom")                            # use-after-free
```

Any pattern that outlives the `Engine` — storing `engine.scene` on an object, returning it from a factory, `functools.partial(engine.scene.set_position, e)` — dangles. Fix: add `cdef object _owner` to each wrapper and assign the `Engine` in the property getters (`:1032-1063`).

### F3 — CRITICAL: `Scene()` / `Input()` / `Physics()` / `Ecs()` are directly constructible and segfault

`__cinit__` sets `_ptr = NULL` (`:191-193`, `:607-609`, `:673-675`, `:789-791`) and no method NULL-checks before dereferencing. Nothing prevents Python from calling the type:

```python
sk.Scene().create_entity("x")   # self._ptr is NULL -> null deref at :204
```

The docstrings say "Obtained via engine.scene — do not construct directly" (`:185`), but that is not enforcement. Fix: raise `TypeError` from `__init__`, and/or NULL-check in a shared `cdef inline` guard.

### F4 — MAJOR: C++ exceptions cross the boundary without `except +` on almost every method

Only four declarations in `_engine_api.pxd` carry `except +`: `createEntity` (`:124`), the `CppEngineAPI` constructor (`:343`), `run()` (`:346`), and `loadGltfScene` (`:367`). Roughly 130 other declarations have none, so Cython generates a bare call with no `try`/`catch` and a thrown C++ exception unwinds out of the extension function through CPython's C frames — `std::terminate` at best, silent stack corruption at worst.

This is not theoretical. `src/` contains 123 `throw` statements, and the throwing translation units include `App/ApplicationBase.cpp`, `Vulkan/VulkanTexture.cpp`, `Vulkan/VulkanImage.cpp`, `Vulkan/VulkanBuffer.cpp`, `Vulkan/VulkanDevice.cpp`. Concretely reachable, unprotected paths:

- `createSprite`, `createUIPanel`, `createText` (`_engine_api.pxd:372-379`) delegate straight to `ApplicationBase::createSprite/createUIPanel/createText` (`src/Facade/EngineAPI.cpp:338,348,359`), which load textures and fonts and upload to the GPU. A missing `orb.png` or `font.ttf` — exactly what `showcase_demo.py` risks, per its own docstring at line 28 — goes down this path.
- `setMaterialTexture` / `setSpriteTexture` (`_engine_api.pxd:196,199`) — same texture-upload path.
- `saveToFile` / `loadFromFile` (`_engine_api.pxd:248-249`).

Fix: add `except +` to every declaration that can reach engine code. There is no downside — `except +` costs nothing when nothing throws.

### F5 — MAJOR: `KeyboardInterrupt` cannot stop `run()`, and `sys.exit()` kills the process mid-frame

Both stem from `PyErr_Print()` being the universal handler (`_callback_bridge.h:50,66,82,98,115,131`; `_ecs_bridge.h:69`).

- **Ctrl+C**: the main thread sits in `run()` with the GIL released, so the pending-call flag is only serviced when a callback re-enters the eval loop. `KeyboardInterrupt` is then raised inside `on_update`, caught by `PyErr_Occurred()`, printed, and discarded. The loop continues. Ctrl+C prints a traceback every frame and never exits. Combined with the total absence of a `close()`/`quit()` binding, **there is no in-Python way to terminate a running engine.**
- **`sys.exit()`**: `PyErr_Print()` special-cases `SystemExit` by calling `_Py_HandleSystemExit` → `Py_Exit()`. A `sys.exit()` anywhere in a callback or ECS system terminates the process from inside the render loop — no `vkDeviceWaitIdle`, no `on_cleanup`, no Vulkan teardown.

Fix: check `PyErr_ExceptionMatches(PyExc_KeyboardInterrupt)` / `PyExc_SystemExit` in the bridges and signal the engine to stop cleanly instead of printing or exiting.

### F6 — MAJOR: silent failure is the default for every callback

Independent of F5, an ordinary bug in `on_init` (typo, `KeyError`, bad path) prints a traceback and the engine proceeds as if initialization succeeded. There's no exit code, no exception surfaced from `run()`, no way to opt into strict behaviour. For `on_init` in particular — which is where scene construction happens — continuing is close to always wrong. F1 is the live demonstration: two shipped examples fail their entire `on_init` and still open a window.

### F7 — MAJOR: 8 exported constants have no API that accepts them

`RIGIDBODY_STATIC/KINEMATIC/DYNAMIC` and `COLLIDER_BOX/SPHERE/CAPSULE/MESH/PLANE` (`_shoonyakasha.pyx:67-76`, re-exported at `__init__.py:34-43`) correspond to `RigidBodyType` / `ColliderShape` in `FacadeTypes.h:42-54`, and **no facade method takes either type** (verified by grep across `include/Facade/` and `src/Facade/`). `docs/api/python/constants.md` documents all eight in detail, and its "examples" for both groups (lines 104-112 and 130-138) only `print()` the values — the docs are working around the same hole. Either add `set_rigidbody_type`/`set_collider_shape` to `PhysicsAPI`, or drop the constants.

### F8 — MAJOR: `load_gltf_scene(**kwargs)` silently ignores unknown keys

`_shoonyakasha.pyx:1084-1114` tests ten specific keys with `if 'x' in kwargs`. `engine.load_gltf_scene("a.glb", load_texture=False)` (singular typo) silently loads textures. `docs/api/python/engine.md:441-453` presents all ten as if they were named parameters, which makes a typo look like a supported call. Fix: declare them as real keyword arguments with defaults, or raise `TypeError` on leftover keys.

### F9 — MAJOR: `requires-python = ">=3.8"` but the code needs 3.9+

`_callback_bridge.h:48` calls `PyObject_CallNoArgs`, which became public C API in **CPython 3.9**. On 3.8 the extension does not compile. `pyproject.toml:17` claims `>=3.8`. Fix: bump to `>=3.9` (or use `PyObject_CallObject(obj, NULL)`).

### F10 — MINOR/MAJOR (unverified severity): `PyRef`'s destructor decrefs without the GIL

`_callback_bridge.h:36` — `~PyRef() { Py_XDECREF(obj); }` — with no `PyGILState_Ensure`. This is inconsistent with `_ecs_bridge.h:38-46`, where the author *did* guard the analogous deleter, so the asymmetry looks unintentional.

In the current code I believe it is safe: `src/Facade/EngineAPI.cpp:274-276` shows `setOnX` assigning under the caller's GIL, `:96,104` shows callbacks invoked on the main loop thread, and I found no `std::thread`/`std::async` in `src/Facade/`. So every `std::function` copy/destroy currently happens on the Python thread with the GIL held. But nothing enforces that: the moment any engine subsystem copies a callback onto a worker thread, or an `EngineAPI` is destroyed after `Py_Finalize`, this is refcount corruption. Making `PyRef`'s destructor and copy operations GIL-safe costs almost nothing and removes the invariant.

### F11 — MINOR/MAJOR: `Ecs.get_component` will corrupt memory if any C++ caller stores a non-`PyObject` payload

`unwrap_py_object` does an unchecked `static_cast<PyObject*>(ptr.get())` followed by `Py_XINCREF` (`_ecs_bridge.h:51-55`). `EcsAPI::setComponent` takes an opaque `shared_ptr<void>` and is explicitly documented as language-agnostic (`include/Facade/EcsAPI.h:19-22`), so a C++ caller storing a `shared_ptr<MyStruct>` under a name Python later reads means incrementing a `Py_ssize_t` at an arbitrary address. I grepped `src/` and found **no** current C++ caller of `setComponent` outside `EcsAPI.cpp` itself, so this is latent, not live. Worth a type tag in the payload if mixed C++/Python ECS use is ever intended.

### F12 — MINOR: `Engine._callback_refs` grows without bound and is redundant

`_shoonyakasha.pyx:927,931` declares the list; every `set_on_*` appends (`:989,994,999,1004,1009,1014,1019,1024`) and nothing ever removes. The C++ `PyRef` already holds a reference, so the list is redundant for lifetime purposes; its only effect is that replacing a callback keeps the old one alive forever. `Input`'s four setters (`:643-657`) correctly don't do this, which makes the inconsistency clearer.

### F13 — MINOR: silent narrowing and no enum validation

- `set_render_layer_mask(entity, int mask)` → `<uint8_t>mask` (`:506-508`); `mask=256` silently becomes 0. Same at `set_text_layer_mask` (`:484-486`).
- `set_camera_type` (`:335`), `set_light_type` (`:373`), `set_text_align` (`:482`), `set_ui_anchor` (`:458`), `create_ui_panel` (`:1184`), `create_text` (`:1205`) all cast an unvalidated `int` to a C++ `enum class`. An out-of-range value reaches a C++ `switch` as an invalid enumerator.

### F14 — MINOR: inconsistent UTF-8 decode error handling

Most decodes use `errors='replace'`, but `Scene.get_text` (`:473`) and `Scene.get_component_names` (`:261`) use bare `.decode('utf-8')`, so malformed bytes raise `UnicodeDecodeError` there and not elsewhere.

### F15 — MINOR (performance): per-frame callback dispatch parses a format string

`PyObject_CallFunction(ref.obj, "f", dt)` (`_callback_bridge.h:64`, and `"i"`, `"II"`, `"iO"`, `"ff"` at `:80,96,112,129`; `_ecs_bridge.h:66`) parses the format and builds an argument tuple on **every invocation**. This is the hottest path in the binding: `on_update`, `on_pre_render`, every mouse-move event, and every registered ECS system, every frame. `PyObject_CallOneArg(ref.obj, PyFloat_FromDouble(dt))` avoids the format parse. Not a correctness issue; it is measurable when you have several ECS systems (`showcase_demo.py` registers five).

### F16 — MINOR (performance): tuple conversion allocates on every vector crossing

- `_tuple_to_vec3` takes `object t` and uses `t[0]`/`t[1]`/`t[2]` (`_shoonyakasha.pyx:116-117`), i.e. three generic `PyObject_GetItem` calls with boxed indices rather than the `PyTuple_GET_ITEM` fast path. The upside is that any sequence works (list, numpy row), which is good ergonomics — but the fast path could be added for the tuple case.
- `_vec3_to_tuple` (`:102-103`) allocates one tuple + three floats per call. `get_world_matrix` (`:108-114`, `:313-315`) allocates 5 tuples + 16 floats and makes 16 `mat4_get` calls.
- There are no batch/array APIs, so moving 1000 entities is 1000 boundary crossings.

### F17 — MINOR: long-running calls hold the GIL

`load_gltf_scene` (`:1084-1118`) can run for seconds (disk I/O + texture decode + GPU upload) and never releases the GIL — it's declared `except +` but not `nogil` (`_engine_api.pxd:367`). Same for `saveToFile`/`loadFromFile`. Any Python worker thread the user has is frozen for the duration.

### F18 — MINOR: `Engine.__init__` can be called twice and leaks

`:968` does `self._ptr = new CppEngineAPI(cfg)` unconditionally; a second `engine.__init__(...)` leaks the first engine (and its Vulkan device). Guard with `if self._ptr != NULL: raise RuntimeError(...)`.

### F19 — MINOR (ergonomics): `range` shadows the builtin

`Scene.set_light_range(self, uint32_t entity, float range)` (`:390`) and `Engine.create_point_light(..., float range=15.0)` (`:1136`). Harmless in these bodies but it's a keyword users must type as `range=`, which reads oddly.

---

## Packaging

**Committed binaries — MAJOR.** `.gitignore:57-63` lists `/python/shoonyakasha/_shoonyakasha*.pyd`, `__pycache__/`, `*.pyc`, and repeats `python/shoonyakasha/_shoonyakasha.pyd` at the end — but `git ls-files` shows both are **tracked anyway** (gitignore never untracks):

```
python/shoonyakasha/__pycache__/__init__.cpython-313.pyc
python/shoonyakasha/_shoonyakasha.pyd          (2,436,096 bytes)
```

The `.pyd` is a 2.4 MB Windows CPython 3.13 build, stale by construction (last built Aug 1, while `_shoonyakasha.pyx` was last edited Jul 1), and it bloats every clone. `docs/architecture/cython-bridge.md:242-253` ("No Wheel Needed") appears to be why it was committed. It should be `git rm --cached`'d. Note the sibling `vulkan-1.dll` (789 KB) is present on disk but correctly *not* tracked.

**Wheel build.** `pyproject.toml` drives the existing top-level CMake via scikit-build-core, and the tricky parts are handled thoughtfully and well-commented: `SHOONYAKASHA_INSTALL=OFF` (`:85`) prevents scattering `include/`/`lib/` into site-packages; `build-dir = "build/{wheel_tag}"` (`:67`) avoids cross-interpreter cache reuse; `wheel.exclude` (`:102-105`) stops a stale locally-built `.pyd` from being bundled — which matters precisely because `python/CMakeLists.txt:75-88` writes build output back into the source tree.

Remaining packaging issues:

- **No `delvewheel`/`auditwheel` repair step** and no `[tool.scikit-build.wheel] repair` config. `Shoonyakasha` is a `STATIC` library (`CMakeLists.txt:55`), so engine code is linked in — but Vulkan loader, GLFW, and Bullet runtime deps are not bundled. The resulting wheel is not redistributable; it only works on the build machine. That undercuts having a wheel story at all.
- **`find_package(Python3 REQUIRED COMPONENTS Interpreter Development)`** (`python/CMakeLists.txt:12`) requires the full `Development` component, which pulls in the Python *library*. For extension modules the correct component is `Development.Module`; requiring `Development` breaks on manylinux images and other setups without `libpython.so`.
- **`find_program(CYTHON_EXECUTABLE NAMES cython cython3)`** (`python/CMakeLists.txt:18`) resolves a Cython from `PATH`, which may belong to a different interpreter than the one being built against. `${Python3_EXECUTABLE} -m cython` is the robust form and also guarantees it finds the Cython that scikit-build-core installed into the isolated build env per `pyproject.toml:8`.
- **Version is duplicated**: `pyproject.toml:13` `version = "1.0.0"` and `__init__.py:46` `__version__ = "1.0.0"`, with no sync mechanism.
- **No type stubs** (`.pyi`). Since all five classes are `cdef class`, Python tooling gets zero signature information. For an extension module this is the single highest-leverage ergonomics fix.
- **No `py.typed`** marker (moot until stubs exist).

---

## Docs vs code discrepancies

### `docs/architecture/cython-bridge.md`

| Line | Claim | Reality |
|---|---|---|
| 24, 73, 81-84 | Facade / `.pxd` / `.pyx` consist of `EngineAPI, SceneAPI, InputAPI, PhysicsAPI` | **`EcsAPI`/`Ecs` is missing from all three lists.** It's a substantial subsystem (16 Python methods, `_shoonyakasha.pyx:767-896`) documented nowhere in this file. |
| 49 | Enums are "`CameraType`, `LightType`, `RigidBodyType`, `ColliderShape`" | Omits `UIAnchor` and `TextHAlign` (`_facade_types.pxd:43-57`). |
| 135-136 | `cdef inline vec3 _tuple_to_vec3(tuple t)` | Actual signature takes `object t` (`_shoonyakasha.pyx:116`). The difference is user-visible: lists and other sequences are accepted. |
| 187 | "`PyRef` ... ensuring the Python callable is not garbage-collected" | Accurate, but the doc omits that `~PyRef` decrefs without the GIL (F10). |
| 242-253 | **"No Wheel Needed"** — "There is no need to build a wheel or install via pip" | Directly contradicts `pyproject.toml`, which is a complete scikit-build-core wheel/sdist configuration with `pip install .` instructions in its own comments (`:50-58`). One of the two is out of date; given the pyproject is the newer file, this section is. |
| 247 | `set PYTHONPATH=H:\cpp_dev\Shoonyakasha\python` | Stale path — the repo is at `H:\engine-dev\Shoonyakasha`. |
| 214-224 | Build steps describe MSVC only | The pyproject targets Linux/macOS too (`expand-macos-universal-tags`, `pyproject.toml:109`). |

### `docs/api/python/`

- **No `ecs.md` at all.** The entire `Ecs` class — script components and custom systems, the most novel part of the binding — is undocumented in the API reference. `engine.md:268-318` lists only `scene`, `input`, `physics` as sub-API properties; the `ecs` property (`_shoonyakasha.pyx:1056-1063`) is absent.
- **`engine.md` is missing three creation helpers**: `create_sprite` (`:1151`), `create_ui_panel` (`:1168`), `create_text` (`:1187`). All three are used by `showcase_demo.py`.
- **`scene.md` is missing seven method groups** that exist in the binding: `set_material_texture` (`:431`); the whole Sprite/UI block `set_sprite_texture`/`set_sprite_color`/`get_sprite_color`/`set_sprite_uv_rect`/`get_sprite_uv_rect`/`is_screen_space_sprite`/`set_ui_anchor`/`get_ui_anchor`/`get_ui_anchor_offset` (`:438-464`); the whole Text block `set_text`/`get_text`/`set_text_color`/`set_text_font_size`/`set_text_align`/`set_text_layer_mask` (`:468-486`); and `get_render_layer_mask`/`set_render_layer_mask`/`get_sort_key`/`set_sort_key` (`:502-515`). Roughly 20 documented-nowhere public methods.
- **`constants.md` omits `UI_ANCHOR_*` (9) and `TEXT_ALIGN_*` (3)** — including from the "Complete Reference Table" (`:143-161`). Its opening instruction "Import them from the top-level package" (`:5-6`) is also wrong for those, per F1.
- **`constants.md:92-138`** documents `RIGIDBODY_*`/`COLLIDER_*` as "Used when configuring physics bodies on entities", which no API supports (F7). `physics.md:288-306` and `:363-382` compound this with a "Physics Setup Checklist" that adds bare `RigidBody`/`Collider` components with no way to configure them.
- **`engine.md:88-89`**: "The engine holds a strong reference to each callback to prevent garbage collection" — true, but understates it: the reference is held permanently, even after replacement (F12).
- **`gltf-result.md`** is accurate against `_shoonyakasha.pyx:130-175`, including the `__repr__` formats. No discrepancies found.
- **`input.md`** is accurate against `_shoonyakasha.pyx:598-657`. No discrepancies found.
- **`physics.md`** property/method list is accurate against `:664-760`; the only issue is the RigidBody/Collider guidance above. Note `physics.md:84` states the `fixed_time_step` default is `1/60` — I did not verify this against `PhysicsAPI.cpp`.

---

## Open questions

1. **Was `_shoonyakasha.pyd` committed deliberately?** `.gitignore` lists it twice, which suggests someone tried to stop tracking it and didn't run `git rm --cached`. Should I confirm before recommending removal — is anyone relying on cloning a prebuilt module?
2. **Is `docs/architecture/cython-bridge.md`'s "No Wheel Needed" section the intended distribution story, or is `pyproject.toml`?** They contradict each other and the fix differs completely depending on the answer.
3. **Does any engine subsystem copy or destroy a `std::function` callback off the main thread?** I checked `src/Facade/` (none) but did not audit `ApplicationBase`/`EventDispatcher`/`InputHandler`. If any do, F10 escalates from latent to live.
4. **Is `EcsAPI` intended to be used from C++ as well as Python?** The header says the payload is deliberately language-agnostic (`include/Facade/EcsAPI.h:19-22`), which is exactly the scenario that makes F11 exploitable. If Python is the only intended client, a comment saying so would close it.
5. **Is `PhysicsAPI` missing rigid-body/collider configuration by design** (deferred to glTF metadata / scene files), or is it an incomplete facade? That determines whether F7's fix is "add setters" or "delete the constants".
6. **Which Python versions are actually supported?** `requires-python = ">=3.8"` is provably wrong (F9), and the only build artifact in the tree is cp313.
