# Shoonyakasha — Build System, Tests, CI, Hygiene & Docs Review

## Scope

Reviewed at `H:\engine-dev\Shoonyakasha` (branch `master`, HEAD `dc9fa3d`), read-only. No builds run, no tests executed, nothing edited.

Files read in full: `CMakeLists.txt`, `cmake/ShoonyakashaConfig.cmake.in`, `vcpkg.json`, `pyproject.toml`, `BUILDING.md`, `.gitignore`, `tests/CMakeLists.txt`, `tests/TestHelpers.h`, `python/CMakeLists.txt`, `examples/facade_test/CMakeLists.txt`, `README.md`, `docs/index.md`, `docs/getting-started/prerequisites.md`. All 22 `tests/**/*.cpp` were parsed programmatically (macro extraction + brace-matched body analysis); representative bodies read directly. All 9 `examples/*/CMakeLists.txt` scanned for shader/warning/glslc handling.

Excluded per instructions: `.venv/`, `build/`, `cmake-build-debug/`, `cmake-build-release/`, `glTF-Sample-Assets/`.

---

## Build system

The root `CMakeLists.txt` is, on the whole, competently written and recently modernized (commit `6965138` "Modernize CMake, packaging and vcpkg baseline"). The inline comments explain *why* each non-obvious decision exists, which is unusual and genuinely useful. Specific assessment:

**What is done correctly**

- `cmake_minimum_required(VERSION 3.21)` with a stated rationale (CMakeLists.txt:3-6). No legacy-policy compatibility mode.
- `C` is listed in `project(... LANGUAGES C CXX)` (CMakeLists.txt:11) because the glob picks up `*.c` — previously it was enabled as a side effect of a dependency's `project()` call. Correct fix.
- C++ standard enforcement is complete and strict: `CMAKE_CXX_STANDARD 20` + `CXX_STANDARD_REQUIRED ON` + `CXX_EXTENSIONS OFF` (CMakeLists.txt:13-15).
- All six dependencies are consumed through imported targets, never raw path variables (CMakeLists.txt:78-86). This is what keeps `build/vcpkg_installed/` absolute paths out of the export set.
- `PUBLIC`/`PRIVATE` split on include directories is correct (CMakeLists.txt:61-71): VMA is `PUBLIC` because it appears in public headers; stb/tinyobjloader/cgltf are `PRIVATE` because they are implementation-only.
- `$<INSTALL_INTERFACE:include>` / `$<BUILD_INTERFACE:...>` generator expressions are used properly.
- `install(DIRECTORY ...)` rather than `install(FILES ${ENGINE_HEADERS})` — the comment at CMakeLists.txt:106-108 documents the flattening bug this fixed.
- VMA headers are installed alongside (CMakeLists.txt:116-119), which is required given it is on the `PUBLIC` interface.
- `ShoonyakashaConfig.cmake.in` correctly calls `find_dependency()` for all six `PUBLIC`-linked packages before including the targets file. This is the part most projects get wrong; it is right here.
- `POSITION_INDEPENDENT_CODE ON` (CMakeLists.txt:59) — needed to link the static lib into the Python `.so`.
- `SHOONYAKASHA_INSTALL` option exists specifically so the wheel build does not scatter `include/`/`lib/` into `site-packages` (CMakeLists.txt:21-24, pyproject.toml:85). Well-reasoned.
- `SHOONYAKASHA_TESTING` is now gated on `BUILD_TESTS` (CMakeLists.txt:91-93) rather than unconditional.
- `GLOB_RECURSE ... CONFIGURE_DEPENDS` (CMakeLists.txt:44) — globbing is still not ideal, but `CONFIGURE_DEPENDS` removes the staleness failure mode.
- vcpkg `builtin-baseline` **is** pinned: `1f5e0348089e8a9b187f57d42866ebc871e815da` (vcpkg.json:32). Test dependencies are correctly isolated into a `tests` feature (vcpkg.json:24-31) so a normal build does not pull GoogleTest.
- No hardcoded absolute paths in any tracked build file. The only `H:/` and `C:/Program Files/` hits are inside `build/`, which is untracked and gitignored (verified by grep across `CMakeLists.txt`/`*.cmake`/`*.cmake.in`/`*.toml`).

**What is missing or wrong**

- **No warning flags on the engine library at all.** `/W4`, `-Wall -Wextra -pedantic` appear on every example target, on `ShoonyakashaTests` (tests/CMakeLists.txt:27-29), and nowhere on the `Shoonyakasha` target itself. The 14,719-LOC Vulkan subsystem — the largest and most error-prone part of the codebase — compiles at MSVC default `/W1`. This is the single biggest build-system gap.
- **No `-Werror`/`/WX`, no sanitizer options, no clang-tidy/cppcheck integration.** Verified absent by grep across all tracked CMake files. There is no `SHOONYAKASHA_ENABLE_ASAN`-style option, so there is no supported way to run the test suite under ASan/UBSan.
- `vcpkg.json:8-15` carries a `$comment-baseline` block instructing the reader to add a `builtin-baseline` — but line 32 already has one. The comment is stale and now reads as a to-do that is already done. Cosmetic, but confusing.
- `tests/CMakeLists.txt:5` uses `file(GLOB_RECURSE ...)` **without** `CONFIGURE_DEPENDS`, unlike the root. Adding a new test file will not be picked up until someone manually re-runs configure — a silent "I wrote a test and it never ran" failure. Inconsistent with CMakeLists.txt:44.
- `tests/CMakeLists.txt:2` and every `examples/*/CMakeLists.txt` still declare `cmake_minimum_required(VERSION 3.12)` while the root requires 3.21. Harmless in practice (the root's value wins for policies) but inconsistent.
- **Shader compilation is per-example copy-paste**, duplicated nine times with drift. `examples/bloom_test/CMakeLists.txt:28` and `examples/particle_test/CMakeLists.txt:28` hint only `$ENV{VULKAN_SDK}/Bin` (capitalized), while the other six hint both `/Bin` and `/bin`. The Vulkan SDK on Linux uses lowercase `bin`.
- **Three examples have no `glslc` guard**: `bloom_test`, `particle_test`, `facade_test` lack the `if(NOT GLSLC) message(FATAL_ERROR ...)` check the other six have. For `bloom_test` and `particle_test` (which do compile shaders), a missing `glslc` means `${GLSLC}` expands to `GLSLC-NOTFOUND` and the `add_custom_command` fails at *build* time with an opaque "command not found" rather than at configure time with a clear message. `facade_test` has no shaders, so it is fine.
- **Examples compile shaders in-source.** `add_custom_command` writes `.spv` next to the `.vert`/`.frag`/`.comp` sources under `examples/<dir>/shaders/` (documented at BUILDING.md:153-157). This dirties the working tree on every build and is the reason 138 `.spv` files are committed (see Hygiene).
- `python/CMakeLists.txt:12` uses `find_package(Python3 REQUIRED COMPONENTS Interpreter Development)`. For building an extension module, the correct component is **`Development.Module`**. Requiring the full `Development` component forces discovery of the linkable `libpython`, which is absent in manylinux containers and is the wrong linkage model on macOS (extension modules there link with `-undefined dynamic_lookup`). This will bite any attempt to build wheels on Linux/macOS CI.
- `python/CMakeLists.txt:75-89` sets `LIBRARY_OUTPUT_DIRECTORY`/`RUNTIME_OUTPUT_DIRECTORY` to **the source tree** (`${CMAKE_CURRENT_SOURCE_DIR}/shoonyakasha`). Builds write binaries into the repo. This is directly why a 2.3 MB `_shoonyakasha.pyd` is committed.
- `python/CMakeLists.txt:18` locates Cython via `find_program`, i.e. from `PATH`, rather than via the `cython` that `pyproject.toml:8` puts in the isolated build environment. Works in practice because scikit-build-core puts the build env's `Scripts`/`bin` on `PATH`, but it is a fragile coupling — a system-wide Cython 0.29 on `PATH` would be picked up over the isolated `cython>=3.0`.

---

## Cross-platform verdict

**Plausibly portable, entirely unverified. In practice this is a Windows/MSVC project.**

The source and CMake do not *prevent* a Linux build. Evidence for portability:

- No Windows-only construct in the root `CMakeLists.txt`. Every dependency goes through `find_package`.
- Only two `_WIN32` blocks exist in `src/` and `include/`, both properly guarded with cross-platform fallbacks:
  - `src/Resources/ResourceManager.cpp:19` — `_setmaxstdio(8192)` to raise the CRT file-handle limit, inside `#ifdef _WIN32`. No POSIX equivalent needed.
  - `src/Vulkan/FrameGraph/FrameGraphJson.cpp:18-22` and `:1211` — `GetLastError()`/`FormatMessageA` for richer file-open diagnostics, with `errno`/`strerror` as the cross-platform path (the comment at `:1208` explicitly says errno is the reliable cross-platform indicator).
- `POSITION_INDEPENDENT_CODE ON` and the `-fPIC` guidance in BUILDING.md:265-267 show Linux was at least thought about.
- BUILDING.md has a genuinely detailed Linux section (lines 62-79 list the exact X11/Wayland dev packages GLFW needs under vcpkg).

Evidence that it has probably never been built anywhere but Windows:

- **No CI of any kind** (see below) — so no Linux or macOS build has ever been verified automatically.
- `python/CMakeLists.txt:12` requests `COMPONENTS Development` rather than `Development.Module` — this specifically breaks manylinux and is wrong for macOS. A working Linux wheel build would have surfaced this.
- `examples/bloom_test/CMakeLists.txt:28` and `examples/particle_test/CMakeLists.txt:28` hint only `$ENV{VULKAN_SDK}/Bin`, not the lowercase `bin` the Linux SDK uses. Someone fixed six of the eight and missed two — consistent with a fix applied by pattern rather than validated by a Linux build.
- The committed artifacts are Windows-only: `python/shoonyakasha/_shoonyakasha.pyd` (win_amd64) and `python/shoonyakasha/__pycache__/__init__.cpython-313.pyc`.
- BUILDING.md:54 hardcodes the author's own path (`C:\Users\maxim\.vcpkg-clion\vcpkg`) as the worked example; the Linux example at :59 reuses the same CLion-specific directory name (`$HOME/.vcpkg-clion/vcpkg`), which no Linux user would have.
- `docs/getting-started/prerequisites.md:40-42` states outright: "Use **CLion with the MSVC toolchain** or **Visual Studio**" and "Do NOT build from a bare CLI terminal" — Windows-only guidance, and it contradicts BUILDING.md.

**macOS is not supported.** `pyproject.toml:109` sets `expand-macos-universal-tags = true` and `classifiers` claims `Operating System :: OS Independent` (pyproject.toml:27), but BUILDING.md's prerequisites table (lines 13-20) has **only** Windows and Linux columns, and macOS has no native Vulkan (it needs MoltenVK, which is mentioned nowhere in the repo).

---

## Test suite

**Framework:** GoogleTest, via vcpkg's `tests` feature, discovered with `gtest_discover_tests()` (tests/CMakeLists.txt:31-32).

### Exact count

| Measure | Count |
|---|---|
| **Source-level test macros** | **522** |
| — `TEST(...)` | 394 |
| — `TEST_F(...)` | 122 |
| — `TEST_P(...)` | 6 |
| Test source files | 22 |
| **Runtime / `ctest` test cases** | **618** |

**How I counted.** Two independent methods agreeing on 522:

1. `grep -cE '^\s*(TEST|TEST_F|TEST_P)\s*\(' ` per file, summed over `git ls-files 'tests/**/*.cpp'`.
2. A Python pass over the same 22 files applying `^[ \t]*(TEST|TEST_F|TEST_P)\s*\(` per macro name separately, then brace-matching each body. Result: `{'TEST': 394, 'TEST_F': 122, 'TEST_P': 6}` = 522.

A scan for `TYPED_TEST`/other `TEST_*` macros returned only `TEST_F` and `TEST_P`, so nothing was missed.

**The 618 figure** accounts for parameterized expansion, which is what `ctest` actually reports (`gtest_discover_tests` registers one ctest entry per instantiated case). 522 macros − 6 `TEST_P` declarations = 516 concrete non-parameterized tests, plus:

| Suite (all in `tests/unit/FrameGraphJsonTest.cpp`) | `TEST_P` bodies | Values | Cases |
|---|---|---|---|
| `FormatConversion` (`VkFormats`) | 2 | 33 | 66 |
| `ResourceUsageConversion` (`ResourceUsages`) | 1 | 14 | 14 |
| `DescriptorTypeConversion` (`DescriptorTypes`) | 1 | 9 | 9 |
| `AddressModeConversion` (`AddressModes`) | 1 | 5 | 5 |
| `CompareOpConversion` (`CompareOps`) | 1 | 8 | 8 |
| **Total parameterized** | | | **102** |

516 + 102 = **618**. *Uncertainty:* this is derived statically; I did not build, so I could not confirm against real `ctest` output. The 522 source-macro figure is exact.

**Neither number is 582.** The docs' split ("518 core + 64 facade") is also wrong: the actual split is **436 core** (`tests/unit` + `tests/ecs` + `tests/framegraph`) + **86 facade** (`tests/facade`) = 522.

### Per-file distribution

| File | Tests |
|---|---|
| `tests/facade/SceneAPITest.cpp` | 57 |
| `tests/unit/DotPathResolverTest.cpp` | 55 |
| `tests/unit/FrameGraphJsonTest.cpp` | 46 |
| `tests/unit/BufferLayoutCompilerTest.cpp` | 45 |
| `tests/framegraph/DotPathResolverECSTest.cpp` | 37 |
| `tests/unit/ResourceCacheTest.cpp` | 30 |
| `tests/unit/GPUTypesTest.cpp` | 30 |
| `tests/ecs/RenderComponentsTest.cpp` | 28 |
| `tests/ecs/SystemsTest.cpp` | 27 |
| `tests/unit/VertexFormatRegistryTest.cpp` | 22 |
| `tests/facade/EcsAPITest.cpp` | 18 |
| `tests/ecs/AnimationPlaybackTest.cpp` | 16 |
| `tests/ecs/TransformTest.cpp` | 14 |
| `tests/ecs/EntityBuilderTest.cpp` | 14 |
| `tests/unit/SharedBufferRegistryTest.cpp` | 13 |
| `tests/ecs/AnimationDataTest.cpp` | 12 |
| `tests/facade/FacadeTypesTest.cpp` | 11 |
| `tests/ecs/AnimationEvaluatorTest.cpp` | 11 |
| `tests/unit/EventSystemTest.cpp` | 10 |
| `tests/ecs/ComponentRegistryTest.cpp` | 10 |
| `tests/ecs/CameraTest.cpp` | 9 |
| `tests/ecs/Sprite2DComponentsTest.cpp` | 7 |

### Per-subsystem coverage

The strongest evidence is the complete set of headers the test suite includes — 22 files include only 13 distinct engine headers. Everything not on that list is untested by construction.

| Subsystem | Source size | Tested? | What is actually covered |
|---|---|---|---|
| **Vulkan RHI** (`src/Vulkan/*.cpp`, 15 files) — Device, Instance, SwapChain, Buffer, Image, Texture, Cubemap, Pipeline, ComputePipeline, RenderPass, DescriptorSystem, CommandBuffer, MemoryAllocator, Window, VertexTypes | ~14.7k LOC total for `src/Vulkan` | **NO — zero** | No test includes any `Vulkan/Vulkan*.h`. Not one line of RHI code is exercised. |
| **FrameGraph compile/execute** (`src/Vulkan/FrameGraph/`, 10 files incl. `RenderGraph.cpp` 111 KB, `FrameGraphCompiler.cpp` 78 KB, `FrameGraphAnalyzer.cpp` 48 KB, `FrameGraphExport.cpp` 42 KB, Executor, Builder, Debugger, RenderTargetSaver) | largest module | **Barely** | Only `FrameGraphJson.cpp` (string↔`VkFormat`/usage/descriptor/address-mode/compare-op conversion tables, 46 tests) and `VertexFormatRegistry` (22). **No compile, no barrier/aliasing analysis, no execution, no pass scheduling is tested at all.** |
| **Dot-path binding** (`src/FrameGraph/DotPathResolver.cpp`) | — | **YES, well** | 92 tests (55 unit + 37 against a real `entt::registry`). The best-covered subsystem, and appropriately so — it is the load-bearing piece of the JSON-pipeline design. |
| **Buffer layout** (`include/FrameGraph/BufferLayoutCompiler.h`, header-only, 209 lines) | — | **YES** | 45 tests on std140/std430 packing. |
| **Shared buffers** (`src/FrameGraph/SharedBufferRegistry.cpp`) | — | Partial | 13 tests. |
| **FrameGraph runtime** — `EntityRenderExecutor`, `FrameGraphRenderer`, `StagingBufferManager` | — | **NO** | No test includes these. |
| **Physics** (`src/ECS/PhysicsSystem.cpp`, `src/Facade/PhysicsAPI.cpp`, Bullet3) | — | **NO — zero** | Grep for `bullet\|btRigidBody\|PhysicsSystem\|PhysicsAPI` across all test sources returns **no files**. Despite physics being a headline README feature, there is not a single physics test. |
| **Animation** (`src/Animation/AnimationEvaluator.cpp`, `src/ECS/SkeletalAnimationSystem.cpp`) | 230 LOC + | Partial | 39 tests (AnimationData 12, AnimationEvaluator 11, AnimationPlayback 16) covering CPU-side clip/channel data and interpolation. Skinning upload and the GPU path are untested. |
| **Resource loading** (`src/Resources/GltfSceneLoader.cpp` 44 KB, `ResourceManager.cpp`, `FontLoader.cpp`, `Sprite2DManager.cpp`) | ~1.9k LOC | **Effectively no** | 30 `ResourceCacheTest` tests cover `ResourceHandle`/`ResourceDescriptor` hashing and cache bookkeeping only. **No glTF file is ever parsed.** The only `gltf` mentions in tests are `GltfOptions_Defaults`/`GltfResult_Defaults` struct-default checks (`tests/facade/FacadeTypesTest.cpp:89,107`) and one string literal `"assets/model.gltf"` used as a cache key (`tests/unit/ResourceCacheTest.cpp:83`). |
| **ECS core** (`include/ECS/Core.h`, `Systems.h`, `RenderComponents.h`, `Sprite2DComponents.h`) | 738 LOC in src | **YES** | 109 tests: Transform 14, Camera 9, ComponentRegistry 10, EntityBuilder 14, RenderComponents 28, Systems 27, Sprite2D 7. Solid. |
| **Facade** (`src/Facade/`, 8 files, 1723 LOC) | — | **Half** | 86 tests, all against `SceneAPI` (57), `EcsAPI` (18), `FacadeTypes` (11). **`EngineAPI`, `PhysicsAPI`, `InputAPI` have zero tests** — no test includes their headers. |
| **GPU types** (`include/GPU/GPUTypes.h`) | — | YES (shallow) | 30 tests on POD handles/validity, using fake non-null pointers. `GPUResourceFactory.cpp` (778 LOC) untested. |
| **IBL** (`src/IBL/IBLGenerator.cpp`) | 683 LOC | **NO — zero** | — |
| **App lifecycle** (`src/App/ApplicationBase.cpp`) | 679 LOC | **NO — zero** | — |
| **Core** (`EventSystem.cpp`, `Logger.cpp`) | 91 LOC | Partial | 10 EventSystem tests (incl. a threading test). Logger untested. |
| **Python bindings** (`python/shoonyakasha/_shoonyakasha.pyx`, 52 KB) | — | **NO — zero** | No `test_*.py`, no `conftest.py`, no pytest/tox/nox config anywhere in the repo. The entire Cython bridge is untested. |

### Honest coverage verdict

**This is a headless CPU-side unit-test suite for the data structures around the engine, not a test suite for the engine.** It is genuinely good at what it covers — dot-path resolution, buffer layout packing, ECS components, and the SceneAPI facade are thoroughly exercised, and the tests are readable and specific. But the three things the README sells hardest — Vulkan rendering, JSON pipeline compilation, and Bullet physics — have **zero, near-zero, and zero** coverage respectively.

Concretely: `src/Vulkan/` is 14,719 LOC across 25 files, roughly 60% of the engine's implementation, and the test suite does not include a single one of its headers except two pure-data helpers.

**Other test-quality observations:**

- **No GPU or integration tests exist.** Every test is headless. `tests/TestHelpers.h:49-63` provides `dummyVkBuffer()`/`dummyVkImage()`/etc. that return `reinterpret_cast<VkBuffer>(uintptr_t(1))` with the comment "These are never dereferenced — only used for isValid() checks". No `VkInstance` is ever created. There is no smoke test that so much as opens a window.
- **Deterministic:** yes, with one caveat. No RNG, no filesystem I/O, no network. `tests/unit/EventSystemTest.cpp` includes `<thread>`, `<atomic>`, and `<chrono>` — a concurrency test there is the only plausible source of flakiness, and I did not run it to confirm.
- **No disabled or skipped tests.** Grep for `DISABLED_`, `GTEST_SKIP`, `FRIEND_TEST` across all test sources returns nothing. Clean.
- **Trivial/tautological tests: only 4 of 522** (0.8%) contain no `EXPECT_`/`ASSERT_` at all, and all four are deliberate "does not crash" tests with honest comments:
  - `tests/unit/DotPathResolverTest.cpp:311` `ValidatePath_SceneTooShort` — comment: "The main thing is it doesn't crash".
  - `tests/unit/ResourceCacheTest.cpp:61` `Hash_DifferentHandles_DifferentHashes` — the name promises a difference check the body does not perform; comment admits "Not guaranteed to be different". **The test name is misleading** and should be `Hash_DoesNotCrash`.
  - `tests/facade/SceneAPITest.cpp:72` `DestroyEntity_NullEntity_NoOp` — one line, `// Should not crash`.
  - `tests/unit/EventSystemTest.cpp` `PublishWithNoSubscribers_NoOp`.

  These are legitimate crash-regression tests (they would catch a segfault), just weakly named. This is a good ratio; assertion discipline in the suite is otherwise strong.
- `SceneAPITest.cpp:4` documents the design honestly: *"Tier 2: Uses entt::registry + ComponentRegistry directly (no GPU)"*. The author knows what tier this suite occupies.

---

## CI

**There is none. Zero.**

- `.github/` does not exist in the repository. `git ls-files ".github/*"` returns nothing; `ls -la .github/workflows/` fails with "No such file or directory".
- `git ls-files "*.yml" "*.yaml"` returns exactly one file: **`third_party/cgltf/.github/workflows/build.yml`** — a vendored copy of an upstream third-party project's own CI, which never runs for this repository. This is the ".yml file the repo lists"; it is not Shoonyakasha's CI.
- No `.gitlab-ci.yml`, no `azure-pipelines.yml`, no `appveyor.yml`, no `Jenkinsfile`.
- No pre-commit config, no linter config, no `.clang-format` or `.clang-tidy`.

Consequently: nothing builds on push, nothing runs the 618 tests, nothing checks formatting, nothing verifies the Linux path, nothing builds wheels, and there is no green/red status to report. Every claim in BUILDING.md about Linux is unverified assertion.

**What is missing, in priority order:**

1. A matrix build (windows-latest MSVC + ubuntu-latest GCC/Clang) running `cmake --build` with `-DBUILD_TESTS=ON -DVCPKG_MANIFEST_FEATURES=tests` and `ctest --output-on-failure`. This alone would catch the `Development` vs `Development.Module` bug and the lowercase-`bin` glslc bug.
2. `cibuildwheel` for the Python bindings — currently a wheel has demonstrably only ever been built on one developer's Windows machine.
3. A job that runs `cmake --install` then a tiny consumer project calling `find_package(Shoonyakasha CONFIG REQUIRED)`. BUILDING.md:213-214 explicitly recommends this ("an install that succeeds proves less than a `find_package` that resolves") but nothing automates it.
4. vcpkg binary caching (`actions/cache` on `VCPKG_DEFAULT_BINARY_CACHE`), otherwise every run rebuilds Bullet/GLFW from source (BUILDING.md:117 notes this takes "several minutes").

---

## Repository hygiene

`git count-objects -vH`: `count: 477`, `size: 95.03 MiB` (loose), `in-pack: 175`, `size-pack: 265.04 KiB`.

**Total tracked content: 216.9 MiB across 574 files.** Breakdown by extension (from `git ls-tree -r -l HEAD`):

| Ext | Size | Files |
|---|---|---|
| `.hdr` | **188.78 MiB** | 2 |
| `.png` | **19.82 MiB** | 2 |
| `.pyd` | 2.32 MiB | 1 |
| `.h` | 2.14 MiB | 87 |
| `.cpp` | 1.25 MiB | 94 |
| `.spv` | 0.84 MiB | 138 |
| `.md` | 0.61 MiB | 47 |
| `.glb` | 0.31 MiB | 3 |

**~96% of this repository by size is binary assets and build artifacts.** Source code (`.h` + `.cpp` + `.pyx`) is about 3.4 MiB.

### Tracked files that should not be

| File | Size | Why it should not be tracked |
|---|---|---|
| `examples/physics_test/cubemaps_hdrs/kloofendal_28d_misty_8k.hdr` | **94.39 MiB** | 8K HDR environment map. Blob `9b3ca300…` |
| `examples/pbr_physics_particles/cubemaps_hdrs/kloofendal_28d_misty_8k.hdr` | **94.39 MiB** | **Byte-identical duplicate** — same blob SHA `9b3ca300…`. Git dedupes storage, but a clone still writes 188 MiB to disk. |
| `python/examples/img.png` | **18.00 MiB** | An 18 MB PNG in an examples folder; almost certainly a screenshot. |
| `python/shoonyakasha/_shoonyakasha.pyd` | **2.32 MiB** | Compiled Windows extension module. **Matches `.gitignore:60` but was committed before the rule was added, so the ignore has no effect.** |
| `logo.png` | 1.82 MiB | Displayed at `width="300"` in README.md:3. A 300 px-wide render needs a tiny fraction of 1.8 MB. |
| `examples/**/*.spv`, `python/examples/**/*.spv` | 0.84 MiB / **138 files** | Compiled SPIR-V — pure build output, regenerated in-source by `add_custom_command` on every build. `*.spv` is **not** in `.gitignore`. |
| `python/shoonyakasha/__pycache__/__init__.cpython-313.pyc` | 727 B | Python bytecode cache. **Matches `.gitignore:52-53`** but was committed first. |

`git ls-files -i -c --exclude-standard` confirms exactly two files are tracked *despite* matching `.gitignore`: `python/shoonyakasha/__pycache__/__init__.cpython-313.pyc` and `python/shoonyakasha/_shoonyakasha.pyd`.

### `.gitignore` adequacy

Better than the on-disk clutter suggests. The scope brief flagged `cmake-build-*`, `.idea/`, `.venv/`, `glTF-Sample-Assets/`, `cubemaps_hdrs/`, `pkg_a_curtains/` — **all of these are correctly ignored and none are tracked** (`.gitignore:1-8, 45, 56-58`). `git ls-files` confirms the only tracked top-level entries are `examples/`, `python/`, `include/`, `src/`, `docs/`, `third_party/`, `tests/`, `cmake/`, and eight root files.

Real gaps:

1. **`*.spv` is absent.** BUILDING.md:174 literally says *"Add `*.spv` to `.gitignore` if it isn't there already"* — it isn't, and 138 are committed. The documentation caught the problem and the fix was never applied.
2. **`cubemaps_hdrs` is listed for five example dirs but not the two that matter.** `.gitignore` covers `sponza_test`, `declarative_sponza_test`, `ssbo_data_flow_example`, `particle_flow_example`, and `python/examples` (lines 15, 24, 26, 35, 44) — but **not** `examples/physics_test/cubemaps_hdrs/` or `examples/pbr_physics_particles/cubemaps_hdrs/`. Those are precisely the two directories holding the 94 MiB files. A single `cubemaps_hdrs/` pattern (no leading slash) would have covered all seven.
3. The per-example asset rules are ~35 lines of near-duplicate paths that clearly drift as examples are added. Generic patterns (`*.spv`, `cubemaps_hdrs/`, `textures/`, `*.gltf`, `*.bin`) would be shorter and would not miss new directories.

Note that removing these files from the working tree does not shrink history; a fresh clone still pays for them. Fixing this properly requires `git filter-repo`/BFG and a force-push, which is a separate decision.

---

## README claim verification

| # | Claim (README.md line) | Verdict | Evidence |
|---|---|---|---|
| 1 | "Automated test suite (**582 tests**)" (:169) | **FALSE** | 522 source macros (394 `TEST` + 122 `TEST_F` + 6 `TEST_P`); 618 runtime cases after parameterized expansion. Neither is 582. Origin of the number is `docs/getting-started/prerequisites.md:34,73` ("518 core + 64 facade"); actual split is 436 core + 86 facade. |
| 2 | "examples/ **8 C++ example applications**" (:168) | **FALSE** | There are **9**. `git ls-files 'examples/*/CMakeLists.txt'` returns 9; `CMakeLists.txt:154-162` calls `add_subdirectory` 9 times; the README's own Examples table (:188-196) lists **9 rows**. BUILDING.md:86 correctly says "nine". The README contradicts itself two sections apart. |
| 3 | "EnTT-based ECS with **17+ component types**" (:112) | **TRUE (as written), but misleading** | 21 distinct `struct *Component` types are defined in `include/ECS/` — so "17+" holds. But only **11** are actually registered with the `ComponentRegistry` (`ECS/Core.h:489-497` registers 9, `ECS/CameraController.h:597-598` registers 2). The other 10 (Mesh, Skeleton, AnimationPlayback, Sprite2D, Text2D, TextBaked, TextGlyphOwner, UIAnchor, Lifetime, RenderableTag) are not reflectable/serializable. |
| 4 | "**CMake 3.12+**" (:140) | **FALSE / STALE** | `CMakeLists.txt:6` requires **3.21**, with an explicit comment rejecting 3.12. `pyproject.toml:71` also pins `>=3.21`. BUILDING.md:16 correctly says 3.21+. 3.12 comes from the stale sub-`CMakeLists.txt` headers. |
| 5 | "C++20 compiler (MSVC 2022, GCC 12+, or Clang 15+)" (:138) | **TRUE** | `CMakeLists.txt:13-15` sets C++20, required, no extensions. Slightly stricter than BUILDING.md:15 (GCC 11+/Clang 14+), but conservative in the right direction. |
| 6 | "**vcpkg or manually installed**: GLFW3, nlohmann_json, EnTT, Bullet3, GLM" (:141) | **STALE** | All six `find_package` calls (`CMakeLists.txt:35-40`) use `CONFIG` mode and the project is a vcpkg **manifest** (`vcpkg.json`) with a pinned baseline. BUILDING.md:4-7 states plainly "You do not install them by hand." "Manually installed" is not a supported path. Also omits Vulkan from the list. |
| 7 | Build commands: `mkdir build && cd build; cmake .. -DBUILD_EXAMPLES=ON -DBUILD_TESTS=ON` (:145-149) | **FALSE — will not work** | Two defects. (a) No `-DCMAKE_TOOLCHAIN_FILE=...vcpkg.cmake`, so all six `find_package(... CONFIG REQUIRED)` calls fail at configure. (b) `BUILD_TESTS=ON` additionally requires `-DVCPKG_MANIFEST_FEATURES=tests` or GoogleTest is never installed and `find_package(GTest CONFIG REQUIRED)` (`CMakeLists.txt:172`) fails. BUILDING.md:182-190 has the correct invocation. |
| 8 | Python build: `cmake .. -DBUILD_PYTHON=ON` (:153-156) | **STALE / incomplete** | Same missing toolchain file. Also omits the `VCPKG_TARGET_TRIPLET=x64-windows-static-md` requirement that BUILDING.md:235,245-251 calls non-optional (default triplet produces `ImportError: DLL load failed`). |
| 9 | "The compiled `_shoonyakasha.pyd` (Windows) or `.so` (Linux) will be in `python/shoonyakasha/`" (:158) | **TRUE** | `python/CMakeLists.txt:66-71` sets `SUFFIX .pyd` on MSVC; `:75-78` sets output dir to `${CMAKE_CURRENT_SOURCE_DIR}/shoonyakasha`. (Accurate, though writing build output into the source tree is itself the defect noted above.) |
| 10 | Project-structure tree: `include/ src/ python/ examples/ tests/ docs/ third_party/ cmake/` (:162-173) | **TRUE** | Exactly matches the tracked top-level directories from `git ls-files`. |
| 11 | "third_party/ VulkanMemoryAllocator, cgltf, stb, tinyobjloader" (:171) | **TRUE** | All four present and tracked. |
| 12 | Examples table: 9 named examples (:188-196) | **TRUE** | All 9 names match `examples/*/` directories and the 9 `add_subdirectory` calls at `CMakeLists.txt:154-162`. |
| 13 | Feature: "Post-processing (bloom)", "Async compute (GPU particle simulation)", "Skeletal animation" (:107-109) | **Plausible, unverified** | Corresponding examples exist (`bloom_test`, `particle_test`, `skinned_mesh_test`) with shaders. I did not build or run them; no test covers any of these paths. |
| 14 | Feature: "Bullet3 rigid body dynamics… Raycasting and gravity control" (:117-119) | **Plausible, untested** | `find_package(Bullet CONFIG REQUIRED)` (`CMakeLists.txt:38`) and `src/ECS/PhysicsSystem.cpp` exist. But zero tests — grep for `bullet\|btRigidBody\|PhysicsSystem\|PhysicsAPI` in `tests/` matches no file. |
| 15 | "MIT License" (:210) | **TRUE** | `LICENSE` is MIT, "Copyright (c) 2026 Shoonyakasha Contributors". Matches `vcpkg.json:7` and `pyproject.toml:16`. |
| 16 | Doc links in README (:177-182, :202) | **TRUE** | All resolve to tracked files. |

### Stale docs beyond README

- **`docs/getting-started/prerequisites.md` is the most inaccurate file in the repository.** It is linked from `docs/index.md` as the entry point for new users and is wrong on nearly every line:
  - `:11` "**C++17 compiler** | MSVC 19.14+ | GCC 8+ / Clang 7+" — the project is **C++20** (`CMakeLists.txt:13`). None of those three compiler versions can build it.
  - `:12` "CMake **3.20+**" — actually 3.21.
  - `:14` "Python **3.10+**" — `pyproject.toml:17` says `>=3.8`; BUILDING.md:20 says 3.8+.
  - `:16-27` "Dependencies (Managed by CMake) … fetched or found automatically" — **never mentions vcpkg at all**, and lists **`tinygltf`**, which this project does not use. It uses **cgltf** (`third_party/cgltf/`, `src/ThirdParty/cgltf_impl.cpp`).
  - `:31-34` The build-options table lists only `BUILD_PYTHON` and `BUILD_TESTS`, omitting `BUILD_EXAMPLES` and `SHOONYAKASHA_INSTALL`.
  - `:34,:73` "582 tests: 518 core + 64 facade" — wrong on all three numbers.
  - `:47` `cmake -B build -DBUILD_TESTS=ON -DBUILD_PYTHON=ON` — no toolchain file, no `VCPKG_MANIFEST_FEATURES=tests`. Will not configure.
  - `:53` "No wheel or `pip install` needed" — directly contradicts `pyproject.toml` and BUILDING.md:218-287, which document `pip install .` as the supported path.
  - `:58` `set PYTHONPATH=H:\cpp_dev\Shoonyakasha\python` — **a hardcoded absolute path to a directory that no longer exists** (the repo is at `H:\engine-dev\Shoonyakasha`).
  - `:73` "If any fail, check that the Vulkan SDK is installed and your GPU drivers are up to date" — **actively misleading**: the suite is entirely headless and never creates a `VkInstance`, so GPU drivers cannot cause a failure.
  - `:40-42` "Use CLion with the MSVC toolchain or Visual Studio… Do NOT build from a bare CLI terminal" — contradicts BUILDING.md, which documents CLI builds as the primary workflow.
- **`docs/faq.md:27,149`** repeats "582 tests (518 core + 64 facade)" twice.
- **`docs/architecture/overview.md:183`** repeats "582 tests (518 core + 64 facade)"; **`:35` and `:176`** both credit **tinygltf** for glTF loading. Wrong library.
- **`docs/old/` (2 files) and `docs/plans/` (3 files) are orphaned** — a link analysis over all 47 markdown files found nothing links to any of them:
  - `docs/old/CameraSystemIntegration.md`, `docs/old/declarative_ssbo_data_flow.md`
  - `docs/plans/2026-03-04-engine-documentation-design.md`, `docs/plans/2026-03-04-engine-documentation-plan.md`, `docs/plans/2026-07-01-low-level-python-ecs-bindings.md`

  A directory literally named `old/` shipping in a public repo is a maintenance signal. The `plans/` files are internal design docs, not user documentation.
- **`BUILDING.md:23-26`** claims "each example calls `find_program(GLSLC glslc HINTS $ENV{VULKAN_SDK}/Bin $ENV{VULKAN_SDK}/bin)` and aborts with `glslc not found!`". Only **six of nine** do; `bloom_test` and `particle_test` hint `/Bin` only and have no abort guard. Same at BUILDING.md:368-371.
- **`BUILDING.md:174`** advises adding `*.spv` to `.gitignore` — not done, 138 committed.
- **10 "broken links"** flagged by the link checker in `docs/architecture/cython-bridge.md`, `docs/architecture/ecs-design.md`, `docs/getting-started/cpp-quickstart.md`, `docs/guides/cameras-and-controllers.md`, `docs/old/CameraSystemIntegration.md` are **false positives** — they are C++ lambda parameter lists inside code (`[](float dt)`, `[](registry, entity)`) that match markdown link syntax. No action needed; noted so it is not re-investigated.

---

## Licensing

MIT is stated consistently across `LICENSE`, `README.md:210`, `vcpkg.json:7`, and `pyproject.toml:16`. All vendored third-party code is license-compatible:

| Component | License | Notice location |
|---|---|---|
| `third_party/cgltf/` | MIT (Johannes Kuhlmann, 2018-2021) | Standalone `third_party/cgltf/LICENSE` **and** header notice (`cgltf.h:8`) |
| `third_party/VulkanMemoryAllocator/vk_mem_alloc.h` | MIT (AMD, 2017-2025) | Header preamble, lines 2-7. No standalone file. |
| `third_party/tinyobjloader/tiny_obj_loader.h` | MIT (Syoyo Fujita, 2012-Present) | Header preamble, lines 2-8. No standalone file. |
| `third_party/stb/*.h` (image, image_write, truetype) | Public domain / MIT dual | Header line 1 + full text at end of each file. No standalone file. |
| `examples/*/font/font.ttf` (Roboto) | SIL Open Font License 1.1 (The Roboto Project Authors, 2011) | `OFL.txt` + `README.txt` alongside the font in each example |

The font row was added after the review: at review time `font.ttf` sat loose in
`examples/full_showcase/` with no accompanying licence, which is the one bundled
asset that was not covered. It now ships with the OFL text and the upstream
README in its own `font/` directory in both examples that use it — the same
notice-beside-the-artifact pattern the gaps below ask for.

All permissive, all compatible with MIT redistribution. Two gaps, both **MINOR**:

1. **No aggregated attribution file.** There is no `NOTICE`, `THIRD_PARTY_LICENSES.md`, or third-party section in `LICENSE`/`README.md`. README.md:171 names the four libraries only as a directory listing in the project-structure tree, which is not attribution.
2. **Binary distributions ship MIT code without its notice.** cgltf, tinyobjloader, and stb are `PRIVATE` includes (`CMakeLists.txt:66-71`), so their headers are **not** installed and are **not** in the sdist-independent wheel — yet their compiled code is inside `libShoonyakasha.a` / `_shoonyakasha.pyd`. MIT requires the copyright notice accompany "substantial portions of the Software", including binary form. VMA is fine (it is installed at `CMakeLists.txt:116-119`, carrying its notice). Adding a `THIRD_PARTY_LICENSES.md` and installing it would close this cleanly.

`LICENSE` attributes copyright to "Shoonyakasha Contributors" while git history shows a single author (Maximilian Winter). Not a defect, just noting it.

---

## Findings

### CRITICAL

**C1 — No CI whatsoever.** No `.github/` directory exists; the only workflow YAML in the repo is `third_party/cgltf/.github/workflows/build.yml`, vendored from upstream and never executed. Nothing builds, tests, or lints on push. Every cross-platform claim in BUILDING.md and every "582 tests pass" claim in the docs is unverified assertion. This is the root cause of most findings below — a single Ubuntu job would have caught M1, M2, and M6 immediately.

**C2 — The three headline features have essentially zero test coverage.** `src/Vulkan/` is ~14.7k LOC (about 60% of the implementation) and no test includes any of its RHI headers. FrameGraph compilation and execution — `RenderGraph.cpp` (111 KB), `FrameGraphCompiler.cpp` (78 KB), `FrameGraphAnalyzer.cpp` (48 KB), Executor, Builder — are entirely untested; only the string↔enum conversion tables in `FrameGraphJson.cpp` are covered. Physics has **zero** tests (grep for `bullet|btRigidBody|PhysicsSystem|PhysicsAPI` across `tests/` matches no file). glTF loading has zero tests — `GltfSceneLoader.cpp` (44 KB) never parses a file in any test. The 618 tests are real and well-written, but they cover the data structures *around* the engine, not the engine.

### MAJOR

**M1 — `python/CMakeLists.txt:12` requests the wrong Python component.** `find_package(Python3 REQUIRED COMPONENTS Interpreter Development)` should be `Development.Module` for an extension module. `Development` forces discovery of a linkable `libpython`, which is absent in manylinux containers and is the wrong linkage model on macOS. This will block Linux/macOS wheel builds.

**M2 — No warning flags on the engine library.** `/W4` and `-Wall -Wextra -pedantic` are applied to all 9 example targets and to `ShoonyakashaTests` (`tests/CMakeLists.txt:27-29`), but never to the `Shoonyakasha` target. The largest and most error-prone code in the project compiles at MSVC default `/W1`. No `-Werror`/`/WX` and no sanitizer options exist anywhere (verified by grep across all tracked CMake files).

**M3 — 188 MiB of duplicate binary assets are committed.** `examples/physics_test/cubemaps_hdrs/kloofendal_28d_misty_8k.hdr` and `examples/pbr_physics_particles/cubemaps_hdrs/kloofendal_28d_misty_8k.hdr` are 94.39 MiB each and **byte-identical** (both blob `9b3ca300c9e67e645c4f315d022bffc8b1f8d071`). Plus `python/examples/img.png` at 18 MiB. Root cause: `.gitignore` lists `cubemaps_hdrs` for five example directories (lines 15, 24, 26, 35, 44) but not for these two. Total tracked content is 216.9 MiB, of which ~96% is binary. Removing them from HEAD does not shrink clone cost — that needs history rewriting.

**M4 — Build artifacts are committed, including two that `.gitignore` already covers.** `git ls-files -i -c --exclude-standard` returns `python/shoonyakasha/_shoonyakasha.pyd` (2.32 MiB, matches `.gitignore:60`) and `python/shoonyakasha/__pycache__/__init__.cpython-313.pyc` (matches `:52-53`) — both committed before the rules were added, so the rules are inert. Additionally **138 `.spv` files** are tracked and `*.spv` is not in `.gitignore` at all, despite BUILDING.md:174 explicitly advising it be added. Underlying cause: `python/CMakeLists.txt:75-89` directs build output into the source tree, and the examples compile shaders in-source.

**M5 — `docs/getting-started/prerequisites.md` is wrong in ~10 distinct ways and is the documented entry point for new users.** It says C++17 (actually C++20), CMake 3.20 (actually 3.21), Python 3.10+ (actually 3.8+), credits **tinygltf** (the project uses cgltf), never mentions vcpkg while claiming dependencies are "found automatically", gives a `cmake` command that cannot configure (no toolchain file, no `VCPKG_MANIFEST_FEATURES=tests`), says "No wheel or `pip install` needed" (contradicting `pyproject.toml`), hardcodes the dead path `H:\cpp_dev\Shoonyakasha\python` at `:58`, and tells users that failing tests indicate a GPU driver problem when the suite never touches a GPU. It also contradicts BUILDING.md on whether CLI builds are supported.

**M6 — README's build commands do not work.** `README.md:145-149` omits `-DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake`, so all six `find_package(... CONFIG REQUIRED)` calls fail; and `-DBUILD_TESTS=ON` without `-DVCPKG_MANIFEST_FEATURES=tests` fails at `find_package(GTest CONFIG REQUIRED)` (`CMakeLists.txt:172`). The Python command at `:153-156` additionally omits the `x64-windows-static-md` triplet that BUILDING.md:245-251 calls mandatory. BUILDING.md has all of this correct — the README was simply never updated to match.

### MINOR

**m1 — Wrong test count in four places.** "582 tests (518 core + 64 facade)" appears at `README.md:169`, `docs/getting-started/prerequisites.md:34` and `:73`, `docs/faq.md:27` and `:149`, and `docs/architecture/overview.md:183`. Actual: 522 source macros (436 core + 86 facade), 618 runtime cases.

**m2 — README says 8 examples; there are 9.** `README.md:168` says "8 C++ example applications" while the README's own table at `:188-196` lists 9, `CMakeLists.txt:154-162` adds 9 subdirectories, and BUILDING.md:86 correctly says "nine".

**m3 — `tests/CMakeLists.txt:5` globs without `CONFIGURE_DEPENDS`**, unlike the root (`CMakeLists.txt:44`). A newly added test file will silently not be compiled or run until someone re-runs configure by hand.

**m4 — `bloom_test` and `particle_test` have fragile shader discovery.** Both hint only `$ENV{VULKAN_SDK}/Bin` (capital B; the Linux SDK uses lowercase `bin`) and neither has the `if(NOT GLSLC) FATAL_ERROR` guard the other six have. A missing `glslc` produces a build-time "GLSLC-NOTFOUND: command not found" instead of a clear configure-time error. BUILDING.md:23-26 describes behavior that only six of the nine examples actually have.

**m5 — Orphaned documentation ships publicly.** `docs/old/` (2 files) and `docs/plans/` (3 internal design docs) are tracked but unreachable — a link analysis across all 47 markdown files found no reference to any of them. `docs/architecture/overview.md:35,176` also still credits tinygltf.

**m6 — No third-party attribution file.** cgltf, tinyobjloader, and stb are `PRIVATE` includes whose compiled code ships inside the static library and the `.pyd`, but their MIT notices are not distributed with binaries. A `THIRD_PARTY_LICENSES.md` (installed) would close this. All licenses are compatible; this is a compliance formality.

**m7 — Two misleading test names.** `ResourceHandle.Hash_DifferentHandles_DifferentHashes` (`tests/unit/ResourceCacheTest.cpp:61`) asserts nothing and its own comment concedes "Not guaranteed to be different"; the name promises a check the body does not make. Similarly `DotPathResolver.ValidatePath_SceneTooShort` (`:311`) only verifies no crash. Only 4 of 522 tests lack assertions and all four are deliberate crash-regression tests, so this is a naming nit, not a coverage problem.

**m8 — `vcpkg.json:8-15` contains a stale `$comment-baseline`** instructing the reader to add a `builtin-baseline` that line 32 already provides.

**m9 — `logo.png` is 1.82 MiB but rendered at `width="300"`** (`README.md:3`). Roughly 50× larger than needed.

**m10 — Sub-`CMakeLists.txt` files declare `cmake_minimum_required(VERSION 3.12)`** (`tests/CMakeLists.txt:2`, `python/CMakeLists.txt:9`, all 9 examples) while the root requires 3.21. Harmless but inconsistent, and it is the likely source of README's incorrect "CMake 3.12+".

**m11 — macOS support is claimed but not real.** `pyproject.toml:27` classifies the package `Operating System :: OS Independent` and `:109` configures macOS universal2 tag expansion, but BUILDING.md's prerequisites table has only Windows and Linux columns, and MoltenVK (required for Vulkan on macOS) is mentioned nowhere in the repository.

---

## Open questions

1. **Is the 582 figure a historical artifact?** It is stated identically ("518 core + 64 facade") in four documents, suggesting it was accurate once and the docs were never regenerated. Worth confirming against git history whether tests were removed, or whether the count was always an estimate. My 618 runtime figure is derived statically and should be checked against a real `ctest -N` before publishing a corrected number.
2. **Has this ever been built on Linux?** Nothing I found proves it has, and M1 (`Development` vs `Development.Module`) suggests not for the Python bindings. Whether the pure C++ library builds under GCC is genuinely unknown to me — I did not build, per the read-only constraint.
3. **Are the 188 MiB HDR files intended to be redistributed?** `kloofendal_28d_misty_8k.hdr` is a Poly Haven asset (CC0, so redistribution is permitted), but committing two identical copies looks accidental rather than deliberate. Should examples download assets on first run instead?
4. **Is history rewriting acceptable?** Removing the large binaries from HEAD leaves clone cost unchanged. Fixing it properly requires `git filter-repo`/BFG plus a force-push, which breaks existing clones and any open PRs. This is a judgment call for the maintainer, not a technical one.
5. **Is `EventSystemTest`'s threading test deterministic?** It is the only test including `<thread>`/`<atomic>`/`<chrono>` and therefore the only plausible flakiness source. I did not run it.
6. **Should `docs/plans/` be published at all?** These read as internal design documents. If they are intended as public roadmap they should be linked from `docs/index.md`; if not, they probably do not belong in a public repo.
7. **Is `python/examples/img.png` (18 MiB) actually used by anything?** It looks like a stray screenshot rather than a required asset. I did not trace references from the Python examples.
