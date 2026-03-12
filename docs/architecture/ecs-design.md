# ECS Design

The Shoonyakasha engine uses an Entity Component System (ECS) architecture built on the [EnTT](https://github.com/skypjack/entt) library. This document explains why ECS was chosen, how EnTT is integrated, how systems are ordered, and the rules governing component design.

---

## Why ECS

Traditional game engines often use deep inheritance hierarchies where a `GameObject` base class branches into `MeshObject`, `PhysicsObject`, `AnimatedPhysicsMeshObject`, and so on. This leads to the diamond inheritance problem, rigid type hierarchies, and poor cache performance because related data is scattered across the heap.

ECS solves these problems through three principles:

1. **Cache-friendly data layout** -- Components of the same type are stored in contiguous memory pools. When a system iterates over all `TransformComponent` values, the data is already packed in a cache-friendly layout. This matters at scale: iterating 10,000 transforms is a linear memory scan rather than 10,000 pointer chases.

2. **Composable behavior** -- An entity is just an ID. You attach any combination of components to it. A "camera" is an entity with `TransformComponent` + `CameraComponent`. A "physics box" is `TransformComponent` + `RigidBodyComponent` + `ColliderComponent`. No inheritance, no type explosion.

3. **Decoupled systems** -- Systems operate on component queries, not on specific entity types. The `TransformSystem` processes every entity that has a `TransformComponent`, regardless of what other components are attached. Adding a new component type requires no changes to existing systems.

---

## EnTT Integration

The engine uses **EnTT 3.x**, a header-only C++ library that provides:

- **`entt::registry`** -- The central database. It stores all entities and their components. Every subsystem in the engine receives a reference to the same registry. There is one registry per scene.

- **`entt::entity`** -- Just a `uint32_t` identifier. Entities have no data or behavior of their own. They are handles into the registry. The null entity is `entt::null`.

- **Components as plain structs** -- Components are added to entities via `registry.emplace<T>(entity, args...)` and queried via `registry.view<T>()`. No registration macros or base classes are required by EnTT itself (though the engine adds a `ComponentRegistry` for string-based access -- see below).

- **`registry.view<>()`** -- The primary query mechanism. `registry.view<TransformComponent, CameraComponent>()` returns an iterable of all entities that have both components. Views are lightweight and cache-friendly.

Example from the engine's `CameraSystem`:

```cpp
auto cameraView = registry.view<CameraComponent, TransformComponent>();
for (auto entity : cameraView) {
    auto [camera, transform] = cameraView.get<CameraComponent, TransformComponent>(entity);
    camera.projectionMatrix = camera.getProjectionMatrix();
    camera.viewMatrix = glm::inverse(transform.worldMatrix);
}
```

---

## System Priorities

Systems implement the `ISystem` interface and are managed by the `SystemManager`. Each system has a `priority` field (lower numbers run first). The `SystemManager` sorts systems by priority and calls `update()` in order every frame.

```cpp
class ISystem {
public:
    virtual void update(entt::registry& registry, float deltaTime) = 0;
    bool enabled = true;
    int priority = 0;
};
```

The engine's systems run in this order:

| Priority | System | Responsibility |
|----------|--------|----------------|
| -10 | **CameraControllerSystem** | Reads input state, updates camera transform (yaw/pitch/position). Runs before TransformSystem so world matrices reflect camera movement this frame. |
| 0 | **TransformSystem** | Computes local matrices from position/rotation/scale, then propagates world matrices through the parent-child hierarchy. |
| 10 | **CameraSystem** | Computes view and projection matrices from the camera's world transform. Runs after TransformSystem so it uses the updated world matrix. |
| 30 (approx.) | **PhysicsSystem** | Steps the Bullet3 simulation. Syncs kinematic bodies from ECS to Bullet before the step, then syncs dynamic bodies from Bullet back to ECS after the step. |
| 35 | **SkeletalAnimationSystem** | Advances animation playback time, evaluates keyframes via `AnimationEvaluator`, computes final bone matrices, and uploads them to per-entity SSBOs for the GPU. |
| (default) | **LifetimeSystem** | Decrements `timeToLive` on entities with a `LifetimeComponent`. Destroys entities whose lifetime has expired. |

The `SystemManager` re-sorts the system list whenever a new system is added:

```cpp
template<typename T, typename... Args>
T* addSystem(Args&&... args) {
    auto system = std::make_unique<T>(std::forward<Args>(args)...);
    T* ptr = system.get();
    m_systems.emplace_back(std::move(system));
    std::sort(m_systems.begin(), m_systems.end(),
        [](const auto& a, const auto& b) { return a->priority < b->priority; });
    return ptr;
}
```

---

## Component Design Rules

All components in the engine follow these rules:

1. **POD-like structs** -- Components are plain structs with public fields. They have default constructors and trivial copy semantics. Example:

    ```cpp
    struct TransformComponent {
        glm::vec3 position{0.0f};
        glm::vec3 rotation{0.0f};  // Euler angles in radians
        glm::vec3 scale{1.0f};
        glm::mat4 localMatrix{1.0f};
        glm::mat4 worldMatrix{1.0f};
        bool isDirty = true;
    };
    ```

2. **No virtual methods** -- Components never have virtual functions. They are data, not polymorphic objects. This keeps them trivially copyable and avoids vtable overhead.

3. **No logic in components** -- Components store data, not behavior. Helper methods like `TransformComponent::getLocalMatrix()` are permitted as pure computation on the component's own fields, but side effects (modifying other entities, calling subsystems) belong in systems.

4. **Systems do the work** -- All mutation logic lives in systems. The `TransformSystem` computes world matrices. The `CameraSystem` computes view/projection. The `PhysicsSystem` steps the simulation. Components are passive data stores.

5. **Bullet3 pointers are opaque** -- `RigidBodyComponent` and `ColliderComponent` contain `void*` pointers to Bullet3 objects. These are managed exclusively by the `PhysicsSystem` (which uses PIMPL to isolate Bullet headers). No other code touches these pointers.

The engine's core components (defined in `ECS/Core.h`):

| Component | Purpose |
|-----------|---------|
| `TagComponent` | Category label (e.g., "Player", "Enemy") |
| `NameComponent` | Human-readable name |
| `HierarchyComponent` | Parent/children relationships |
| `TransformComponent` | Position, rotation, scale, local/world matrices |
| `CameraComponent` | Perspective/orthographic settings, view/projection matrices |
| `LightComponent` | Light type, color, intensity, range, shadow settings |
| `RigidBodyComponent` | Physics body type, mass, velocity, Bullet3 handle |
| `ColliderComponent` | Collider shape, size, friction, restitution, Bullet3 handle |
| `LifetimeComponent` | Time-to-live countdown |
| `ActiveComponent` | Enabled/disabled flag |

Additional components from other headers:

| Component | Header | Purpose |
|-----------|--------|---------|
| `InputStateComponent` | `ECS/CameraController.h` | Keyboard/mouse state (singleton) |
| `CameraControllerComponent` | `ECS/CameraController.h` | Camera control mode and parameters |
| `SkeletonComponent` | `ECS/SkeletonComponents.h` | Bone hierarchy and SSBO for skinned meshes |
| `AnimationPlaybackComponent` | `ECS/SkeletonComponents.h` | Current clip, time, speed, looping state |
| Render components | `ECS/RenderComponents.h` | Mesh, material, and renderable data |

---

## String-Based Access via ComponentRegistry

EnTT's `registry.emplace<T>()` and `registry.view<T>()` are C++ templates. Cython (and Python in general) cannot instantiate C++ templates at runtime, so the engine provides a `ComponentRegistry` that maps string names to type-erased factory, remover, and checker functions.

### How it works

At startup, all components are registered by name:

```cpp
inline void registerAllComponents(ComponentRegistry& registry) {
    registry.registerComponent<TagComponent>("Tag");
    registry.registerComponent<NameComponent>("Name");
    registry.registerComponent<HierarchyComponent>("Hierarchy");
    registry.registerComponent<TransformComponent>("Transform");
    registry.registerComponent<CameraComponent>("Camera");
    registry.registerComponent<LightComponent>("Light");
    registry.registerComponent<RigidBodyComponent>("RigidBody");
    registry.registerComponent<ColliderComponent>("Collider");
    registry.registerComponent<ActiveComponent>("Active");
}
```

Each `registerComponent<T>(name)` call stores three lambdas:

- **Factory**: `[](registry, entity) { registry.emplace<T>(entity); }`
- **Remover**: `[](registry, entity) { registry.remove<T>(entity); }`
- **Checker**: `[](registry, entity) { return registry.all_of<T>(entity); }`

### Usage from the Facade (and Python)

The `SceneAPI` facade exposes string-based component management:

```cpp
bool addComponent(EntityHandle entity, const std::string& componentName);
bool removeComponent(EntityHandle entity, const std::string& componentName);
bool hasComponent(EntityHandle entity, const std::string& componentName) const;
std::vector<std::string> getComponentNames() const;
```

From Python:

```python
scene.add_component(entity, "RigidBody")
scene.add_component(entity, "Collider")
print(scene.has_component(entity, "Camera"))  # False
print(scene.get_component_names())  # ["Tag", "Name", "Transform", ...]
```

This design bridges the gap between C++ compile-time generics and Python's dynamic nature without sacrificing type safety on the C++ side. The template instantiations happen once at registration time; thereafter, all access is through string dispatch via `std::unordered_map`.
