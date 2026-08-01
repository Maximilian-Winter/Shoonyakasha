# Facade & Application Layer Review — Shoonyakasha

## Scope

Read in full:

- `include/Facade/EngineAPI.h`, `EcsAPI.h`, `SceneAPI.h`, `InputAPI.h`, `PhysicsAPI.h`, `FacadeTypes.h`
- `src/Facade/EngineAPI.cpp`, `EcsAPI.cpp`, `SceneAPI.cpp`, `InputAPI.cpp`, `PhysicsAPI.cpp`, `FacadeInternal.h`, `InputAPIImpl.h`, `PhysicsAPIImpl.h`
- `include/App/ApplicationBase.h`, `src/App/ApplicationBase.cpp`
- `include/Shoonyakasha.h`
- `tests/facade/SceneAPITest.cpp`, `EcsAPITest.cpp`, `FacadeTypesTest.cpp`
- `examples/facade_test/main.cpp`
- `docs/api/cpp/engine-api.md`, `scene-api.md`, `input-api.md`, `physics-api.md`, `application-base.md`

Cross-checked (read-only, for claims that depend on them): `include/ECS/Core.h`, `include/ECS/Scene.h`, `include/ECS/Systems.h`, `include/ECS/TextRenderSystem.h`, `include/ECS/Sprite2DComponents.h`, `src/FrameGraph/EntityRenderExecutor.cpp`, `src/Vulkan/VulkanSwapChain.cpp`, `src/Vulkan/VulkanWindow.cpp`.

Not in scope / not reviewed: Vulkan RHI internals, FrameGraph internals, resource loaders, Python bindings.

---

## Public API inventory

~170 public methods across five classes. `EntityHandle` is `uint32_t` (`FacadeTypes.h:24`), `NullEntity = UINT32_MAX` (`FacadeTypes.h:25`), both binary-compatible with `entt::entity` (verified by `tests/facade/FacadeTypesTest.cpp:17-38`).

### EngineAPI — `include/Facade/EngineAPI.h` (27 methods)

| Group | Method | Description |
|---|---|---|
| Ctor | `EngineAPI(const EngineConfig&)` | Builds `ApplicationConfig` and constructs the internal `CallbackApp`. Does **not** initialize Vulkan (`EngineAPI.cpp:253-258`). |
| Lifecycle | `run()` | Initializes everything, blocks in the main loop until the window closes (`EngineAPI.cpp:266`). |
| Callbacks | `setOnInit(VoidCallback)` | Fires after Vulkan+ECS+IBL init, **before** render-graph compile (`ApplicationBase.cpp:73`). |
| | `setOnPostInit(VoidCallback)` | Fires after render-graph compile, IBL bind and sync-object creation (`ApplicationBase.cpp:80`). |
| | `setOnUpdate(UpdateCallback)` | Per-frame, before ECS system update (`ApplicationBase.cpp:334`). |
| | `setOnPreRender(UpdateCallback)` | Per-frame, after image acquire and scene-context update, before command recording (`ApplicationBase.cpp:371`). |
| | `setOnPostRender(VoidCallback)` | Per-frame, **after** present (`ApplicationBase.cpp:398`). |
| | `setOnKeyPressed(KeyCallback)` | Key-down only; GLFW key codes (`ApplicationBase.cpp:274-275, 302`). |
| | `setOnResize(ResizeCallback)` | Fires before swapchain recreation (`ApplicationBase.cpp:270-271`). |
| | `setOnCleanup(VoidCallback)` | Fires after the loop exits and `vkDeviceWaitIdle`, before resource teardown (`ApplicationBase.cpp:98-101`). |
| Sub-APIs | `getScene() / getInput() / getPhysics() / getEcs()` | Return references to the four sub-APIs. Constructed only inside `CallbackApp::onInit` (`EngineAPI.cpp:80-93`). |
| Helpers | `createCamera(pos, fov=60, speed=8, near=0.1, far=1000)` | Camera + free-fly controller; sets it as `m_cameraEntity`. FOV in **degrees** (`Core.h:115`). |
| | `loadGltfScene(path, opts={})` | Loads glTF into the active scene, then allocates bone SSBOs for new skinned meshes (`EngineAPI.cpp:313-318`). |
| | `createDirectionalLight(dir, color=1, intensity=2)` | Direction converted to Euler rotation; position hard-coded to (0,50,0) (`ApplicationBase.cpp:547-548`). |
| | `createPointLight(pos, color=1, intensity=5, range=15)` | Point light entity. |
| | `createSprite(worldPos, texturePath, size=1, tint=1)` | World-space quad, alpha-blended, `screenSpace=0`. |
| | `createUIPanel(anchor, offsetPx, sizePx, texturePath="", color=1)` | Screen-space quad; flat color if no texture. |
| | `createText(text, anchor, offsetPx, fontPath, fontSize=24, color=1)` | Label entity; `TextRenderSystem` bakes one sprite entity per glyph. |
| | `getCameraEntity() const` | Last entity returned by `createCamera`, else `NullEntity`. |
| | `getDeltaTime() const` | Seconds since previous frame, clamped to 0.1 (`ApplicationBase.cpp:314`). |
| Uniforms | `setCustomFloat / Vec2 / Vec3 / Vec4 / Uint(key, value)` | Write into the render graph's scene context for shader dot-paths (`EngineAPI.cpp:375-393`). |

### SceneAPI — `include/Facade/SceneAPI.h` (102 methods)

| Group | Methods | Description |
|---|---|---|
| Entity lifecycle | `createEntity(name="")`, `destroyEntity`, `isValid`, `getEntityCount` | Create auto-adds Transform + Active (+ Name if non-empty). |
| Queries | `findEntityByName`, `findEntitiesWithTag`, `getMainCamera`, `getAllEntities` | Linear scans; return by value. |
| Components (by name) | `addComponent`, `removeComponent`, `hasComponent`, `getComponentNames` | Routed through `ECS::ComponentRegistry`; ~9 registered types. |
| Identity | `getName/setName`, `getTag/setTag`, `isActive/setActive` | Setters create the component if absent. |
| Transform | `get/setPosition`, `get/setRotation` (Euler **radians**), `get/setScale`, `getWorldPosition`, `getWorldMatrix`, `getForward`, `getRight`, `getUp` | Setters mark `isDirty`; world values are last frame's, computed by `TransformSystem`. |
| Camera | `get/setCameraType`, `get/setCameraFov` (**degrees**), `get/setCameraNear`, `get/setCameraFar`, `get/setCameraOrthoSize`, `isCameraMain/setCameraMain` | Plain component field access. |
| Light | `get/setLightType`, `get/setLightColor`, `get/setLightIntensity`, `get/setLightRange`, `get/setLightCastShadows` | Plain component field access. |
| Material | `set/getMaterialFloat`, `set/getMaterialVec3`, `set/getMaterialVec4`, `hasMaterialParam`, `setMaterialTexture(slot, path)` | Getters take an explicit default. `setMaterialTexture` loads via `Sprite2DManager` and returns `bool`. |
| Sprite / UI | `setSpriteTexture`, `set/getSpriteColor`, `set/getSpriteUVRect`, `isScreenSpaceSprite`, `setUIAnchor`, `getUIAnchor`, `getUIAnchorOffset` | Sprite color/UV are thin wrappers over material params `tintColor` / `uvRect` (`SceneAPI.cpp:550-564`). |
| Text | `set/getText`, `setTextColor`, `setTextFontSize`, `setTextAlign`, `setTextLayerMask` | Operate on `Text2DComponent`; re-bake is triggered by `TextRenderSystem`. |
| Renderable tag | `isVisible/setVisible`, `getCastShadows/setCastShadows`, `get/setRenderLayerMask` (8-bit), `get/setSortKey` | Only affect entities carrying `RenderableTagComponent`. |
| Hierarchy | `getParent`, `setParent`, `getChildren` | `setParent(child, NullEntity)` detaches. |
| Animation | `getAnimationClipCount/Name/Duration`, `playAnimation(idx)`, `stopAnimation`, `isAnimationPlaying`, `get/setAnimationSpeed`, `get/setAnimationTime` (seconds), `is/setAnimationLooping`, `getCurrentAnimationClip` | Operate on `AnimationPlaybackComponent`. |
| Serialization | `saveToFile`, `loadFromFile` | JSON; covers Transform/Name/Tag/Camera/Light/Hierarchy only (`Scene.h:179-260`). |
| Internal | `wireSprite2DManager(Sprite2DManager*)` | Called once by `EngineAPI` (`EngineAPI.cpp:81`); public in a public header. |

### InputAPI — `include/Facade/InputAPI.h` (10 methods)

`isKeyDown(int)`, `isMouseButtonDown(int)`, `getMousePosition()`, `getMouseDelta()`, `getScrollDelta()`, `isMouseCaptured()`; callbacks `setOnKeyEvent(int,bool)`, `setOnMouseMove(float,float)`, `setOnMouseButton(int,bool)`, `setOnMouseScroll(float,float)`. All polling methods return a safe default when unwired (`InputAPI.cpp:24-52`). GLFW key codes, not re-exported by the facade.

### PhysicsAPI — `include/Facade/PhysicsAPI.h` (17 methods)

`isEnabled/setEnabled` (registered but disabled by default, `EngineAPI.cpp:71-72`); `set/getGravity`, `set/getFixedTimeStep` (seconds), `set/getMaxSubSteps`; `addForce`, `addImpulse`, `addTorqueImpulse`; `set/getLinearVelocity`, `set/getAngularVelocity`; `rebuildBody`, `getBodyCount`. No raycast, no collision/trigger callbacks, no per-body mass/damping/constraint access.

### EcsAPI — `include/Facade/EcsAPI.h` (14 methods)

Script components: `setComponent(entity, name, shared_ptr<void>)`, `getComponent`, `hasComponent`, `removeComponent`, `getComponentNames(entity)`, `findEntitiesWithComponent(name)`.
Systems: `addSystem(name, fn, priority=0, maxConsecutiveFailures=5)`, `removeSystem`, `hasSystem`, `set/isSystemEnabled`, `getSystemFailureCount`, `get/setSystemMaxFailures`, `resetSystemFailureCount`. Callback returns `bool`; N consecutive `false` returns auto-disable the system (`Systems.h:195-208`).

### ApplicationBase — `include/App/ApplicationBase.h`

Inheritance alternative. 8 protected virtual hooks + `registerSystems()`; 14 accessors returning raw engine references (`VulkanDevice&`, `entt::registry&`, `FrameGraph::RenderGraph&`, …); 7 convenience helpers (same set as `EngineAPI` plus `getSprite2DManager`/`getFontLoader`). `include/Shoonyakasha.h` is an umbrella header for the *internal* layer only — it does not include any `Facade/` header.

---

## Application lifecycle (traced)

`ApplicationBase::run()` (`ApplicationBase.cpp:61-102`):

1. `new Logger`, `new EventDispatcher` (:63-65).
2. `initializeVulkan()` (:69) — instance, window, device, swapchain, ResourceManager, DescriptorManager, GltfSceneLoader, Sprite2DManager, FontLoader, command manager, command buffers sized to swapchain image count (:108-128).
3. `initializeECS()` (:70) — SceneManager, one scene named `"ApplicationScene"`, `registerSystems()` (:134), `StandaloneInputHandler`, an `InputState` entity, `scene->initialize()`.
   `registerSystems()` (:147-157) registers `TextRenderSystem` (priority -2), `UILayoutSystem` (-1), `TransformSystem`, `CameraSystem`, `CameraControllerSystem` (all 0). `CallbackApp` additionally registers `PhysicsSystem`, disabled (`EngineAPI.cpp:71-72`).
4. `loadIBLTextures()` (:71) — skipped if `hdrEnvironmentPath` empty; exceptions swallowed and logged (:177-179).
5. **`onInit()`** (:73). `CallbackApp::onInit` creates `SkeletalAnimationSystem`, then `SceneAPI`, `EcsAPI`, `InputAPI`, `PhysicsAPI`, wires them, then invokes the user callback (`EngineAPI.cpp:75-97`). **The render graph does not exist yet.**
6. `initializeRenderGraph()` (:75) — loads JSON, binds scene, applies parameters, imports swapchain images, compiles. Throws on compile failure (:212-214).
7. `bindIBLTextures()`, `createSyncObjects()`, `setupEventHandlers()` (:76-78).
8. **`onPostInit()`** (:80).
9. Loop (:92-96): `pollEvents()` → `update()` → `render()`. Exit condition is `m_window->shouldClose()` only.
10. `vkDeviceWaitIdle`, then **`onCleanup()`** (:98-101). `run()` returns; Vulkan/ECS objects are still alive.
11. `~ApplicationBase` (:52-55) calls `cleanup()` (destroys semaphores/fences) and `m_iblResources.destroy()`, then members destruct in reverse declaration order — input handler, scene, scene manager, render graph, … device, window, instance. Order is correct: the render graph and all GPU objects die before `VulkanDevice`.

`update()` (:310-345): `dt = now - lastFrame`, clamped to `min(dt, 0.1f)` — no lower clamp, no fixed-step accumulator, no spiral-of-death protection beyond the clamp. Then `inputHandler->beginFrame()`, copy input state into every `InputStateComponent`, **`onUpdate(dt)`**, refresh `m_screenSize` from the swapchain extent, `m_activeScene->update()`, `inputHandler->endFrame()`, `resourceManager->update()`.

`render()` (:351-399): wait fence[currentFrame] → `vkAcquireNextImageKHR` (on `OUT_OF_DATE`: recreate and return) → `setScreenExtent` → `updateSceneContext(dt)` → `updateStandardBuffers` → **`onPreRender(dt)`** (also where `SkeletalAnimationSystem::update` runs, `EngineAPI.cpp:107-113`) → reset fence → re-import the acquired swapchain image → record `m_commandBuffers[imageIndex]` → submit → `presentFrame` → `currentFrame = (currentFrame+1) % maxFramesInFlight` → **`onPostRender()`**.

Resize: `WindowResizeEvent` → log → `onResize(w,h)` → `handleSwapChainRecreation()` (:267-272), all inside `pollEvents()`, i.e. between frames. Recreation does `vkDeviceWaitIdle`, `swapChain->recreate()`, reallocates command buffers, re-imports images, `renderGraph->recompile()` (throws on failure), `bindIBLTextures()`.

**Can user callbacks mutate the scene mid-frame?** `onUpdate` and `onPreRender` run outside `SystemManager::update`, so creating/destroying entities there is safe. `EcsAPI` system callbacks run *inside* `SystemManager::update` — entity mutation is the usual EnTT hazard, and system mutation is outright UB (Finding 4).

---

## Features

Reachable through the facade without touching engine internals:

- Windowed Vulkan app driven by a JSON render graph; window size, title, log file/level, frames-in-flight, render-graph integer parameters.
- IBL from an HDR equirect file, auto-bound to `iblSet` / `transparentIBLSet` if the pipeline declares them.
- glTF 2.0 loading with per-load options (textures, materials, entities, skins, animations, hierarchy flattening, max texture size, mipmaps, sRGB albedo, name prefix), returning entity handles, geometry/texture/material counts, and animation clip metadata.
- Full ECS entity CRUD, name/tag lookup, string-named component add/remove, parent/child hierarchy.
- Transform, camera (perspective/ortho), light (directional/point/spot), and PBR material parameter access, plus runtime texture assignment from an image path.
- Skeletal animation playback: play/stop, speed, time scrubbing, looping, clip enumeration. Bone SSBOs allocated automatically after glTF load.
- 2D: world-space sprites, screen-space UI panels with 9-way anchoring, TTF text labels with color/size/alignment, UV sub-rects, per-entity 8-bit render layer masks and sort keys.
- Bullet physics: enable/disable, gravity, fixed timestep, substeps, forces/impulses/torque, linear/angular velocity, body rebuild.
- Input: key/mouse polling, mouse delta, scroll delta, capture state, plus key/move/button/scroll event callbacks.
- Script-defined opaque per-entity components and script-defined per-frame systems with priority ordering and failure-based auto-disable.
- Scene save/load to JSON (partial — see gaps).
- Custom named float/vec2/vec3/vec4/uint values injected into shader uniforms via render-graph dot-paths.

`examples/facade_test/main.cpp` is a working end-to-end consumer using only `Facade/` headers — no ApplicationBase, EnTT or Vulkan types (verified: its only includes are the four facade headers plus `<iostream>` and `<windows.h>`).

---

## Limitations & gaps

Verified absent from the code, not speculation:

- **No audio subsystem anywhere in the repo.** `src/` and `include/` contain no audio directory or source.
- **No way to quit programmatically.** The only loop exit is `m_window->shouldClose()` (`ApplicationBase.cpp:92`); nothing in `Facade/` or `App/` exposes `setShouldClose`/`requestExit`. A callback cannot end the application.
- **No immediate-mode UI.** UI is anchored textured quads and baked glyph sprites only — no buttons, no hit-testing, no focus, no input routing to UI. No mouse-picking or raycast of any kind (`PhysicsAPI` has no raycast either).
- **No screenshot / render-to-texture / readback** exposed. `grep readback|screenshot|saveImage` over `src/Facade/`, `include/Facade/`, `src/App/`, `include/App/` returns nothing.
- **Single scene, no scene switching.** `SceneManager` is constructed and used exactly once to create `"ApplicationScene"` (`ApplicationBase.cpp:131-132`); there is no API to create, switch, or unload scenes.
- **Single window.** `VulkanWindow` is a single `unique_ptr` member; no multi-window or headless mode.
- **No hot reload** of pipeline JSON, shaders, scripts or assets. The render graph is loaded once at init and only ever `recompile()`d on resize.
- **Partial serialization.** `Scene::serialize` (`Scene.h:179-260`) writes only Transform, Name, Tag, Camera, Light and Hierarchy. Meshes, materials, textures, sprites, text, animation state, physics bodies and script components are silently dropped, so `saveToFile`/`loadFromFile` cannot round-trip a loaded glTF scene.
- **`EngineConfig` cannot reach several `ApplicationConfig` fields**: `resourceCacheSize` (2 GiB default) and `iblParams` are never mapped (`EngineAPI.cpp:174-200`). Neither struct offers vsync/present mode, fullscreen, resizable, MSAA, or device selection.
- **No frame statistics**: no FPS, frame index, or GPU timing through the facade (`ApplicationBase::getCurrentFrame` exists but is protected and not forwarded).
- **No font preloading or text measurement.** `FontLoader` is only reachable from `ApplicationBase`; the facade cannot query text width, and glyph baking cost is paid inside the frame.
- **No `EcsAPI` documentation** — `docs/api/cpp/` has no ecs-api.md.
- **No thread-safety contract, stated or implied.** No mutex, atomic, or thread mention exists anywhere in `src/Facade/`, `include/Facade/`, `src/App/`, `include/App/`, or the facade docs. Every method touches `entt::registry` unguarded; callbacks are invoked on the main thread. **In practice the whole API is main-thread-only**, and nothing in the headers or docs says so. Calling any facade method from a `std::thread` (a natural thing to do from a script binding) races the ECS. This is a documentation gap rather than a code defect, but it is a real one.

**ABI / PIMPL stability:** all five facade classes use `struct Impl` + `unique_ptr` and expose no engine types. `EngineAPI.h`, `SceneAPI.h`, `EcsAPI.h`, `InputAPI.h`, `PhysicsAPI.h` include only `FacadeTypes.h`, GLM and the standard library — verified, no Vulkan/EnTT leakage. Two leaks of the abstraction remain: `InputAPI::Impl` and `PhysicsAPI::Impl` are declared **public** so `EngineAPI` can wire them (`InputAPI.h:58`, `PhysicsAPI.h:79`), and `SceneAPI::wireSprite2DManager` takes a `Shoonyakasha::Sprite2DManager*` in the public header (`SceneAPI.h:318`). Neither breaks ABI, both widen the public surface. Key codes are raw GLFW ints with no facade-side enum, so consumers must hardcode magic numbers — `examples/facade_test/main.cpp:126` uses `76` with a comment, and `:153` uses `80`.

**Error reporting model** is inconsistent by design layer:
- Construction/initialization failures: **exceptions** (`ApplicationBase.cpp:83-86, 213, 440`) propagating out of `run()`.
- Loading: **result struct** (`GltfResult.success` + `error`).
- Set operations on missing components/invalid entities: **silent no-op** — every `SceneAPI` setter does `if (!valid) return;` then `if (comp) …`.
- Get operations: **caller-invisible default** — `getCameraFov` on an entity with no camera returns 45.0 (`SceneAPI.cpp:333-337`), indistinguishable from a real 45° camera.
- Some operations: **bool** (`addComponent`, `setMaterialTexture`, `addSystem`, `saveToFile`).

There is no error string, error code, or "last error" query anywhere in the facade. A script that misspells a component name gets a `false`; one that misspells a material parameter gets a default value and no signal.

---

## Findings

### CRITICAL

**F1 — `destroyEntity` on any parent entity is undefined behaviour.**
`ECS/Core.h:428-446`, reached from `src/Facade/SceneAPI.cpp:93` → `ECS/Scene.h:58-60`.

```cpp
for (auto child : hierarchy->children) {      // Core.h:438
    if (registry.valid(child)) destroyEntity(registry, child);
}
```

The recursive call reaches `Core.h:433` → `parentHierarchy->removeChild(entity)`, which is `children.erase(std::remove(...))` (`Core.h:54-56`) on **the same vector being iterated**. The range-for iterator is invalidated on the first child. Additionally, `hierarchy` is a `try_get` pointer into the `HierarchyComponent` pool (`Core.h:430`); the recursive `registry.destroy` calls can relocate that pool, dangling the pointer mid-loop. Any `SceneAPI::destroyEntity` on a glTF subtree (`flattenHierarchy=false`) or on any parent built via `setParent` hits this. Not covered by tests: the test-mode branch (`SceneAPI.cpp:95`) calls `registry.destroy` directly and never enters this path, so `SceneAPITest.cpp:65-70` passes while the production path is broken.

**F2 — Sub-API getters dereference a null `unique_ptr` before `run()`.**
`src/Facade/EngineAPI.cpp:287-301`:

```cpp
SceneAPI& EngineAPI::getScene() { return *m_impl->sceneAPI; }
```

`sceneAPI`, `inputAPI`, `physicsAPI`, `ecsAPI` are default-constructed null (`EngineAPI.cpp:47-50`) and only assigned inside `CallbackApp::onInit` (`EngineAPI.cpp:80-93`), which runs from `ApplicationBase.cpp:73`, i.e. partway through `run()`. Any call between construction and `onInit` — the natural place to register input callbacks or configure physics before starting — is UB with no diagnostic. `EngineAPI.h:54` says "valid after construction; populated after onInit", which is self-contradictory, and `docs/api/cpp/engine-api.md:78` and `:212` both state outright that they are valid immediately after construction.

**F3 — `setCustom*` and `ApplicationBase::getRenderGraph()` dereference a null render graph during `onInit`.**
`src/Facade/EngineAPI.cpp:376` (and :380, :384, :388, :392):

```cpp
m_impl->app->getRenderGraph().getSceneContext().setCustom(key, value);
```

`ApplicationBase::getRenderGraph()` is `return *m_renderGraph;` (`ApplicationBase.cpp:480`), and `m_renderGraph` is constructed at `ApplicationBase.cpp:187` — *after* `onInit()` at `:73`. So calling `setCustomFloat` from an `onInit` callback dereferences null. Same for any `ApplicationBase` subclass calling `getRenderGraph()` in `onInit` — which `docs/api/cpp/application-base.md:276` explicitly recommends ("Useful in `onInit()` to set parameters before compilation") and `:150` repeats. As written, the documented way to set render-graph parameters crashes; the working way is `EngineConfig::renderGraphParameters`.

**F4 — Adding or removing an ECS system from inside a system callback is UB.**
`ECS/Systems.h:275-281`:

```cpp
void update(entt::registry& registry, float deltaTime) {
    for (auto& system : m_systems) {          // range-for over std::vector
        if (system->enabled) system->update(registry, deltaTime);
    }
}
```

`EcsAPI::addSystem` (`EcsAPI.cpp:124`) does `m_systems.emplace_back` + `std::sort` (`Systems.h:227-237`); `EcsAPI::removeSystem` (`EcsAPI.cpp:130`) does `m_systems.erase` (`Systems.h:261-268`). Both invalidate the loop's iterator. `removeSystem("self")` from within that system's own callback additionally destroys the `std::function` currently executing (`Systems.h:216`) — use-after-free. `EcsAPI` is explicitly built for scripting languages, where "unregister this system when done" is the obvious idiom. Nothing in `EcsAPI.h` warns against it. `setSystemEnabled` is safe (bool flag only).

### MAJOR

**F5 — Destroying a text label permanently leaks its glyph entities.**
`TextRenderSystem` creates one entity per glyph and records them in `TextBakedComponent::glyphEntities` (`ECS/TextRenderSystem.h:97-125`). They are torn down only in `rebuild()` (`:57-60`), which runs only when the *label's* text/font/size/color/align/visible/layerMask changes (`:36-43`). `SceneAPI::destroyEntity(label)` destroys the label and its `TextBakedComponent` — the glyph entities survive as orphans and keep rendering forever. `TextGlyphOwnerComponent` (written at `TextRenderSystem.h:123`) is never read anywhere in the codebase — grep over `src/` and `include/` finds only its definition (`Sprite2DComponents.h:136`) and that one write. A UI that swaps labels leaks unboundedly.

**F6 — `setUIAnchor` has no effect on a text label.**
`SceneAPI.h:209-212` documents `setUIAnchor` as being for "entities created with createUIPanel/createText", and `SceneAPI.cpp:572-578` does update the label's `UIAnchorComponent`. But the glyph anchors are snapshotted at bake time (`TextRenderSystem.h:75-78, 117-121`), and the change-detection at `:36-42` does not compare the anchor. Moving a label silently does nothing until its text or style also changes. Same for `getUIAnchorOffset`, which reports the new offset while the glyphs stay put.

**F7 — `setVisible` and `setSortKey` are silent no-ops on text labels.**
Both require `RenderableTagComponent` (`SceneAPI.cpp:646-650, 682-686`), which `createText` never adds — the label carries only Transform + Text2D + UIAnchor (`ApplicationBase.cpp:657-673`). `Text2DComponent` has its own `visible` and `sortKey` fields (`Sprite2DComponents.h:100, 105`) that the bake path honours, but the facade exposes **no** `setTextVisible` / `setTextSortKey`. `setTextLayerMask` exists precisely for this reason (`SceneAPI.h:227-230`), so the omission of the other two looks like an oversight. Hiding a text label through the public API is impossible.

**F8 — `setActive` is a semantic no-op.**
`SceneAPI::setActive` (`SceneAPI.cpp:229-239`) writes `ActiveComponent::active`. Grep over all of `src/` and `include/` finds that field read in exactly one place — `EntityHelper::isActive` (`Core.h:471-474`), a helper no system calls. No system filters on it, and `EntityRenderExecutor.cpp:100-102` filters on `RenderableTagComponent::shouldRender()`, not on active state. `docs/api/cpp/scene-api.md:252` states the entity "will be processed by systems" — deactivating an entity changes nothing observable.

**F9 — Two independent, divergent delta times.**
`ApplicationBase::update` computes `m_deltaTime` (`ApplicationBase.cpp:311-314`) and passes it to `onUpdate`, `onPreRender`, `updateSceneContext` and `updateStandardBuffers`. But `m_activeScene->update()` (`:341`) takes no argument and recomputes its own delta from its own `m_lastUpdateTime` (`Scene.h:152-160`). The two clocks are sampled at different points in the frame and drift by the cost of `onUpdate` plus the input copy. Every ECS system — including user systems registered through `EcsAPI::addSystem` — receives the Scene delta, while user `onUpdate` code receives the ApplicationBase delta. Animation driven from a script system and animation driven from `onUpdate` will not stay in sync. Both clamp to 0.1 s independently.

**F10 — `setParent` accepts cycles.**
`SceneAPI.cpp:698-720` performs no ancestry check. `setParent(a, a)` or `setParent(grandparent, child)` produces a cycle; `TransformSystem::updateChildTransforms` (`Systems.h:67+`) then recurses until the stack overflows. Trivially reachable from a script.

**F11 — `saveToFile` reports success when the file was never written.**
`Scene.h:424-433` constructs `std::ofstream file(filename)` and writes without checking `is_open()` or the stream state; `ofstream` does not throw by default, so a bad path or a read-only directory yields `return true`. `SceneAPI::saveToFile` (`SceneAPI.cpp:823-826`) forwards that. Silent data loss on the only persistence API the facade offers.

**F12 — Command buffers are indexed per swapchain image but synchronized per frame-in-flight.**
`ApplicationBase.cpp:352` waits on `m_inFlightFences[m_currentFrame]` (`maxFramesInFlight` = 2 by default), then `:382-385` records into `m_commandBuffers[imageIndex]` where the buffer array is sized to the swapchain image count (`:125`, typically 3). With 3 images and 2 frames in flight, `imageIndex` can repeat before the submission that last used that command buffer has completed — the fence guards a different index space. There is no per-image fence tracking (`m_imagesInFlight` does not exist). This is the classic recording-into-a-pending-command-buffer race; expect validation errors and intermittent corruption.

**F13 — `renderFinished` semaphore is indexed by frame, not by image.**
`ApplicationBase.cpp:392` signals `m_renderFinishedSemaphores[m_currentFrame]` and `:404` waits on it in `vkQueuePresentKHR`. Present-wait semaphores must not be reused until the presentation engine is done with them; with fewer semaphores than swapchain images this can be violated. Per-image (or per-acquire) present semaphores are the standard fix.

**F14 — Minimizing the window is unhandled.**
`VulkanWindow::getWindowExtent` returns `capabilities.currentExtent` verbatim (`VulkanWindow.cpp:130-136`), and `VulkanSwapChain::chooseSwapExtent` returns it unchanged when it is not `UINT32_MAX` (`VulkanSwapChain.cpp:318-321`). On Windows a minimized window reports `{0,0}`. `handleSwapChainRecreation` (`ApplicationBase.cpp:418-446`) calls `recreate()` unconditionally and then `renderGraph->recompile(newExtent, …)`, which throws `std::runtime_error` on failure (`:439-441`) out of an event handler inside `pollEvents()`. There is no `glfwWaitEvents` wait-while-minimized loop anywhere — grep for `glfwWaitEvents|iconif|minimi` across `src/` and `include/` returns nothing.

**F15 — Helper-created entities are invisible to `isActive` and to name/tag queries.**
`createDirectionalLight` (`ApplicationBase.cpp:542-543`), `createPointLight` (`:562-563`), `createSprite` (`:591-592`), `createUIPanel` (`:622-623`) and `createText` (`:657-658`) all use `registry.create()` directly instead of `Scene::createEntity`, so they get no `ActiveComponent`, `NameComponent` or `HierarchyComponent`. `SceneAPI::isActive` returns `false` for every one of them (`SceneAPI.cpp:223-227`, no component → `false`), and `findEntityByName` can never find them. Only `createCamera` goes through the builder (`:495-499`). The inconsistency is invisible until a script filters on `isActive`.

**F16 — No programmatic exit.**
Covered under gaps, repeated here because it is a functional defect rather than a missing nicety: a facade-only application cannot close itself. `onKeyPressed(ESC)` cannot quit, an in-game "Exit" button cannot quit, and an unrecoverable script error cannot shut down cleanly. The only exit is the OS window close button.

### MINOR

**F17 — Sub-APIs outlive the engine objects they reference.**
`EngineAPI::Impl` declares `sceneAPI, inputAPI, physicsAPI, ecsAPI` *before* `app` (`EngineAPI.cpp:47-54`), so on destruction `app` — and with it `ECS::Scene`, `entt::registry`, `EventDispatcher`, `Sprite2DManager` — is destroyed **first**, leaving the four sub-APIs holding dangling references (`SceneAPI.cpp:29-32`, `EcsAPI.cpp:25-26`, `InputAPIImpl.h:23-24`, `PhysicsAPIImpl.h:18-19`) for the remainder of `~Impl`. Benign today because those destructors are `= default` and dereference nothing, but the ordering is backwards and one non-trivial destructor makes it a use-after-free. Also, a user who caches `SceneAPI&` and touches it after `~EngineAPI` begins has no way to know.

**F18 — `InputAPI` never unsubscribes from the `EventDispatcher`.**
`InputAPIImpl.h:41-61` subscribes four lambdas capturing `this`; there is no unsubscribe in `~InputAPI` (`InputAPI.cpp:18`). Safe only because the dispatcher happens to die first (see F17). Repeated `wire()` calls would also double-subscribe.

**F19 — `run()` is not re-entrant or restartable.**
`ApplicationBase::run` (`:63-78`) unconditionally reassigns `m_logger`, `m_eventDispatcher` and every subsystem `unique_ptr`. Calling it twice re-initializes Vulkan over the top of a live instance and re-registers systems on the existing scene. Nothing guards against it.

**F20 — `run()` does not release resources when it returns.**
`docs/api/cpp/application-base.md:135` says run "Calls `onCleanup()` and destroys all resources before returning" and the lifecycle diagram (`:41`) lists `cleanup()` under `run()`. In fact `run()` ends at `onCleanup()` (`ApplicationBase.cpp:101`); `cleanup()` runs only from `~ApplicationBase` (`:53`). The window stays open and the GPU stays allocated between `run()` returning and the object being destroyed.

**F21 — Double swapchain recreation on every resize.** The resize event handler recreates (`ApplicationBase.cpp:271`), and the next `vkAcquireNextImageKHR` frequently still returns `OUT_OF_DATE`, recreating again (`:362-364`). Two full render-graph recompiles per resize event; visible stutter while dragging.

**F22 — FOV units are undocumented in the facade.** `setCameraFov`/`getCameraFov` (`SceneAPI.h:132-133`) say nothing about units. It is degrees (`Core.h:115`, converted by `glm::radians` at `:130`), while `setRotation` in the same class is radians (`SceneAPI.h:113`). Mixed units in one class with only one of them labelled.

**F23 — `getSystemFailureCount` conflates "healthy" and "not found".** `EcsAPI.cpp:149-152` returns 0 for a missing system and for a system with zero failures. `EcsAPI.h:104` acknowledges this ("0 if healthy or not found") but it still makes the API unusable for detecting a typo'd system name; `hasSystem` must be called first.

**F24 — `getSystemMaxFailures` / `setSystemMaxFailures` / `resetSystemFailureCount` silently ignore non-`CallbackSystem` systems.** All four use `dynamic_cast` (`EcsAPI.cpp:150, 155, 160, 165`); a name collision with a built-in system (`TransformSystem`, etc. — none of which set `name`, so this is unlikely but possible) produces a silent no-op with no return value to check on the two setters.

**F25 — `EngineAPI` is documented non-movable but not declared so.** `EngineAPI.h:29-31` deletes copy only; the comment says "Non-copyable, non-movable" and `docs/api/cpp/engine-api.md:84` repeats it. Move is implicitly suppressed by the user-declared destructor, so the behaviour is right, but the declaration does not match the stated intent. Same pattern in `SceneAPI.h:40-42`, `EcsAPI.h:56-57`, `InputAPI.h:22-24`, `PhysicsAPI.h:21-23`.

**F26 — Test-mode and production-mode `SceneAPI` diverge.** `createEntity` (`SceneAPI.cpp:75-88`), `destroyEntity` (`:90-97`), `getEntityCount` (`:103-106`), `findEntityByName` (`:112-123`) and `getMainCamera` (`:140-151`) each have a separate `#ifdef SHOONYAKASHA_TESTING` code path. The entire test suite exercises only the non-Scene branch, so the production branches — including the F1 crash — are untested. `getMainCamera` differs semantically: production delegates to `CameraSystem::getMainCamera`, test mode scans for the first `isMainCamera` flag.

### What the facade tests actually assert

- `FacadeTypesTest.cpp` (116 lines, 9 tests): `sizeof(EntityHandle) == sizeof(entt::entity)`, `NullEntity == entt::null == UINT32_MAX`, handle round-trip, the numeric values of `CameraType`/`LightType`/`RigidBodyType`/`ColliderShape`, and the default field values of `EngineConfig`/`GltfOptions`/`GltfResult`. Pure struct/enum checks — no behaviour. Note `UIAnchor` and `TextHAlign` values are **not** covered despite being `static_cast` across the boundary (`EngineAPI.cpp:347, 358`; `SceneAPI.cpp:535-543`), which is exactly the kind of silent breakage these tests exist to catch.
- `SceneAPITest.cpp` (469 lines, ~45 tests): entity create/destroy/validity/count, name/tag lookup, `getAllEntities`, string-based add/remove/has component, name/tag/active round-trips, transform get/set + dirty-flag propagation + invalid-entity defaults, camera fov/type/near/far/main + `getMainCamera`, light color/intensity/type/range/shadows, material float/vec4/hasParam/defaults, renderable visible/layer-mask/sort-key including the "no tag" cases, hierarchy parent/children, and that save/load return `false` in test mode. **Not asserted anywhere:** sprite/UI/text methods (all 15), `setMaterialTexture`, `getWorldPosition`/`getWorldMatrix`, animation (all 13 methods), `setParent` detach or cycles, and destroying an entity that has children.
- `EcsAPITest.cpp` (223 lines, 15 tests): script-component set/get identity (same pointer, not a copy), missing→null, has/remove semantics, invalid-entity no-op, name listing, `findEntitiesWithComponent` filtering; system registration and per-update invocation, duplicate-name rejection, removal stopping execution, enable/disable, consecutive-failure auto-disable at the threshold, success resetting the counter, `maxFailures=0` never disabling, manual reset, and priority ordering. This is the best-tested part of the facade. It does not test mutating the system list from inside a callback (F4).

No test covers `EngineAPI`, `InputAPI`, `PhysicsAPI` or `ApplicationBase` at all — those need a GPU and a window. `examples/facade_test/main.cpp` is the only exercise of `EngineAPI`, and it is a manual example, not an assertion.

---

## Docs vs code discrepancies

### `docs/api/cpp/engine-api.md`

| Line | Doc claim | Reality |
|---|---|---|
| 78, 212 | "Sub-APIs (`getScene()`, `getInput()`, `getPhysics()`) are valid immediately after construction" | Null until `CallbackApp::onInit` runs inside `run()` (`EngineAPI.cpp:80-93`). Following the doc is UB — see F2. |
| 78 | "Creates the window, Vulkan device, and all internal subsystems" | The constructor only builds an `ApplicationConfig` and a `CallbackApp` object (`EngineAPI.cpp:253-258`). Nothing Vulkan happens until `run()`. |
| 118 | "`setOnInit` — Called once after all engine systems are initialized **and the render graph is compiled**" | `onInit` runs at `ApplicationBase.cpp:73`; the render graph is created and compiled at `:75`/`:187-214`. Exactly backwards, and it is what makes F3 reachable. |
| 166 | "`setOnPostRender` — Called every frame after GPU rendering completes and **before the frame is presented**" | Called at `ApplicationBase.cpp:398`, after `presentFrame(imageIndex)` at `:395`. Also nothing waits for GPU completion — the submit at `:387` is asynchronous. |
| 210-243 | Sub-API section lists three getters | `getEcs()` (`EngineAPI.h:60`) is missing. |
| 246-342 | Convenience Helpers section | `createSprite`, `createUIPanel`, `createText` (`EngineAPI.h:89-109`) are entirely undocumented, as are the `UIAnchor` and `TextHAlign` enums (`FacadeTypes.h:57-73`). |
| 84 | "non-copyable and non-movable" | Only copy is deleted (`EngineAPI.h:30-31`). See F25. |

### `docs/api/cpp/application-base.md`

| Line | Doc claim | Reality |
|---|---|---|
| 276 | "`getRenderGraph()` — Useful in `onInit()` to set parameters before compilation" (repeated at :150) | `m_renderGraph` is null during `onInit` (`ApplicationBase.cpp:187` vs `:73`); `getRenderGraph()` returns `*m_renderGraph` (`:480`). The documented usage is a null dereference — see F3. |
| 22 | Lifecycle diagram places `registerSystems()` between `loadIBLTextures()` and `onInit()` | It is called from inside `initializeECS()` (`ApplicationBase.cpp:134`), i.e. **before** `loadIBLTextures()`. |
| 41 | Diagram lists `cleanup()` as part of `run()`; `:135` says run "destroys all resources before returning" | `cleanup()` is called from `~ApplicationBase` (`:53`), not from `run()` (`:61-102`). See F20. |
| 233-236 | Default `registerSystems()` registers "TransformSystem, CameraSystem, CameraControllerSystem" | Also registers `TextRenderSystem` (priority -2) and `UILayoutSystem` (priority -1) (`ApplicationBase.cpp:152-153`). The omission matters because the doc tells overriders to call the base implementation to keep "the default systems". |
| 242-366 | Accessor list | `getSprite2DManager()` and `getFontLoader()` (`ApplicationBase.h:186-189`) are missing. |
| 368-446 | Convenience Helpers | `createSprite`, `createUIPanel`, `createText` (`ApplicationBase.h:162-183`) are missing. |
| 84 | `ApplicationConfig` table | `resourceCacheSize` (`ApplicationBase.h:64`) is not listed. |

### `docs/api/cpp/scene-api.md`

| Line | Doc claim | Reality |
|---|---|---|
| 250-256 | "`isActive` — Check whether the entity is active (**will be processed by systems**)" | No system reads `ActiveComponent::active`. See F8. |
| 22 | "The `SceneAPI` … is valid for the lifetime of the `EngineAPI`" | It is constructed partway through `run()` and its backing `Scene` is destroyed before it is (F2, F17). |
| n/a | Entire method groups absent | 21 public methods are undocumented: `setMaterialTexture`, `setSpriteTexture`, `set/getSpriteColor`, `set/getSpriteUVRect`, `isScreenSpaceSprite`, `setUIAnchor`, `getUIAnchor`, `getUIAnchorOffset`, `set/getText`, `setTextColor`, `setTextFontSize`, `setTextAlign`, `setTextLayerMask`, `get/setRenderLayerMask`, `get/setSortKey`, `wireSprite2DManager`. The doc's own header line (`:3`) does not mention sprites, UI or text at all. |
| 467-493 | `getCameraFov`/`setCameraFov` | Units not stated. Degrees (`Core.h:115`). See F22. |
| 1150-1176 | `saveToFile` | Does not mention that only Transform/Name/Tag/Camera/Light/Hierarchy are persisted, nor that `true` does not mean the file was written (F11). |
| 935-947 | `setParent` | No mention that cycles are unchecked (F10). |
| 43-54 | `destroyEntity` — "Destroy an entity and remove it from parent hierarchy" | Also recursively destroys all descendants (`Core.h:437-442`) — a materially different contract that a caller needs to know. And it corrupts memory doing so (F1). |

### `docs/api/cpp/input-api.md`, `physics-api.md`

Method sets match the headers exactly (10 and 17 respectively) — no discrepancies found. `input-api.md` does not state that key codes are raw GLFW values with no facade constants, and neither doc states the "returns a default when the engine is not yet running" behaviour (`InputAPI.cpp:24-52`, `PhysicsAPI.cpp:27-65`).

### Missing entirely

`EcsAPI` (14 public methods) has no page in `docs/api/cpp/`. It is referenced only in `docs/plans/2026-07-01-low-level-python-ecs-bindings.md`.

---

## Open questions

1. **Is the facade intended to be usable before `run()`?** `EngineAPI.h:54` and the docs say yes, the code says no. Either the sub-APIs should be constructed in the `EngineAPI` constructor and wired later, or the getters should return `SceneAPI*` / throw, and the docs corrected. This decision also determines the fix for F3.
2. **Is `ActiveComponent` meant to gate systems (F8)?** If yes, the render executor and the transform/camera systems need to honour it. If no, `isActive`/`setActive` should be removed from the facade rather than left as a trap.
3. **Who owns glyph entities (F5)?** `TextGlyphOwnerComponent` exists and is written but never read — was a cleanup pass (an `on_destroy` observer on `Text2DComponent`, or a sweep in `TextRenderSystem`) planned and dropped?
4. **Which delta time is authoritative (F9)?** `Scene::update()` taking an explicit `float dt` would collapse the two clocks; I could not tell whether the parameterless signature is load-bearing for another caller.
5. **Is the main-thread-only contract intentional?** Nothing in the code or docs says either way. Python bindings in particular need this stated explicitly, since releasing the GIL around `run()` and calling back in from another thread would be a natural mistake.
6. **F12/F13 (per-image sync) overlap with the Vulkan RHI reviewer's scope** — I flagged them because they live in `ApplicationBase.cpp`, but the fix may belong in `VulkanSwapChain`/`VulkanCommandManager`. Worth reconciling with the rhi-vulkan findings before acting.
7. **Was `EngineConfig` deliberately narrowed?** `resourceCacheSize` and `iblParams` exist in `ApplicationConfig` but are never mapped in `toAppConfig` (`EngineAPI.cpp:174-200`). Unclear whether this is an oversight or a decision to keep the scripting config minimal.
