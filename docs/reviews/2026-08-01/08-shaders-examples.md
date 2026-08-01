# Shoonyakasha Review — GLSL Shaders, Example Applications, Pipeline JSONs

## Scope

Reviewed at commit `dc9fa3d` (master):

- All 142 tracked GLSL files (`*.vert` 40, `*.frag` 55, `*.comp` 47) and all 138 tracked `*.spv`.
- Read in full: `examples/declarative_sponza_test/shaders/*.{vert,frag}` and `shaders/ibl/*.comp`, `examples/bloom_test/shaders/*`, `examples/particle_flow_example/shaders/particle_sim.comp` + `particle.{vert,frag}` + `bloom_extract.frag` + `tonemap.frag`, `examples/particle_test/shaders/particle_sim.comp`, `examples/full_showcase/shaders/sprite.*`, `skinned_gbuffer.vert`, `forward_transparent.frag`.
- Pipeline JSONs: all 16 tracked under `examples/` and `python/examples/`.
- Example C++: `examples/facade_test/main.cpp`, all `examples/*/CMakeLists.txt`, root `CMakeLists.txt:148-165`, plus structural inspection of the app classes.
- Supporting engine code consulted for verification: `src/Vulkan/VulkanSwapChain.cpp`, `src/Vulkan/FrameGraph/FrameGraphExecutor.cpp`.

Verification tooling: `md5sum` over the tracked shader set for duplication; `glslc` 1.4.321.1 (`/c/VulkanSDK/1.4.321.1/Bin/glslc`) recompilation of every GLSL file with byte-comparison against the committed `.spv`; `spirv-dis` to diff the one mismatch.

Read-only review. Nothing in the repository was modified.

---

## Shader inventory & duplication

**142 tracked GLSL files contain only 39 distinct byte sequences.** 103 files (72.5%) are exact duplicates of another tracked file.

```
$ git ls-files '*.frag' '*.vert' '*.comp' | wc -l
142
$ git ls-files '*.frag' '*.vert' '*.comp' | xargs md5sum | awk '{print $1}' | sort -u | wc -l
39
```

Duplicate groups, largest first (md5 → copy count):

| md5 (short) | File | Copies |
|---|---|---|
| `0286820e` | `ibl/prefilter_convolution.comp` | 9 |
| `03c30941` | `ibl/irradiance_convolution.comp` | 9 |
| `09285d78` | `pbr_gbuffer.vert` | 9 |
| `3f9f1f0d` | `pbr_gbuffer.frag` | 9 |
| `d22a383d` | `ibl/brdf_lut.comp` | 9 |
| `f767cb75` | `ibl/equirect_to_cubemap.comp` | 9 |
| `75bb6696` | `fullscreen.vert` | 8 |
| `04cc2e90` | `pbr_ibl_lighting.frag` | 7 |
| `63b2528e` | `forward_transparent.frag` | 7 |
| `a1076e54` | `forward_transparent.vert` | 7 |
| `9c1c8ef5` | `tonemap.frag` | 7 |
| `9bda7a19` | `particle.vert` | 4 |
| `db2c4e79` | `particle.frag` | 4 |
| `211f3132` | `particle_sim.comp` | 4 |
| `1d613af2` | `bloom_blur_h.frag` | 3 |
| `6fe1a639` | `bloom_blur_v.frag` | 3 |
| `6bf7ca3f` | `bloom_extract.frag` | 3 |
| `3d3558dd` | `skinned_gbuffer.vert` | 3 |
| `c367fd12` | `sprite.vert` | 2 |
| `18fde427` | `sprite.frag` | 2 |
| `360d10aa` | `particle.vert` (flow variant) | 2 |
| `d3ada45c` | `particle.frag` (flow variant) | 2 |
| `921f956e` | `tonemap.frag` (flow variant) | 2 |
| `4a35f4a2` | `particle_sim.comp` (combined variant) | 2 |
| `a64c8ff6` | `pbr_ibl_lighting.frag` (flow variant) | 2 |

The duplication spans 9 directories: `examples/{declarative_sponza_test,particle_flow_example,pbr_physics_particles,physics_test,skinned_mesh_test,ssbo_data_flow_example}/shaders/`, `examples/particle_flow_example/pbr_ibl_shaders/`, `python/examples/shaders/`, `python/examples/pbr_ibl_shaders/`.

`examples/particle_flow_example/` carries **two** full shader trees: `shaders/` and `pbr_ibl_shaders/`, the latter being a copy of the declarative_sponza set.

### Near-identical (not byte-identical) copies

These are the dangerous ones — they look like duplicates but have silently diverged:

- **`fullscreen.vert`**: `examples/particle_flow_example/shaders/fullscreen.vert` (`ae6de72c`) differs from the 8-copy group (`75bb6696`) by **indentation only** (lines 21-23). A pure whitespace fork inflating the unique-file count.
- **`tonemap.frag`**: the 7-copy group (`9c1c8ef5`) and the 2-copy flow variant (`921f956e`) differ materially — see CRITICAL-1. The flow variant adds bloom compositing, luminance-only tonemapping, and **removes the manual gamma**; the 7-copy group still has it.
- **`pbr_ibl_lighting.frag`**: `a64c8ff6` (2 copies) is `04cc2e90` (7 copies) plus a 22-line `#define IBL_DEBUG` block inserted at line 118.
- **`particle_sim.comp`**: four distinct implementations exist (`211f3132` ×4, `4a35f4a2` ×2, `845698de` ×1, `d87c5449` ×1) with different buffer strategies (ping-pong vs. in-place) and incompatible `SimParams` layouts.

### Pipeline JSON duplication

16 tracked pipeline JSONs, **13 unique**:

```
a68a3b77  examples/declarative_sponza_test/pbr_ibl_pipeline_v3.json
a68a3b77  examples/ssbo_data_flow_example/pbr_ibl_pipeline_v3.json
a68a3b77  python/examples/pbr_ibl_pipeline_v3.json
41eb755f  examples/pbr_physics_particles/pipeline.json
41eb755f  python/examples/pipeline.json
6b45ec5b  examples/skinned_mesh_test/skinned_pipeline.json
6b45ec5b  python/examples/skinned_pipeline.json
```

`examples/full_showcase/showcase_pipeline.json` and `examples/sprite_ui_test/sprite_pipeline.json` are not byte-identical but share every `bufferLayout`, `descriptorSetLayout`, `sampler`, and `vertexFormat` definition verbatim (lines 5-80 of each are identical); showcase adds two extra passes.

### The BRDF is implemented four times

The same GGX/Smith/Schlick math is written out independently in:
- `examples/declarative_sponza_test/shaders/pbr_ibl_lighting.frag:200-211`
- `examples/declarative_sponza_test/shaders/forward_transparent.frag:227-238`
- `examples/declarative_sponza_test/shaders/ibl/brdf_lut.comp:50-62`
- `examples/declarative_sponza_test/shaders/ibl/prefilter_convolution.comp:49-68`

Multiplied by the 7-9 directory copies, the GGX distribution term exists in roughly 30 places. There is no `#include` mechanism and no shared `.glsl` header anywhere in the repo (`git ls-files '*.glsl'` returns 0 files).

---

## PBR/IBL correctness

The core split-sum implementation is sound and follows the standard Karis/LearnOpenGL formulation. Terms verified correct:

- **Fresnel** — `pbr_ibl_lighting.frag:58-60` (Schlick) and `:63-65` (roughness-aware for IBL). Both standard.
- **GGX/Trowbridge-Reitz D** — `pbr_ibl_lighting.frag:201-204`. `alpha = roughness²`, `alpha2 = alpha²`, `D = alpha2/(PI·denom²)`. Correct.
- **Smith geometry, direct** — `:206-208`. `k = (roughness+1)²/8` is the correct *direct-lighting* remap.
- **Smith geometry, IBL** — `brdf_lut.comp:50-54`. `k = a²/2` with `a = roughness` is the correct *IBL* remap. The two variants are correctly kept distinct.
- **Split-sum combination** — `:147`, `specular = prefilteredColor * (kS·brdf.x + brdf.y)`. Correct.
- **Irradiance normalization** — `irradiance_convolution.comp:91` divides by sample count without the `π` factor. Under cosine-weighted sampling (`:59-69`, pdf = cosθ/π) this stores `E/π`, not `E`. The lighting shader then computes `diffuse = irradiance * albedo` (`:129`) with no `1/π`. **The two cancel exactly** — the result is the correct Lambertian `E·albedo/π`. This is correct but undocumented; the variable name `irradiance` is misleading and a future "fix" to either side alone would introduce a π-factor error.
- **sRGB/linear on the G-buffer** — `gAlbedo` is `R8G8B8A8_SRGB` (`pbr_ibl_pipeline_v3.json:196`), written with linear-ish values and read back through `texture()` which hardware-decodes to linear. Consistent.
- **Descriptor set indices** — verified against every JSON. `pbr_ibl_lighting.frag` sets 0/1/2/3 match `["gbufferReadSet","iblSet","cameraSet","lightsSet"]` (`pbr_ibl_pipeline_v3.json:268`). `forward_transparent.frag` sets 0/1/2/3 match `["cameraSet","materialSet","iblSet","lightsSet"]` (`:293`). `skinned_gbuffer.vert:21` declares `set = 2` and `skinned_pipeline.json:221` lists `skeletonSet` third. `sprite.vert`/`sprite.frag` sets 0/1 match both sprite JSONs. **No set/binding mismatches found.**
- **Push constant sizes** — `pbr_gbuffer.frag:21-30` = 104 bytes, matching `pbr_ibl_pipeline_v3.json:226`. `sprite.vert:17-25` = 112 bytes, matching `showcase_pipeline.json:110`. Both under the 128-byte Vulkan guaranteed minimum.
- **Fullscreen triangle winding/UV** — `fullscreen.vert:20-33`. Positions and texcoords are consistent with Vulkan's Y-down NDC; no flip needed and none applied. Correct.

Problems found are enumerated in **Findings**. The most consequential are the double gamma correction (CRITICAL-1), the dropped TBN handedness sign (MAJOR-3), and the ad-hoc prefilter mip selection (MAJOR-7).

---

## Compute shaders

**Bounds handling is correct throughout.** Every compute shader guards its global invocation ID:
- `particle_sim.comp:48` — `if (index >= params.particleCount) return;`
- `irradiance_convolution.comp:74`, `prefilter_convolution.comp:73`, `brdf_lut.comp:103` — face/LUT size guards
- `bright_extract.comp:25`, `blur_horizontal.comp:29`, `blur_vertical.comp:29` — image size guards

And the dispatch side rounds up correctly — `FrameGraphExecutor.cpp:652` computes `(paramValue + dim.divisor - 1) / dim.divisor`, so a particle count that is not a multiple of 256 launches the extra partial workgroup, which the in-shader guard then trims. This pairing is right.

**Workgroup sizes** are 256×1×1 (particle sim) and 8×8 / 16×16 (IBL, bloom) — all within the Vulkan guaranteed minimums (1024 invocations, 128/128/64 per dimension). JSON `workgroupSize` declarations match the GLSL `local_size_*` in every case checked (`pbr_ibl_pipeline_v3.json:233` vs `particle_sim.comp:9`; `bloom_pipeline.json:142,166,190` vs the three bloom `.comp` files).

**No race conditions.** `particle_flow_example/shaders/particle_sim.comp` does an in-place read-modify-write where invocation *i* touches only `particles[i]` (`:50`, `:135`) — no cross-lane access, no atomics needed, none used. `particle_test/shaders/particle_sim.comp` uses proper ping-pong (`:16-22`, separate readonly-in / writeonly-out SSBOs). Both are safe.

**No barriers are declared, and none are needed inside the shaders** — but see OPEN-1 regarding the compute→vertex hazard at the frame-graph level.

Correctness issues found: the unguarded `normalize()` of a possibly-zero vector (MAJOR-4) and the frame-invariant respawn seed (MAJOR-5), both in the particle simulations. Details in Findings.

---

## Shader build & .spv in git

### How shaders get compiled

Each C++ example's `CMakeLists.txt` defines its **own private copy** of a `target_compile_shaders()` function — 9 near-identical copies of the same ~35 lines (e.g. `examples/declarative_sponza_test/CMakeLists.txt:44-77`, `examples/bloom_test/CMakeLists.txt:31-58`, `examples/physics_test/CMakeLists.txt:41-74`). There is no shared CMake module and **no `examples/CMakeLists.txt`** — the root `CMakeLists.txt:153-163` adds each example directory individually, gated on `BUILD_EXAMPLES` (default `OFF`, `:17`).

Compilation **does** run automatically when examples are built: each `add_custom_command` output is collected into a custom target that the example executable depends on (`declarative_sponza_test/CMakeLists.txt:73-75`).

### Are the committed .spv stale?

Recompiled all 142 GLSL files with `glslc` 1.4.321.1 and byte-compared against the committed binaries:

```
$ for s in $(git ls-files '*.frag' '*.vert' '*.comp'); do ... glslc "$s" -o fresh.spv; cmp -s fresh.spv "$s.spv" ...
=== identical=137 differs=1 compilefail=0 ===
DIFFERS: python/examples/shaders/particle_sim.comp
```

- **All 142 GLSL files compile cleanly** — zero syntax/link errors.
- **137 of 138 committed `.spv` are byte-identical** to a fresh compile. Not stale.
- **One is genuinely stale**: `python/examples/shaders/particle_sim.comp.spv`. See CRITICAL-2.
- **Four GLSL files have no committed `.spv` at all**: `examples/full_showcase/shaders/sprite.{vert,frag}` and `examples/sprite_ui_test/shaders/sprite.{vert,frag}`.

### Should .spv be in git?

The current arrangement is backwards. The 138 committed binaries belong to C++ examples that already regenerate them from CMake — so they are redundant, and CMake overwrites them in-place. The **only four shaders that actually need a committed binary** are the ones that have none: the two Python demos have no CMake target, and their docstrings instruct the user to run `glslc` by hand (`examples/full_showcase/showcase_demo.py:24-25`, `examples/sprite_ui_test/sprite_ui_demo.py:15-16`).

Additional build issues:

- **CMake writes `.spv` into the source tree**, not the build tree. Every example passes `${CMAKE_CURRENT_SOURCE_DIR}/shaders` as both `SHADER_DIR` and `OUTPUT_DIR` (e.g. `declarative_sponza_test/CMakeLists.txt:80-83`). A build therefore dirties the working copy of 138 tracked files. This is also why they stay in sync — the build silently rewrites them.
- **Two examples don't check that `glslc` was found.** `bloom_test/CMakeLists.txt:29` and `particle_test/CMakeLists.txt:29` call `find_program(GLSLC ...)` with no `if(NOT GLSLC) FATAL_ERROR`, unlike the other seven. Worse, `GLSLC` is a CMake cache variable shared across the whole project, so behaviour depends on subdirectory ordering.
- `find_program` hints differ: `bloom_test`/`particle_test` search only `$ENV{VULKAN_SDK}/Bin`, the rest search `Bin` and `bin`.

---

## Examples status

| Example | Present | In CMake | Type | Shaders build | API current |
|---|---|---|---|---|---|
| `facade_test` | yes | yes (`CMakeLists.txt:162`) | C++ | n/a (no shaders) | yes — all 9 Facade symbols used resolve in `include/Facade/` |
| `declarative_sponza_test` | yes | yes (`:156`) | C++ | yes | yes (`ApplicationBase`) |
| `particle_test` | yes | yes (`:154`) | C++ | yes, unguarded glslc | yes |
| `bloom_test` | yes | yes (`:155`) | C++ | yes, unguarded glslc | yes |
| `particle_flow_example` | yes | yes (`:158`) | C++ | yes | yes |
| `ssbo_data_flow_example` | yes | yes (`:157`) | C++ | yes | yes — but ships 696 lines of dead pre-refactor code (MAJOR-8) |
| `skinned_mesh_test` | yes | yes (`:159`) | C++ | yes | yes |
| `physics_test` | yes | yes (`:160`) | C++ | yes | yes |
| `pbr_physics_particles` | yes | yes (`:161`) | C++ | yes | yes |
| `full_showcase` | yes | **no** | Python demo | **no** — manual glslc, no committed .spv | n/a |
| `sprite_ui_test` | yes | **no** | Python demo | **no** — manual glslc, no committed .spv | n/a |

**Verdict: 9 C++ examples exist and all 9 are wired into CMake.** `full_showcase` and `sprite_ui_test` are Python demos (`showcase_demo.py`, `sprite_ui_demo.py`), correctly absent from CMake — their gap is the missing `.spv`, not the missing target.

`README.md:168` states "examples/ &nbsp; 8 C++ example applications" while the table at `README.md:186-196` correctly lists 9. The prose count is off by one.

No API drift detected in the live examples. Every header the examples include still exists, and all Facade methods used by `facade_test/main.cpp` (`createCamera`, `createDirectionalLight`, `loadGltfScene`, `getAllEntities`, `setOnPostInit`, `setOnKeyPressed`, `getCameraEntity`, `hdrEnvironmentPath`, `pipelineJsonPath`) resolve in `include/Facade/`. `facade_test` also honours its CMakeLists claim (`examples/facade_test/CMakeLists.txt:4-7`) of using only Facade headers — `main.cpp:10-13` includes exactly four `Facade/*.h` and nothing internal.

### pkg_a_curtains/

**Not repo weight.** 1.5 GB on disk (209 MB of it textures), but `git ls-files pkg_a_curtains` returns **0 files** — it is excluded by `.gitignore:5` (`/pkg_a_curtains`). It is the Intel "New Sponza — Curtains" asset pack (glTF + FBX + USD + 3ds Max sources + 6 marketing renders), sitting untracked in the project root. Nothing in the codebase references it (`grep -rn "pkg_a_curtains\|Curtains"` over all `.cpp/.h/.json/.md/.py/.txt` outside the directory itself returns nothing). It is local scratch, not dead weight in the repository — but it is also unused by any example, including the Sponza one, which loads from `examples/declarative_sponza_test/Sponza/glTF/` (`facade_test/main.cpp:85`).

---

## Findings

### CRITICAL-1 — Double gamma correction in 8 shader copies

`examples/declarative_sponza_test/shaders/tonemap.frag:71-72`

```glsl
float gamma = 2.2;
mapped = pow(mapped, vec3(1.0 / gamma));
```

The swapchain is created as `VK_FORMAT_B8G8R8A8_SRGB` — `src/Vulkan/VulkanSwapChain.cpp:300` explicitly prefers it, and only falls back to `availableFormats[0]` if unavailable (`:305`). Writing to an sRGB attachment makes the hardware apply the linear→sRGB encode. Applying `pow(1/2.2)` in the shader **as well** encodes the image twice, badly washing out midtones.

The shader's own comment at `:69-70` flags the hazard and then does the wrong thing anyway.

This is confirmed rather than inferred: the author already fixed exactly this bug in one variant. `examples/particle_flow_example/shaders/tonemap.frag:71-72` reads:

```glsl
// No manual gamma — the swapchain is VK_FORMAT_B8G8R8A8_SRGB,
// so the hardware applies linear → sRGB conversion automatically.
```

The fix was never propagated to the other copies. Affected files (all 7 copies of md5 `9c1c8ef5`):
- `examples/declarative_sponza_test/shaders/tonemap.frag:71-72`
- `examples/particle_flow_example/pbr_ibl_shaders/tonemap.frag:71-72`
- `examples/physics_test/shaders/tonemap.frag:71-72`
- `examples/skinned_mesh_test/shaders/tonemap.frag:71-72`
- `examples/ssbo_data_flow_example/shaders/tonemap.frag:71-72`
- `python/examples/pbr_ibl_shaders/tonemap.frag:71-72`
- `python/examples/shaders/tonemap.frag:71-72`

Plus the same bug independently in `examples/bloom_test/shaders/bloom_composite.frag:26` (`result = pow(result, vec3(1.0/2.2))`), which also writes to the swapchain (`bloom_pipeline.json:209-223`).

This is the single highest-impact defect found, and it is a direct consequence of the duplication described above.

---

### CRITICAL-2 — `python/examples/shaders/particle_sim.comp.spv` is stale with an incompatible UBO layout

The only `.spv` that does not match its source. `spirv-dis` diff of a fresh compile (`<`) against the committed binary (`>`):

```
<  OpMemberName %SimParams 4 "attractorPos0"
<  OpMemberName %SimParams 5 "attractorPos1"
<  OpMemberName %SimParams 6 "attractorPos2"
<  OpMemberName %SimParams 7 "attractorPos3"
<  OpMemberName %SimParams 8 "wind"
<  OpMemberName %SimParams 9 "damping"
<  OpMemberName %SimParams 10 "spawnHeight"
<  OpMemberName %SimParams 11 "groundY"
<  OpMemberName %SimParams 12 "padding1"
---
>  OpMemberName %SimParams 4 "attractorPos"
>  OpMemberName %SimParams 5 "wind"
>  OpMemberName %SimParams 6 "damping"
>  OpMemberName %SimParams 7 "spawnHeight"
>  OpMemberName %SimParams 8 "padding1"
>  OpMemberName %SimParams 9 "padding2"
```

The current GLSL declares four attractors plus a `groundY`; the committed binary was built from a single-attractor version. Sizes differ too (12212 vs 10256 bytes; SPIR-V ID bound 495 vs 418).

Because `python/examples/` has **no CMake shader-compile step**, the stale binary is what actually executes. The host fills a UBO matching the current GLSL layout while the GPU reads it under the old layout — every field from offset 4 onward is misinterpreted. The `groundY`/`attractorPos1..3` data is read as `wind`/`damping`/`spawnHeight`.

---

### MAJOR-3 — TBN construction drops the handedness sign, inverting normal maps on mirrored UVs

`examples/declarative_sponza_test/shaders/pbr_gbuffer.frag:71`

```glsl
float invDet = 1.0 / det;                          // :65
vec3 T = (Q1 * st2.t - Q2 * st1.t) * invDet;       // :66
vec3 B = (Q2 * st1.s - Q1 * st2.s) * invDet;       // :67
T = normalize(T - N * dot(N, T));                  // :70
B = cross(N, T);                                   // :71  <-- discards sign(det)
```

The correctly-signed bitangent is computed at `:67`, then thrown away at `:71` and replaced with an unsigned `cross(N,T)`. On geometry with mirrored UV islands `det` is negative, and the reconstructed `B` points the wrong way — the normal map's green channel is inverted, so surface detail lights from the opposite direction.

Sponza uses mirrored UVs extensively on its symmetric architecture, so this is visible in the flagship example. Fix is `B = sign(det) * cross(N, T);` — `det` is already in scope.

Identical bug in `examples/declarative_sponza_test/shaders/forward_transparent.frag:101`. Across all copies: 9 instances in `pbr_gbuffer.frag`, 7 in `forward_transparent.frag`.

---

### MAJOR-4 — `normalize()` of a possibly-zero vector produces NaN

`examples/particle_flow_example/shaders/particle_sim.comp:56-59`

```glsl
vec3 toAttractor = params.attractorPos.xyz - p.position.xyz;
float dist = max(length(toAttractor), 0.5);
float attractForce = params.attractorPos.w / (dist * dist);
p.velocity.xyz += normalize(toAttractor) * attractForce * params.deltaTime;
```

`dist` is clamped to avoid division blow-up, but `normalize(toAttractor)` at `:59` is unguarded. A particle landing exactly on the attractor yields `normalize(vec3(0))` = `0/0` = NaN, which propagates into velocity, then position, and persists forever — the particle never recovers, and `:113`'s life check `p.position.w <= 0.0` is false for NaN so it never respawns.

Same defect at `examples/particle_test/shaders/particle_sim.comp:59`.

Fix: `toAttractor / dist` (reusing the already-clamped `dist`) instead of `normalize(toAttractor)`.

---

### MAJOR-5 — Particle respawn seed is frame-invariant; particles respawn at identical positions forever

`examples/particle_flow_example/shaders/particle_sim.comp:114`

```glsl
seed = index * 1023u + uint(gl_GlobalInvocationID.x * 31u);
```

`gl_GlobalInvocationID.x` **is** `index` (assigned at `:47`), so this reduces to `index * 1054u` — a pure function of the particle index with no time or frame input. Every particle therefore respawns at the exact same position with the exact same velocity and lifetime on every cycle (`:117-131`), turning what should be a scattered emitter into a set of fixed, repeating trajectories.

This is a regression rather than an oversight: `examples/particle_test/shaders/particle_sim.comp:79` does it correctly —

```glsl
uint seed = index * 1023u + pc.frameNumber * 7919u;
```

— using a `frameNumber` push constant declared at `:32-34`. The flow variant has no such push constant.

---

### MAJOR-6 — Debug branches shipped in the production lighting shader

`examples/declarative_sponza_test/shaders/pbr_ibl_lighting.frag` contains four NaN/inf guard blocks that early-out with solid diagnostic colours:

- `:87-91` — cyan on bad normal
- `:96-100` — yellow on bad view direction
- `:118-122` — green on bad irradiance sample
- `:217-221` — red on NaN in final colour

These run for every pixel of every frame. Beyond the wasted `isnan`/`isinf` work and branch pressure, if any of them ever trips in a shipped build the user sees flat cyan/yellow/green/red geometry rather than a graceful degradation.

Related dead code in the same shader family: `examples/particle_flow_example/shaders/pbr_ibl_lighting.frag:118-139` carries a 22-line `#define IBL_DEBUG 0` block, and `examples/declarative_sponza_test/shaders/pbr_gbuffer.frag:149-167` carries a `#define DEBUG_MODE 0` block (with the `#define` placed inside `main()` at `:149` — legal, since the GLSL preprocessor is file-scoped, but misleading).

---

### MAJOR-7 — Prefilter mip selection is an ad-hoc approximation; MAX_REFLECTION_LOD hardcoded

`examples/declarative_sponza_test/shaders/ibl/prefilter_convolution.comp:96`

```glsl
float mipLevel = pc.roughness * 4.0;  // Approximate mip selection
```

The standard firefly-suppression technique selects the source mip from the sample's solid angle versus the texel solid angle:

```
pdf      = D·NdotH / (4·HdotV)
saTexel  = 4π / (6·resolution²)
saSample = 1 / (sampleCount·pdf)
mipLevel = 0.5·log2(saSample / saTexel)
```

Using `roughness * 4.0` instead couples the source mip to roughness rather than to sample density, which both under-filters at low sample counts (fireflies survive) and double-blurs at high roughness (the pre-blurred mip is then GGX-convolved again, over-widening the lobe). It also silently assumes the environment cubemap has at least 5 mips.

The consumer side has the matching hardcode — `pbr_ibl_lighting.frag:137`:

```glsl
const float MAX_REFLECTION_LOD = 4.0;
```

Nothing ties this constant to the actual mip count of the prefilter chain allocated by `IBLGenerator`. If that count ever changes, roughness→mip mapping breaks with no compile-time or runtime signal. Same hardcode at `forward_transparent.frag:176`.

---

### MAJOR-8 — 696 lines of dead, pre-refactor example code

`examples/ssbo_data_flow_example/DeclarativeSponzaApp.h` (133 lines) and `.cpp` (563 lines) are a stale copy of the declarative_sponza example that is **not listed in that example's CMakeLists** (`examples/ssbo_data_flow_example/CMakeLists.txt:9-13` builds only `test_app.cpp`, `SSBODataFlowApp.h`, `SSBODataFlowApp.cpp`).

It predates the `ApplicationBase` refactor. Diff against the live version:

```
examples/declarative_sponza_test/DeclarativeSponzaApp.h:16   #include "App/ApplicationBase.h"
examples/declarative_sponza_test/DeclarativeSponzaApp.h:18   class DeclarativeSponzaApp : public ApplicationBase {
```
versus the dead copy, which hand-rolls 17 individual Vulkan/ECS includes and declares a standalone `class DeclarativeSponzaApp {`. The live `.cpp` is 115 lines; the dead copy is 563 — the refactor removed ~450 lines of boilerplate that this copy still carries.

Since it is not compiled, nothing catches it drifting further. It will read to a newcomer as a second, contradictory example of how to write an app.

---

### MINOR-9 — Dead variable in the G-buffer shader

`examples/declarative_sponza_test/shaders/pbr_gbuffer.frag:96-97`

```glsl
float normalLength = length(fragWorldNormal);
bool badVertexNormal = (normalLength < 0.001) || isnan(normalLength) || isinf(normalLength);
```

`badVertexNormal` is never read. `usedTexture` (`:127`, `:134`) is read only inside the `#if DEBUG_MODE == 1` block, so it is also dead in the default configuration. Both will draw unused-variable diagnostics.

---

### MINOR-10 — Tonemap operator mislabelled

`examples/bloom_test/shaders/bloom_composite.frag:22-23`

```glsl
// Simple ACES-inspired tone mapping
result = result / (result + vec3(1.0));
```

That is Reinhard, not ACES. The actual ACES approximation lives in `tonemap.frag:24-31`. Misleading for anyone matching output between the two paths.

---

### MINOR-11 — Per-vertex `transpose(inverse(mat3(model)))`

`examples/declarative_sponza_test/shaders/pbr_gbuffer.vert:42` and `skinned_gbuffer.vert:67` compute a 3×3 matrix inverse **per vertex**. The model matrix is uniform across the draw; this belongs in the push constant or a per-object UBO. On Sponza's ~260k vertices this is pure waste.

---

### MINOR-12 — Skinning: no weight normalization, no joint index bounds check

`examples/declarative_sponza_test/shaders/skinned_gbuffer.vert:52-56`

```glsl
mat4 skinMatrix =
    inWeights.x * bones[inJoints.x] + ... + inWeights.w * bones[inJoints.w];
```

Weights are assumed to sum to 1.0 — glTF requires it but does not guarantee it survives quantization to `u8`/`u16` normalized. Joint indices are used unchecked to index an unsized SSBO array (`:21-23`); an out-of-range index is an out-of-bounds read whose behaviour depends on `robustBufferAccess`. A `min(inJoints, boneCount-1)` clamp would be cheap.

---

### MINOR-13 — Hardcoded magic numbers where uniforms belong

None of the following is driven by the JSON pipeline or any UBO:

- `tonemap.frag:62` — `float exposure = 0.4;  // Reduced from 1.0 for better visibility`
- `particle_flow_example/shaders/tonemap.frag:46` — `float bloomIntensity = 0.5;`, `:51` — `float exposure = 1.8;`, `:66` — `float sat = 1.3;`
- `particle_flow_example/shaders/bloom_extract.frag:24-25` — `threshold = 0.5`, `softKnee = 0.6`
- `particle_flow_example/shaders/particle_sim.comp:84-86` — `boundsMin/boundsMax/restitution`, which **contradict** the `boundaryRadius` uniform declared at `:27` and never used
- `particle.vert:43` — point size curve constants `30.0`, `2.0`, `24.0`

The `bloom_test` example does this correctly — `bright_extract.comp:14-19` takes `threshold`/`softKnee`/`intensity` from a UBO. The pattern exists; it just was not applied to the copies.

---

### MINOR-14 — `bloom_test/forward.frag` declares a camera UBO that does not match the engine layout

`examples/bloom_test/shaders/forward.frag:12-16`

```glsl
layout(binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 proj;
    mat4 model;
} camera;
```

Every other shader in the repo declares the engine's camera block as `view/proj/invView/invProj/position/params` (e.g. `pbr_gbuffer.vert:10-17`). Here the third member is `model`, which would alias `invView` in the buffer the engine fills. Harmless in practice — this shader is fully procedural (`:23-59`) and never reads `camera` — but it is a trap for anyone copying it as a template.

---

### MINOR-15 — Blur shaders mix input texel size with output dimensions

`examples/bloom_test/shaders/blur_horizontal.comp:31-32`

```glsl
vec2 texelSize = 1.0 / vec2(textureSize(blurInput, 0));
vec2 uv = (vec2(coord) + 0.5) / vec2(size);   // 'size' is imageSize(blurOutput)
```

Tap offsets are scaled by the **input** texel size while UVs are normalized against the **output** extent. Correct only while input and output are the same resolution, which holds today for the ping-pong pair but is an unstated invariant. Same at `blur_vertical.comp:31-32`.

(For contrast, `bright_extract.comp:28-36` mixes the two deliberately and correctly: with output = input/2, `uv` lands exactly on a 2×2 input corner and the `±0.5 · texelSize` taps hit the four input texel centres. That one is right.)

---

### MINOR-16 — Sky detection misclassifies geometry at the world origin

`examples/declarative_sponza_test/shaders/pbr_ibl_lighting.frag:79`

```glsl
if (length(worldPos) < 0.001 && albedoAlpha.a < 0.01) {
```

Uses "position near origin AND alpha near zero" as a proxy for "no geometry here". Geometry legitimately sitting at the world origin with a transparent albedo would be shaded as sky. The G-buffer clears make this work today (`pbr_ibl_pipeline_v3.json:212,214` clear both position and albedo to zero), but a stencil bit or a dedicated coverage channel would be robust. Low practical risk.

---

### MINOR-17 — G-buffer stores world position in fp16 and round-trips normals through [0,1]

`pbr_ibl_pipeline_v3.json:194-195` declares `gPosition` and `gNormal` as `R16G16B16A16_SFLOAT`.

- **Position**: fp16 has an 11-bit mantissa, so at Sponza's ~20-unit scale the ULP is ≈0.016 units. That quantization feeds directly into `V = normalize(camera.position - worldPos)` (`:94`) and the light distance/attenuation math (`:180-184`), which can show as banding in tight specular highlights. Reconstructing position from depth would be both cheaper and more accurate.
- **Normal**: written as `N * 0.5 + 0.5` (`pbr_gbuffer.frag:142`) and unpacked with `* 2.0 - 1.0` (`pbr_ibl_lighting.frag:70`). The target is *float*, not UNORM — it can store [-1,1] directly, so the encode/decode pair costs precision for nothing. An octahedral encoding into RG16 would halve the bandwidth as well.

Neither is a correctness bug; both are avoidable precision/bandwidth cost.

---

### MINOR-18 — README C++ example count is off by one

`README.md:168` reads `examples/          8 C++ example applications`. There are 9, and the table at `README.md:186-196` lists all 9 correctly.

---

### MINOR-19 — Nine copy-pasted `target_compile_shaders()` CMake functions; two skip the glslc check

Each example redefines the same ~35-line function (`examples/declarative_sponza_test/CMakeLists.txt:44-77` and eight siblings). `examples/bloom_test/CMakeLists.txt:29` and `examples/particle_test/CMakeLists.txt:29` additionally omit the `if(NOT GLSLC) message(FATAL_ERROR ...)` guard that the other seven have — and because `GLSLC` is a shared CMake cache variable, whether they work depends on whether a sibling example already populated it. A single `cmake/CompileShaders.cmake` module would remove all nine copies.

---

## Open questions

**OPEN-1 — Is there a barrier between `ParticleSimulate` (compute write) and `ParticleRender` (vertex read)?**
`pbr_ibl_pipeline_v3.json` binds `particleSSBO` to the compute pass via `particleComputeSet` (`:181`) and to the vertex stage via `particleRenderSet` (`:187`), but the buffer appears in neither pass's `inputs` nor `outputs` arrays, and it is not in the top-level `resources` list (`:192-200` contains only images). If the frame graph derives its barriers from declared `inputs`/`outputs`, the write→read hazard is invisible to it and the `VK_ACCESS_SHADER_WRITE_BIT → VK_ACCESS_SHADER_READ_BIT` barrier would be missing. The pass does carry `"hasSideEffects": true` (`:245`), which may be what forces ordering. Resolving this requires reading the barrier-insertion logic in `FrameGraphCompiler`/`RenderGraph` — outside this review's scope, but worth confirming, as a missing barrier here is a genuine race that would manifest as one-frame-stale or torn particle data.

**OPEN-2 — What mip count does `IBLGenerator` actually allocate for the prefilter chain?**
`MAX_REFLECTION_LOD = 4.0` (`pbr_ibl_lighting.frag:137`, `forward_transparent.frag:176`) and `mipLevel = pc.roughness * 4.0` (`prefilter_convolution.comp:96`) both assume 5 mips. I did not read `src/IBL/IBLGenerator.cpp` to confirm. If it allocates a different count, roughness→mip is wrong at the high-roughness end.

**OPEN-3 — Are the IBL cubemap samplers configured with `clamp_to_edge` and `mipmapMode: linear`?**
`irradianceMap`, `prefilterMap`, and `brdfLUT` are declared in `iblSet` (`pbr_ibl_pipeline_v3.json:162-168`) **without** `autoBindSampler`, unlike the G-buffer bindings which explicitly request `nearestClamp` (`:156-159`) and the tonemap input which requests `linearClamp` (`:176`). Whatever sampler the engine defaults to for these three matters: a non-`clamp_to_edge` BRDF LUT sampler wraps at grazing angles, and a `mipmapMode` other than `linear` makes `textureLod` on the prefilter map snap between mips, producing visible roughness banding.

**OPEN-4 — Do the two Python demos run at all as committed?**
`full_showcase` and `sprite_ui_test` reference `shaders/sprite.vert.spv` and `shaders/sprite.frag.spv` (`showcase_pipeline.json:101-102`, `sprite_pipeline.json:100-101`), neither of which exists in the repo or is generated by any build step. Unless the Python binding layer compiles GLSL at load time, these demos fail on first run until the user follows the manual `glslc` instructions in their docstrings. I did not check whether the binding layer has a runtime compile path.

**OPEN-5 — Is `boundaryRadius` meant to be live?**
`particle_flow_example/shaders/particle_sim.comp:27` declares `boundaryRadius` in `SimParams` and the JSON sources it from `scene.custom.particles.boundaryRadius` (`pbr_ibl_pipeline_v3.json:99`), but the shader ignores it in favour of hardcoded box bounds (`:84-85`). Unclear whether the box was a deliberate change of shape (making the uniform vestigial) or a debugging hardcode that was never reverted.
