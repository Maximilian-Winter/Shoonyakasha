# Shoonyakasha Engine Documentation — Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Create comprehensive Markdown documentation for the Shoonyakasha engine covering Python game developers and C++ engine developers — ~35 documents organized by audience and purpose.

**Architecture:** Layered docs in `docs/` — getting-started guides, topic guides, API reference (Python + C++), architecture deep-dives, example walkthroughs, and FAQ. Each document is self-contained with cross-links.

**Tech Stack:** Markdown files in-repo, Mermaid diagrams where useful, code examples from actual engine source.

---

## Conventions

- **File paths** in examples use forward slashes
- **Python examples** shown first in guides, C++ equivalent below
- **Cross-links** use relative Markdown links: `[Scene API](../api/python/scene.md)`
- **Code blocks** use language-specific syntax highlighting (```python, ```cpp, ```json)
- **No build/run instructions** — CLAUDE.md says don't run cmake from CLI
- **Source references** use format: `include/Facade/SceneAPI.h:45`

---

## Task 1: Foundation — index.md and philosophy.md

**Files:**
- Create: `docs/index.md`
- Create: `docs/philosophy.md`

**Step 1: Write docs/index.md**

The main entry point. Include:
- Engine name, tagline, brief description (2-3 sentences)
- Feature highlights (bullet list: Vulkan rendering, ECS, Physics, Python bindings, JSON pipeline)
- Navigation table linking to all sections:
  - Getting Started (Python / C++)
  - Guides (each guide linked)
  - API Reference (Python / C++)
  - Architecture
  - Examples
  - FAQ
- "Quick links" for common tasks: "Load a scene", "Add physics", "Animate a character"

**Step 2: Write docs/philosophy.md**

Content:
- The meaning of Śūnya (emptiness) and Ākāśa (space) in the engine context
- The Three Design Treasures: Simplicity, Clarity, Stability
- How they map to code: clean APIs, RAII, composable ECS
- "JSON is the Engine" — declarative philosophy explained
- Dedication to Vajrayogini

**Step 3: Commit**

```bash
git add docs/index.md docs/philosophy.md
git commit -m "docs: add documentation index and philosophy pages"
```

---

## Task 2: Getting Started — Prerequisites

**Files:**
- Create: `docs/getting-started/prerequisites.md`

**Step 1: Write prerequisites.md**

Content:
- Required software: C++17 compiler (MSVC recommended), CMake 3.20+, Vulkan SDK, Python 3.10+
- Key dependencies (managed by CMake): EnTT, Bullet3, GLM, VMA, GLFW, nlohmann-json, stb, tinygltf
- Build options: `BUILD_PYTHON=ON` for Cython bindings, `BUILD_TESTS=ON` for tests
- Python setup: add `python/` to PYTHONPATH, import `shoonyakasha`
- Note: IDE build recommended (CLion/Visual Studio), not CLI cmake

**Step 2: Commit**

```bash
git add docs/getting-started/prerequisites.md
git commit -m "docs: add prerequisites guide"
```

---

## Task 3: Getting Started — Python Quickstart

**Files:**
- Create: `docs/getting-started/python-quickstart.md`

**Step 1: Write python-quickstart.md**

Structure:
1. **Goal:** Render the Sponza scene with a camera and light in ~30 lines of Python
2. **Minimal example** (adapted from demo.py):
   ```python
   from shoonyakasha import Engine

   engine = Engine(
       title="My First Scene",
       width=1600, height=900,
       pipeline_json_path="path/to/pbr_ibl_pipeline_v3.json",
       hdr_environment_path="path/to/environment.hdr"
   )

   def on_init():
       engine.create_camera((0, 2, 5), fov=60.0, speed=8.0)
       engine.create_directional_light((1, -1, 0.5), (1, 1, 1), 2.0)
       result = engine.load_gltf_scene("path/to/scene.glb")
       print(f"Loaded {result.total_vertices} vertices")

   engine.set_on_init(on_init)
   engine.run()
   ```
3. **What happened:** Explain each line
4. **Adding interactivity:** on_update callback, input polling
5. **Next steps:** Link to guides (loading-scenes, cameras, physics)

**Step 2: Commit**

```bash
git add docs/getting-started/python-quickstart.md
git commit -m "docs: add Python quickstart guide"
```

---

## Task 4: Getting Started — C++ Quickstart

**Files:**
- Create: `docs/getting-started/cpp-quickstart.md`

**Step 1: Write cpp-quickstart.md**

Structure:
1. **Goal:** Render a scene using ApplicationBase in ~50 lines
2. **Example** (adapted from declarative_sponza_test):
   ```cpp
   #include <Shoonyakasha.h>

   class MyApp : public ApplicationBase {
   public:
       MyApp() {
           config.title = "My First Scene";
           config.pipelineJsonPath = "path/to/pipeline.json";
           config.hdrEnvironmentPath = "path/to/env.hdr";
       }

   protected:
       void onInit() override {
           createCamera({0, 2, 5}, 60.f, 8.f);
           createDirectionalLight({1, -1, 0.5}, {1, 1, 1}, 2.0f);
           loadGltfScene("path/to/scene.glb");
       }

       void onUpdate(float dt) override {
           // Game logic here
       }
   };

   int main() {
       MyApp app;
       app.run();
       return 0;
   }
   ```
3. **Explanation** of the ApplicationBase lifecycle
4. **Facade alternative:** Show EngineAPI approach for those who prefer callbacks over inheritance
5. **Next steps:** Link to guides and C++ API reference

**Step 2: Commit**

```bash
git add docs/getting-started/cpp-quickstart.md
git commit -m "docs: add C++ quickstart guide"
```

---

## Task 5: Python API Reference — Engine, Constants, GltfResult

**Files:**
- Create: `docs/api/python/engine.md`
- Create: `docs/api/python/constants.md`
- Create: `docs/api/python/gltf-result.md`

**Step 1: Write engine.md**

Full reference for the `Engine` class:
- Constructor with all parameters (title, width, height, log_file, hdr_environment_path, pipeline_json_path, render_graph_parameters)
- Lifecycle callbacks: set_on_init, set_on_post_init, set_on_update, set_on_pre_render, set_on_post_render, set_on_key_pressed, set_on_resize, set_on_cleanup
- Sub-API properties: scene, input, physics
- Entity helpers: create_camera, create_directional_light, create_point_light, load_gltf_scene, camera_entity, delta_time
- Custom uniforms: set_custom_float, set_custom_vec2, set_custom_vec3, set_custom_vec4, set_custom_uint
- run() method

Format: signature, parameters table, return type, example, notes.

**Step 2: Write constants.md**

Document all exported constants:
- NULL_ENTITY
- CAMERA_PERSPECTIVE, CAMERA_ORTHOGRAPHIC
- LIGHT_DIRECTIONAL, LIGHT_POINT, LIGHT_SPOT
- RIGIDBODY_STATIC, RIGIDBODY_KINEMATIC, RIGIDBODY_DYNAMIC
- COLLIDER_BOX, COLLIDER_SPHERE, COLLIDER_CAPSULE, COLLIDER_MESH, COLLIDER_PLANE

Include usage examples showing where each constant is used.

**Step 3: Write gltf-result.md**

Document GltfResult:
- success, error, entities, total_vertices, total_indices, total_textures, total_materials, animation_clips, skeleton_count
- Example of checking result and iterating entities

**Step 4: Commit**

```bash
git add docs/api/python/engine.md docs/api/python/constants.md docs/api/python/gltf-result.md
git commit -m "docs: add Python API reference for Engine, constants, and GltfResult"
```

---

## Task 6: Python API Reference — Scene

**Files:**
- Create: `docs/api/python/scene.md`

**Step 1: Write scene.md**

The largest Python API reference. Organize by category:

1. **Entity Lifecycle:** create_entity, destroy_entity, is_valid, entity_count
2. **Entity Queries:** find_entity_by_name, find_entities_with_tag, get_main_camera, get_all_entities
3. **Components:** add_component, remove_component, has_component, get_component_names
4. **Name/Tag/Active:** get/set_name, get/set_tag, is_active/set_active
5. **Transform:** get/set_position, get/set_rotation, get/set_scale, get_world_position, get_world_matrix, get_forward/right/up
6. **Camera:** get/set_camera_type/fov/near/far/ortho_size, is/set_camera_main
7. **Light:** get/set_light_type/color/intensity/range, get/set_light_cast_shadows
8. **Material:** set/get_material_float/vec3/vec4, has_material_param
9. **Renderable:** is/set_visible, get/set_cast_shadows
10. **Hierarchy:** get_parent, set_parent, get_children
11. **Animation:** get_animation_clip_count/name/duration, play/stop_animation, is_animation_playing, get/set_animation_speed/time/looping, get_current_animation_clip

Each method: signature, parameters, returns, brief example where helpful.

**Step 2: Commit**

```bash
git add docs/api/python/scene.md
git commit -m "docs: add Python API reference for Scene"
```

---

## Task 7: Python API Reference — Input and Physics

**Files:**
- Create: `docs/api/python/input.md`
- Create: `docs/api/python/physics.md`

**Step 1: Write input.md**

Document Input class:
- Polling: is_key_down, is_mouse_button_down, mouse_position, mouse_delta, scroll_delta, is_mouse_captured
- Callbacks: set_on_key_event, set_on_mouse_move, set_on_mouse_button, set_on_mouse_scroll
- Note about GLFW key codes
- Example: WASD movement pattern

**Step 2: Write physics.md**

Document Physics class:
- Properties: enabled, gravity, fixed_time_step, max_sub_steps
- Forces: add_force, add_impulse, add_torque_impulse
- Velocity: set/get_linear_velocity, set/get_angular_velocity
- Body management: rebuild_body, get_body_count
- Example: applying impulse on key press

**Step 3: Commit**

```bash
git add docs/api/python/input.md docs/api/python/physics.md
git commit -m "docs: add Python API reference for Input and Physics"
```

---

## Task 8: C++ API Reference — Facade APIs

**Files:**
- Create: `docs/api/cpp/engine-api.md`
- Create: `docs/api/cpp/scene-api.md`
- Create: `docs/api/cpp/input-api.md`
- Create: `docs/api/cpp/physics-api.md`

**Step 1: Write all four facade API references**

Mirror the Python API docs but with C++ signatures:
- Use `glm::vec3` instead of tuples
- Show `EntityHandle` type
- Include `std::function<>` callback types
- Reference source headers: `include/Facade/EngineAPI.h`, etc.
- Note: These are the same APIs that Python wraps via Cython

**Step 2: Commit**

```bash
git add docs/api/cpp/engine-api.md docs/api/cpp/scene-api.md docs/api/cpp/input-api.md docs/api/cpp/physics-api.md
git commit -m "docs: add C++ facade API reference"
```

---

## Task 9: C++ API Reference — ECS Components

**Files:**
- Create: `docs/api/cpp/ecs-components.md`

**Step 1: Write ecs-components.md**

Document all component structs organized by category:

1. **Core:** TagComponent, NameComponent, HierarchyComponent, TransformComponent, ActiveComponent, LifetimeComponent
2. **Camera:** CameraComponent (with Type enum), CameraControllerComponent (with Mode enum)
3. **Light:** LightComponent (with Type enum)
4. **Physics:** RigidBodyComponent (with Type enum), ColliderComponent (with Shape enum)
5. **Render:** MeshComponent, MaterialComponentV5 (with AlphaMode), RenderableTagComponent, SceneEnvironment
6. **Animation:** SkeletonComponent, AnimationPlaybackComponent
7. **Data Types:** Joint, Skeleton, AnimationChannel, AnimationClip, AnimationInterpolation

Also document:
- EntityBuilder — fluent factory with all builder methods
- ComponentRegistry — string-based component creation

Reference: `include/ECS/Core.h`, `include/ECS/RenderComponents.h`, `include/ECS/SkeletonComponents.h`

**Step 2: Commit**

```bash
git add docs/api/cpp/ecs-components.md
git commit -m "docs: add ECS components reference"
```

---

## Task 10: C++ API Reference — ApplicationBase, GPU Types

**Files:**
- Create: `docs/api/cpp/application-base.md`
- Create: `docs/api/cpp/gpu-types.md`

**Step 1: Write application-base.md**

Document:
- ApplicationConfig struct (all fields with defaults)
- Virtual hooks: onInit, onPostInit, onUpdate, onPreRender, onPostRender, onKeyPressed, onResize, onCleanup, registerSystems
- Convenience helpers: createCamera, createDirectionalLight, createPointLight, loadGltfScene
- Accessors: getDevice, getWindow, getRenderGraph, getRegistry, getInputHandler
- Lifecycle diagram: construct → run() → [init → compile → loop(update → render) → cleanup]

**Step 2: Write gpu-types.md**

Document:
- GPUBuffer struct (buffer, allocation, size)
- GPUTexture struct (image, view, sampler, allocation, format, dimensions, mipLevels)
- MaterialParam struct (Type enum, from<T>(), as<T>())
- AlphaMode enum (Opaque, Mask, Blend)
- IndexType enum (UInt16, UInt32)

**Step 3: Commit**

```bash
git add docs/api/cpp/application-base.md docs/api/cpp/gpu-types.md
git commit -m "docs: add ApplicationBase and GPU types reference"
```

---

## Task 11: C++ API Reference — Frame Graph, glTF Loader, IBL

**Files:**
- Create: `docs/api/cpp/frame-graph.md`
- Create: `docs/api/cpp/gltf-loader.md`
- Create: `docs/api/cpp/ibl-generator.md`

**Step 1: Write frame-graph.md**

Document:
- RenderGraph — loadGraphFromJson/File, saveGraphToJson/File
- Key enums: PassType, ResourceKind, ResourceUsage, QueueType
- ExecutionDesc — execution types and their fields
- DispatchDimension — fixed, resource-based, parameter-based
- DotPathResolver — ResolvedValue, dot-path syntax reference table
- FrameGraphRenderer — setRegistry, setCameraPosition, executeGeometryPass, queryEntities
- EntityFilter and EntitySortMode enums

**Step 2: Write gltf-loader.md**

Document:
- GltfOptions struct (all fields with defaults)
- GltfPrimitive struct (mesh data, textures, material params)
- GltfLoadResult struct (success, entities, primitives, skeletons, clips, counts)
- Usage pattern: configure options → load → iterate entities

**Step 3: Write ibl-generator.md**

Document:
- IBLGenerationParams struct (all sizes and sample counts)
- IBLResources struct (environment, irradiance, prefilter, brdfLUT)
- IBLGenerator class — generate(), individual map functions
- Usage: provide HDR path, get IBL resources for PBR

**Step 4: Commit**

```bash
git add docs/api/cpp/frame-graph.md docs/api/cpp/gltf-loader.md docs/api/cpp/ibl-generator.md
git commit -m "docs: add frame graph, glTF loader, and IBL generator reference"
```

---

## Task 12: Guide — Loading Scenes

**Files:**
- Create: `docs/guides/loading-scenes.md`

**Step 1: Write loading-scenes.md**

Content:
1. **glTF format overview** — what glTF 2.0 is, what the engine supports (meshes, materials, textures, skeletons, animations)
2. **Python: Basic loading** — `engine.load_gltf_scene(path)` with default options
3. **Python: Load options** — textures, materials, entities, skins, animations, flattenHierarchy, namePrefix
4. **C++: Loading** — `loadGltfScene(path, options)` with GltfOptions struct
5. **Inspecting results** — vertex/index/texture counts, entity list, animation clips
6. **Working with loaded entities** — find by name, iterate, modify materials
7. **Common patterns** — loading multiple models, positioning loaded scenes

**Step 2: Commit**

```bash
git add docs/guides/loading-scenes.md
git commit -m "docs: add scene loading guide"
```

---

## Task 13: Guide — JSON Render Pipeline

**Files:**
- Create: `docs/guides/json-render-pipeline.md`

**Step 1: Write json-render-pipeline.md**

The most important guide — explains the engine's core philosophy.

Content:
1. **"JSON is the Engine"** — philosophy: declare, don't code
2. **Pipeline structure** — top-level keys: version, name, bufferLayouts, entityDataBindings, samplers, descriptorSetLayouts, resources, passes
3. **Resources** — defining render targets (images with format, width/height or backbuffer, usage, scaling)
4. **Buffer layouts** — field types (Float, Vec2, Vec3, Vec4, Mat4, etc.), packing rules (Std140, Std430, PushConstant)
5. **Dot-path syntax** — full reference:
   - `entity.transform.worldMatrix`
   - `entity.material.params.<name>`
   - `entity.material.textures.<name>`
   - `scene.camera.view`, `scene.camera.projection`, `scene.camera.position`
   - `scene.environment.irradianceMap`, `scene.environment.prefilterMap`, `scene.environment.brdfLUT`
   - `scene.lights[N].*`
   - `scene.custom.<name>`
   - `const.<value>`, `const.vec4(r,g,b,a)`
6. **Execution types** — opaque_geometry, transparent_geometry, shadow_casters, skinned_geometry, fullscreen, compute_dispatch, draw
7. **Descriptor sets** — layout definition, binding types (uniform_buffer, storage_buffer, combined_image_sampler, storage_image)
8. **Entity data bindings** — per-draw and per-material data mappings
9. **Samplers** — filter modes, address modes, mipmap modes
10. **Complete example** — annotated walkthrough of the PBR+IBL pipeline
11. **Common patterns** — deferred rendering, post-processing, compute pipelines

**Step 2: Commit**

```bash
git add docs/guides/json-render-pipeline.md
git commit -m "docs: add JSON render pipeline guide"
```

---

## Task 14: Guide — Entities and Components

**Files:**
- Create: `docs/guides/entities-and-components.md`

**Step 1: Write entities-and-components.md**

Content:
1. **ECS overview** — entities are IDs, components are data, systems process them
2. **Creating entities** — Python: `scene.create_entity("name")`, C++: EntityBuilder
3. **Adding components** — string-based: `scene.add_component(entity, "RigidBody")`
4. **Transform** — position, rotation (Euler radians, Y→X→Z order), scale, world vs local
5. **Hierarchy** — set_parent, get_children, world matrix propagation
6. **Name, Tag, Active** — organizational tools
7. **Querying** — find by name, find by tag, get all entities
8. **C++ EntityBuilder** — fluent API example with `.withName().withTransform().withLight().build()`
9. **C++ ComponentRegistry** — string-based access for generic tools

**Step 2: Commit**

```bash
git add docs/guides/entities-and-components.md
git commit -m "docs: add entities and components guide"
```

---

## Task 15: Guide — Cameras and Controllers

**Files:**
- Create: `docs/guides/cameras-and-controllers.md`

**Step 1: Write cameras-and-controllers.md**

Content (incorporate existing CameraSystemIntegration.md):
1. **Camera types** — Perspective (fov, near, far) vs Orthographic (orthoSize)
2. **Creating cameras** — Python: `engine.create_camera(pos, fov, speed)`, C++: EntityBuilder
3. **Main camera** — `set_camera_main(entity, True)`, only one at a time
4. **Four controller modes:**
   - Free (fly camera) — WASD, mouse look, sprint
   - Orbit — rotate around target, zoom, pan
   - FirstPerson — grounded FPS, eye height
   - ThirdPerson — follow entity with offset
5. **Controls reference** (from existing docs)
6. **Customization** — speed, sensitivity, smoothing

**Step 2: Commit**

```bash
git add docs/guides/cameras-and-controllers.md
git commit -m "docs: add cameras and controllers guide"
```

---

## Task 16: Guide — Lighting and IBL

**Files:**
- Create: `docs/guides/lighting-and-ibl.md`

**Step 1: Write lighting-and-ibl.md**

Content:
1. **Light types** — Directional, Point, Spot (with cone angles)
2. **Creating lights** — Python convenience methods, C++ helpers
3. **Light properties** — color, intensity, range, attenuation, shadows
4. **HDR environment** — what it is, setting hdr_environment_path in config
5. **IBL pipeline** — irradiance map (diffuse), prefilter map (specular), BRDF LUT
6. **IBL in JSON pipeline** — binding iblSet with scene.environment.* dot-paths
7. **Dynamic lights** — spawning point lights at runtime (from demo.py pattern)

**Step 2: Commit**

```bash
git add docs/guides/lighting-and-ibl.md
git commit -m "docs: add lighting and IBL guide"
```

---

## Task 17: Guide — Materials

**Files:**
- Create: `docs/guides/materials.md`

**Step 1: Write materials.md**

Content:
1. **PBR material model** — baseColorFactor, metallic, roughness, emissive
2. **Material textures** — albedoMap, normalMap, metallicRoughnessMap, aoMap, emissiveMap
3. **Alpha modes** — Opaque, Mask (alphaCutoff), Blend
4. **Setting material params** — Python: `scene.set_material_float(entity, "roughness", 0.5)`
5. **glTF material import** — automatic from loaded models
6. **How materials flow to shaders** — entity.material.params.* dot-paths in JSON pipeline
7. **Per-entity material override** — modify individual entity materials at runtime

**Step 2: Commit**

```bash
git add docs/guides/materials.md
git commit -m "docs: add materials guide"
```

---

## Task 18: Guide — Physics

**Files:**
- Create: `docs/guides/physics.md`

**Step 1: Write physics.md**

Content:
1. **Bullet3 overview** — what it provides (rigid bodies, colliders, forces)
2. **Enabling physics** — `physics.enabled = True`
3. **World settings** — gravity, fixed_time_step, max_sub_steps
4. **Rigid body types** — Static (immovable), Kinematic (animated), Dynamic (simulated)
5. **Collider shapes** — Box, Sphere, Capsule, Mesh, Plane — with size parameters
6. **Adding physics to entities** — Python: add_component("RigidBody") + add_component("Collider"), C++: EntityBuilder
7. **Forces and impulses** — add_force (continuous), add_impulse (instant), add_torque_impulse
8. **Velocity control** — set/get linear and angular velocity
9. **Rebuilding bodies** — when to call rebuild_body()
10. **Example** — spawning physics objects on key press (from physics_test)

**Step 2: Commit**

```bash
git add docs/guides/physics.md
git commit -m "docs: add physics guide"
```

---

## Task 19: Guide — Animation

**Files:**
- Create: `docs/guides/animation.md`

**Step 1: Write animation.md**

Content (based on skinned_fox_demo.py patterns):
1. **Skeletal animation overview** — joints, keyframes, GPU skinning
2. **Loading animated models** — glTF with `load_skins=True, load_animations=True`
3. **Querying animation data** — clip count, clip names, durations
4. **Playing animations** — `scene.play_animation(entity, clip_index)`
5. **Playback control** — speed, time scrubbing, looping, stop
6. **Switching clips** — change animation at runtime
7. **How it works** — SkeletonComponent + AnimationPlaybackComponent → bone matrices → SSBO → GPU
8. **C++ specifics** — SkeletalAnimationSystem, AnimationChannel interpolation modes

**Step 2: Commit**

```bash
git add docs/guides/animation.md
git commit -m "docs: add animation guide"
```

---

## Task 20: Guide — Custom Shader Uniforms and Scene Serialization

**Files:**
- Create: `docs/guides/custom-shader-uniforms.md`
- Create: `docs/guides/scene-serialization.md`

**Step 1: Write custom-shader-uniforms.md**

Content:
1. **Purpose** — pass runtime values to shaders without modifying C++
2. **API** — set_custom_float/vec2/vec3/vec4/uint with string keys
3. **Accessing in JSON** — dot-path `scene.custom.<key>`
4. **Example** — particle simulation parameters (from demo.py: particles.gravity, particles.wind, etc.)
5. **Per-frame updates** — set in on_update/on_pre_render callbacks

**Step 2: Write scene-serialization.md**

Content:
1. **Save/Load** — `scene.save_to_file(path)` / `scene.load_from_file(path)`
2. **What's saved** — entity hierarchy, components, transforms, names, tags
3. **Limitations** — GPU resources (meshes, textures) not serialized; re-load models after loading scene

**Step 3: Commit**

```bash
git add docs/guides/custom-shader-uniforms.md docs/guides/scene-serialization.md
git commit -m "docs: add custom uniforms and scene serialization guides"
```

---

## Task 21: Architecture — Overview and Frame Graph Pipeline

**Files:**
- Create: `docs/architecture/overview.md`
- Create: `docs/architecture/frame-graph-pipeline.md`

**Step 1: Write overview.md**

Content:
1. **System diagram** (Mermaid):
   ```
   Python Script → Cython Bridge → C++ Facade (PIMPL) → ApplicationBase → Vulkan
                                                       → ECS (EnTT)
                                                       → Physics (Bullet3)
                                                       → Frame Graph (JSON→Compile→Execute)
   ```
2. **Data flow:** JSON pipeline → FrameGraph compiler → Compiled passes → DotPathResolver → ECS → FrameGraphRenderer → Vulkan commands
3. **Subsystem map** — which directories own which responsibilities
4. **Threading model** — main thread, render thread considerations

**Step 2: Write frame-graph-pipeline.md**

Content:
1. **The problem** — Vulkan requires massive boilerplate for render passes, descriptors, pipelines
2. **The solution** — Declare everything in JSON, compile once, execute every frame
3. **Compilation phase** — JSON → FrameGraphBuilder → resource allocation → pipeline creation → descriptor set layout
4. **Execution phase** — per-frame: DotPathResolver reads ECS → fills UBOs/push constants → FrameGraphRenderer executes geometry passes
5. **Entity data binding** — how per-draw and per-material data flows from ECS to GPU
6. **Resource management** — images sized to backbuffer, format conversion, MSAA
7. **Compute integration** — compute_dispatch with parameter-driven group counts

**Step 3: Commit**

```bash
git add docs/architecture/overview.md docs/architecture/frame-graph-pipeline.md
git commit -m "docs: add architecture overview and frame graph pipeline docs"
```

---

## Task 22: Architecture — ECS, Facade, Cython Bridge

**Files:**
- Create: `docs/architecture/ecs-design.md`
- Create: `docs/architecture/facade-pattern.md`
- Create: `docs/architecture/cython-bridge.md`

**Step 1: Write ecs-design.md**

Content:
1. **Why ECS** — cache-friendly, composable, decoupled
2. **EnTT integration** — registry, entity, component, view
3. **System priorities** — Transform(0) → Camera(10) → CameraController(5) → Render(20)
4. **Component design rules** — POD-like, no logic in components, systems do the work
5. **String-based access** — ComponentRegistry enables Python genericity

**Step 2: Write facade-pattern.md**

Content:
1. **Purpose** — hide Vulkan, EnTT, Bullet3 from Python bindings
2. **PIMPL implementation** — EngineAPI/SceneAPI/etc. hold opaque Impl pointers
3. **EntityHandle** — opaque type wrapping entt::entity
4. **Type conversion** — glm::vec3 ↔ tuples, callbacks ↔ std::function
5. **Why 4 classes** — separation of concerns matching Python module design

**Step 3: Write cython-bridge.md**

Content:
1. **Architecture** — .pxd (C++ declarations) → .pyx (Python wrapper) → .pyd (compiled module)
2. **GLM bridge** — glm_bridge.h converts between glm types and C arrays
3. **GIL safety** — callback bridge ensures Python callbacks work with Vulkan thread
4. **Build process** — Cython → C++ → MSVC → .pyd, controlled by BUILD_PYTHON cmake option

**Step 4: Commit**

```bash
git add docs/architecture/ecs-design.md docs/architecture/facade-pattern.md docs/architecture/cython-bridge.md
git commit -m "docs: add ECS, facade, and Cython bridge architecture docs"
```

---

## Task 23: Examples Walkthrough

**Files:**
- Create: `docs/examples/python-examples.md`
- Create: `docs/examples/cpp-examples.md`

**Step 1: Write python-examples.md**

Walk through:
1. **demo.py** — Full-featured demo: Sponza loading, particle simulation, dynamic lights, physics toggle. Annotate the callback pattern and custom uniform usage.
2. **skinned_fox_demo.py** — Skeletal animation: glTF with skins, clip querying, playback control, speed adjustment. Annotate the animation API usage.

**Step 2: Write cpp-examples.md**

Walk through each C++ example briefly:
1. **facade_test** — Demonstrates facade API without ApplicationBase
2. **declarative_sponza_test** — Full PBR+IBL deferred pipeline with particles
3. **skinned_mesh_test** — GPU skeletal animation
4. **physics_test** — Bullet3 physics with procedural meshes
5. **bloom_test** — Post-processing with compute shaders
6. **particle_test** — GPU particle compute simulation
7. **ssbo_data_flow_example** — Persistent particle state (save/load)
8. **pbr_physics_particles** — Combined full-stack demo

For each: purpose, key pattern demonstrated, relevant files.

**Step 3: Commit**

```bash
git add docs/examples/python-examples.md docs/examples/cpp-examples.md
git commit -m "docs: add Python and C++ example walkthroughs"
```

---

## Task 24: FAQ

**Files:**
- Create: `docs/faq.md`

**Step 1: Write faq.md**

Categories:

**Setup:**
- "How do I build the engine?" — Use CLion/VS, not CLI cmake
- "How do I use Python bindings?" — Add python/ to PYTHONPATH, import shoonyakasha
- "What Vulkan SDK version do I need?"

**Rendering:**
- "How do I create a custom render pipeline?" — Write JSON, point engine to it
- "What is a dot-path?" — Brief explanation with link to guide
- "How do I add post-processing?" — Fullscreen pass in JSON

**Physics:**
- "Physics isn't working?" — Check enabled, check components added
- "How do I change collider shape at runtime?" — Modify + rebuild_body()

**Python:**
- "How do I debug Python scripts?" — Logging, error handling
- "Can I use async/await?" — No, engine runs synchronous main loop
- "How do I pass data to shaders?" — set_custom_* API

**Step 2: Commit**

```bash
git add docs/faq.md
git commit -m "docs: add FAQ"
```

---

## Task 25: Final Review and Cross-Link Pass

**Step 1: Review all documents**

Read through all created docs and verify:
- All cross-links resolve correctly
- No broken relative paths
- Consistent formatting
- No placeholder content left
- index.md links to all documents

**Step 2: Update index.md with final link list**

Ensure every document is linked from the index.

**Step 3: Final commit**

```bash
git add docs/
git commit -m "docs: final review — fix cross-links and update index"
```

---

## Summary

| Task | Documents | Est. Size |
|------|-----------|-----------|
| 1 | index.md, philosophy.md | Foundation |
| 2 | prerequisites.md | Getting Started |
| 3 | python-quickstart.md | Getting Started |
| 4 | cpp-quickstart.md | Getting Started |
| 5 | engine.md, constants.md, gltf-result.md | Python API Ref |
| 6 | scene.md | Python API Ref |
| 7 | input.md, physics.md | Python API Ref |
| 8 | engine-api.md, scene-api.md, input-api.md, physics-api.md | C++ API Ref |
| 9 | ecs-components.md | C++ API Ref |
| 10 | application-base.md, gpu-types.md | C++ API Ref |
| 11 | frame-graph.md, gltf-loader.md, ibl-generator.md | C++ API Ref |
| 12 | loading-scenes.md | Guide |
| 13 | json-render-pipeline.md | Guide |
| 14 | entities-and-components.md | Guide |
| 15 | cameras-and-controllers.md | Guide |
| 16 | lighting-and-ibl.md | Guide |
| 17 | materials.md | Guide |
| 18 | physics.md | Guide |
| 19 | animation.md | Guide |
| 20 | custom-shader-uniforms.md, scene-serialization.md | Guide |
| 21 | overview.md, frame-graph-pipeline.md | Architecture |
| 22 | ecs-design.md, facade-pattern.md, cython-bridge.md | Architecture |
| 23 | python-examples.md, cpp-examples.md | Examples |
| 24 | faq.md | FAQ |
| 25 | Cross-link review | Final |

**Total: 25 tasks, ~35 documents**
