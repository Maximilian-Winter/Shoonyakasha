# Documentation refresh audit

This audit records findings checked against the source during the September 2026 refresh. It is a documentation audit, not an engine certification. Engine code was not changed to make a documentation claim true.

## Corrected documentation

- README coverage now includes sprites/UI/text, layers, script ECS, shared geometry, capture, and Python tooling, with links to detailed guides.
- Quickstarts use bundled assets and real target/script paths. BUILD_EXAMPLES defaults to OFF; native Python installation requires the build dependencies.
- Init precedes render-graph compilation; post-init follows it. Early physics setters are no-ops despite the old header comment.
- Public references distinguish C++ and Python and include source-derived member inventories.
- JSON metadata is not an enforced versioned schema. Presentation is independent of color blending, and default application execution is single-queue.
- Historical reviews/plans retain their paths and now carry non-authoritative banners.

## Engine/tooling follow-ups

| Finding | Source / consequence |
|---|---|
| Scene save does not test stream success; load clears first | [Scene.h](../include/ECS/Scene.h): return true is not a durable-write guarantee; loading is not a merge or transaction |
| Mesh collider is a box fallback | [PhysicsSystem.cpp](../src/ECS/PhysicsSystem.cpp): do not advertise triangle-mesh collision |
| No public raycast/constraint API | [PhysicsAPI.h](../include/Facade/PhysicsAPI.h), [PhysicsSystem.h](../include/ECS/PhysicsSystem.h) |
| Text label destruction can leave glyphs | [SceneAPI.h](../include/Facade/SceneAPI.h): use text visibility before destruction |
| Texture option gaps | [GltfSceneLoader.cpp](../src/Resources/GltfSceneLoader.cpp): maxTextureSize warns, srgbAlbedo is not consulted |
| Python validator/native parser mismatch | [Pipeline reference](reference/pipeline-json.md#validation-and-export): layout shapes/types and descriptor-set usage differ |
| Builder JSON export omits authoring data | [FrameGraphJson.cpp](../src/Vulkan/FrameGraph/FrameGraphJson.cpp): retain authored pipelines |
| Facade has no async submission loop | [ApplicationBase.cpp](../src/App/ApplicationBase.cpp): multi-queue execution requires native integration |

## Verification

Checked locally on Windows during this refresh:

- Active documentation check: 57 Markdown files, no missing local paths/anchors or Python/JSON snippet syntax errors.
- Source-derived API inventory check: all ten marked references current.
- Python utility/documentation suite: 31 tests passed, including shader compilation and shipped-pipeline validation.
- Release builds of `FacadeTest` and `InstancingTest` succeeded using the existing build tree after loading the Visual Studio development environment.
- The C++ quickstart's complete code block passed MSVC syntax checking against current headers/dependencies.
- `InstancingTest --selftest` exited successfully with 13 passing assertions and 0 failures, including screenshot/video capture. The captured image was visually checked. No Vulkan validation errors appeared in the captured log.
- The generated Python starter compiled both shaders, passed pipeline validation, loaded the box, and reached repeated draw calls during a bounded 15-second run. It used current package sources staged beside an existing CPython 3.12 extension and its dependency DLLs; the local installed package lacked that extension. This was **not** a fresh wheel/install verification.
- A small native probe confirmed `view<entt::entity>()` enumerates ordinary entities with the installed EnTT version; no enumeration defect is claimed here.
- `git diff --check` passed.

Smoke-test files/logs are under the ignored `build/docs-smoke/` directory. The Python run was stopped at the time limit rather than testing interactive shutdown. No Linux/macOS run, fresh dependency installation, full engine unit-suite rerun, external-link crawl, or exhaustive feature/GPU validation was performed. Engine implementation files were unchanged.

Re-run the commands in [maintenance](maintenance.md) after future documentation or API changes.
