# Prerequisites

Everything you need to build and run Shoonyakasha.

---

## Required Software

| Tool | Version | Notes |
|------|---------|-------|
| **C++20 compiler** | MSVC 2022 | GCC 12+ / Clang 15+ should work; only MSVC on Windows is verified by CI |
| **CMake** | 3.21+ | Enforced by `CMakeLists.txt` |
| **Vulkan SDK** | LunarG, recent | [vulkan.lunarg.com](https://vulkan.lunarg.com/) — provides the validation layers and `glslc` |
| **vcpkg** | any recent checkout | All C++ dependencies come from here |
| **Python** | 3.9+ | Only for the Python bindings (`BUILD_PYTHON=ON`) |

## Dependencies

These come from **vcpkg**, declared in `vcpkg.json` and pinned to a
`builtin-baseline` so two machines resolve the same versions. You do not install
them by hand, but you *do* have to pass vcpkg's toolchain file — see below.

- **EnTT** — entity component system
- **Bullet3** — physics
- **GLM** — mathematics
- **GLFW3** — windowing and input
- **nlohmann-json** — JSON parsing
- **Vulkan** — via the SDK
- **GoogleTest** — only with the `tests` feature

Vendored under `third_party/` rather than fetched: **cgltf** (glTF loading),
**stb** (image loading), **VulkanMemoryAllocator**, **tinyobjloader**.

## CMake Build Options

| Option | Default | Description |
|--------|---------|-------------|
| `BUILD_EXAMPLES` | `OFF` | Build the nine C++ examples. Needs `glslc` from the Vulkan SDK. |
| `BUILD_TESTS` | `OFF` | Build the GoogleTest suite. Also needs `-DVCPKG_MANIFEST_FEATURES=tests`. |
| `BUILD_PYTHON` | `OFF` | Build the Cython extension module. |
| `SHOONYAKASHA_INSTALL` | `ON` | Generate install/export rules. The wheel build turns this off. |

## Building

```bash
cmake -B build -S . \
  -DCMAKE_TOOLCHAIN_FILE=<path-to-vcpkg>/scripts/buildsystems/vcpkg.cmake \
  -DBUILD_TESTS=ON -DVCPKG_MANIFEST_FEATURES=tests
cmake --build build --config Release
```

Two things are easy to miss, and both fail at configure time:

- **The toolchain file is required.** Every dependency is resolved with
  `find_package(... CONFIG REQUIRED)` against vcpkg, so without it the first
  `find_package` fails.
- **`BUILD_TESTS=ON` alone is not enough.** GoogleTest sits behind the `tests`
  feature in the manifest, so `VCPKG_MANIFEST_FEATURES=tests` must be set too.

An IDE that manages CMake for you (CLion, Visual Studio) will normally supply
the toolchain file once vcpkg is configured. A plain terminal will not, but
works fine otherwise — from a Developer Command Prompt, or any shell where
`vcvars64.bat` has been sourced, so that MSVC is on `PATH`.

## Python Setup

`pip install .` is the supported path; it drives CMake through scikit-build-core
using the settings in `pyproject.toml`.

Building the extension by hand on Windows additionally needs
`-DVCPKG_TARGET_TRIPLET=x64-windows-static-md`. Without it the module builds but
fails to import with `DLL load failed`, because it links against vcpkg DLLs that
do not end up beside it.

## Verification

```bash
ctest --test-dir build --output-on-failure
```

The suite is entirely headless — it never creates a `VkInstance` — so it needs
neither a GPU nor a display, and a failure does **not** indicate a driver
problem.

See [BUILDING.md](../../BUILDING.md) for the full details, including the Linux
package list GLFW needs.
