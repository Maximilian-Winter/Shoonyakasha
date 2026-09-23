# Python utilities

The package's `assets`, `keys`, `pipeline`, and `shaders` modules work without the native extension. `shoonyakasha.extension_available()` returns whether the extension loaded. Importing the package alone is not an engine installation test.

## Shader compilation

| Function | Result / behavior |
|---|---|
| `shaders.find_glslc(hint=None)` | Locate compiler through hint, SDK, PATH, and known SDK locations; raises `GlslcNotFound` if absent |
| `shaders.output_path_for(source)` | Default `.spv` output path |
| `shaders.include_dir()` | The shared GLSL library shipped in the package, passed to glslc as `-I` on every compile |
| `shaders.is_stale(source, output=None)` | Whether compilation is needed: the output is missing, or the source or any file it `#include`d (from the `.spv.d` file glslc writes beside the output) is newer |
| `shaders.compile(source, output=None, *, glslc=None, args=(), force=False, quiet=True)` | Compile one shader; returns its output Path |
| `shaders.compile_dir(directory, *, recursive=True, force=False, glslc=None, args=(), extensions=SHADER_EXTENSIONS, quiet=True)` | Returns output Paths actually rebuilt; empty when up to date |

Compilation errors raise `ShaderCompileError`; missing directories raise `FileNotFoundError`. Source: [shaders.py](../../../python/shoonyakasha/shaders.py).

### Shared GLSL library

Shaders compiled by `sk.shaders` or by CMake's `target_compile_shaders` can include the engine's GLSL library, which lives in [`python/shoonyakasha/glsl/sk/`](../../../python/shoonyakasha/glsl/sk) and ships in the wheel:

| Include | Contents |
|---|---|
| `sk/pbr.glsl` | `fresnelSchlick`, `fresnelSchlickRoughness`, `baseReflectivity`, `distributionGGX`, `geometrySmith`, `cookTorrance`, `SK_PI` |
| `sk/tonemap.glsl` | `ACESFilm`, `Reinhard`, `Uncharted2Tonemap`, `Uncharted2` |
| `sk/noise.glsl` | integer `hash`, value `noise`, five-octave `fbm` |
| `sk/shapes2d.glsl` | antialiased `fill`/`stroke`, premultiplied `over`, 2D distances (`sdBox`, `sdTriangle`, `sdVesica`, `sdPetal`, `sdEllipse`), `foldPolar`, `rotate`, `SK_TAU` |

```glsl
#include "sk/pbr.glsl"
```

Each header has an include guard, so including one twice is harmless. Editing a header rebuilds the shaders that include it, in both builds.

## Pipeline diagnostics

`pipeline.validate(path)` returns `Problem` objects; `validate_json(document, base_dir=None, source="<json>")` checks an already parsed object. A Problem has `where`, `message`, `severity`, `hint`, and `is_error`; printing it includes context.

`pipeline.check(path, warnings_are_errors=False)` raises `ValueError` for fatal diagnostics, otherwise returning the problem list. It is not a boolean success test. Missing files/JSON parse errors become diagnostics; some malformed shapes or I/O failures can still raise exceptions.

The validator checks a subset of native behavior. See [known differences](../../reference/pipeline-json.md#validation-and-export) and [pipeline.py](../../../python/shoonyakasha/pipeline.py). Exported vocabulary sets (`RESOURCE_USAGES`, `BUFFER_USAGES`, `PASS_TYPES`, `RESOURCE_KINDS`, `FIELD_TYPES`, `PACKING_RULES`) describe that validator, not a complete engine schema.

## Assets

| Function | Result / behavior |
|---|---|
| `assets.root(refresh=False)` | Asset-root Path or None; caches discovery |
| `assets.describe()` | Description of root discovery |
| `assets.locate(relative)` | Existing given path, asset-root path, or original Path if unresolved; None for empty input |
| `assets.exists(relative)` | Boolean existence check |
| `assets.fetch(*what)` | Runs repository `tools/fetch_assets.py`; returns success bool |

Set `SHOONYAKASHA_ASSET_DIR` for external projects. Fetch requires the repository tools directory; a standalone installed wheel does not contain it and may raise `FileNotFoundError`. Source: [assets.py](../../../python/shoonyakasha/assets.py), [asset catalog](../../../assets/README.md).

## Project generator and keys

`python -m shoonyakasha.init destination [--name NAME] [--force]` writes a starter. Python callers can import `create` from `shoonyakasha.init`: `create(destination, project_name=None, force=False)` returns a Path. Nonempty destinations are rejected unless force is selected; force overwrites template file destinations. Missing templates raise `FileNotFoundError`.

The generated program still needs the extension, glslc, a Vulkan driver/device, and shared assets. See [quickstart](../../getting-started/python-quickstart.md).

`keys.name(code)` returns a readable key name. Named key/button values are listed in [keys.py](../../../python/shoonyakasha/keys.py).
