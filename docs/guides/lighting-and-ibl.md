# Lighting and image-based lighting

The facade creates directional and point lights; native components also represent spot lights. Shaders decide how those values contribute to an image.

```python
# Inside init:
engine.create_directional_light((-0.5, -1.0, -0.3), intensity=3.0)
engine.create_point_light((2.0, 3.0, 1.0), intensity=5.0, range=15.0)
```

The engine publishes up to 128 lights as `scene.lights[N]`; a pipeline whose arrays are shorter reads the first ones. C++ equivalents are `createDirectionalLight` and `createPointLight` with GLM vectors. A directional light's direction is the way its light travels, so a sun shining down has a negative y. Scene setters modify type, color, intensity, range, and shadow flags. A shadow flag does not create a shadow-map pipeline; the pipeline has to read the cascades.

## The default pipeline

`sk.Engine()` without a `pipeline_json_path` (C++: an empty `pipelineJsonPath`) renders with the [default pipeline](../../python/shoonyakasha/pipelines/default/README.md): deferred PBR with every scene light, image-based light, cascaded sun shadows with filtering and contact shadows, alpha-tested and skinned casters, ambient occlusion, forward-shaded blended materials, bloom and automatic exposure. It is also the starting point for a pipeline of your own: copy its directory and point `pipeline_json_path` at the copy. `shoonyakasha.pipeline.DEFAULT` is its path. From C++ it is found through `$SHOONYAKASHA_DEFAULT_PIPELINE`, or else in the source tree the engine was built from.

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

## Spot and point light shadows

Spot and point lights with the shadow flag set compete for shadow slots, a fixed number per type that the pipeline's shadow maps provide. Every frame the engine drops the lights whose range cannot reach the camera's view, ranks the rest by intensity and nearness, and gives the slots out in that order:

```python
lamp = engine.create_point_light((0.3, 1.2, 0.0), intensity=12.0, range=10.0)
engine.scene.set_light_cast_shadows(lamp, True)
engine.set_local_shadows(spot=8, point=4, spot_resolution=2048, point_resolution=1024, atlas_resolution=4096)
```

The counts must match the pipeline's passes; `set_local_shadows` defaults to the default pipeline's (C++ `setLocalShadowSettings`). With `atlas_resolution` above 0 every map is a square tile of one atlas image: each frame a light's tile is sized by how large its range looks from the camera, a power of two up to `spot_resolution` (`point_resolution` for each cube face) and down to 128, halved for the least important lights when they would not all fit, and packed without gaps. With 0 each slot is an array layer of exactly those sizes. A spot light's shadow is a perspective map covering its cone; a point light's is a cube map rendered one face at a time. Each face's matrix follows the cube-map face selection of the Vulkan specification, so a pass rendering face F writes exactly the texels a cube lookup along the same direction reads, and the depth it stores is the usual 0..1 perspective depth of the distance along the face's major axis. A shader recomputes it from the light-to-point vector `r` as `depthParams.x + depthParams.y / max(|r.x|, |r.y|, |r.z|)`.

| Path | Type | Value |
|---|---|---|
| `scene.shadows.spot.count`, `scene.shadows.point.count` | uint | Slots in use; they come first |
| `scene.shadows.spot[N].viewProj` | mat4 | World to clip for spot slot N, depth 0..1 |
| `scene.shadows.spot[N].lightIndex`, `scene.shadows.point[N].lightIndex` | int | The light's index in `scene.lights`, -1 for an empty slot |
| `scene.shadows.spot[N].params` | vec4 | x: world size of a texel one unit from the light; y, z: near and far |
| `scene.shadows.point[N].positionFar` | vec4 | Light position and far plane |
| `scene.shadows.point[N].depthParams` | vec4 | x, y: depth from the major-axis distance as above; z: texel size one unit away; w: near |
| `scene.shadows.point.faces[N].viewProj` | mat4 | Face N % 6 (+X, -X, +Y, -Y, +Z, -Z) of point slot N / 6 |
| `scene.shadows.spot[N].rect`, `scene.shadows.point.faces[N].rect` | vec4 | The map's tile in the atlas: x, y, width, height in fractions of it. (0, 0, 1, 1) without an atlas; all 0 for an empty slot |

Passes render a slot with `"view": "shadows.spot[{s}]"` or `"view": "shadows.point.faces[{f}]"` in a repeated pass. With an atlas they also name the tile, `"viewport": "scene.shadows.spot[{s}].rect"`, and write the atlas image; a shader reads face F's tile through that face's `viewProj`. Without one they write layer `{s}` of a `2d_array` or layer `{f}` of a `cube_array` image. A slot no light has this frame draws nothing. The [default pipeline](../../python/shoonyakasha/pipelines/default/README.md) is a complete example.

## Ray-traced sun shadows

On a device with ray queries (`VK_KHR_ray_query` and `VK_KHR_acceleration_structure`, which the engine turns on when present), the engine keeps an acceleration structure of the scene for shaders to trace against: a bottom-level structure per mesh, built the first time the mesh is drawn, and a top-level one rebuilt every frame. It holds the static, opaque, shadow-casting meshes; skinned and alpha-tested meshes are left out. Set `SHOONYAKASHA_DISABLE_RAY_QUERY=1` to run without it.

The default pipeline's `raytraced` preset uses it for the sun: one ray per pixel to a random point on the sun's disc, averaged over frames by TAA, so shadows are sharp at contact and soften with distance. The meshes left out of the acceleration structure still cast through the cascades.

```python
if engine.ray_query_supported():
    engine.apply_pipeline_preset("raytraced")
engine.set_custom_float("default.sunAngle", 0.27)   # the real sun's angular radius, in degrees
```

A pipeline of your own traces the same structure through an `acceleration_structure` descriptor, in a pass that declares `"requires": ["rayQuery"]`; see the [pipeline JSON reference](../reference/pipeline-json.md#descriptors-and-samplers).

## IBL setup

For image-based lighting, configure `hdr_environment_path` / `hdrEnvironmentPath` and use a pipeline with environment bindings. The engine generates irradiance, prefiltered environment, and BRDF lookup textures. The [PBR demo](../../examples/python/getting_started/demo/demo.py) and [deferred pipeline](../../examples/cpp/rendering/declarative_sponza_test/pbr_ibl_pipeline_v3.json) are complete examples.

Dot-paths include `scene.environment.irradianceMap`, `prefilterMap`, `brdfLUT`, and `environmentMap`. Without an HDR map, or when it fails to load, a pipeline that declares an `iblSet` gets a uniform environment instead of unbound textures: every direction is `environment_color` (C++ `uniformEnvironmentColor`), default (0.25, 0.28, 0.33). The IBL compute shaders are looked for in `shaders/ibl/` under the working directory, then beside the pipeline JSON; the default pipeline ships a copy.

The simple starter has analytic forward shading and does not consume every scene light/IBL property. Use the PBR example's shader/layout combination when learning lighting data bindings. Environment assets and their optional full-resolution versions are listed in the [asset guide](../../assets/README.md).

References: [IBL generator](../api/cpp/ibl-generator.md), [JSON reference](../reference/pipeline-json.md), [materials](materials.md).
