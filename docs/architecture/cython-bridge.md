# Cython Bridge

The Cython bridge connects Python to the C++ Facade layer. This document describes the three-layer architecture, the key files involved, how GLM types and callbacks cross the boundary, GIL safety, and the build process.

---

## Architecture

The bridge has three layers:

```
Python (user code)
    |
    v
.pyx  (Python wrapper classes)      -- implements Engine, Scene, Input, Physics
    |
    v
.pxd  (C++ declarations)            -- tells Cython about C++ types and functions
    |
    v
.pyd  (compiled binary module)       -- the final DLL that Python imports
    |
    v
C++ Facade (EngineAPI, SceneAPI, InputAPI, PhysicsAPI)
```

### .pxd files -- C++ declarations

`.pxd` files are Cython's equivalent of C/C++ header files. They declare external C++ types, functions, and classes so that Cython knows how to generate the correct C++ code. They contain no Python logic.

### .pyx files -- Python wrapper

`.pyx` files contain the actual Python classes and functions. They `cimport` from `.pxd` files to access C++ declarations, then wrap them in Python-friendly classes with type conversions (e.g., tuples to `glm::vec3`).

### .pyd files -- compiled module

The `.pyx` file is compiled by Cython into a `.cpp` file, which is then compiled by MSVC into a `.pyd` (a Windows DLL that Python can `import` directly).

---

## Key Files

### `_facade_types.pxd`

Declares the fundamental types from `Facade/FacadeTypes.h`:

- `EntityHandle` (typedef for `uint32_t`)
- `NullEntity` constant
- Enums: `CameraType`, `LightType`, `RigidBodyType`, `ColliderShape`
- Structs: `EngineConfig`, `GltfOptions`, `ClipInfo`, `GltfResult`

```cython
cdef extern from "Facade/FacadeTypes.h" namespace "Shoonyakasha::Facade":
    ctypedef uint32_t EntityHandle
    cdef EntityHandle NullEntity

    cpdef enum CameraType "Shoonyakasha::Facade::CameraType":
        CameraType_Perspective "Shoonyakasha::Facade::CameraType::Perspective"
        CameraType_Orthographic "Shoonyakasha::Facade::CameraType::Orthographic"
    # ...
```

### `_engine_api.pxd`

Declares all four API classes and the GLM/callback bridge functions in a single file. This consolidation is intentional: Cython cannot "complete" a forward-declared `cppclass` from another `.pxd` file (the first declaration wins), so all classes that reference each other must be declared together.

Contents:

- GLM opaque types (`vec2`, `vec3`, `vec4`, `mat4`) declared from `glm/glm.hpp`
- GLM bridge functions from `_glm_bridge.h` (construction and extraction)
- `std::function` types declared as opaque from `<functional>`
- Callback bridge functions from `_callback_bridge.h`
- Full declarations of `CppEngineAPI`, `CppSceneAPI`, `CppInputAPI`, `CppPhysicsAPI`

### `_shoonyakasha.pyx`

The single `.pyx` file that implements all Python wrapper classes. Having one `.pyx` avoids circular `cimport` issues between classes that reference each other.

Implements:

- `Engine` -- wraps `CppEngineAPI`. Provides `run()`, callback setters, convenience helpers, and properties for accessing `scene`, `input`, `physics`.
- `Scene` -- wraps `CppSceneAPI`. Entity CRUD, component management, transform/camera/light/material/animation access.
- `Input` -- wraps `CppInputAPI`. Key/mouse polling, event callbacks.
- `Physics` -- wraps `CppPhysicsAPI`. World config, forces, velocity.
- `GltfResult` -- pure Python class wrapping the C++ `GltfResult` struct.
- Module-level constants: `NULL_ENTITY`, `CAMERA_PERSPECTIVE`, `LIGHT_DIRECTIONAL`, etc.
- Helper functions for GLM tuple conversion (`_vec3_to_tuple`, `_tuple_to_vec3`, etc.)

### `__init__.py`

Re-exports all public classes and constants from `_shoonyakasha`:

```python
from ._shoonyakasha import (
    Engine, Scene, Input, Physics, GltfResult,
    NULL_ENTITY,
    CAMERA_PERSPECTIVE, CAMERA_ORTHOGRAPHIC,
    LIGHT_DIRECTIONAL, LIGHT_POINT, LIGHT_SPOT,
    RIGIDBODY_STATIC, RIGIDBODY_KINEMATIC, RIGIDBODY_DYNAMIC,
    COLLIDER_BOX, COLLIDER_SPHERE, COLLIDER_CAPSULE, COLLIDER_MESH, COLLIDER_PLANE,
)
```

Users import the package as `import shoonyakasha as sk`.

---

## GLM Bridge

Cython cannot access GLM's `glm::vec3` members directly because GLM uses unions, swizzle operators, and template metaprogramming. The bridge header `_glm_bridge.h` (in the `ShoonyakashaBridge` namespace) provides plain C-compatible functions:

**Construction** -- Python tuple components to GLM types:

```cpp
glm::vec3 make_vec3(float x, float y, float z);
glm::vec4 make_vec4(float x, float y, float z, float w);
glm::mat4 make_mat4(float m00, float m01, ...);  // 16 floats, column-major
```

**Extraction** -- GLM types to individual float components:

```cpp
float vec3_x(const glm::vec3& v);
float vec3_y(const glm::vec3& v);
float vec3_z(const glm::vec3& v);
float mat4_get(const glm::mat4& m, int col, int row);
```

In the `.pyx` file, helper functions convert between Python tuples and GLM types:

```cython
cdef inline tuple _vec3_to_tuple(vec3 v):
    return (vec3_x(v), vec3_y(v), vec3_z(v))

cdef inline vec3 _tuple_to_vec3(tuple t):
    return make_vec3(<float>t[0], <float>t[1], <float>t[2])
```

From Python, positions and directions are plain tuples:

```python
scene.set_position(entity, (0.0, 5.0, 0.0))
pos = scene.get_position(entity)  # returns (0.0, 5.0, 0.0)
```

---

## GIL Safety

Python's Global Interpreter Lock (GIL) must be managed carefully because the engine's main loop runs on the calling thread while Python callbacks are invoked from within C++ code.

### The problem

When Python calls `engine.run()`, the engine enters a blocking loop (Vulkan render loop). If the GIL is held during this loop, no other Python threads can run, and -- critically -- callback invocations from C++ back into Python would deadlock (the C++ code would try to acquire the GIL that `run()` is already holding).

### The solution

The `.pyx` file releases the GIL before calling `run()`:

```cython
def run(self):
    with nogil:
        self._ptr.run()
```

The `_engine_api.pxd` declares `run()` with `nogil`:

```cython
void run() except + nogil
```

When the engine needs to invoke a Python callback (e.g., `onUpdate`, `onInit`), the callback bridge (`_callback_bridge.h`) acquires the GIL first:

```cpp
inline std::function<void(float)> make_update_callback(PyObject* callable) {
    PyRef ref(callable);
    return [ref](float dt) {
        PyGILState_STATE gstate = PyGILState_Ensure();
        PyObject* result = PyObject_CallFunction(ref.obj, "f", dt);
        Py_XDECREF(result);
        if (PyErr_Occurred()) PyErr_Print();
        PyGILState_Release(gstate);
    };
}
```

The `PyRef` struct is a RAII ref-counted holder for `PyObject*`, ensuring the Python callable is not garbage-collected while the C++ `std::function` holds a reference to it.

### Flow diagram

```
Python                          C++ Engine
  |
  |  engine.run()
  |  ---- release GIL --->
  |                              render loop
  |                                |
  |                              onUpdate callback
  |                              ---- acquire GIL --->
  |                                                     call Python callable
  |                              <--- release GIL ----
  |                                |
  |                              continue render loop
  |                                |
  |                              window closed
  |  <--- return (GIL re-acquired)
  |
```

---

## Build Process

The Python bindings are built as part of the CMake project when `BUILD_PYTHON=ON` is specified.

### Steps

1. **CMake configuration** -- `BUILD_PYTHON=ON` enables the Python bindings target. CMake finds the Python development headers and Cython compiler.

2. **Cython compilation** -- Cython compiles `_shoonyakasha.pyx` (along with its `.pxd` imports) into a generated C++ source file (`_shoonyakasha.cpp`).

3. **MSVC compilation** -- The generated C++ file is compiled by MSVC and linked against the engine's static/shared library, producing `_shoonyakasha.pyd`.

4. **Output** -- The `.pyd` file is placed in `python/shoonyakasha/_shoonyakasha.pyd`, alongside the `__init__.py`.

### File layout after build

```
python/
    shoonyakasha/
        __init__.py               -- re-exports classes and constants
        _shoonyakasha.pyd         -- compiled binary module
        _shoonyakasha.pyx         -- Python wrapper source
        _facade_types.pxd         -- C++ type declarations
        _engine_api.pxd           -- C++ API declarations
        _glm_bridge.h             -- GLM conversion bridge
        _callback_bridge.h        -- GIL-safe callback bridge
```

---

## No Wheel Needed

The compiled `.pyd` file is a standard Python extension module. Python's import machinery loads it directly when you `import shoonyakasha`, provided the `python/` directory is on `PYTHONPATH`:

```bash
set PYTHONPATH=H:\cpp_dev\Shoonyakasha\python;%PYTHONPATH%
python -c "import shoonyakasha as sk; print(sk.__version__)"
```

There is no need to build a wheel or install via pip. The `.pyd` is self-contained (it links against the engine library and system Vulkan/Python libraries).

This makes the development loop fast: rebuild the CMake target, and the updated `.pyd` is immediately available to Python scripts.
