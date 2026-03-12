# Custom Shader Uniforms

How to pass runtime values to shaders without modifying C++ rendering code.
Custom uniforms let you drive dynamic visual effects -- particle simulations,
post-processing tuning, procedural animations -- entirely from Python or
gameplay code.

> **Prerequisite:** This guide assumes familiarity with the
> [JSON Render Pipeline](json-render-pipeline.md) and its dot-path system.

---

## Purpose

Shaders often need parameters that change at runtime: gravity strength, wind
direction, exposure level, color grading values. Normally wiring new data into a
GPU pipeline means editing C++ buffer-binding code. Shoonyakasha's custom
uniform system bypasses that entirely:

1. **You set a named value** from Python (or C++).
2. **The JSON pipeline references it** through a `scene.custom.<key>` dot-path.
3. **The render graph fills it in** automatically each frame.

No recompilation, no new C++ code -- just a key/value pair and a matching
dot-path in your pipeline JSON.

---

## API

### Python

```python
engine.set_custom_float("key", value)
engine.set_custom_vec2("key", (x, y))
engine.set_custom_vec3("key", (x, y, z))
engine.set_custom_vec4("key", (x, y, z, w))
engine.set_custom_uint("key", value)
```

### C++

```cpp
engine.setCustomFloat("key", value);
engine.setCustomVec2("key", glm::vec2(x, y));
engine.setCustomVec3("key", glm::vec3(x, y, z));
engine.setCustomVec4("key", glm::vec4(x, y, z, w));
engine.setCustomUint("key", value);
```

---

## Supported Types

| Type | Python setter | C++ setter | GLSL type |
|------|--------------|------------|-----------|
| `float` | `set_custom_float` | `setCustomFloat` | `float` |
| `vec2` | `set_custom_vec2` | `setCustomVec2` | `vec2` |
| `vec3` | `set_custom_vec3` | `setCustomVec3` | `vec3` |
| `vec4` | `set_custom_vec4` | `setCustomVec4` | `vec4` |
| `uint` | `set_custom_uint` | `setCustomUint` | `uint` |

---

## Accessing in the JSON Pipeline

Reference custom values in buffer layout fields using the dot-path prefix
`scene.custom.`, followed by the key you set in code. The key's dots become
part of the dot-path hierarchy:

```json
{
  "name": "ParticleParams",
  "type": "uniform",
  "fields": [
    { "name": "deltaTime",      "type": "float", "source": "scene.time.delta" },
    { "name": "gravity",        "type": "float", "source": "scene.custom.particles.gravity" },
    { "name": "particleCount",  "type": "uint",  "source": "scene.custom.particles.count" },
    { "name": "boundaryRadius", "type": "float", "source": "scene.custom.particles.boundaryRadius" },
    { "name": "attractorPos",   "type": "vec4",  "source": "scene.custom.particles.attractorPos" },
    { "name": "wind",           "type": "vec4",  "source": "scene.custom.particles.wind" },
    { "name": "damping",        "type": "float", "source": "scene.custom.particles.damping" }
  ]
}
```

The render graph resolves each `source` at bind time. If a custom value has not
been set yet, the field receives a zero-initialized default.

---

## Naming Convention

Use **dot-separated names** to organize related values into logical groups:

| Key | Purpose |
|-----|---------|
| `"particles.gravity"` | Particle simulation gravity |
| `"particles.damping"` | Particle velocity damping |
| `"particles.wind"` | Wind force vector |
| `"postprocess.exposure"` | HDR exposure value |
| `"postprocess.gamma"` | Display gamma |
| `"debug.wireframe"` | Debug visualization toggle |

The dots in the key name map directly to the dot-path hierarchy in JSON. Setting
key `"particles.gravity"` makes it available as `scene.custom.particles.gravity`
in the pipeline definition.

---

## Example: Driving a Particle Compute Shader

This example from `demo.py` shows the typical pattern. Static parameters are set
once in `on_init`, while dynamic values are updated every frame in
`on_pre_render`:

```python
import math
import shoonyakasha as sk

engine = sk.Engine()
PARTICLE_COUNT = 65536
particle_time = 0.0

def on_init():
    engine.load_gltf_scene("assets/sponza/Sponza.gltf")

    # Static parameters -- set once
    engine.set_custom_float("particles.gravity", 1.5)
    engine.set_custom_uint("particles.count", PARTICLE_COUNT)
    engine.set_custom_float("particles.boundaryRadius", 15.0)
    engine.set_custom_float("particles.damping", 0.998)
    engine.set_custom_float("particles.spawnHeight", 0.5)

    # Attractor position (xyz) + strength (w)
    engine.set_custom_vec4("particles.attractorPos", (0.0, 5.0, 0.0, 25.0))

def on_pre_render(dt):
    global particle_time
    particle_time += dt

    # Animated wind -- rotates over time
    wind_angle = particle_time * 0.2
    engine.set_custom_vec4("particles.wind", (
        math.sin(wind_angle) * 0.4,
        0.1,
        math.cos(wind_angle) * 0.4,
        0.3,
    ))

engine.set_on_init(on_init)
engine.set_on_pre_render(on_pre_render)
engine.run()
```

The matching compute shader receives these values through the uniform buffer
declared in the pipeline JSON. No C++ code changes were required -- the shader
reads `gravity`, `wind`, `damping`, etc. directly from the buffer layout shown
above.

---

## Per-Frame Updates

Custom uniforms are ideal for animation. Set them in `on_update` or
`on_pre_render` callbacks to create time-varying effects:

```python
def on_pre_render(dt):
    # Pulse exposure based on time
    t = engine.scene.elapsed_time
    engine.set_custom_float("postprocess.exposure", 1.0 + 0.3 * math.sin(t))
```

Values persist between frames. If a value does not change, you do not need to
re-set it every frame. Set static parameters once in `on_init` and only update
dynamic ones per frame.

---

## Tips

- **Pack related scalars into vec4** when you need to minimize uniform buffer
  size. For example, store `(posX, posY, posZ, strength)` in a single vec4
  instead of using three floats and a separate float.

- **Use `const.0` for padding fields** in your JSON layout when you need to
  maintain std140 alignment but have no data to fill a slot.

- **Zero-initialized defaults** -- if you reference a custom key in your JSON
  but never set it, the value will be zero. This is safe but may produce
  unexpected visual results, so set initial values in `on_init`.

---

## See Also

- [JSON Render Pipeline](json-render-pipeline.md) -- how dot-paths, buffer
  layouts, and the render graph work together
- [Engine API (Python)](../api/python/engine.md#custom-uniform-values) --
  full method signatures and parameter details
- [Engine API (C++)](../api/cpp/engine-api.md#custom-uniform-values) -- C++
  equivalents
