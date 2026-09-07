# ApplicationBase

Include [App/ApplicationBase.h](../../../include/App/ApplicationBase.h); namespace `Shoonyakasha`. This is the native inheritance-based application interface. Most applications can instead use [EngineAPI](engine-api.md).

## Lifecycle and extension points

Subclass ApplicationBase, pass an `ApplicationConfig`, override lifecycle hooks, and call `run()`. The base owns window/device/swapchain setup, the active ECS scene, asset managers, renderer, input, and main loop.

Initialization creates Vulkan and ECS state, loads optional IBL resources, and constructs the RenderGraph object. `onInit()` follows; the graph is loaded/compiled afterwards. `onPostInit()` follows compilation and event setup. This means `getRenderGraph()` exists during init, but compiled-resource access belongs after compilation.

`onUpdate(float)` runs before standard buffer upload. `onPreRender(float)` runs after that upload, then rendering/presentation occurs and `onPostRender()` runs. Input/resize hooks receive key or size values; `onCleanup()` is the user cleanup hook. Use the [implementation](../../../src/App/ApplicationBase.cpp) for exact ordering when extending lifecycle behavior.

## Native access

Protected accessors expose `getScene()`, `getRegistry()`, `getRenderGraph()`, `getDevice()`, `getWindow()`, `getSwapChain()`, asset managers, logger, input handler, and event dispatcher. Include their headers when using their members. These references are tied to the application's lifetime and initialization phase.

Convenience methods cover glTF, cameras/lights, sprites/text, and capture. The facade's `CallbackApp` adds physics and skeletal-animation wiring; a custom subclass should follow the relevant native example rather than assuming all facade additions are in the base class.

The base loop uses single-command-buffer RenderGraph execution. Multi-queue APIs are native extension points, not automatic submission from a queue key alone.

Examples: [BloomTest](../../../examples/cpp/rendering/bloom_test), [PhysicsTest](../../../examples/cpp/physics/physics_test), [SkinnedMeshTest](../../../examples/cpp/animation/skinned_mesh_test).
