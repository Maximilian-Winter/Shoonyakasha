# Examples Refactor — Focused Examples with a Progression

**Status:** planned, not started. Written 2026-08-02 against commit `38f594f`.

| Phase | | |
|---|---|---|
| 0 | Shared pipeline and shaders per category | ☐ |
| 1 | `cpp/getting_started/` — the five-step ladder | ☐ |
| 2 | Re-file the existing examples | ☐ |
| 3 | Fill the C++ gaps | ☐ |
| 4 | Python | ☐ |
| 5 | Documentation | ☐ |

Phases 0–2 are worth doing together. Phase 1 alone fixes most of the problem and can
ship on its own.

---

## Context

The examples are organised by topic (`examples/cpp/{api,rendering,compute,physics,animation}/`,
commit `d70230b`) but the examples themselves are feature collections rather than
demonstrations of one thing. The problem is measurable:

| | |
|---|---|
| C++ examples loading an HDR environment **and** a glTF scene before showing anything | 6 of 10 |
| `physics_test` | 345 lines; first `RigidBodyComponent` at line 226 |
| `ssbo_data_flow_example` / `pbr_physics_particles` | 658 LOC / 6 passes, 484 LOC / 8 passes |
| Smallest pipeline JSON in the repo | 101 lines |
| Shader files across all examples | 144 files, **40 distinct** |

**Cause:** standing up a working Vulkan pipeline is expensive, so once one example
had deferred PBR + IBL working, the next feature was added to it rather than built
from nothing. `bloom_test` is the counter-example: its C++ demonstrates nothing
because everything it shows lives in its JSON. That is the shape to aim for.

**The largest gap is the bottom of the ladder.** The engine's headline feature is
JSON-declared render pipelines, and no example teaches one — nothing shows a single
pass, one buffer layout, or one dot-path. A newcomer's first contact is a 213-line JSON.

**Outcome:** each example teaches one thing, ordered from a cleared window to a full
scene, with the current large examples kept as end-to-end capstones.

---

## Decisions taken

Settled before writing this. Do not relitigate without a reason.

1. **Examples share one pipeline JSON and one `shaders/` per category.** Each example
   is a small `main.cpp` pointing at `../<name>.json`.
2. **Existing large examples move to `integration/` unchanged.** The refactor is
   additive; nothing is deleted.
3. **Python gets a lighter set:** `getting_started`, `ecs`, `animation`, `games`,
   `integration`. Rendering, compute and physics stay C++ only; the Python README
   points at them.
4. **No `examples/cpp/tests/`.** Self-verification stays a `--selftest` flag on the
   example it verifies, so the repo's `tests/` remains the only "tests".

### Verified feasible with no engine change

- Shader modules are opened with a plain `std::ifstream(filename)`
  (`src/Vulkan/VulkanPipeline.cpp:507`), so `"../shaders/forward.vert.spv"` in a
  pipeline JSON resolves against the working directory and works.
- `pipelineJsonPath` is passed through unresolved to `RenderGraph::loadFromFile`
  (`src/App/ApplicationBase.cpp:214`), so `"../forward.json"` works.
- `target_compile_shaders(<target> <dir>...)` in `cmake/CompileShaders.cmake`
  already accepts multiple directories, so an example can compile a sibling
  `../shaders`.
- Examples run from their own source directory; `VS_DEBUGGER_WORKING_DIRECTORY`
  is already `${CMAKE_CURRENT_SOURCE_DIR}`.

### Pass vocabulary available for the ladder

`fullscreen` (37 uses), `opaque_geometry` (15), `compute_dispatch` (12), `draw` (9),
`transparent_geometry` (7), `sprite_geometry` (5), `skinned_geometry` (2).
A `fullscreen` pass needs no vertex buffers, no ECS and no glTF — that is the
minimal teaching pipeline.

---

## Target structure

```
examples/
  README.md                       index, grouped and ordered
  cpp/
    getting_started/              numbered; the only numbered category
      forward.json  shaders/
      01_window/ 02_shader/ 03_mesh/ 04_materials/ 05_ibl/
    rendering/     deferred.json shaders/   + focused examples
    ecs/           forward.json  shaders/   + focused examples
    compute/       particles.json shaders/  + focused examples
    physics/       forward.json  shaders/   + focused examples
    animation/     skinned.json  shaders/   + focused examples
    integration/                  today's large examples, unchanged
  python/
    getting_started/  ecs/  animation/  games/  integration/
```

Numbered prefixes only inside `getting_started/`, where the order is real.

---

## Phase 0 — Shared assets per category

Do this first; every later phase depends on it.

- [ ] Create `examples/cpp/getting_started/shaders/` with `forward.vert` /
      `forward.frag`. Base them on `python/shoonyakasha/templates/pipeline.json`
      and its shaders — the scaffold already generates a working 1-pass pipeline,
      and reusing it keeps the scaffold and the first example identical.
- [ ] Create one pipeline JSON per category at the category root.
- [ ] Each example's `CMakeLists.txt` calls
      `target_compile_shaders(<T> "${CMAKE_CURRENT_SOURCE_DIR}/../shaders")`.
- [ ] Add the new shared directories to `.gitignore` as
      `examples/**/shaders/*.spv`, matching what `examples/python/**` already does.

**Gate:** one example per category builds and runs from its own directory with zero
validation messages before writing the rest.

---

## Phase 1 — `cpp/getting_started/`

Five examples, each adding exactly one concept to the previous.

| | teaches | pipeline |
|---|---|---|
| `01_window` | `EngineAPI`, config, `run()`, a clear colour | 1 `fullscreen` pass, no shaders bound |
| `02_shader` | `vertexFormats`, a fragment shader, `resources`/`passes` | 1 `fullscreen` pass |
| `03_mesh` | camera, a glTF box, a directional light | 1 `opaque_geometry` pass |
| `04_materials` | `bufferLayouts`, dot-path sources, material params | + per-draw push constants |
| `05_ibl` | `hdrEnvironmentPath`, why later examples all have it | + IBL bindings |

Each `main.cpp` stays under ~80 lines. Assets come from `assets/` via `AssetPaths`;
`models/Box.gltf` and `env/*_1k.hdr` already ship.

Each example gets a `README.md`: what it shows, and what to change to see it change.

---

## Phase 2 — Re-file what exists

Pure moves, no content change. Use `git mv` so history follows.

| from | to |
|---|---|
| `cpp/rendering/bloom_test` | `cpp/rendering/bloom` |
| `cpp/rendering/declarative_sponza_test` | `cpp/integration/sponza_pbr_ibl` |
| `cpp/compute/particle_test` | `cpp/compute/particles` |
| `cpp/compute/particle_flow_example` | `cpp/integration/particle_flow` |
| `cpp/compute/ssbo_data_flow_example` | `cpp/integration/ssbo_data_flow` |
| `cpp/physics/pbr_physics_particles` | `cpp/integration/pbr_physics_particles` |
| `cpp/physics/physics_test` | `cpp/physics/rigid_bodies` |
| `cpp/animation/skinned_mesh_test` | `cpp/animation/skinned_mesh` |
| `cpp/api/facade_test` | superseded by `getting_started/03_mesh`; retire or fold in |
| `cpp/api/instancing_test` | `cpp/integration/instancing` (keeps `--selftest`) |
| `python/games_2d/pong_game` | `python/games/pong` |
| `python/games_2d/full_showcase` | `python/integration/full_showcase` |
| `python/games_2d/sprite_ui_test` | `python/getting_started/04_sprites` |
| `python/getting_started/demo` | `python/integration/demo` |

Update in the same commit: `CMakeLists.txt` `add_subdirectory` paths, `.gitignore`,
`examples/README.md`, `docs/examples/*.md`, `BUILDING.md`.

CMake target names stay as they are — renaming those is a separate change.

**Watch for:** `git add -A` after a `git rm --cached` re-tracks what was just
untracked. This bit twice during the previous reorganisation, once staging 137
texture files that had been deliberately excluded. Check `git ls-files <dir>` after
staging, not before.

---

## Phase 3 — Fill the C++ gaps

Only after Phases 0–2 are green. Each is a small `main.cpp` on its category's
shared pipeline.

- **`rendering/`** — `blend_modes` (render layer masks; only the Python showcase
  shows this today), `render_target_readback` (readback and screenshot on its own).
- **`ecs/`** — nothing exists in C++ today. `components_and_systems`,
  `hierarchy` (parent/child transforms — no example anywhere), `queries_and_tags`.
- **`compute/`** — `ssbo_readback` and `compute_to_graphics` (the barrier story),
  both extracted from `integration/ssbo_data_flow` without deleting it.
- **`physics/`** — `colliders`, `raycast`.
- **`animation/`** — `clip_playback` (play/pause/speed, split from `skinned_mesh`).

---

## Phase 4 — Python

`getting_started/` mirrors the C++ ladder in four steps: `01_window`, `02_shader`,
`03_mesh`, `04_sprites` — the sprite/UI path is Python's natural entry point.
`ecs/` takes `ecs_bindings_demo`; `animation/` keeps `skinned_fox_demo`;
`games/` takes `pong`; `integration/` takes `demo` and `full_showcase`.

Python examples import the installed package. No `sys.path` shims — see the
packaging fix in `d70230b` for why one hid a broken wheel for so long.

---

## Phase 5 — Documentation

- [ ] `examples/README.md` as an ordered index: the ladder, then focused examples by
      category, then integration capstones.
- [ ] `docs/examples/cpp-examples.md` and `python-examples.md` in the same order.
- [ ] README project-structure block and `BUILDING.md` run instructions.

---

## Verification

Per phase, before moving on:

1. **Builds:** `cmake --build cmake-build-debug` with zero warnings, and
   `cmake --build cmake-build-tests --target ShoonyakashaTests` + `ctest` green
   (691 tests at time of writing).
2. **Every example runs from its own directory with zero validation messages:**
   ```bash
   (cd examples/cpp/<cat>/<name> && (<path-to-exe> > /tmp/x.log 2>&1 &))
   sleep 20; taskkill //IM <exe>.exe //F
   grep -c 'Validation layer' /tmp/x.log     # must be 0
   ```
3. **Python examples** run from a venv with only the wheel installed, not the source
   tree, so a packaging regression is caught.
4. **`integration/instancing --selftest` exits 0** with no `[FAIL]` lines.
5. **Shader count does not grow:** compare
   `find examples -name '*.vert' -o -name '*.frag' -o -name '*.comp' | wc -l`
   against distinct content by `md5sum`. 40 distinct today; the ratio should improve,
   never worsen.

### Known, pre-existing, not caused by this work

- `ecs_bindings_demo` and `full_showcase` print tracebacks by design — each
  registers a system written to fail, demonstrating the auto-disable behaviour.
- `ecs_bindings_demo` emits 10 validation errors: its pipeline declares IBL
  descriptor bindings but it sets no `hdr_environment_path`, so `brdfLUT` and
  `prefilterMap` are never written. Confirmed identical at `HEAD` before this work
  began. Fix separately or leave.

---

## Risks

- **Phase 0 is load-bearing.** If shared assets turn out to be awkward in practice,
  everything after it changes shape. Validate with one category before writing more.
- **Scope.** Phases 1 and 3 are ~15 new examples, each needing a pipeline that runs.
  Several sessions, not one.
- **Poor parallelisation.** Shared pipelines must exist before the examples using them.
- `examples/cpp/api/facade_test` is currently the closest thing to a first example.
  Do not retire it until `getting_started/03_mesh` runs.
