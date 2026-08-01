# ECS, Scene Graph, Gameplay Systems & Core Utilities — Review

## Scope

Files read in full:

- `include/ECS/`: `Core.h`, `Scene.h`, `Systems.h`, `RenderComponents.h`, `SkeletonComponents.h`,
  `Sprite2DComponents.h`, `CameraController.h`, `CameraControllerBuilders.h`, `InputSystem.h`,
  `PhysicsSystem.h`, `ScriptComponentBag.h`, `SkeletalAnimationSystem.h`, `TextRenderSystem.h`,
  `UILayoutSystem.h`
- `src/ECS/`: `PhysicsSystem.cpp`, `SkeletalAnimationSystem.cpp`
- `include/Animation/AnimationEvaluator.h`, `src/Animation/AnimationEvaluator.cpp`,
  `include/Resources/AnimationData.h`
- `include/Core/EventSystem.h`, `src/Core/EventSystem.cpp` (3-line comment stub),
  `include/Core/Logger.h`, `src/Core/Logger.cpp`
- Tests: `tests/ecs/*.cpp` (9 files), `tests/unit/EventSystemTest.cpp`

Supporting greps only (not full reads): `src/App/ApplicationBase.cpp` (system registration + input
copy), `src/Facade/EngineAPI.cpp` (animation system driving), `include/GPU/GPUTypes.h` (GPUBuffer
lifetime).

Read-only review; nothing was modified.

## Component inventory

**23 component types exist.** The README claim of "17+" (`README.md:112`) is technically true but
undersells it; the number registered for name-based/Python access is only **11**
(`Core.h:488-498` registers 9, `CameraController.h:596-599` registers 2 more).

| # | Component | Header:line | Fields | Purpose |
|---|-----------|-------------|--------|---------|
| 1 | `TagComponent` | `ECS/Core.h:30` | `std::string tag` | Group label; `findEntitiesWithTag` does a linear scan |
| 2 | `NameComponent` | `ECS/Core.h:37` | `std::string name` | Display/lookup name; `findEntityByName` is a linear scan |
| 3 | `HierarchyComponent` | `ECS/Core.h:44` | `entt::entity parent`, `std::vector<entt::entity> children` | Parent/child links; both directions stored (must be kept consistent by hand) |
| 4 | `TransformComponent` | `ECS/Core.h:60` | `position`, `rotation` (Euler rad), `scale`, `localMatrix`, `worldMatrix`, `bool isDirty` | TRS + cached matrices. Rotation order Y→X→Z (`Core.h:80-82`) |
| 5 | `CameraComponent` | `ECS/Core.h:111` | `type`, `fov`, `nearPlane`, `farPlane`, `aspectRatio`, `orthoSize`, `viewMatrix`, `projectionMatrix`, `isMainCamera` | Perspective/ortho camera |
| 6 | `LightComponent` | `ECS/Core.h:140` | `type`, `color`, `intensity`, `range`, `constant/linear/quadratic`, `innerCone`, `outerCone`, `castShadows`, `shadowMapSize` | Directional/point/spot light |
| 7 | `RigidBodyComponent` | `ECS/Core.h:163` | `type`, `mass`, `velocity`, `angularVelocity`, `drag`, `angularDrag`, `useGravity`, `isKinematic`, `freezeRotation`, `void* bulletRigidBody` | Bullet body descriptor + opaque handle |
| 8 | `ColliderComponent` | `ECS/Core.h:181` | `shape`, `size`, `center`, `isTrigger`, `friction`, `restitution`, `void* bulletShape` | Collision shape descriptor. `center` is **never read** by the physics code |
| 9 | `LifetimeComponent` | `ECS/Core.h:197` | `timeToLive`, `destroyOnExpire` | Auto-destroy timer |
| 10 | `ActiveComponent` | `ECS/Core.h:204` | `bool active` | Enable flag — **no system reads it** (see Findings F13) |
| 11 | `MeshComponent` | `ECS/RenderComponents.h:31` | `vertexBuffer`, `indexBuffer`, `indexType`, `vertexCount`, `indexCount`, `vertexStride` | GPU geometry handles |
| 12 | `MaterialComponentV5` | `ECS/RenderComponents.h:69` | `params` map, `textures` map, `alphaMode`, `alphaCutoff`, `doubleSided` | Generic material; two hash lookups per parameter fetch |
| 13 | `RenderableTagComponent` | `ECS/RenderComponents.h:184` | `visible`, `castShadows`, `receiveShadows`, `renderLayerMask`, `sortKey` | Render filter marker |
| 14 | `SkeletonComponent` | `ECS/SkeletonComponents.h:33` | `shared_ptr<Skeleton>`, `vector<mat4> boneMatrices`, `GPUBuffer boneSSBO`, `bool dirty` | Per-entity skinning state |
| 15 | `AnimationPlaybackComponent` | `ECS/SkeletonComponents.h:74` | `clips`, `currentClipIndex`, `currentTime`, `speed`, `playing`, `loop`, `jointTranslations/Rotations/Scales` | Playback state + scratch pose buffers |
| 16 | `Sprite2DComponent` | `ECS/Sprite2DComponents.h:35` | `bool screenSpace` | Sprite pass marker |
| 17 | `UIAnchorComponent` | `ECS/Sprite2DComponents.h:47` | `anchor` (9-way enum), `offsetPixels` | Screen-space anchoring |
| 18 | `Text2DComponent` | `ECS/Sprite2DComponents.h:92` | `text`, `font`, `fontSize`, `color`, `hAlign`, `visible`, `sortKey`, `renderLayerMask` | Text label |
| 19 | `TextBakedComponent` | `ECS/Sprite2DComponents.h:124` | 7 `baked*` mirror fields + `vector<entt::entity> glyphEntities` | Internal dirty-tracking for TextRenderSystem |
| 20 | `TextGlyphOwnerComponent` | `ECS/Sprite2DComponents.h:136` | `entt::entity owner` | Back-pointer on generated glyph entities |
| 21 | `InputStateComponent` | `ECS/CameraController.h:34` | `keys[GLFW_KEY_LAST+1]`, `mousePosition`, `lastMousePosition`, `mouseDelta`, `scrollDelta`, `mouseButtons[]`, `mouseCaptured`, `firstMouseCapture` | Singleton-by-convention input state (~350 bytes) |
| 22 | `CameraControllerComponent` | `ECS/CameraController.h:95` | 30+ fields: mode, move/rotate speeds, orbit params, FPS params, `followTarget`, key bindings, internal `velocity`/`currentYaw`/`currentPitch`/`initialized` | Camera control config + state |
| 23 | `ScriptComponentBag` | `ECS/ScriptComponentBag.h:33` | `unordered_map<string, shared_ptr<void>>` | Type-erased bag for Python-defined components |

Not a component despite living in the same header: `SceneEnvironment`
(`ECS/RenderComponents.h:146`) — explicitly documented as scene-level, not per-entity.

A repo-wide grep for `emplace<...>` found no component types outside this list.

## Systems inventory

Two parallel system idioms exist. Most systems derive from `ISystem` (`ECS/Systems.h:24`) and are
owned by `SystemManager` (`ECS/Systems.h:225`); `SkeletalAnimationSystem` does not and is driven
manually.

| System | Where | Priority | What it does |
|--------|-------|----------|--------------|
| `CameraControllerSystem` | `ECS/CameraController.h:236` | −10 (ctor `:239`) | Free/Orbit/FirstPerson/ThirdPerson camera update from `InputStateComponent` |
| `TextRenderSystem` | `ECS/TextRenderSystem.h:23` | −2 (`ApplicationBase.cpp:152`) | Bakes each `Text2DComponent` into one sprite entity per glyph |
| `UILayoutSystem` | `ECS/UILayoutSystem.h:19` | −1 (`ApplicationBase.cpp:153`) | Resolves `UIAnchorComponent` → `TransformComponent::position` |
| `TransformSystem` | `ECS/Systems.h:40` | 0 (`Scene.h:583`) | Local matrices for dirty transforms, then world matrices for the whole hierarchy |
| `PhysicsSystem` | `ECS/PhysicsSystem.h:35`, `src/ECS/PhysicsSystem.cpp` | 5 (ctor `:129`) | Bullet3 dynamics world, ECS↔Bullet sync, force/impulse API |
| `CameraSystem` | `ECS/Systems.h:95` | 10 (`Scene.h:584`) | `projectionMatrix` = f(camera), `viewMatrix` = `inverse(worldMatrix)` |
| `LifetimeSystem` | `ECS/Systems.h:141` | 100 (`Scene.h:585`) | Decrements TTL, destroys expired entities |
| `CallbackSystem` | `ECS/Systems.h:178` | ctor arg | Generic `std::function<bool(float)>` bridge for scripting; self-disables after N consecutive failures |
| `InputSystem` | `ECS/InputSystem.h:29` | 1 | Event→state bridge. **Dead code / broken** (F5) |
| `StandaloneInputHandler` | `ECS/InputSystem.h:174` | n/a | Non-ECS input state holder; this is what the engine actually uses (`ApplicationBase.cpp:136`) |
| `SkeletalAnimationSystem` | `ECS/SkeletalAnimationSystem.h:33` | `getPriority()` returns 35, never consulted | Advances playback, evaluates clips, computes + uploads bone matrices. **Not an `ISystem`**; driven from `EngineAPI.cpp:110` |

## Features

**ECS design.** EnTT `entt::registry` owned by value inside `Scene` (`Scene.h:568`). Systems are
classes, not free functions, held as `std::vector<std::unique_ptr<ISystem>>` sorted by `priority`
on every `addSystem` (`Systems.h:227-240`). `getSystem<T>()` is a linear `dynamic_cast` scan
(`Systems.h:242-250`). Entity creation goes through a fluent `EntityBuilder` (`Core.h:289`) that
guarantees `TransformComponent` + `ActiveComponent` at `build()` (`Core.h:365-374`).
`ComponentRegistry` (`Core.h:212`) provides string-keyed add/remove/has for the Python layer via
type-erased `std::function`s. Entity handles *are* stored inside components
(`HierarchyComponent`, `CameraControllerComponent::followTarget`, `TextBakedComponent::glyphEntities`,
`TextGlyphOwnerComponent::owner`) — EnTT versioned handles make `registry.valid()` checks reliable,
and the camera and text systems do check; the hierarchy code does not always (F1, F2).

**Hierarchical transforms.** `TransformSystem::update` (`Systems.h:42-64`) runs three passes:
(1) recompute `localMatrix` for entities with `isDirty`, clearing the flag; (2) `worldMatrix =
localMatrix` for entities *without* `HierarchyComponent`; (3) recursive
`updateChildTransforms(registry, entt::null)` (`Systems.h:67-88`). World matrices are recomputed
unconditionally every frame — the dirty flag gates only the local matrix. There is no depth limit
and no cycle detection.

**Skeletal animation.** Skeleton = flat `std::vector<Joint>` with `parentIndex`, per-joint
`inverseBindMatrix` and default TRS (`Resources/AnimationData.h:29-70`). All three glTF
interpolation modes are implemented: Step, Linear, CubicSpline (Hermite with in/out tangents)
(`AnimationEvaluator.cpp:147-156, 201-228`). Bone matrix = `globalTransform[i] *
inverseBindMatrix[i]`, single forward pass assuming `parentIndex < i`
(`AnimationEvaluator.cpp:90-107`). Matrices are computed on the CPU and uploaded to a per-entity
host-coherent SSBO (`SkeletalAnimationSystem.cpp:142-178`) — CPU evaluation, GPU skinning. Looping
uses `fmod` with negative-time correction (`SkeletalAnimationSystem.cpp:65-79`). No joint-count
limit is enforced anywhere.

**Physics.** Bullet3 behind a PIMPL (`PhysicsSystem.cpp:82-122`); Bullet headers appear in exactly
one translation unit. `btDiscreteDynamicsWorld` + `btDbvtBroadphase` +
`btSequentialImpulseConstraintSolver`. Body lifetime is automatic via
`registry.on_construct<RigidBodyComponent>` / `on_destroy<...>` (`PhysicsSystem.cpp:164-168`), plus
a catch-up pass for pre-existing entities (`:171-176`). Each body's `btRigidBody`,
`btCollisionShape` and `btDefaultMotionState` are owned by a `PhysicsBodyData` record in an
`unordered_map<entt::entity, ...>` (`:71-76, 91`) and deleted in `removeBodyInternal`
(`:475-502`). Sync model: ECS→Bullet for Kinematic bodies before the step (`:189-202`),
Bullet→ECS for Dynamic bodies after (`:211-234`). `stepSimulation(deltaTime, maxSubSteps=10,
fixedTimeStep=1/60)` — internally fixed-step, externally variable. Shapes: box, sphere, capsule,
static plane; **mesh colliders are a stub that silently falls back to a unit box**
(`:531-534`). Force/impulse/velocity API present (`:253-299`), each call `activate(true)` first.

**Camera controllers.** Four modes (`CameraController.h:100-105`). All store yaw/pitch as scalars
and write Euler angles back into `TransformComponent::rotation` — no quaternions, so no gimbal
issue in the controller itself beyond the deliberate pitch clamps (`:307, 487, 559`). Orbit and
third-person derive rotation from a look direction via `atan2(-dir.x, -dir.z)` / `asin(dir.y)`
(`:454-457, 584-585`), which is consistent with `TransformComponent`'s Y→X→Z order and with
`getForward()` (`Core.h:87-94`). Third-person falls back to orbit when the follow target is invalid
(`:539-543`).

**Serialization.** `Scene::serialize` (`Scene.h:179`) writes Transform, Name, Tag, Camera, Light and
Hierarchy only. `deserialize` (`:266`) is a three-pass rebuild with an old-ID→new-entity map.
Prefabs (`:463-526`) store and restore *only* the Transform.

**EventSystem.** ~40 lines (`Core/EventSystem.h:21-46`): `unordered_map<type_index,
vector<function<void(const std::any&)>>>`, `subscribe<T>` / `publish<T>`. `src/Core/EventSystem.cpp`
is a 3-line comment — the whole thing is header-only.

**Logger.** Level filter, file + stdout, size-based rotation, and a per-call-site throttle keyed on
the format-string pointer (`Logger.cpp:56-67`), plus a global auto-throttle for Info/Debug
(`Logger.h:31-33`).

## Limitations

- **No animation blending or cross-fade of any kind.** One clip plays at a time
  (`AnimationPlaybackComponent::currentClipIndex`, `SkeletonComponents.h:79`); `sampleClip` writes
  poses directly with no weight parameter (`AnimationEvaluator.h:29-36`). No additive layers, no
  masks, no animation events/callbacks.
- **No raycast API.** `README.md:119` advertises "Raycasting and gravity control"; a repo-wide grep
  for `raycast`/`rayTest` finds hits only in `README.md` and `docs/index.md` — nothing in `src/` or
  `include/`. `btDiscreteDynamicsWorld::rayTest` is never called.
- **No collision/trigger events.** `ColliderComponent::isTrigger` sets
  `CF_NO_CONTACT_RESPONSE` (`PhysicsSystem.cpp:454-458`) but there is no contact callback, no
  manifold iteration, no way to learn that a trigger fired.
- **No mesh colliders** (`PhysicsSystem.cpp:531-534`, prints a warning, returns a unit box).
- **No reparenting API.** Parent can only be set at build time (`Core.h:313-322`). Changing a parent
  later means editing both `HierarchyComponent`s by hand.
- **Serialization covers 6 of 23 components.** Mesh, material, rigid body, collider, skeleton,
  sprite/UI/text and script components are all silently dropped by `Scene::saveToFile`.
- **`Scene::fixedUpdate` is an empty stub** (`Scene.h:164-169`, body commented out) yet is exposed
  and forwarded by `SceneManager::fixedUpdate` (`Scene.h:670-674`).
- **Nothing here is thread-safe.** No locks around the registry, no locks in `EventDispatcher`, and
  the `Logger` mutex does not cover its throttle map (F12). The only worker threads in the engine
  live in `ResourceManager`, which does not touch the registry (grep-level confidence).
- **`Scene::m_resourceManager` / `m_device`** are stored but never used (`Scene.h:571-572`).

## Findings

### CRITICAL

**F1 — `EntityHelper::destroyEntity` mutates the child vector it is iterating (`ECS/Core.h:428-446`).**
`destroyEntity(parent)` grabs `hierarchy` (`:430`) and range-for-iterates `hierarchy->children`
(`:438`). The recursive `destroyEntity(child)` call unlinks the child from its parent via
`parentHierarchy->removeChild(entity)` (`:434`), which is `erase(remove(...))` on that very vector
(`Core.h:54-56`). Iterators past the erase point are invalidated → the loop walks freed/shifted
storage. Any entity with ≥2 children hits this. Second, independent defect on the same lines: each
recursive `registry.destroy(child)` erases from the `HierarchyComponent` pool, and EnTT's default
swap-and-pop moves the last element into the vacated slot — so the parent's own
`HierarchyComponent` can be relocated, leaving `hierarchy` dangling for the rest of the loop.
Both are silent memory corruption, not crashes-on-first-run.

**F2 — Unbounded recursion with no cycle detection in the transform graph
(`ECS/Systems.h:67-88`, `ECS/Core.h:437-442`).** `updateChildTransforms` recurses on
`hierarchy.parent == parent` with no visited set and no depth cap. A parent cycle (trivially
creatable — nothing validates `withParent`, and `deserialize` at `Scene.h:391-415` restores
parent/child links straight from JSON without consistency checks) produces infinite recursion and a
stack overflow. `EntityHelper::destroyEntity` has the same exposure.

**F3 — Bone SSBOs are never freed (`ECS/SkeletonComponents.h:42`, `src/ECS/SkeletalAnimationSystem.cpp:142-156`).**
`GPUBuffer` is a plain handle struct with no destructor (`GPU/GPUTypes.h:55-69`, whose own comment
says `reset()` "does NOT free GPU memory"). `createBoneSSBO` allocates one VMA buffer per skinned
entity, and there is no `on_destroy<SkeletonComponent>` hook anywhere in the codebase (grep for
`on_destroy` finds only the physics one). Destroying a skinned entity leaks its bone buffer for the
lifetime of the allocator.

**F4 — Bone SSBO is written with no frames-in-flight protection
(`src/ECS/SkeletalAnimationSystem.cpp:158-178`).** One host-visible buffer per skeleton is
`memcpy`-ed every frame while previous frames may still be reading it on the GPU. There is no
per-frame ring, no fence wait, no double buffering. With ≥2 frames in flight this is a data race
between CPU writes and in-flight draws (visible as one-frame pose tearing at best). *Uncertainty:
I did not read the submission/synchronisation code — if the renderer fully idles between frames
this is latent rather than live.*

### MAJOR

**F5 — `ECS::InputSystem` never delivers input (`ECS/InputSystem.h:68-87` vs `:153-163`).**
Event callbacks write into the private `m_pendingState` member (`:122-151`), and
`syncToComponent()` — the only thing that copies `m_pendingState` into the actual
`InputStateComponent` — is called from nowhere in the repo (grep: the only hit is its own
definition). `update()` (`:68`) syncs only `mouseCaptured`. Any project that adds `InputSystem` to a
scene gets a camera that never moves. The shipped engine sidesteps this by using
`StandaloneInputHandler` and copying state by hand in `ApplicationBase.cpp:318-332`, which makes
`InputSystem` broken dead code sitting in the public headers.

**F6 — Repeated `Scene::initialize()` double-connects the physics signals
(`Scene.h:147-150, 418-421` + `src/ECS/PhysicsSystem.cpp:160-168`).**
`PhysicsSystem::initialize` assigns `m_impl->onRigidBodyConstruct` without releasing whatever was
there. `Scene::deserialize` re-runs `m_systemManager.initialize(m_registry)` at `:420`, so after a
scene load the registry holds two live connections to the same `Impl` and the handle for the first
one is lost. `cleanup()` (`PhysicsSystem.cpp:239-240`) can then only release one — the other
outlives the system and fires into freed memory when the registry is destroyed or cleared.

**F7 — `SystemManager::removeSystem` destroys a system without calling `cleanup()`
(`ECS/Systems.h:262-268`).** For `PhysicsSystem` this leaves the EnTT `on_construct`/`on_destroy`
sinks pointing at a freed `Impl`, and leaks every `btRigidBody`/`btCollisionShape`/`btMotionState`
that `cleanupAllBodies` would have freed (the destructor at `PhysicsSystem.cpp:149-154` *does*
tear down bodies, but nothing disconnects the signals). `SystemManager` also has no destructor that
calls `cleanup` — only `~Scene` (`Scene.h:41-43`) does.

**F8 — Double destroy in `LifetimeSystem` (`ECS/Systems.h:146-160`).** Expired entities are
collected into `toDestroy`, then destroyed in a second loop. If a parent and its child both expire
in the same frame, destroying the parent cascades into the child (`Core.h:437-442`), and the loop
then calls `EntityHelper::destroyEntity` on an already-invalid handle — which immediately does
`registry.try_get<HierarchyComponent>(entity)` (`Core.h:430`) on a dead entity. That is an EnTT
precondition violation (assert in debug, undefined in release). No validity check exists at
`Systems.h:158` or `Core.h:429`.

**F9 — Rotation channels use nlerp with no shortest-path correction
(`src/Animation/AnimationEvaluator.cpp:191-199` + `:64`).** `interpolateLinear` is
`glm::mix(v0, v1, t)` on raw `vec4`s, applied identically to translation, scale and quaternions;
`sampleClip` normalises afterwards (`:64`), so the result is nlerp. Two problems: (a) no
`dot(q0,q1) < 0` sign flip, so any keyframe pair more than 180° apart interpolates the long way
around, and exactly-opposite quaternions produce a zero-length vector before normalisation; (b) the
glTF spec requires spherical linear interpolation for `LINEAR` rotation channels, so imported
animations will not match the reference renderer on fast rotations.

**F10 — `O(N²)` hierarchical transform update (`ECS/Systems.h:67-88`).**
`updateChildTransforms` constructs a fresh view and iterates *every* entity carrying
`HierarchyComponent` once per recursion node. For N hierarchical entities the total work is
`N × N` view iterations per frame, not `N`. A 5 000-node imported glTF scene means 25 million
iterations per frame purely to propagate matrices. On top of that, world matrices are recomputed for
every entity every frame regardless of `isDirty` (`:76, 80`) — the dirty flag saves only the local
TRS compose (`:49-52`), and dirtiness is never propagated from parent to child.

**F11 — `TransformComponent::isDirty` is not maintained by the obvious mutation path
(`ECS/Core.h:60-68`).** `position`/`rotation`/`scale` are public fields with no setters. Writing
`transform.position = p` — the pattern used throughout the examples and the natural one from Python
— leaves `isDirty == false`, so `localMatrix` keeps the previous frame's value and the entity does
not move. Only code that remembers `transform.isDirty = true` (physics `:232`, the camera
controllers, `UILayoutSystem.h:38`, `EntityHelper::setWorldPosition` `Core.h:424`) works. This is a
correctness trap baked into the component's public shape.

**F12 — `Logger` throttle map is mutated outside the mutex
(`include/Core/Logger.h:31-33, 41-44` → `src/Core/Logger.cpp:56-67`).** `shouldLogThrottled` reads
and inserts into `m_throttleTimestamps` before `logMessage` takes `m_mutex` (`Logger.cpp:31`).
Concurrent logging from two threads is a data race on an `unordered_map` — including rehash during
another thread's lookup. The class advertises thread-safety by carrying a mutex; the throttle path
silently isn't covered.

**F13 — Level filtering happens after formatting (`include/Core/Logger.h:34-35` →
`src/Core/Logger.cpp:29`).** `log()` always calls `formatString` (two `snprintf` passes plus a
`std::string` allocation, `Logger.h:61-68`) and only then does `logMessage` return early on
`level < m_currentLogLevel`. Suppressed Debug logging costs the same as emitted logging. Also, every
emitted line does `m_logFile.flush()` (`Logger.cpp:45`) — a synchronous disk write per log call.

**F14 — Physics ignores `TransformComponent::scale` entirely
(`src/ECS/PhysicsSystem.cpp:372-469, 508-539`).** `createCollisionShape` builds shapes from
`ColliderComponent::size` only, and `setLocalScaling` is never called anywhere. A mesh rendered at
`scale = 3` collides as if it were at `scale = 1`. `ColliderComponent::center` (`Core.h:186`) is
likewise never read — there is no `btCompoundShape` to apply the offset — so a configured collider
offset is silently discarded.

**F15 — Physics syncs *local* position, not world (`src/ECS/PhysicsSystem.cpp:198-199, 223-226`).**
Both directions read/write `transform->position` and `transform->rotation`, which are parent-local
for any entity with a `HierarchyComponent`. A rigid body parented under anything non-identity gets
its world-space Bullet transform written into a local-space field, and the resulting `worldMatrix`
is wrong by the parent transform. Nothing warns or asserts.

**F16 — One-frame lag on every dynamic body (priority ordering, `Scene.h:583` vs
`PhysicsSystem.cpp:129`).** `TransformSystem` runs at priority 0 and `PhysicsSystem` at 5, so
physics writes `transform->position` and `isDirty = true` *after* the matrices for this frame have
already been built. Rendering (which reads `worldMatrix`) therefore shows every physics object one
frame behind, and `CameraSystem` (priority 10) computes the view matrix from a `worldMatrix` that
predates the physics step. The header comment at `PhysicsSystem.h:9` states the ordering as
intentional but does not mention the consequence.

**F17 — Dynamic-body rotation round-trips through Euler angles every frame
(`src/ECS/PhysicsSystem.cpp:51-65, 226`).** `toEuler` decomposes with
`asin(clamp(-R[2][1]))`; at pitch near ±90° the yaw/roll `atan2`s degenerate (gimbal lock) and a
tumbling body's orientation becomes unstable. The next frame's kinematic path re-encodes those
Eulers via `toBtQuat` (`:42-48`). The engine has quaternions available and chooses not to store
them on `TransformComponent`.

**F18 — Runtime edits to rigid-body properties do nothing
(`src/ECS/PhysicsSystem.cpp:405-451`).** `mass`, `drag`, `angularDrag`, `friction`, `restitution`,
`useGravity`, `freezeRotation` and `type` are consumed once at body creation. Changing
`RigidBodyComponent::mass` afterwards has no effect until the caller happens to know about
`rebuildBody` (`:363-366`). `RigidBodyComponent::isKinematic` (`Core.h:174`) is a second, redundant
representation of `type == Kinematic` and is read by nothing.

**F19 — `EventDispatcher` has no unsubscribe, and subscribers capture `this`
(`Core/EventSystem.h:26-32`).** `subscribe` returns `void`; there is no handle, no token, no
`unsubscribe`. `InputSystem` (`InputSystem.h:39-53`) and `StandaloneInputHandler`
(`InputSystem.h:179-209`) both register `this`-capturing lambdas and neither unregisters in its
destructor. Destroying either while the dispatcher lives — a scene teardown, a system swap — leaves
callbacks that will call into freed memory on the next input event. Callbacks also cannot be
removed when an entity dies.

**F20 — `EventDispatcher::publish` allocates per callback per event
(`Core/EventSystem.h:34-42`).** The stored callback takes `const std::any&`, so `callback(event)`
at `:39` constructs a fresh `std::any` from the event for *each* subscriber — heap-allocating for
any event larger than the small-object buffer. `publish` also iterates the callback vector without
any re-entrancy guard: a callback that calls `subscribe<T>` for the same `T` invalidates the vector
being iterated. Neither `subscribe` nor `publish` takes a lock.

**F21 — Animation is fully re-evaluated and re-uploaded every frame even when stopped
(`src/ECS/SkeletalAnimationSystem.cpp:42-48, 86-136`).** `evaluateAnimation` is called
unconditionally (`:43`), calls `setDefaultPose` over every joint (`:108`), re-samples the clip,
recomputes all bone matrices and unconditionally sets `skeleton.dirty = true` (`:135`), which
triggers a map/memcpy/unmap (`:46-48`). A paused or idle character costs the same as an animating
one. `computeBoneMatrices` additionally heap-allocates a `std::vector<glm::mat4>` of scratch global
transforms on every call (`AnimationEvaluator.cpp:88`) — one allocation per skinned entity per
frame.

**F22 — Text labels leak their glyph entities on destroy
(`ECS/Sprite2DComponents.h:86-91`, `ECS/TextRenderSystem.h:56-60`).** Documented honestly in the
header, but the consequence is that destroying a label entity orphans every generated glyph entity
(each with Transform/Mesh/Material/Renderable/Sprite/Anchor components), and they keep rendering.
The suggested workaround is "set `visible = false` instead of destroying", which is a leak by
policy.

**F23 — `AnimationEvaluator::sampleChannel` trusts channel array sizes
(`src/Animation/AnimationEvaluator.cpp:114-159`).** Nothing validates
`values.size() == timestamps.size()` (Linear/Step) or `== timestamps.size() * 3` (CubicSpline).
`interpolateLinear` reads `values[keyIndex + 1]` (`:197`) and `interpolateCubicSpline` reads
`values[(keyIndex+1)*3 + 2]` (`:210-215`) with no bounds check. A truncated or mislabelled channel
from a malformed glTF is an out-of-bounds read, not a rejected asset. `findKeyframe` (`:165-178`)
also assumes strictly ascending timestamps and would compute nonsense on unsorted data.

### MINOR

**F24 — `getEntityCount()` likely over-reports (`Scene.h:453-455`).** `m_registry.view<entt::entity>().size()`
returns the entity storage's packed size, which in EnTT includes released entities held in the free
list, while *iterating* the same view skips them. After destroying entities the count should stay
inflated. *Uncertainty: I did not read the vendored EnTT sources (they live under
`build/`/`cmake-build-*`, excluded from this review), so I'm going on EnTT's documented storage
semantics rather than the exact version in use.* `registry.storage<entt::entity>().free_list()` is
the intended accessor.

**F25 — `ISystem::cleanup` is never called on `SystemManager` destruction (`ECS/Systems.h:284-292`).**
Only `~Scene` (`Scene.h:42`) calls it. A standalone `SystemManager`, which the tests use directly
(`tests/ecs/SystemsTest.cpp:231-303`), silently skips cleanup.

**F26 — `std::sort` on systems is unstable (`ECS/Systems.h:234-237`).** Systems sharing a priority
(e.g. `TransformSystem` at 0 and any user system left at the default 0) can be reordered
arbitrarily, and the whole vector is re-sorted on every `addSystem`, so registration order is not a
tiebreaker. `std::stable_sort` would cost nothing here.

**F27 — `Core.h` uses `std::find`/`std::remove` without including `<algorithm>`
(`ECS/Core.h:49, 55`; includes at `:11-20`).** It compiles only because EnTT or GLM pulls
`<algorithm>` in transitively.

**F28 — Priority comments contradict the code.** `CameraController.h:234` says "Runs at priority 5"
while the constructor sets `-10` (`:239`). `SkeletalAnimationSystem.h:10` says "Priority: 35 (after
physics at 30)" — physics is actually 5 (`PhysicsSystem.cpp:129`), and the value is never used
because the class isn't an `ISystem`.

**F29 — Log rotation is broken on Windows (`src/Core/Logger.cpp:69-77`).** The rotated filename is
`m_filename + "." + getCurrentTimestamp()`, and the timestamp is `"%Y-%m-%d %H:%M:%S.mmm"` —
colons and a space. `:` is illegal in NTFS filenames, so `std::rename` fails; its return value is
ignored, `m_currentFileSize` is reset to 0 anyway, and the original file keeps growing while
rotation is retried every `maxFileSize` bytes. `std::localtime` (`:85`) is also not thread-safe.

**F30 — `Logger::formatString` doesn't check `snprintf`'s return (`Logger.h:63-67`).** A negative
`size` (encoding error) makes `std::string result(size + 1, '\0')` a huge/negative-converted
allocation. The format string is also unchecked (no `__attribute__((format))` / SAL annotation), so
a mismatched argument is undefined behaviour rather than a compiler warning.

**F31 — Two independent frame clocks.** `ApplicationBase::update` computes and clamps
`m_deltaTime` (`ApplicationBase.cpp:311-314`), and `Scene::update` independently measures its own
delta from its own `high_resolution_clock` and clamps to 0.1 (`Scene.h:152-162`). Systems see
Scene's value, gameplay code sees the app's, and the two differ by whatever ran in between.

**F32 — Throttle keys are format-string pointers (`Logger.cpp:56-58`).** Two call sites sharing an
identical literal may be merged by the linker into one address and thus throttle each other; a
runtime-built format string produces an unbounded number of map entries. The map is never pruned.

## Test coverage

10 files, ~1 750 lines. What they actually assert:

- **`TransformTest.cpp` (156 lines)** — `TransformComponent` in isolation: identity/translation/
  scale/90° yaw matrix entries, TRS composition through two point multiplications, the three
  direction vectors at identity and after a 90° yaw, and that `isDirty` starts `true`. No test
  covers a mutation *clearing* or *failing to set* the dirty flag (F11).
- **`SystemsTest.cpp` (354 lines)** — `TransformSystem`: root world == local, dirty cleared after
  update, local matrix reflects position, one two-level parent/child composition
  (`:59-85`), disabled system is a no-op. `CameraSystem`: view == `inverse(worldMatrix)`,
  `getMainCamera` with flag / without flag / with no cameras. `LifetimeSystem`: TTL decrement,
  destroy on expiry, `destroyOnExpire = false` survival, selective destruction, disabled no-op.
  `SystemManager`: add/get/get-missing, both systems findable after priority assignment (note:
  `PriorityOrdering` at `:254-266` asserts only that both are non-null — it never checks execution
  order), disabled skip, both-systems update, `findSystem`/`removeSystem` by name.
  `CallbackSystem`: delta forwarded, priority and name set from constructor.
  **Not covered:** grandchildren, multiple children per parent, cycles, deletion during hierarchy
  traversal (F1/F2), `CallbackSystem`'s auto-disable-after-N-failures logic (the headline feature of
  the class, `Systems.h:204-207`).
- **`EntityBuilderTest.cpp` (181 lines)** — `build()` adds Transform + Active; no duplicate
  Transform; `withName`/`withTag`/`withTransform`/`withCamera`(±main)/`withLight`/`withLifetime`/
  `withRigidBody`/`withCollider` each set their fields; fluent chaining; `withParent` sets both
  sides of the link; two children accumulate. **Not covered:** `withParent` on an invalid parent,
  the camera-controller builders.
- **`ComponentRegistryTest.cpp` (116 lines)** — create/has/remove round-trips by string name,
  unknown names return `false` for all three operations, `getAllComponentNames` contains the 9
  registered names (`EXPECT_GE(names.size(), 9u)`), `getComponentName` by `type_index` for a known
  and an unknown type, independence of multiple components on one entity.
- **`CameraTest.cpp` (123 lines)** — `CameraComponent` projection maths only: default is
  perspective, perspective and ortho matrices are non-identity, fov/aspect/near-far each change the
  matrix, larger `orthoSize` shrinks `[0][0]`, default field values. It asserts *differences*, never
  a matrix against a reference — a sign or convention error would pass.
- **`AnimationDataTest.cpp` (153 lines)** — pure data structures: `Joint` defaults,
  `Skeleton::jointCount`, `buildNameLookup` (including that empty names are skipped),
  `findJoint` hit and miss, parent indices, `AnimationChannel`/`AnimationClip` defaults.
- **`AnimationEvaluatorTest.cpp` (247 lines)** — on a 2-joint skeleton: `setDefaultPose` sizes and
  values; `sampleClip` linear midpoint, step holding the first value, clamping before the first and
  after the last keyframe, single-keyframe channels, untargeted joints retaining their default;
  `computeBoneMatrices` all-identity, root translation, parent→child composition.
  **Every one of these tests uses translation channels only.** There is no rotation test at all, so
  the nlerp/shortest-path defect (F9) is untested, and no CubicSpline test, so
  `interpolateCubicSpline` (`AnimationEvaluator.cpp:201-228`) is entirely uncovered.
- **`AnimationPlaybackTest.cpp` (178 lines)** — `play`/`stop`/invalid index/`getCurrentClip`
  variants, `allocate` sizes and defaults, `SkeletonComponent::allocate`, `jointCount`, `ssboSize`
  (with and without a skeleton), `dirty` defaults true. **No test exercises
  `SkeletalAnimationSystem` itself** — looping/`fmod` wrap, non-looping stop-at-end, negative speed
  (`SkeletalAnimationSystem.cpp:56-79`) are all uncovered, presumably because the constructor
  requires a `VulkanDevice`.
- **`Sprite2DComponentsTest.cpp` (73 lines)** — `UIAnchorComponent::resolve` for TopLeft (±offset),
  BottomRight (±offset), MiddleCenter; `Sprite2DComponent` and `Text2DComponent` defaults. 5 of the
  9 anchors are untested.
- **`RenderComponentsTest.cpp` (215 lines)** — `MaterialComponentV5` param round-trips
  (float/vec4/mat4), `hasParam` hit/miss, missing-param default, overwrite (I read the first 60
  lines in full; the remainder covers textures, alpha modes and `RenderableTagComponent` per its
  header).
- **`EventSystemTest.cpp` (142 lines)** — subscribe+publish delivery, publish with no subscribers,
  three subscribers all fire, type isolation between `KeyEvent` and `MouseMoveEvent`, field
  round-trips for all five event structs, three sequential publishes all delivered.
  **Nothing tests listener lifetime, unsubscribe (there is none), re-entrant subscribe during
  publish, or threading** — i.e. none of F19/F20.

**Untested entirely:** `PhysicsSystem` (no test file exists — the whole Bullet integration,
including every finding F14–F18, is unverified), `CameraControllerSystem` (all four modes),
`InputSystem`/`StandaloneInputHandler` (F5 would have been caught by one test),
`TextRenderSystem`, `UILayoutSystem`, `Scene` serialize/deserialize round-trip, prefabs,
`EntityHelper::destroyEntity` (F1), `ScriptComponentBag`, and `Logger`.

## Open questions

1. **Is `InputSystem` (`ECS/InputSystem.h:29`) intended to be deleted?** It is included by
   `ApplicationBase.cpp:25` but only `StandaloneInputHandler` is instantiated. Leaving a
   non-functional system in a public header is a trap for library users.
2. **What is the intended contract for `TransformComponent::isDirty`?** Making the TRS fields
   private with setters, or dropping the flag and always recomposing, would both close F11; the
   current shape works only for callers who read the source.
3. **Should `SkeletalAnimationSystem` become an `ISystem`?** It already declares a priority
   (`SkeletalAnimationSystem.h:57`) that nothing reads, and it is driven manually from
   `EngineAPI.cpp:110`, so its ordering relative to `TransformSystem`/`PhysicsSystem` is implicit.
4. **Is the `README.md:119` raycasting claim aspirational or a regression?** No raycast code exists
   in `src/` or `include/`. Bullet's `rayTest` would be ~20 lines behind the existing PIMPL.
5. **How are frames-in-flight handled for the bone SSBO (F4)?** If the renderer already serialises
   frames the risk is latent; if not, the buffer needs per-frame copies. I did not read the
   submission path.
6. **`Scene::fixedUpdate` (`Scene.h:164-169`) is commented out** — was fixed-timestep stepping
   deliberately abandoned in favour of Bullet's internal substepping, or is this an unfinished
   refactor? `SceneManager::fixedUpdate` still forwards to it.
7. **Does the glTF loader validate animation channel array sizes** before constructing
   `AnimationChannel`? If not, F23 is reachable from any downloaded asset. I did not read
   `GltfSceneLoader.cpp` (outside this scope).
