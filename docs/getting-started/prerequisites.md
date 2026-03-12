# Prerequisites

Everything you need to build and run Shoonyakasha.

---

## Required Software

| Tool | Version | Notes |
|------|---------|-------|
| **C++17 compiler** | MSVC 19.14+ recommended | GCC 8+ / Clang 7+ also work, but MSVC is the primary target on Windows |
| **CMake** | 3.20+ | Build system generator |
| **Vulkan SDK** | Latest (LunarG) | [https://vulkan.lunarg.com/](https://vulkan.lunarg.com/) — includes validation layers and shader compiler |
| **Python** | 3.10+ | Only required if building Python bindings (`BUILD_PYTHON=ON`) |

## Dependencies (Managed by CMake)

These are fetched or found automatically — you do not need to install them manually:

- **EnTT** — Entity Component System
- **Bullet3** — Physics (collision detection, rigid body dynamics)
- **GLM** — Math library (vectors, matrices, quaternions)
- **VMA (Vulkan Memory Allocator)** — GPU memory management
- **GLFW** — Windowing and input
- **nlohmann-json** — JSON parsing
- **stb** — Image loading (stb_image)
- **tinygltf** — glTF model loading

## CMake Build Options

| Option | Default | Description |
|--------|---------|-------------|
| `BUILD_PYTHON` | `OFF` | Build Cython/Python bindings. Produces `python/shoonyakasha/_shoonyakasha.pyd`. |
| `BUILD_TESTS` | `OFF` | Build GTest test suite (582 tests: 518 core + 64 facade). |

## Building

### IDE Build (Recommended)

Use **CLion with the MSVC toolchain** or **Visual Studio** (open the CMake project directly).

> **Warning:** Do NOT build from a bare CLI terminal. The MSVC compiler requires `vcvars64.bat` environment setup, which IDE toolchains handle automatically. If you must use the command line, run from a **Developer Command Prompt** or source `vcvars64.bat` first.

### CMake Configuration Example

```
cmake -B build -DBUILD_TESTS=ON -DBUILD_PYTHON=ON
cmake --build build --config Release
```

## Python Setup

No wheel or `pip install` needed. The bindings compile to a local `.pyd` module.

1. Build with `BUILD_PYTHON=ON`.
2. Add the `python/` directory to your `PYTHONPATH`:
   ```
   set PYTHONPATH=H:\cpp_dev\Shoonyakasha\python;%PYTHONPATH%
   ```
3. Import:
   ```python
   import shoonyakasha
   ```

## Verification

After building with `BUILD_TESTS=ON`, run the test suite to confirm everything works:

```
ctest --test-dir build --output-on-failure
```

All **582 tests** should pass (518 core + 64 facade). If any fail, check that the Vulkan SDK is installed and your GPU drivers are up to date.
