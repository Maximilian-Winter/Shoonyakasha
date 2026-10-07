# The showroom pipeline

A copy of the [default pipeline](../../../../../python/shoonyakasha/pipelines/default/README.md), made the way its guide describes [making it your own](../../../../../docs/guides/default-pipeline.md#making-it-your-own), with:

- **a virtual shadow map for the sun**, in place of the cascades: 8 clipmap levels of 4096² virtual texels around the camera, in pages of 128². Only the pages that visible pixels need are rendered, into an 8192² pool of 4096 physical pages, and a page keeps its depth from frame to frame until something invalidates it.
- **sphere lights**: point and spot lights with a source radius. Their highlights take the light's size, and where the device has ray queries, their shadows come from a ray per pixel towards a random point of the sphere, which TAA averages into a penumbra as wide as the light.
- **spot cones** with an inner and an outer angle.
- **ray-traced reflections** of what is on screen, in glossy surfaces.
- **an overlay pass**: screen-space panels and text over the finished image, as in [sprite_ui_test](../../../games_2d/sprite_ui_test).

Everything else is the default pipeline's, and its settings (`scene.custom.default.*`) work the same. The [showroom](../README.md) is its demo.

```python
engine = sk.Engine(pipeline_json_path="examples/python/rendering/showroom/pipeline/pipeline.json")
sk.shaders.compile_dir("examples/python/rendering/showroom/pipeline/shaders")    # before, once
```

## Engine features it needs

Both were added for it:

- **Light source data.** `LightComponent::sourceRadius` and the dot-path `scene.lights[N].source`: x the radius, y 1 when the light casts shadows, z the cosine of the inner cone. Python sets them with `scene.set_light_source_radius(light, r)` and `scene.set_light_cone(light, inner, outer)`, in degrees.
- **Fragment-stage atomics.** `fragmentStoresAndAtomics` is enabled where the device has it, which every desktop GPU does. The virtual shadow map's casters write their depth with atomics.

## Presets

| Preset | Sun | Spot and point lights | Needs ray queries |
|---|---|---|---|
| `hybrid` | Virtual shadow map | Ray traced, soft; ray-traced reflections | yes |
| `raytraced` | Ray traced (the map keeps only alpha-tested and skinned casters) | Ray traced, soft; ray-traced reflections | yes |
| `high` | Virtual shadow map | Shadow maps in the atlas | no |
| `medium` | Virtual shadow map, no contact shadows | Shadow maps | no |
| `cascades` | The default pipeline's cascades | Shadow maps | no |
| `low` | The default pipeline's `low` | Shadow maps, opaque casters only | no |

The pipeline starts as `high`. As in the default pipeline, ray tracing covers only static, opaque casters. Alpha-tested and skinned casters still go into the virtual shadow map (sun) or the atlas (lights), and the result is the product of both.

## Settings

`scene.custom.showroom.*` values, with defaults in `pipeline.json` (`engine.set_custom_float("showroom.vsmSoftness", 2.0)`):

| Setting | Default | Meaning |
|---|---|---|
| `vsmFirstLevel` | 4.0 | Width of the finest clipmap level in world units; each level doubles it. 4 m gives 1 mm texels |
| `vsmLevelBias` | 0.0 | Added to the level each pixel picks: 1 is half the resolution, -1 double |
| `vsmSoftness` | 1.5 | Filter radius in texels of the level read |
| `vsmNormalBias` | 1.0 | How far lookups move along the normal, in texels |
| `vsmDepthBias` | 0.002 | Constant bias in world units, on top of one texel's |
| `vsmDepthRange` | 200.0 | Casters and receivers must lie within this of the world origin along the sun's axis |
| `vsmDirty0`..`vsmDirty3` | (0,0,0,0) | Bounding spheres (xyz, radius) of moving shadow casters. The pages under them are drawn again each frame; w = 0 is unused (`set_custom_vec4`) |
| `vsmEpoch` | 0 | Change it to have every page drawn again, for instance after a scene change (`set_custom_uint`) |
| `vsmContent` | 0 | Set by presets that change which casters the map holds; a change invalidates every page |
| `sunShadowMode` | 1 | 1 the virtual shadow map, 0 the cascades; presets set it |
| `lightSoftness` | 1.0 | Scales every light's source radius in the shadow rays |
| `reflections` | 1.0 | Strength of ray-traced reflections; 0 turns them off |

`default.toneMapper` (`set_custom_uint`) picks the tone curve: 0 the default pipeline's ACES fit, 1 Khronos PBR Neutral, which keeps base colours as authored and only compresses highlights, and 2 AgX, which rolls very bright light off towards white without shifting its hue. The showroom starts on AgX: with auto-exposure aiming for mid-grey it keeps the mid-tones where they are, where PBR Neutral, made for exposures that put white at 1, comes out dark, and ACES adds strong contrast.

## Clear coat

Materials with `KHR_materials_clearcoat` get a clear lacquer layer over their base: a dielectric with its own roughness (`clearcoatFactor`, `clearcoatRoughnessFactor` material parameters), whose sharp reflection sits on top of the base's and whose Fresnel dims the base under it, for every light, the environment, and in the `raytraced` and `hybrid` presets the traced reflections, which follow the coat rather than the base. The coat's textures are not read, only its factors. The G-buffer's `gMaterial` carries metallic, roughness, coat and coat roughness. The showroom gives car body paint a coat when its file declares none.

Two more debug views join the default pipeline's (`default.debugView`):

- **1:** with the virtual shadow map on, shows its levels in colour, with page borders (and texel borders where texels are large).
- **7:** shows each pixel's page, red if it was drawn this frame and green if it was kept from earlier ones.

## The virtual shadow map

A frame runs four steps between the G-buffer and the shadow mask:

1. **`VSMMark`** (compute, per pixel): each pixel works out which level's texels best match its size on screen, and marks the page it will read there. It also marks the pages under the corners of its filter.
2. **`VSMAllocate`** (compute, one workgroup): updates the page table (`vsmPageTable`, 32 × 32 entries per level).
   - **Stale entries:** an entry whose slot now stands for a different page, because the camera moved the level's window, gives its physical page back. So does one whose contents are out of date.
   - **Requested pages:** a requested page that still has memory keeps it, and its depth. Pages without memory get a free physical page, or else one no pixel asked for this frame.
   - **Dirty pages:** new pages, and pages under a moving caster's bounding sphere, are marked dirty.
   - **Invalidation:** the whole map is invalidated when the sun turns, or when the level size, depth range, epoch or content change.
3. **`VSMClear`** (compute, one workgroup per physical page): resets dirty pages to the far depth.
4. **`VSMOpaque{level}`, `VSMMasked{level}`, `VSMSkinned{level}`** (8 repeats each): draw the casters into each level's 4096² window.
   - **Writing depth:** the fragment shader looks its texel's page up. If the page is dirty, it writes depth into its physical texel with `imageAtomicMin`. There is no depth buffer: positive floats order the same as their bits. A small 16-bit depth attachment is bound only because the engine records draws inside a rendering scope.

**Sampling:** `ShadowMaskVSM` (and the blended materials' forward pass) reads 12 Vogel-disc taps from the pool through the page table, turned per pixel and averaged by TAA. Contact shadows are added as in the default pipeline.

**Addressing:** pages are addressed by their absolute position in light space and wrapped into the table, so a moving camera only brings in the pages that come into view. Depth is absolute along the sun's axis, so it does not change as the camera moves either.

**Limitations**, from what the engine's render graph offers today:

- **No indirect draws.** Every level draws every caster. A level with nothing to redraw makes its vertex shader place all vertices outside the view, so no triangles are rasterised, but the vertex work remains. With indirect draws, or conditional rendering, a level with no dirty pages would skip its draws entirely, and casters could be culled per page.
- **Allocation budget.** At most 2048 pages are newly backed per frame, and the pool holds 4096 (`vsmPhysical`, 256 MB). Pages over budget fall back to a coarser level that has the page, or to no shadow.
- **Moving casters must be declared.** The map cannot know what moved: the application gives their bounding spheres (`vsmDirty0..3`), or bumps `vsmEpoch`. A skinned character walking through the scene needs a sphere too, or its shadow lags.
- **Depth range.** `vsmDepthRange` bounds the world along the sun's axis. Larger is coarser, with 32-bit floats to spend.

## Ray-traced lights and reflections

`LightingRT` replaces `Lighting` in the `hybrid` and `raytraced` presets.

- **Light shadows:** for each spot or point light whose shadow flag is set, it traces one ray towards a random point of the light's sphere.
- **Reflections:** it traces one reflection ray, spread by roughness, from every surface smoother than 0.55.
  - **Visible hit:** when the hit point is on screen and not hidden there, last frame's image (`taaHistory`) at that point is the reflected light.
  - **Hidden hit:** otherwise something blocks the reflection that the screen cannot show, such as a car's underside seen in a glossy floor, and the environment's reflection is darkened there.
  - **No hit:** rays that hit nothing keep the environment.

Like the shadow rays, the acceleration structure holds only static, opaque, shadow-casting meshes, so anything with its shadow flag off is invisible to reflections too.

**Blended materials:** `TransparentRT` traces a ray to the centre of each shadowed light, a hard shadow, since TAA cannot average a surface it sees through. It does not trace reflections.

## Changing it

The pipeline is generated from the default one plus the additions above. Edit `pipeline.json` and the shaders directly, as with any copy. Shaders recompile with `sk.shaders.compile_dir("shaders")`, and `sk.pipeline.check("pipeline.json")` validates the JSON without starting the engine.

**Passes and shaders added over the default pipeline:**

- `VSMMark`, `VSMAllocate`, `VSMClear`, `VSM*{level}` and `ShadowMaskVSM`, in `vsm*.comp`, `vsm*.vert`, `vsm*.frag`, `vsm.glsl` and `vsm_sample.glsl`
- `LightingRT`, in `lighting_rt.frag`, `lighting_body.glsl` and `rt_lights.glsl`
- `Overlay`, in `overlay.vert` and `overlay.frag`

**Shaders changed:** `lights.glsl` (sphere lights, inner cones), `forward_body.glsl`, `shadow_rt.frag` and `common.glsl`.
