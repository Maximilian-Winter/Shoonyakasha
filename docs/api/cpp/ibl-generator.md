# IBLGenerator

Include [IBL/IBLGenerator.h](../../../include/IBL/IBLGenerator.h); namespace `Shoonyakasha`. This native subsystem converts an HDR environment into environment/irradiance/prefilter cubemaps and a BRDF lookup texture.

`IBLGenerationParams` defaults to environment size 1024, irradiance size 32, prefilter size 512, BRDF LUT size 512, irradiance samples 2048, prefilter samples 1024, and BRDF samples 1024. These are generator defaults; applications may select different settings.

`IBLResources` contains pointers to the produced textures, offers `isValid()` (true with the irradiance map, the prefiltered map and the BRDF lookup table; the environment map is null for maps loaded from a file), and has explicit cleanup. Follow its ownership contract; do not treat returned pointers as independently owned copies. See the header and [implementation](../../../src/IBL/IBLGenerator.cpp) for native generation and resource lifetime.

`generate(hdrPath, params, keep)` also reads the maps back into an `IBLData` when `keep` is given, and `upload(data)` makes GPU maps from one. `computeBRDFLUT(size, samples)` is the lookup table on the CPU, on every hardware thread. [IBL/IBLCache.h](../../../include/IBL/IBLCache.h) writes and reads `IBLData` as `.ibl` files, with the source hash and parameters a cache is checked against.

The facade configures IBL through `hdrEnvironmentPath` / Python `hdr_environment_path` (an HDR map or a baked `.ibl` file), changes it with `setEnvironment` / `setEnvironmentColor`, and bakes files with `bakeEnvironment`; `ApplicationBase` has the same methods and the `iblCache` setting. The pipeline must reference matching environment bindings. See [lighting/IBL](../../guides/lighting-and-ibl.md).
