# Compute and GPU data flow

Use [ParticleTest](../../examples/cpp/compute/particle_test) for a small compute/render pipeline, [ParticleFlowExample](../../examples/cpp/compute/particle_flow_example) for a larger simulation, and [SSBODataFlowExample](../../examples/cpp/compute/ssbo_data_flow_example) for initialization, sharing, and readback. Build instructions and target names are in the [C++ catalog](../examples/cpp-examples.md).

## Connect simulation and rendering

1. Define an SSBO element layout and element count under `bufferLayouts`, usually with `std430` packing.
2. Initialize it through a layout-level `source` or provide a native buffer. Declare/import the graph resource and bind it through descriptor layouts.
3. Give the compute pass a compiled compute shader and `compute_dispatch` execution with group counts matching its local size.
4. Declare shader read/write accesses in the producer and shader-read accesses in consumers so dependency/barrier analysis sees the data flow.
5. Keep allocation count, draw/dispatch parameters, and shader-side count consistent. Updating a custom uniform alone does not resize the storage buffer.

## Buffer synchronization

Storage buffers rarely appear in a pass's inputs and outputs, so the compiler finds them through descriptor sets: a `uniform_buffer` or `storage_buffer` binding with `autoBindBuffer` names the buffer, and the pass's SPIR-V says which stages declare it and whether any may write it. Declare a buffer a shader only reads as `readonly buffer`; otherwise the pass counts as writing it, and the next pass to use the buffer waits for it. From that, each pass gets at most one memory barrier, placed only where a pass reads a buffer an earlier pass wrote, or writes one an earlier pass is still using, including the previous frame's last user. Uniform buffers the host fills need none.

Some passes record through code the graph cannot see: those with an execute callback, execution type `none`, `manual` or `scene_geometry`, the entity geometry types (their renderers bind vertex and index buffers), and those with a buffer binding without `autoBindBuffer`. They are taken to touch every buffer, graphics passes reading and compute passes reading and writing, so a buffer a compute pass writes is still visible to a renderer that binds it as vertex data. Give buffer bindings an `autoBindBuffer` where you can, so unrelated passes do not wait on each other.

For a side-effect-only compute pass, set `hasSideEffects: true` if otherwise unused outputs would allow culling. JSON `queue: compute` only selects queue intent: the default ApplicationBase/facade render loop records through single-queue `execute`. Native `executeMultiQueue` requires appropriate command buffers and synchronized queue submission by its caller.

## Initialization and sharing

[ssbo_pipeline.json](../../examples/cpp/compute/ssbo_data_flow_example/ssbo_pipeline.json) demonstrates seeded sphere/gaussian initialization and publishing a target named `particles.currentState`. A consumer can use a `buffer_ref` source referring to a target in `SharedBufferRegistry`. Native application code must arrange graph lifetimes, registration, and execution order.

Binary file sources load raw layout-compatible bytes; they are not a portable schema conversion. [ssbo_pipeline_from_file.json](../../examples/cpp/compute/ssbo_data_flow_example/ssbo_pipeline_from_file.json) illustrates the file source. Keep stride, field types, and count consistent with the file producer.

## Readback and saving

A target object can request periodic/manual readback and binary save. Image resources have corresponding target/readback/save policies for render-target capture. `StagingBufferManager` manages transfer staging and callbacks; the native example shows the callback and manual-trigger setup needed beyond JSON declarations.

The public Python facade does not expose the native readback callback/trigger API. Its frame-capture methods save the presented image, a separate facility. Do not assume an intermediate HDR target can be selected through `capture_screenshot`.

Readback frequency, memory placement, transfer direction, and buffer size affect correctness and cost. The parser accepting a policy does not prove a workflow has been exercised; use the matching example and GPU validation when changing it.

See [JSON policy reference](../reference/pipeline-json.md#initialization-memory-and-readback), [native frame graph](../api/cpp/frame-graph.md), and [capture](frame-capture.md).
