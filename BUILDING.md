# Building Shoonyakasha

Shoonyakasha is a static C++20 library built with CMake, with optional examples,
a GoogleTest suite, and Cython-based Python bindings. All C++ dependencies come
from [vcpkg](https://vcpkg.io) in manifest mode — `vcpkg.json` in the repository
root declares them, and vcpkg installs them into the build directory during
configure. You do not install them by hand.

---

## Prerequisites

| | Windows | Linux |
|---|---|---|
| Compiler | MSVC 2022 (17.x) | GCC 11+ or Clang 14+ |
| CMake | 3.21+ | 3.21+ |
| Generator | Ninja or Visual Studio 17 2022 | Ninja or Make |
| vcpkg | any clone (CLion's bundled one is fine) | any clone |
| Vulkan | GPU driver + [LunarG SDK](https://vulkan.lunarg.com/) | GPU driver + `glslc` |
| Python (bindings only) | 3.8+, 64-bit | 3.8+, with `python3-dev` |

The Vulkan **loader and headers** are supplied by vcpkg. The **SDK** is still
worth installing separately: it provides `glslc` for shader compilation and the
validation layers, neither of which vcpkg's `vulkan` port carries.

### Getting vcpkg

Any clone works. If you use CLion, it already has one at
`~/.vcpkg-clion/vcpkg` (Windows: `C:\Users\<you>\.vcpkg-clion\vcpkg`) — reuse it
rather than creating a second, which only invites building against two different
port sets.

Otherwise:

```bash
git clone https://github.com/microsoft/vcpkg
cd vcpkg && ./bootstrap-vcpkg.sh     # bootstrap-vcpkg.bat on Windows
```

Point `VCPKG_ROOT` at whichever clone you use. Everything below assumes it is set.

```powershell
# Windows (PowerShell, persists for new shells)
[Environment]::SetEnvironmentVariable("VCPKG_ROOT", "C:\Users\maxim\.vcpkg-clion\vcpkg", "User")
```

```bash
# Linux (add to ~/.bashrc)
export VCPKG_ROOT="$HOME/.vcpkg-clion/vcpkg"
```

### Linux system packages

vcpkg builds GLFW from source, which needs the X11/Wayland development headers
present on the system. On Debian/Ubuntu:

```bash
sudo apt install build-essential cmake ninja-build pkg-config git curl zip unzip tar \
                 python3-dev libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev \
                 libxi-dev libxkbcommon-dev libwayland-dev wayland-protocols libgl1-mesa-dev
```

On Fedora, the equivalents are `libX11-devel`, `libXrandr-devel`,
`libXinerama-devel`, `libXcursor-devel`, `libXi-devel`, `libxkbcommon-devel`,
`wayland-devel`, `mesa-libGL-devel`, `python3-devel`.

A missing package here surfaces as a GLFW build failure deep inside vcpkg's log
rather than as a clear message, so it is worth installing all of them up front.

---

## Build options

| Option | Default | Effect |
|---|---|---|
| `BUILD_EXAMPLES` | `OFF` | Build the nine example applications |
| `BUILD_TESTS` | `OFF` | Build the GoogleTest suite; also defines `SHOONYAKASHA_TESTING` |
| `BUILD_PYTHON` | `OFF` | Build the Cython extension module |
| `SHOONYAKASHA_INSTALL` | `ON` | Generate install/export rules for the C++ library |

`BUILD_TESTS=ON` additionally requires the manifest's `tests` feature so that
vcpkg installs GoogleTest:

```
-DVCPKG_MANIFEST_FEATURES=tests
```

---

## Building the engine

### Windows

Run these from a **Developer PowerShell for VS 2022**. CMake needs `cl.exe` and
`link.exe` on `PATH`; a plain PowerShell will fail at compiler detection.

```powershell
cd H:\engine-dev\Shoonyakasha

cmake -S . -B build -G Ninja `
      -DCMAKE_BUILD_TYPE=Release `
      -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"

cmake --build build
```

The first configure takes several minutes — vcpkg builds Bullet, GLFW, and the
Vulkan loader from source. Subsequent configures restore from the binary cache in
seconds.

### Linux

```bash
cd ~/engine-dev/Shoonyakasha

cmake -S . -B build -G Ninja \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"

cmake --build build -j$(nproc)
```

### With examples

```bash
cmake -S . -B build -G Ninja \
      -DCMAKE_BUILD_TYPE=Release \
      -DBUILD_EXAMPLES=ON \
      -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"

cmake --build build
```

Executables land in `build/examples/<name>/`. Run them **from their own source
directory** — examples resolve shader and model paths relative to the working
directory:

```bash
cd examples/physics_test
../../build/examples/physics_test/physics_test
```

Available examples: `particle_test`, `bloom_test`, `declarative_sponza_test`,
`ssbo_data_flow_example`, `particle_flow_example`, `skinned_mesh_test`,
`physics_test`, `pbr_physics_particles`, `facade_test`.

### With tests

```bash
cmake -S . -B build -G Ninja \
      -DCMAKE_BUILD_TYPE=Release \
      -DBUILD_TESTS=ON \
      -DVCPKG_MANIFEST_FEATURES=tests \
      -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"

cmake --build build
ctest --test-dir build --output-on-failure
```

---

## Installing the C++ library

```bash
cmake --install build --prefix /path/to/install
```

This writes `include/`, `lib/`, and `lib/cmake/Shoonyakasha/`. Consumers then use:

```cmake
find_package(Shoonyakasha CONFIG REQUIRED)
target_link_libraries(myapp PRIVATE Shoonyakasha::Shoonyakasha)
```

with `CMAKE_PREFIX_PATH` pointing at the install prefix. The consuming project
needs the same dependencies findable — `ShoonyakashaConfig.cmake` calls
`find_dependency()` for Vulkan, GLFW, Bullet, EnTT, glm, and nlohmann_json,
because they are linked `PUBLIC`.

Worth doing once after any change to the export set: an install that succeeds
proves less than a `find_package` that resolves.

---

## Python bindings

The `pyproject.toml` build drives the same `CMakeLists.txt` with `BUILD_PYTHON=ON`,
`BUILD_EXAMPLES=OFF`, `BUILD_TESTS=OFF`, and `SHOONYAKASHA_INSTALL=OFF` — the last
so the C++ library's `include/`/`lib/` install rules don't spill into
`site-packages`. Only the compiled extension module is packaged.

### Windows

```powershell
cd H:\engine-dev\Shoonyakasha
py -3.13 -m venv .venv
.\.venv\Scripts\Activate.ps1
python -m pip install -U pip

python -m pip install . -v `
  -C cmake.define.CMAKE_TOOLCHAIN_FILE="C:/Users/maxim/.vcpkg-clion/vcpkg/scripts/buildsystems/vcpkg.cmake" `
  -C cmake.define.VCPKG_TARGET_TRIPLET=x64-windows-static-md
```

Two details that are not optional:

**Forward slashes in the toolchain path.** scikit-build-core splits arguments
with POSIX `shlex`, where `\` is an escape character — a backslash path silently
collapses to `C:Usersmaxim...` and CMake reports the toolchain as missing.
Forward slashes work fine on Windows.

**The `x64-windows-static-md` triplet.** The default `x64-windows` builds GLFW as
a DLL that never gets copied into the wheel, producing
`ImportError: DLL load failed while importing _shoonyakasha` with no indication of
which DLL is absent. `static-md` links vcpkg's libraries statically while keeping
the CRT dynamic, which CPython extension modules require. Do **not** use plain
`x64-windows-static` — its `/MT` runtime mismatches the interpreter's `/MD` and
corrupts the heap at runtime.

### Linux

```bash
cd ~/engine-dev/Shoonyakasha
python3 -m venv .venv
source .venv/bin/activate
python -m pip install -U pip

python -m pip install . -v \
  -C cmake.define.CMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
```

The default `x64-linux` triplet is static and position-independent, so no triplet
override is needed. If linking fails with *"recompile with -fPIC"*, use
`-C cmake.define.VCPKG_TARGET_TRIPLET=x64-linux-dynamic` instead.

### Verifying

```bash
python -c "import shoonyakasha; print(shoonyakasha.__file__)"
cd python/examples
python <script>.py
```

### Editable installs for development

```bash
python -m pip install "scikit-build-core>=0.10" "cython>=3.0" cmake ninja
python -m pip install --no-build-isolation -e . -v \
  -C cmake.define.CMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
```

`--no-build-isolation` is required: an editable install needs the build backend
importable from the venv itself. Afterwards, changes to `.pyx` sources rebuild
automatically on import.

---

## Updating dependencies

`vcpkg.json` pins a `builtin-baseline` — a vcpkg registry commit SHA. Because each
registry commit defines the recipe for *every* port, that single line fixes the
exact version **and port revision** of all dependencies, including transitive ones
you never named (`vulkan-headers`, `vulkan-loader`, `vcpkg-cmake`). Port revisions
matter: `bullet3 3.25#3` and `3.25#5` share upstream source but differ in applied
patches and CMake config.

To move everything forward in one reviewable step:

```bash
git -C "$VCPKG_ROOT" pull
cd /path/to/Shoonyakasha
"$VCPKG_ROOT/vcpkg" x-update-baseline
```

That rewrites the SHA in `vcpkg.json`. Commit it on its own, rebuild, and run the
tests — one line changed, one clear thing to revert if something broke.

To check the age of the current pin:

```bash
git -C "$VCPKG_ROOT" show -s --format=%ci <baseline-sha>
```

### Requiring a minimum version

When code depends on an API added in a specific release, state it — vcpkg uses
minimum version selection, so this documents a requirement rather than forcing an
upgrade:

```json
{ "name": "entt", "version>=": "3.16.0" }
```

### Holding a package back

When a newer port breaks the build and the fix has to wait:

```json
"overrides": [
  { "name": "bullet3", "version": "3.25", "port-version": 3 }
]
```

Overrides apply only from the top-level manifest and ignore all other constraints.
Always comment why one exists — an unexplained override becomes permanent.

---

## Troubleshooting

**`INTERFACE_INCLUDE_DIRECTORIES property contains path ... prefixed in the build directory`**
A dependency path from `build/vcpkg_installed/` reached the exported target. Never
add vcpkg include directories by hand; link the imported target and let it carry
its own includes.

**`ninja: error: build.ninja:35: loading 'CMakeFiles\rules.ninja'`**
Collateral from a failed generate step — `build.ninja` exists but references files
that were never written. Delete the build directory; a plain reconfigure won't
recover it.

**`Could not find toolchain file: C:Usersmaxim...`**
Backslashes eaten by POSIX `shlex`. Use forward slashes, and prefer
`-C cmake.define.X=Y` over the `CMAKE_ARGS` environment variable.

**`ImportError: DLL load failed while importing _shoonyakasha`**
A vcpkg dependency built as a DLL that isn't beside the `.pyd`. Rebuild with
`VCPKG_TARGET_TRIPLET=x64-windows-static-md`.

**`pip` and `python` disagree about the venv**
The launcher `.exe` files in `Scripts/` hard-code an absolute interpreter path, so
a venv breaks if its directory is renamed, copied, or moved. Compare `pip -V` with
`python -m pip -V`; if they differ, delete and recreate the venv. Using
`python -m pip` throughout avoids the class of problem entirely.

**Configure succeeds but examples can't find shaders or models**
Run them from their own source directory, not from the build tree.

**vcpkg rebuilds everything after switching triplets**
Expected — the binary cache is keyed per triplet. The Windows engine build
(`x64-windows`) and the Python build (`x64-windows-static-md`) maintain separate
sets. Both are cached after their first build.
