# Geometry sharing and instancing

With `flattenHierarchy=false` (Python `flatten_hierarchy=False`), the glTF loader keeps node transforms in the ECS and reuses reference-counted vertex/index buffers for nodes referencing the same mesh. This reduces duplicate geometry allocations within a load and lets instances retain their own transforms.

The loader returns renderable primitive entities beneath node entities. Native `GltfLoadResult` also includes `nodeEntities` and `rootEntities`; the facade/Python result does not. Use scene parent queries when navigating from facade handles.

Setting `flattenHierarchy=true` bakes world transforms into vertex data, so differently transformed instances need different geometry. Keep the default unless baked geometry is explicitly needed.

## What sharing does and does not mean

Shared buffers do not imply that the entity renderer combines objects into one Vulkan instanced draw. The generic JSON `draw` execution supports an explicit `instanceCount`, but the application/shaders must supply the corresponding per-instance data. The loader also caches primitives across loads using file path, mesh/primitive index, and skinning mode. That cache belongs to the loader; do not assume automatic invalidation when source files or load options change.

Run [InstancingTest](../../examples/cpp/api/instancing_test) from its source directory after building with examples enabled. Its `--selftest` mode checks shared buffers, hierarchy behavior, and capture; use it when changing loader ownership or transforms.

See [loading scenes](loading-scenes.md), [native loader](../api/cpp/gltf-loader.md), and [GPU ownership](../api/cpp/gpu-types.md).
