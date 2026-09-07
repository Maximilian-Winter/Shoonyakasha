# Script-defined ECS components and systems

`engine.ecs` complements `engine.scene`: Scene manages native components and transforms; Ecs stores named Python payloads and registers per-frame callbacks with the native SystemManager.

## Example

Use this inside init, after constructing the engine:

```python
def on_init():
    entity = engine.scene.create_entity("mover")
    engine.ecs.set_component(entity, "motion", {"speed": 2.0})

    def move(dt):
        for handle in engine.ecs.find_entities_with_component("motion"):
            motion = engine.ecs.get_component(handle, "motion")
            x, y, z = engine.scene.get_position(handle)
            engine.scene.set_position(handle, (x + motion["speed"] * dt, y, z))

    if not engine.ecs.add_system("move", move, priority=-1):
        print("A system named move is already registered")
```

Register `on_init` with the engine before running. Priority -1 places this system before the priority-0 transform system. Lower priorities run earlier; choose priorities deliberately when depending on native systems.

## Ownership and failures

The engine retains the actual Python object, not a serialized copy. Getting it returns that object; replacing/removing it or destroying its entity releases the engine's reference. Missing values return `None`. These payloads are neither shader dot-path sources nor part of scene serialization.

Duplicate system names return false without replacing the existing callback. Python callbacks receive delta seconds. Exceptions are printed and count as failed updates; ordinary return values, including `False`, do not count as failure. After five consecutive exceptions by default, the system disables itself. Successful updates reset the counter. A failure limit ≤0 disables that protection.

After fixing a failing system, reset its counter and explicitly re-enable it. Register/remove systems during setup or outside system iteration; no deferred-mutation contract is established for modifying the system list from its own callbacks.

Native C++ uses `EcsAPI::setComponent` with `std::shared_ptr<void>` and `addSystem` with a `bool(float)` callback, where false does report failure.

References: [Python Ecs](../api/python/ecs.md), [C++ EcsAPI](../api/cpp/ecs-api.md), [runnable demo](../../examples/python/getting_started/ecs_bindings_demo/ecs_bindings_demo.py).
