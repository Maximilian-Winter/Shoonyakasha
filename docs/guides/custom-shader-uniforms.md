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

Supported facade setters are float, vec2, vec3, vec4, and uint. The key omits `scene.custom.`; embedded dots remain part of the key.

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

Graph parameters are a separate string→unsigned-integer configuration mechanism for buffer sizing and dispatch. Updating a custom uint does not automatically reallocate an SSBO. Python script component fields are opaque and are not resolved by arbitrary dot-paths.

See the [pipeline walkthrough](json-render-pipeline.md), [JSON reference](../reference/pipeline-json.md), and [Engine reference](../api/python/engine.md).
