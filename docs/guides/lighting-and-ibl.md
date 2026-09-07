# Lighting and image-based lighting

The facade creates directional and point lights; native components also represent spot lights. Shaders decide how those values contribute to an image.

```python
# Inside init:
engine.create_directional_light((-0.5, -1.0, -0.3), intensity=3.0)
engine.create_point_light((2.0, 3.0, 1.0), intensity=5.0, range=15.0)
```

C++ equivalents are `createDirectionalLight` and `createPointLight` with GLM vectors. Scene setters modify type, color, intensity, range, and shadow flags. A shadow flag does not create a shadow-map pipeline.

## IBL setup

For image-based lighting, configure `hdr_environment_path` / `hdrEnvironmentPath` and use a pipeline with environment bindings. The engine generates irradiance, prefiltered environment, and BRDF lookup textures. The [PBR demo](../../examples/python/getting_started/demo/demo.py) and [deferred pipeline](../../examples/cpp/rendering/declarative_sponza_test/pbr_ibl_pipeline_v3.json) are complete examples.

Dot-paths include `scene.environment.irradianceMap`, `prefilterMap`, `brdfLUT`, and `environmentMap`. These resources require a loaded environment; an empty HDR configuration is suitable only when the pipeline does not depend on them.

The simple starter has analytic forward shading and does not consume every scene light/IBL property. Use the PBR example's shader/layout combination when learning lighting data bindings. Environment assets and their optional full-resolution versions are listed in the [asset guide](../../assets/README.md).

References: [IBL generator](../api/cpp/ibl-generator.md), [JSON reference](../reference/pipeline-json.md), [materials](materials.md).
