# C++ Example Walkthroughs

The engine ships with nine C++ example applications in the `examples/` directory. They range from minimal Facade API demos to full-stack applications combining physics, particles, bloom, and PBR rendering. Each example demonstrates a specific pattern or subsystem.

---

## 1. facade_test -- Pure Facade API

**Source:** [`examples/facade_test/`](../../examples/facade_test/)

**Purpose:** Demonstrates how to build a complete application using *only* the Facade API layer -- no `ApplicationBase`, no EnTT, no Vulkan types. This is the simplest C++ entry point and the closest analog to the Python API.

The example creates an `EngineAPI` with an `EngineConfig`, then registers lambda callbacks for `onInit`, `onPostInit`, `onUpdate`, `onKeyPressed`, and `onCleanup`. Inside these callbacks it uses `SceneAPI` to query entities and positions, `InputAPI` to poll key state with a cooldown timer, and `PhysicsAPI` to toggle simulation. Point lights are spawned at the camera position when `L` is held.

**Key pattern:** Callback-driven application structure using `EngineAPI::setOnInit()`, `setOnUpdate()`, etc. The entire application lives in a single `main.cpp` with no class inheritance.

**Key file:** `examples/facade_test/main.cpp`

---

## 2. declarative_sponza_test -- PBR + IBL Deferred Rendering with Particles

**Source:** [`examples/declarative_sponza_test/`](../../examples/declarative_sponza_test/)

**Purpose:** Full PBR deferred rendering with image-based lighting, a GPU particle compute pass, and SSBO readback. This is the primary `ApplicationBase` showcase.

`DeclarativeSponzaApp` subclasses `ApplicationBase` and overrides `onInit()`, `onPostInit()`, `onPreRender()`, and `onKeyPressed()`. In `onInit` it creates a camera and directional + point lights, loads a glTF scene with `GltfLoadOptions`, and counts opaque vs. transparent entities. In `onPostInit` it registers a readback callback on the `particleSSBO` resource. In `onPreRender` it updates particle simulation parameters (gravity, attractor, wind, damping) via `getRenderGraph().getSceneContext().setCustom()`. The particle count is 50,000.

**Key pattern:** `ApplicationBase` lifecycle hooks + custom uniform injection into compute shaders via the scene context dot-path system.

**Key files:** `examples/declarative_sponza_test/DeclarativeSponzaApp.cpp`, `DeclarativeSponzaApp.h`

---

## 3. skinned_mesh_test -- GPU Skeletal Animation

**Source:** [`examples/skinned_mesh_test/`](../../examples/skinned_mesh_test/)

**Purpose:** GPU skeletal animation with the Fox.glb model. Loads a skinned glTF, creates bone SSBOs, registers the `SkeletalAnimationSystem`, and provides interactive clip switching.

`SkinnedMeshApp` creates a `SkeletalAnimationSystem` in `onInit`, then calls `loadGltfScene` with `loadSkins = true` and `loadAnimations = true`. After loading, it iterates all `SkeletonComponent` entities and calls `createBoneSSBO()` to allocate GPU storage for bone matrices. In `onPreRender`, the animation system evaluates keyframes and uploads bone matrices each frame. Keys 4/5/6 switch animation clips, Space pauses/resumes, and +/- adjust playback speed.

**Key pattern:** `SkeletalAnimationSystem` registration and per-frame `update()` call in `onPreRender`. Bone SSBO creation for GPU-side skinning. Animation clip management via `AnimationPlaybackComponent`.

**Key files:** `examples/skinned_mesh_test/SkinnedMeshApp.cpp`, `SkinnedMeshApp.h`

---

## 4. physics_test -- Bullet3 Physics with Procedural Meshes

**Source:** [`examples/physics_test/`](../../examples/physics_test/)

**Purpose:** Bullet3 rigid body physics with procedurally generated box and sphere meshes. Demonstrates collision detection, impulse application, and dynamic object spawning.

`PhysicsTestApp` registers a `PhysicsSystem` in `registerSystems()` (starting paused). `onInit` creates an elevated camera, then calls `createPhysicsScene()` which builds a ground plane, a stacked tower of five colored boxes, three spheres, and scattered boxes. Each object gets a `MeshComponent` (procedurally generated vertices and indices uploaded to the GPU), a `MaterialComponentV5`, a `RigidBodyComponent`, and a `ColliderComponent`. After emplacing components, `m_physicsSystem->rebuildBody()` registers the object with Bullet3.

Pressing Space spawns a new random object in front of the camera and launches it forward with `addImpulse()`. Pressing R resets the scene by destroying all physics entities and recreating from scratch.

**Key pattern:** Procedural mesh generation + GPU upload, ECS component assembly for physics (`RigidBodyComponent` + `ColliderComponent`), dynamic spawning with impulse.

**Key files:** `examples/physics_test/PhysicsTestApp.cpp`, `PhysicsTestApp.h`

---

## 5. bloom_test -- Post-Processing Bloom Effect

**Source:** [`examples/bloom_test/`](../../examples/bloom_test/)

**Purpose:** Post-processing bloom pipeline with bright extract, horizontal/vertical blur, and composite passes. This example does **not** use `ApplicationBase` -- it manually initializes Vulkan, the swapchain, command buffers, and synchronization objects.

`BloomTestApp` creates `VulkanInstance`, `VulkanWindow`, `VulkanDevice`, and `VulkanSwapChain` directly. It loads `bloom_pipeline.json` into a `RenderGraph`, imports swapchain images, and compiles. The pipeline defines five passes: a forward fullscreen pass, a bright-extract compute dispatch, horizontal and vertical blur compute dispatches, and a fullscreen composite pass. All descriptor bindings and samplers are declared in JSON and auto-bound -- no manual `bindDescriptorSets()` call is needed.

Each frame, the app updates camera UBO (animated orbit), bloom parameter UBO (animated threshold), and sets `elapsedTime` as a named parameter for push constants. The render graph handles all pass execution and image transitions.

**Key pattern:** Manual Vulkan setup without `ApplicationBase`. Fully declarative JSON pipeline with compute and fullscreen passes. Auto-binding of descriptors and samplers from JSON.

**Key files:** `examples/bloom_test/BloomTestApp.cpp`, `BloomTestApp.h`

---

## 6. particle_test -- GPU Particle Compute Simulation

**Source:** [`examples/particle_test/`](../../examples/particle_test/)

**Purpose:** Standalone GPU particle simulation with 100,000 particles using ping-pong SSBO buffers and async compute. Like `bloom_test`, this example manages Vulkan directly without `ApplicationBase`.

`ParticleTestApp` creates two SSBO buffers (`particlesIn` and `particlesOut`) for double-buffering, initializes them with random particle data via a staging buffer upload, and registers them with the render graph using `registerStorageBuffer()`. It also registers uniform buffers for simulation parameters and camera data.

The pipeline (`particle_pipeline.json`) defines a compute dispatch pass (`ParticleSimulate`) that reads from one buffer and writes to the other, plus a draw pass (`ParticleRender`) that renders the output buffer as point sprites. Each frame, the app updates simulation parameters (delta time, gravity, orbiting attractor, boundary radius) and swaps the ping-pong buffer index.

**Key pattern:** Ping-pong SSBO double-buffering for GPU compute. External buffer registration with `registerStorageBuffer()` and `registerUniformBuffer()`. Parameter-based compute dispatch sizing.

**Key files:** `examples/particle_test/ParticleTestApp.cpp`, `ParticleTestApp.h`

---

## 7. ssbo_data_flow_example -- Persistent Particle State

**Source:** [`examples/ssbo_data_flow_example/`](../../examples/ssbo_data_flow_example/)

**Purpose:** Demonstrates GPU-to-CPU data readback and save/load of particle state to disk. Extends the particle simulation with ring-buffered readback callbacks and file persistence.

`SSBODataFlowApp` (150,000 particles) registers a readback callback on `particleSSBO` in `onPostInit` that interprets raw GPU data as particle structs and logs position/velocity of the first particle. Pressing `S` triggers `getRenderGraph().triggerSave("particleSSBO")` which saves the current particle buffer contents to `data/particles.bin`. The app can be launched in "load mode" to restore saved state. Pressing `F12` saves screenshots of all render targets (G-buffer, HDR lit color, bloom, final composite) with timestamped filenames.

**Key pattern:** `registerReadbackCallback()` for GPU-to-CPU data flow. `triggerSave()` for particle state persistence. Render target screenshot capture via `saveRenderTarget()`.

**Key files:** `examples/ssbo_data_flow_example/SSBODataFlowApp.cpp`, `SSBODataFlowApp.h`

---

## 8. pbr_physics_particles -- Combined Full-Stack Demo

**Source:** [`examples/pbr_physics_particles/`](../../examples/pbr_physics_particles/)

**Purpose:** The most comprehensive example -- combines physics simulation, GPU particle compute (75,000 particles), bloom post-processing, and PBR deferred rendering in a single application. Physics impacts create particle attractors.

`CombinedExampleApp` registers a `PhysicsSystem` and creates a full physics scene (ground plane, stacked boxes, spheres). Unique to this example is impact-driven particle behavior: `detectImpacts()` monitors velocity changes of dynamic rigid bodies each frame and, when a collision exceeds a threshold near the ground, creates an `ImpactAttractor` at that location. These attractors are time-limited and fade out over their lifetime. Up to four attractors are simultaneously fed into the particle simulation as `particles.attractorPos0` through `particles.attractorPos3`.

Controls: Space spawns objects with impulse, P toggles physics, R resets the scene, T toggles the particle system on/off.

**Key pattern:** Cross-system integration -- physics collision velocity monitoring drives particle simulation attractors. Impact detection with velocity deltas. Time-decaying attractor system with fading strength.

**Key files:** `examples/pbr_physics_particles/CombinedExampleApp.cpp`, `CombinedExampleApp.h`

---

## 9. particle_flow_example -- Large-Scale Particle Showcase

**Source:** [`examples/particle_flow_example/`](../../examples/particle_flow_example/)

**Purpose:** A visual showcase of 100,000+ particles with dramatic, animated simulation parameters. Loads the full Sponza scene and overlays an aggressive particle system with pulsing gravity, a Lissajous-figure orbiting attractor, and high-turbulence wind.

`ParticleFlowApp` loads the new Sponza glTF (`NewSponza_Main_glTF_003.gltf`) and drives the particle system with more extreme parameters than the other examples: gravity pulses between 0.5 and 2.0, the attractor traces a figure-8 pattern through the atrium with strength oscillating between 15 and 45, wind turbulence is set to 0.8 (vs. 0.3 in the standard demo), and damping is reduced to 0.992 for wilder motion. Pressing F12 saves screenshots of all eight render targets (G-buffer passes, HDR lit, bloom stages, final composite) with millisecond timestamps.

**Key pattern:** Large-scale particle simulation with aggressive, animated parameters. Multi-target screenshot capture for visual debugging.

**Key files:** `examples/particle_flow_example/ParticleFlowApp.cpp`, `ParticleFlowApp.h`

---

## Summary Table

| Example | API Layer | Particle Count | Physics | Key Feature |
|---------|-----------|---------------|---------|-------------|
| `facade_test` | Facade (no ApplicationBase) | -- | Toggle | Pure callback API |
| `declarative_sponza_test` | ApplicationBase | 50,000 | -- | PBR+IBL + particle uniforms |
| `skinned_mesh_test` | ApplicationBase | -- | -- | GPU skeletal animation |
| `physics_test` | ApplicationBase | -- | Full | Procedural meshes + Bullet3 |
| `bloom_test` | Manual Vulkan | -- | -- | Declarative JSON bloom pipeline |
| `particle_test` | Manual Vulkan | 100,000 | -- | Ping-pong SSBO compute |
| `pbr_physics_particles` | ApplicationBase | 75,000 | Full | Physics-driven particle attractors |
| `particle_flow_example` | ApplicationBase | 100,000 | -- | Large-scale visual showcase |
| `ssbo_data_flow_example` | ApplicationBase | 150,000 | -- | GPU readback + save/load |
