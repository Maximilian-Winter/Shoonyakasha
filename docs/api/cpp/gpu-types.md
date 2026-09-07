# GPU types and ownership

[GPU/GPUTypes.h](../../../include/GPU/GPUTypes.h) defines the engine's thin GPU types; [GPUResourceFactory.h](../../../include/GPU/GPUResourceFactory.h) implements resource creation helpers. These native interfaces are not directly exposed in Python.

Mesh geometry uses `GpuBufferRef` shared references. Copies can share an allocation, so a glTF node instance can reuse vertex/index data while retaining its own transform. Ownership is distinct from an individual draw or entity handle.

GPU textures, buffers, samplers, and borrowed Vulkan views do not all share one ownership rule. Follow the specific type's destructor/factory contract. Never destroy a raw handle obtained from a resource that another owner still holds. Keep the Vulkan device/allocator alive until owned allocations have been released; deferred deletion facilities are in [GpuDeleteQueue.h](../../../include/GPU/GpuDeleteQueue.h).

The facade intentionally hides raw GPU handles. See [instancing](../../guides/instancing.md), [loader](gltf-loader.md), and [architecture](../../architecture/overview.md).
