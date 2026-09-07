# IBLGenerator

Include [IBL/IBLGenerator.h](../../../include/IBL/IBLGenerator.h); namespace `Shoonyakasha`. This native subsystem converts an HDR environment into environment/irradiance/prefilter cubemaps and a BRDF lookup texture.

`IBLGenerationParams` defaults to environment size 1024, irradiance size 32, prefilter size 512, BRDF LUT size 512, irradiance samples 2048, prefilter samples 1024, and BRDF samples 1024. These are generator defaults; applications may select different settings.

`IBLResources` contains pointers to the produced textures, offers `isValid()`, and has explicit cleanup. Follow its ownership contract; do not treat returned pointers as independently owned copies. See the header and [implementation](../../../src/IBL/IBLGenerator.cpp) for native generation and resource lifetime.

The facade configures IBL through `hdrEnvironmentPath` / Python `hdr_environment_path`, rather than exposing the generator API or all quality controls. The pipeline must reference matching environment bindings. See [lighting/IBL](../../guides/lighting-and-ibl.md).
