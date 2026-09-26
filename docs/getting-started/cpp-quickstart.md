# C++ quickstart

Start with the callback-based `Facade::EngineAPI`. Use the inheritance-based `ApplicationBase` when you need direct registry or render-graph access.

## Build the reference application

Follow [BUILDING.md](../../BUILDING.md) to configure a Release build with `BUILD_EXAMPLES=ON`. From the repository root:

```sh
cmake --build build --config Release --target FacadeTest
```

For Ninja on Windows:

```powershell
cd examples/cpp/api/facade_test
../../../../build/examples/cpp/api/facade_test/FacadeTest.exe
```

Visual Studio adds a `Release/` directory before the executable. On Linux, use the same relative path without `.exe`. Run from the example's **source** directory: its pipeline references shaders compiled there by CMake.

The example loads Sponza if present and otherwise uses the bundled box. No HDR environment is needed. Startup output lists controls.

## Minimal facade application

This uses the `pipeline.json` and shaders in the `facade_test` source directory:

```cpp
#include "Facade/EngineAPI.h"
#include <iostream>

int main() {
    using namespace Shoonyakasha::Facade;
    EngineConfig config;
    config.title = "My scene";
    config.pipelineJsonPath = "pipeline.json";
    EngineAPI engine(config);
    engine.setOnInit([&] {
        auto result = engine.loadGltfScene("models/Box.gltf");
        if (!result.success) std::cerr << result.error << std::endl;
        engine.createCamera(glm::vec3(0.f, 1.5f, 5.f));
    });
    engine.run();
}
```

Leaving `pipelineJsonPath` empty loads the [default pipeline](../../python/shoonyakasha/pipelines/default/README.md): deferred PBR with cascaded sun shadows. It is found through `$SHOONYAKASHA_DEFAULT_PIPELINE`, or else in the source tree the engine was built from, so a program installed elsewhere sets the variable.

For your own CMake target, follow [installing the C++ library](../../BUILDING.md#installing-the-c-library). Include the corresponding `Facade/SceneAPI.h`, `InputAPI.h`, `PhysicsAPI.h`, or `EcsAPI.h` to call sub-API methods; `EngineAPI.h` only forward-declares them.

## Lifecycle

`setOnInit` runs after Vulkan/scene creation and before pipeline compilation; `setOnPostInit` follows compilation. `getScene()` and `getEcs()` throw before initialization. Set physics values in the init callback too: unwired setters are no-ops.

`setOnUpdate` receives seconds per frame. `setOnPreRender` runs after standard buffer updates, so use update for values intended for that frame's automatic upload. `setOnPostRender` follows presentation.

See [EngineAPI](../api/cpp/engine-api.md), [ApplicationBase](../api/cpp/application-base.md), and [examples](../../examples/README.md).
