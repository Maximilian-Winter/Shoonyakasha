# Cython bridge

The Python package wraps the facade through `_shoonyakasha.pyx` and `.pxd` declarations. Small C++ bridge headers convert callbacks, GLM values, and shared script payloads. The wheel build uses scikit-build-core to drive the same CMake project as native builds.

## Values and ownership

Python entity handles are integers; vectors cross as tuples; strings are encoded to UTF-8. GltfResult is converted into Python fields and clip tuples. Scene/Input/Physics/Ecs wrappers retain their owning engine so a borrowed facade pointer is not detached from its owner by garbage collection.

`run()` releases the GIL around the native loop. Python callbacks reacquire it; this is not a guarantee that Python systems run concurrently or have native execution speed. Callback references stay alive through the wrapper/bridge.

Ordinary callback exceptions are printed by the bridge. Script-system exceptions additionally report failure to the native failure counter; a Python callback returning False is still successful. C++ exceptions on declared translated calls can propagate back to direct Python callers.

## Public surface and packaging

The package imports pure-Python utilities before attempting the extension. `extension_available()` distinguishes utility-only import from native availability. Accessing a missing engine symbol raises an actionable import error.

Bindings do not expose every facade/config field: `EngineConfig.enableValidation`, native loader root/node lists, and detailed collider fields are examples. Keep `.pxd`, `.pyx`, package exports, and references aligned when changing the bridge.

Sources: [wrapper](../../python/shoonyakasha/_shoonyakasha.pyx), [callback bridge](../../python/shoonyakasha/_callback_bridge.h), [ECS bridge](../../python/shoonyakasha/_ecs_bridge.h). See [build instructions](../../BUILDING.md#python-bindings) and [maintenance](../maintenance.md).
