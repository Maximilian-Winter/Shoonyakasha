# FAQ and troubleshooting

## Where should I start?

Use the [Python starter](getting-started/python-quickstart.md) or [C++ FacadeTest](getting-started/cpp-quickstart.md). Build/install prerequisites live in [BUILDING.md](../BUILDING.md).

## Does JSON replace shaders and C++?

It configures resources, passes, supported execution types, and bindings. You still compile GLSL to SPIR-V. New native execution behavior or unsupported data sources may require C++. Restart to apply file edits; automatic live reload is not a documented facade feature.

## Why does importing the package work but Engine fails?

Utilities import without the extension. Check `sk.extension_available()` and `sk.Engine`; build the extension for the active interpreter. On Windows, follow the build guide's static-md triplet instructions. Do not assume a `.pyd` built for another Python version is usable.

## Why are pipeline or shader files missing?

Run from the example source directory. The asset-root resolver handles models/fonts/textures/environments, not arbitrary shader paths. Compile GLSL through CMake for C++ examples or `sk.shaders.compile_dir` for Python.

## Why is my scene black or empty?

Check the log, glTF `result.success`, camera position/clip range, required environment resources, matching shader/vertex/descriptor layouts, and final present output. Confirm the pass execution type matches the geometry (ordinary, skinned, or sprite). The [pipeline guide](guides/json-render-pipeline.md) explains validation.

## Does validator success guarantee rendering?

No. The validator checks selected declarations and files, not all C++ parser cases, shader interfaces, or Vulkan constraints. `pipeline.check` raises on errors and returns a diagnostic list on success. See [differences](reference/pipeline-json.md#validation-and-export).

## Are all engine features exposed in Python?

No. Typed collider settings, raw registry/graph access, loader root/node result lists, and the validation config switch are examples of native-only surfaces. Use the public references instead of translating native names into guessed Python calls.

## Can I save and restore a complete scene?

Not through the current component snapshot API. It clears entities on load and omits GPU/gameplay resources; it also has known write-check limitations. See [serialization](guides/scene-serialization.md).

## Why does text remain after destroying its entity?

Labels generate separate glyph entities. Hide the label with `set_text_visible` before destruction. See [text lifetime](guides/sprites-ui-text.md#text-limitations-and-lifetime).

## Why does recording slow rendering down?

It synchronously reads back presented frames and pipes them to ffmpeg. Check capture return values and logs; see [capture](guides/frame-capture.md).

## Which platforms are verified?

CI builds and runs headless Windows/MSVC tests. Linux instructions are provided, but that CI does not verify Linux or macOS rendering. Headless tests also do not establish Vulkan runtime correctness.
