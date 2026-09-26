# Prerequisites

[BUILDING.md](../../BUILDING.md) is the authoritative guide for dependencies, build options, installation, tests, and troubleshooting.

You need a C++20 compiler, CMake 3.21+, vcpkg, the Vulkan SDK, and a driver/device supporting Vulkan 1.3 (the frame graph renders with core dynamic rendering) to render. Examples and the Python starter need `glslc`. The Python extension is built from source through `pip`; importing pure-Python utilities alone does not prove the extension is installed.

Current CI builds and runs headless tests on Windows/MSVC. Linux instructions are provided, but do not constitute a CI-verified Linux runtime. The repository's CI establishes no macOS runtime support.

## Runtime files

- Run from the directory containing the pipeline and its shader paths.
- Keep `glslc` discoverable via `VULKAN_SDK` or `PATH` for shader compilation.
- Models, fonts, textures, and environments use the shared `assets/` directory. Set `SHOONYAKASHA_ASSET_DIR` when running outside the checkout.
- See the [asset catalog](../../assets/README.md) for bundled files and optional downloads. An HDR environment is needed only for pipelines using IBL.

Continue with the [Python quickstart](python-quickstart.md) or [C++ quickstart](cpp-quickstart.md).
