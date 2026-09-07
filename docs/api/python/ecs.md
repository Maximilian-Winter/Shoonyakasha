# Ecs

Access: `engine.ecs`. [Other language reference](../cpp/ecs-api.md).

[Binding implementation](../../../python/shoonyakasha/_shoonyakasha.pyx). Import the package as `import shoonyakasha as sk`.

Adds script-defined component payloads and per-frame systems alongside the built-in ECS. Obtain it during init or later; native component fields remain in SceneAPI.

## Components

`setComponent` / `set_component` attaches or replaces a named payload. C++ stores `std::shared_ptr<void>`; Python stores the actual object with shared ownership, so mutations to a retrieved dictionary or object remain visible. Retrieval returns null/None when absent. Removal returns whether it existed; removal, replacement, and entity destruction release engine ownership.

A query for a name searches entities with script component bags, not arbitrary EnTT component types. Payloads are opaque to rendering and scene serialization; publish shader values separately through the engine's custom-value setters.

## Systems

Registration accepts a name, callback, priority (default 0), and consecutive failure limit (default 5). Lower priority numbers run earlier in SystemManager order. Duplicate names return false without replacement. Use removal or enable/disable controls to manage registered systems.

C++ callbacks return bool: false counts as failure. Python callbacks receive `dt` in seconds; an exception prints a traceback and counts as failure. Python return values are ignored, including `False`. Success resets the consecutive counter; reaching the limit disables the system. A limit ≤0 disables automatic shutdown. Reset the counter and re-enable after addressing the error.

See [script ECS](../../guides/script-ecs.md) for an example and lifecycle constraints.

<!-- BEGIN SOURCE API -->

## Members

Signatures and short descriptions below are extracted from the Cython wrapper; return shapes follow its conversions and the native declarations.

| Member | Returns / property value | Description |
|---|---|---|
| `set_component(entity, name, value)` | None | Attach (or replace) a Python object as a named component on an entity. |
| `get_component(entity, name)` | object or None | Get a previously-attached component's value (None if absent). |
| `has_component(entity, name)` | bool | Calls native `hasComponent`; see the class contract above. |
| `remove_component(entity, name)` | bool | Detach a component. Returns True if it was present. |
| `get_component_names(entity)` | list[str] | Names of all script-defined components attached to an entity. |
| `find_entities_with_component(name)` | list[int] | All entities carrying a component named `name`. |
| `add_system(name, callback, priority=0, max_consecutive_failures=5)` | bool | Register a per-frame system: callback(dt: float). |
| `remove_system(name)` | bool | Unregister a system. Returns True if it was found and removed. |
| `has_system(name)` | bool | Calls native `hasSystem`; see the class contract above. |
| `set_system_enabled(name, enabled)` | bool | Calls native `setSystemEnabled`; see the class contract above. |
| `is_system_enabled(name)` | bool | Calls native `isSystemEnabled`; see the class contract above. |
| `get_system_failure_count(name)` | int | Consecutive failures reported so far (0 if healthy or not found). |
| `get_system_max_failures(name)` | int | Calls native `getSystemMaxFailures`; see the class contract above. |
| `set_system_max_failures(name, max_failures)` | None | Calls native `setSystemMaxFailures`; see the class contract above. |
| `reset_system_failure_count(name)` | None | Calls native `resetSystemFailureCount`; see the class contract above. |

<!-- END SOURCE API -->
