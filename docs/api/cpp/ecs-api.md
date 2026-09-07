# EcsAPI

Access: `engine.getEcs()`. [Other language reference](../python/ecs.md).

Include `Facade/EcsAPI.h`; namespace `Shoonyakasha::Facade`. [Source declaration](../../../include/Facade/EcsAPI.h).

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

Declarations below are extracted from the public facade header; test constructors and internal wiring are omitted.

```cpp
explicit EcsAPI(ECS::Scene& scene);
~EcsAPI();
void setComponent(EntityHandle entity, const std::string& name, std::shared_ptr<void> data);
std::shared_ptr<void> getComponent(EntityHandle entity, const std::string& name) const;
bool hasComponent(EntityHandle entity, const std::string& name) const;
bool removeComponent(EntityHandle entity, const std::string& name);
std::vector<std::string> getComponentNames(EntityHandle entity) const;
std::vector<EntityHandle> findEntitiesWithComponent(const std::string& name) const;
using SystemUpdateFn = std::function<bool(float)>;
bool addSystem(const std::string& name, SystemUpdateFn fn, int priority = 0, int maxConsecutiveFailures = 5);
bool removeSystem(const std::string& name);
bool hasSystem(const std::string& name) const;
bool setSystemEnabled(const std::string& name, bool enabled);
bool isSystemEnabled(const std::string& name) const;
int getSystemFailureCount(const std::string& name) const;
int getSystemMaxFailures(const std::string& name) const;
void setSystemMaxFailures(const std::string& name, int max);
void resetSystemFailureCount(const std::string& name);
```

<!-- END SOURCE API -->
