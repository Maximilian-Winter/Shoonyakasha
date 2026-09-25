# Lighting and image-based lighting

The facade creates directional and point lights; native components also represent spot lights. Shaders decide how those values contribute to an image.

```python
# Inside init:
engine.create_directional_light((-0.5, -1.0, -0.3), intensity=3.0)
engine.create_point_light((2.0, 3.0, 1.0), intensity=5.0, range=15.0)
```

C++ equivalents are `createDirectionalLight` and `createPointLight` with GLM vectors. A directional light's direction is the way its light travels, so a sun shining down has a negative y. Scene setters modify type, color, intensity, range, and shadow flags. A shadow flag does not create a shadow-map pipeline.

## Sun shadow cascades

When a directional light has its shadow flag set, the engine fits cascaded shadow maps for it to the camera every frame. The first such light is the sun:

```python
sun = engine.create_directional_light((-0.45, -0.55, 0.7), intensity=3.0)
engine.scene.set_light_cast_shadows(sun, True)
engine.set_sun_shadows(cascades=4, max_distance=60.0, split_lambda=0.75, resolution=2048)
```

The cascades split the view up to `max_distance` (capped at the camera's far plane), between evenly spaced (`split_lambda` 0) and logarithmic (1). Each is fitted with a bounding sphere of its slice of the view, so turning the camera keeps the shadow map's texel size, and snapped so moving the camera shifts the map by whole texels; both stop shadow edges from shimmering. `resolution` is the shadow map's size in texels and should match it. Casters up to `caster_extension` (default 50) beyond a cascade towards the light still land in it; enable `depthClamp` on the shadow pass for casters farther than that.

A pipeline reads the result through dot-paths:

| Path | Type | Value |
|---|---|---|
| `scene.shadows.sun.cascades[N].viewProj` | mat4 | World to light clip space for cascade N, depth 0..1; use `[i]` in an array field |
| `scene.shadows.sun.splits` | vec4 | View depth at which each cascade ends |
| `scene.shadows.sun.texelWorldSize` | vec4 | World size of a shadow texel per cascade, for normal-offset bias |
| `scene.shadows.sun.cascadeCount` | uint | Cascades in use, 0 when no light casts |
| `scene.shadows.sun.enabled` | uint | 1 when a sun casts shadows |
| `scene.shadows.sun.lightIndex` | int | The sun's index in `scene.lights`, -1 when none |
| `scene.shadows.sun.direction` | vec4 | Direction the sun's light travels |

A shadow pass declared with `"repeat": { "count": 4, "index": "cascade" }` writes one cascade per layer of a `2d_array` depth image and selects its matrix with a `pass.repeatIndex` push constant; the lighting shader compares view depth against `splits` to choose a cascade. See [repeated passes](../reference/pipeline-json.md#repeated-passes). `get_sun_shadow_cascade(i)` returns a cascade's matrix for debugging.

## IBL setup

For image-based lighting, configure `hdr_environment_path` / `hdrEnvironmentPath` and use a pipeline with environment bindings. The engine generates irradiance, prefiltered environment, and BRDF lookup textures. The [PBR demo](../../examples/python/getting_started/demo/demo.py) and [deferred pipeline](../../examples/cpp/rendering/declarative_sponza_test/pbr_ibl_pipeline_v3.json) are complete examples.

Dot-paths include `scene.environment.irradianceMap`, `prefilterMap`, `brdfLUT`, and `environmentMap`. These resources require a loaded environment; an empty HDR configuration is suitable only when the pipeline does not depend on them.

The simple starter has analytic forward shading and does not consume every scene light/IBL property. Use the PBR example's shader/layout combination when learning lighting data bindings. Environment assets and their optional full-resolution versions are listed in the [asset guide](../../assets/README.md).

References: [IBL generator](../api/cpp/ibl-generator.md), [JSON reference](../reference/pipeline-json.md), [materials](materials.md).
