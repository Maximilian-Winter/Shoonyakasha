# Custom shader uniforms

Publish application values under `scene.custom.<key>` and reference them from JSON buffer fields. Use init for initial values and update for per-frame changes before automatic buffer upload.

```python
engine.set_custom_float("effect.exposure", 1.0)
engine.set_custom_vec4("effect.tint", (1.0, 0.8, 0.6, 1.0))
```

```cpp
engine.setCustomFloat("effect.exposure", 1.f);
engine.setCustomVec4("effect.tint", glm::vec4(1.f, 0.8f, 0.6f, 1.f));
```

Supported facade setters are float, vec2, vec3, vec4, mat4, and uint, each also for [arrays](#arrays); [`set_buffer`](#whole-buffers) writes a whole buffer's bytes. The key omits `scene.custom.`; embedded dots remain part of the key. From Python, `set_custom_mat4` takes four columns of four floats, the column-major shape `Scene.get_world_matrix` returns; a `mat4` buffer field receives it unchanged.

## Pipeline fragment

Merge this into the pipeline's `bufferLayouts` object:

```json
{
  "EffectUBO": {
    "usage": "uniform_buffer",
    "packing": "std140",
    "updateFrequency": "per_frame",
    "fields": [
      { "name": "tint", "type": "vec4", "source": "scene.custom.effect.tint" },
      { "name": "exposure", "type": "float", "source": "scene.custom.effect.exposure" }
    ]
  }
}
```

Reference `EffectUBO` through a descriptor binding's `autoBindBuffer`, include that descriptor set in the pass, and declare matching GLSL members/packing. JSON does not modify the shader's layout. Initialize all used values rather than relying on missing-path fallback behavior.

## Arrays

A field with `arrayCount` whose source ends in `[i]` reads one value per element: `scene.custom.beams.color[i]` reads the custom values `beams.color[0]`, `beams.color[1]` and so on. The array setters set them all from a sequence:

```python
engine.set_custom_vec4_array("beams.color", [(1, 0, 0, 1), (0, 1, 0, 1)])
engine.set_custom_uint("beams.count", 2)
```

```json
{ "name": "color", "type": "vec4", "arrayCount": 16, "source": "scene.custom.beams.color[i]" },
{ "name": "count", "type": "uint", "source": "scene.custom.beams.count" }
```

There is one setter per type: `set_custom_float_array`, `set_custom_vec2_array`, `set_custom_vec3_array`, `set_custom_vec4_array`, `set_custom_mat4_array` and `set_custom_uint_array` (C++ `setCustomFloatArray` and so on, taking a `std::vector`). Setting a shorter array removes the elements past its end, and the field's elements without a value read as its `default`, or zero. The setters do not set a count; publish one alongside, as above.

## Whole buffers

`set_buffer(name, data, offset=0)` writes raw bytes into the buffer of a `bufferLayouts` entry: a uniform buffer, or a storage buffer with an `elementCount`. `data` is anything with contiguous memory: `bytes`, `bytearray`, `array.array` or a NumPy array, whose bytes are written as they are, at byte `offset`. Lay them out as the buffer's `packing` places its fields.

```python
import numpy as np

lasers = np.zeros((16, 2, 4), dtype=np.float32)        # 16 elements of two vec4 fields
lasers[:4, 0] = origins                                  # field "origin"
lasers[:4, 1] = colors                                   # field "color"
engine.set_buffer("Lasers", lasers)                      # a storage buffer with "elementCount": 16
engine.set_buffer("Lasers", np.float32([0, 1, 0, 1]), offset=1 * 32 + 16)   # element 1's color
```

The engine keeps the bytes until they are set again, also when the pipeline is compiled again. A uniform buffer gets them in every frame's copy, after the fields with a `source` are filled, so set bytes take the place of those fields. A storage buffer gets the bytes that changed copied in at the start of the next frame, before its passes; bytes never set keep the values of its `source` initialisation, or zero. A uniform layout whose fields have no `source` gets a buffer for this, unless the application registered one of that name. `set_buffer` may be called from `on_init`, before the pipeline is loaded; the bytes are checked against it then, and dropped with a warning if they do not fit. Afterwards a name without such a buffer, or bytes past its end, raise `ValueError`. In C++, `EngineAPI::setBuffer(name, data, size, offset)` returns the message instead, empty on success.

Graph parameters are a separate string→unsigned-integer configuration mechanism for buffer sizing and dispatch. Updating a custom uint does not automatically reallocate an SSBO. Python script component fields are opaque and are not resolved by arbitrary dot-paths.

See the [pipeline walkthrough](json-render-pipeline.md), [JSON reference](../reference/pipeline-json.md), and [Engine reference](../api/python/engine.md).
