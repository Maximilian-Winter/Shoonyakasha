# Frame graph API

Include [Vulkan/FrameGraph/FrameGraph.h](../../../include/Vulkan/FrameGraph/FrameGraph.h); namespace `Shoonyakasha::FrameGraph`. JSON authors should start with the [walkthrough](../../guides/json-render-pipeline.md) and [JSON reference](../../reference/pipeline-json.md).

## Core objects

| Object | Responsibility |
|---|---|
| `RenderGraph` | Coordinates declarations, compilation, bindings, execution, and diagnostics |
| `FrameGraphBuilder` | Owns resource/pass/layout declarations and imported-resource handles |
| `FrameGraphCompiler` | Analyzes dependencies and builds executable resources/passes/barriers |
| `FrameGraphExecutor` | Records compiled passes, callbacks, and built-in execution types |
| `FrameGraphRenderer` | Connects ECS geometry categories to graph execution |
| `DotPathResolver` / `BufferLayoutCompiler` | Resolve runtime values and compute packed layouts |
| `SharedBufferRegistry` / `StagingBufferManager` | Named shared targets and transfer/readback staging |

The first four live under `Vulkan/FrameGraph`; ECS binding helpers live under `FrameGraph`. These are related layers, not duplicate public APIs.

## Native integration

Construct RenderGraph with a device and command manager. Load declarations with `loadFromFile`, bind the scene, import swapchain/external resources, set parameters, register callbacks, then call `compile`. Check its bool result and `getLastError()` before executing.

`registerPassCallback` selects a callback by pass name; `registerGeometryRenderer` supplies a renderer for an execution type. Register before compilation. `registerPassPipeline` supports native pipeline overrides. External uniform/storage buffers can be registered by name; their owners must preserve their lifetimes.

`setParameter` accepts scalar float/int/uint and float arrays for vectors/matrices. These values serve dispatch/draw counts and named push constants; scene custom values are a separate dot-path mechanism. `bindScene`, `updateSceneContext`, and `updateStandardBuffers` connect current ECS data to GPU buffers.

`execute(frameIndex, swapchainImageIndex, commandBuffer)` uses a single command buffer. `executeMultiQueue` records separate graphics/compute buffers; its caller owns submission and synchronization. Recompile when target extents/backing resources change and restore external bindings as needed.

## Diagnostics and persistence

Analysis/debug interfaces provide graph reports, DOT/JSON export, execution observations, and optional GPU timing. Their exact declarations and option types live in [FrameGraphAnalyzer.h](../../../include/Vulkan/FrameGraph/FrameGraphAnalyzer.h), [FrameGraphDebugger.h](../../../include/Vulkan/FrameGraph/FrameGraphDebugger.h), and [FrameGraphExport.h](../../../include/Vulkan/FrameGraph/FrameGraphExport.h).

Diagnostic exports and builder serialization are not interchangeable with authored pipeline JSON, nor a guaranteed lossless round trip. Readback/save policies also require the matching native setup; see [compute/data flow](../../guides/compute-and-data-flow.md).
