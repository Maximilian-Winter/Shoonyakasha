# Facade Pattern

The Facade layer is a set of C++ classes that provide a clean, Python-friendly API over the engine's internals. All Vulkan, EnTT, Bullet3, and GLFW details are hidden behind this boundary.

---

## Purpose

The engine internally depends on:

- **Vulkan** (rendering) -- massive header, C API, complex types
- **EnTT** (ECS) -- template-heavy, `entt::registry` and `entt::entity` are template types
- **Bullet3** (physics) -- C++ class hierarchy with forward declarations
- **GLM** (math) -- union-based vector types with swizzle operators
- **GLFW** (windowing) -- C API with platform-specific headers

Exposing any of these directly to the Python bindings would require Cython to parse their headers, which is impractical or impossible (Cython cannot handle C++ templates, unions, or complex preprocessor logic). The Facade solves this by providing headers that include **only standard library types and simple structs**.

The Facade headers (`include/Facade/`) can be included by Cython without pulling in any engine-internal headers.

---

## PIMPL Implementation

Each Facade API class uses the Pointer to Implementation (PIMPL) idiom:

```cpp
// In EngineAPI.h (public header):
class EngineAPI {
public:
    explicit EngineAPI(const EngineConfig& config);
    ~EngineAPI();
    void run();
    SceneAPI& getScene();
    // ...
private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
```

The `Impl` struct is defined only in the corresponding `.cpp` file in `src/Facade/`. It holds the actual engine objects:

```cpp
// In src/Facade/EngineAPI.cpp (private):
struct EngineAPI::Impl {
    std::unique_ptr<ApplicationBase> app;
    std::unique_ptr<SceneAPI> sceneApi;
    std::unique_ptr<InputAPI> inputApi;
    std::unique_ptr<PhysicsAPI> physicsApi;
    // ... Vulkan, EnTT, Bullet3 types live here
};
```

This means:

- The public header never includes `<vulkan/vulkan.h>`, `<entt/entt.hpp>`, or any Bullet3 header.
- Changing engine internals does not require recompiling the bindings (as long as the Facade API surface is stable).
- The Facade `.cpp` files are the only place where both worlds (internal engine + public API) meet.

---

## EntityHandle

Internally, the engine uses `entt::entity`, which is a `uint32_t` underneath but wrapped in a strongly-typed enum class. The Facade defines its own opaque handle:

```cpp
// In Facade/FacadeTypes.h:
using EntityHandle = uint32_t;
static constexpr EntityHandle NullEntity = UINT32_MAX;
```

Cython sees `EntityHandle` as a plain `uint32_t`. The Facade implementation converts between `EntityHandle` and `entt::entity` internally using static casts:

```cpp
// Internal conversion (in Facade .cpp files):
entt::entity toEntt(EntityHandle h) {
    return static_cast<entt::entity>(h);
}
EntityHandle toHandle(entt::entity e) {
    return static_cast<EntityHandle>(e);
}
```

From Python's perspective, entity handles are just integers:

```python
entity = scene.create_entity("Player")  # returns an int (uint32)
print(entity)                           # e.g., 0
scene.set_position(entity, (0, 5, 0))
```

The null entity is exposed as `sk.NULL_ENTITY` (value `0xFFFFFFFF`).

---

## Type Conversion at the Boundary

The Facade layer converts between internal engine types and Python-compatible types:

### GLM vectors

GLM types (`glm::vec3`, `glm::mat4`, etc.) use union-based memory layouts with swizzle operators that Cython cannot parse. The Facade headers still use `glm::vec3` in their signatures (since GLM's forward declarations are simple enough), but the Cython `.pxd` files declare them as opaque types. A bridge header (`_glm_bridge.h`) provides plain C functions for construction and extraction:

```cpp
// _glm_bridge.h
glm::vec3 make_vec3(float x, float y, float z);
float vec3_x(const glm::vec3& v);
float vec3_y(const glm::vec3& v);
float vec3_z(const glm::vec3& v);
```

On the Python side, GLM vectors become tuples: `(x, y, z)`.

### Callbacks

Python callables are wrapped into `std::function` objects via the `_callback_bridge.h` header. Each bridge function takes a raw `PyObject*`, wraps it in a ref-counted `PyRef` holder, and returns a `std::function` that acquires the GIL before invoking the Python callable.

### Enums

Internal enums (e.g., `CameraComponent::Type::Perspective`) are mirrored as Facade-level enums with stable numeric values:

```cpp
enum class CameraType : uint8_t {
    Perspective  = 0,
    Orthographic = 1
};
```

Python sees these as integer constants: `sk.CAMERA_PERSPECTIVE`, `sk.CAMERA_ORTHOGRAPHIC`.

---

## Why 4 Classes

The Facade is split into four API classes, each wrapping a different slice of the engine:

| Class | Responsibility | Internal Systems Wrapped |
|-------|---------------|--------------------------|
| **EngineAPI** | Engine lifecycle: construction, `run()`, callback registration, convenience helpers (create camera, load glTF, create lights) | `ApplicationBase`, window management, render loop |
| **SceneAPI** | Entity CRUD, component management, transform/camera/light/material/animation access, hierarchy, serialization | `entt::registry`, `ComponentRegistry`, `Scene` |
| **InputAPI** | Key/mouse polling, event callbacks | `InputStateComponent`, GLFW input events |
| **PhysicsAPI** | World configuration, forces/impulses, velocity, body management | `PhysicsSystem`, Bullet3 dynamics world |

This separation matches the Python module design:

```python
import shoonyakasha as sk

engine = sk.Engine(title="My Game", pipeline_json_path="pipeline.json")
scene = engine.scene       # SceneAPI
input = engine.input       # InputAPI
physics = engine.physics   # PhysicsAPI
```

Each class can also be tested in isolation. The engine's test suite includes 64 facade-specific tests that exercise the Facade classes without running the full Vulkan render loop (using a test-only `SceneAPI` constructor that wraps a raw `entt::registry`).

---

## Benefits

1. **Fast compile times for bindings** -- Cython only needs to parse the five Facade headers (FacadeTypes.h, EngineAPI.h, SceneAPI.h, InputAPI.h, PhysicsAPI.h). No Vulkan, Bullet, or EnTT headers are included. This keeps the Cython compilation step under a few seconds.

2. **Stable ABI** -- The Facade headers change rarely. Internal refactoring (e.g., replacing the Vulkan backend, changing component storage) does not break the Python bindings as long as the Facade signatures remain the same.

3. **Clear public API surface** -- The Facade defines exactly what Python can do. There are no accidental leaks of internal types or half-implemented features. The 5 headers in `include/Facade/` are the complete public API.

4. **Testable in isolation** -- The 64 facade tests verify the API layer independently. `SceneAPI` has a `SHOONYAKASHA_TESTING` constructor that accepts a raw `entt::registry` and `ComponentRegistry`, allowing tests to exercise entity/component logic without Vulkan initialization.

---

## File Layout

```
include/Facade/
    FacadeTypes.h       -- EntityHandle, enums, EngineConfig, GltfOptions, GltfResult
    EngineAPI.h         -- Engine lifecycle, callbacks, convenience helpers
    SceneAPI.h          -- Entity CRUD, component access, serialization
    InputAPI.h          -- Key/mouse polling, event callbacks
    PhysicsAPI.h        -- World config, forces, velocity, body management

src/Facade/
    EngineAPI.cpp       -- PIMPL Impl with ApplicationBase
    SceneAPI.cpp        -- PIMPL Impl with entt::registry, ComponentRegistry
    InputAPI.cpp        -- PIMPL Impl with InputStateComponent
    PhysicsAPI.cpp      -- PIMPL Impl with PhysicsSystem
```

All five headers are self-contained. Including any one of them brings in only `<string>`, `<vector>`, `<memory>`, `<functional>`, `<cstdint>`, and `<glm/glm.hpp>`. No transitive engine dependencies leak through.
