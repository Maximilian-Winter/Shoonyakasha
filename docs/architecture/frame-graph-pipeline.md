# From JSON to rendering

1. `loadGraphFromJson` populates named layouts, resources, and passes in FrameGraphBuilder.
2. FrameGraphCompiler analyzes resource dependencies per mip level and array layer, orders/culls passes, and creates images, buffers, views, descriptors, pipelines, attachments, and barriers. The scheduling half (dependencies, culling, barrier planning) lives in FrameGraphSchedule and needs no device. Graphics passes render with Vulkan 1.3 dynamic rendering; there are no render pass or framebuffer objects.
3. RenderGraph connects external resources and scene bindings; its runtime layout helpers fill buffers and per-entity push constants from supported dot-paths.
4. FrameGraphExecutor records compiled passes, dispatches/draws, and registered callbacks. Entity renderers supply geometry for the selected execution categories.
5. ApplicationBase submits and presents, then runs post-render. Resize recreates swapchain bindings and recompiles the graph.

## Contracts that matter

Input/output declarations drive dependency analysis; shader descriptor references do not describe all resource hazards. Presentation is a final state, independent of the color write/blend usage. Shader locations, descriptor sets, and byte layouts must match JSON.

Queue declarations express intent. The standard application calls single-queue execution; separate native multi-queue recording and submission is required to exploit independent queues. Transfer pass declarations do not constitute a general JSON copy-command language.

Dot-path resolution is an explicit supported set, not arbitrary ECS reflection. Script components remain opaque; publish custom shader values through the facade's setters.

Graph diagnostics, builder serialization, and authored JSON serve different purposes. Export is not guaranteed to retain all authoring fields. Keep source JSON and shader sources together under version control.

References: [JSON reference](../reference/pipeline-json.md), [native graph API](../api/cpp/frame-graph.md), [compute/data flow](../guides/compute-and-data-flow.md).
