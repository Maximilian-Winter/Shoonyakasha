# The Volume Tiled Forward Shading pipeline

A copy of the [default pipeline](../../../../../python/shoonyakasha/pipelines/default/README.md) in which Volume Tiled Forward Shading (van Oosten 2015) replaces the deferred lighting:
- a depth prepass in place of the G-buffer;
- compute passes that sort the lights into a BVH and list the lights of each cluster that visible pixels lie in;
- forward passes in place of the `Lighting` pass.

Shadows, ambient occlusion, TAA, bloom, exposure and tone mapping are the default pipeline's, and so are its settings (`scene.custom.default.*`) and presets. The [example](../README.md) explains the technique and measures it.

```python
sk.shaders.compile_dir("examples/python/rendering/volume_tiled/pipeline/shaders")    # before, once
engine = sk.Engine(pipeline_json_path="examples/python/rendering/volume_tiled/pipeline/pipeline.json")
```

## Passes

| Pass | Work |
|---|---|
| Shadow passes | As in the default pipeline |
| `DepthPrepassOpaque`, `…Masked`, `…Skinned`, `…SkinnedMasked` | Depth, the normal (`gNormal`) and the motion (`gVelocity`), for the screen-space passes and TAA (`prepass*.frag`) |
| `VtfsReset` | Clears the counters |
| `VtfsClusterAABBs` | Every cluster's view-space box (Algorithm 8.1) |
| `VtfsUpdateLights` | Places this frame's lights in world and view space |
| `VtfsReduceLights`, `VtfsReduceBounds` | The box around all lights, in two passes |
| `VtfsMorton` | 30-bit Morton codes in that box |
| `VtfsRadixSort` | Sorts chunks of 256 codes in shared memory, one bit at a time |
| `VtfsMergeSort{m}` | 12 Merge Path passes, runs of 256 to 1,048,576 |
| `VtfsBvhBottom`, `VtfsBvhTop{level}` | The 32-ary light BVH, lowest level first |
| `VtfsMark` | Flags the cluster of every pixel the prepass covered |
| `VtfsMarkTransparent` | Flags the clusters of blended fragments in front of the prepass depth |
| `VtfsFindUnique` | Gathers the flagged clusters into a list, clears the flags |
| `VtfsIndirectArgs` | The arguments that follow the number of active clusters: the assignment's group counts, one workgroup per cluster, and the box overlay's instance count |
| `VtfsAssignBvh` | Lights of each active cluster, through the BVH; `dispatch_indirect` |
| `VtfsAssignAll` | The same, testing every light; off unless the preset `all` is applied |
| `ShadowMask`, `GTAO`, `AODenoise` | As in the default pipeline, from the prepass |
| `Sky` | The sky where the prepass left the far plane, black elsewhere |
| `ForwardOpaque`, `…Masked`, `…Skinned`, `…SkinnedMasked` | Forward shading with depth test EQUAL against the prepass |
| `Transparent`, `TransparentRT` | Forward shading of blended materials, as in the default pipeline, plus the VTFS lights |
| `VtfsClusterBoxes` | Debug overlay, off unless enabled: the active clusters' boxes as lines, one instance per cluster through `draw_indirect` |
| `TAA` … `UIOverlay` | As in the default pipeline |

The prepass and the forward passes draw with the same vertex shaders, whose `gl_Position` is `invariant`, so the depth test EQUAL passes. The lights in the `Lights` block (the sun and up to 128 more) are shaded as in the default pipeline, but every pixel loops over all of them: the `LightClusters` pass is gone.

## Presets

Besides the default pipeline's `low`, `medium`, `high` and `raytraced`:
- **`bvh`** (the start): `VtfsAssignBvh` with the passes that build the BVH.
- **`all`**: `VtfsAssignAll`, with the BVH's passes off.

## Settings

`scene.custom.vtfs.*` (`set_custom_uint`, `set_custom_float`, `set_custom_vec4`), read through the `Vtfs` uniform block:

| Setting | Default | Meaning |
|---|---|---|
| `lightCount` (uint) | 4096 | Lights, up to 1,048,576 |
| `lightRange` | 1.5 | Each light's range in metres |
| `lightIntensity` | 1.0 | Each light's colour is multiplied by it |
| `boxMin`, `boxMax` (vec4) | (-10, 0, -5), (10, 8, 5) | The volume the lights fill |
| `motion` | 0.5 | How far, in metres, each light drifts around its place |
| `heatScale` | 64 | Lights at which the light view turns red |

The lights are made by `VtfsUpdateLights` from a hash of their index: each one's place in the box, colour and drift. They are not scene entities, and they cast no shadows.

`default.debugView` adds three views to the default pipeline's:
- **5:** lights per cluster, which replaces the default pipeline's view of its own clusters;
- **7:** each cluster in its own colour;
- **8:** the image lit by every VTFS light at every pixel, without clusters. It is tone mapped like the final image, so it can be compared with it.

## Buffers

Storage buffers in `bufferLayouts`, with capacities from `shaders/vtfs.glsl`. The two must agree, and so must the worst-case dispatch sizes in `pipeline.json`.

| Buffer | Elements | Holds |
|---|---|---|
| `VtfsCounters` | 1 | Active clusters, entries of the index list, overflow flags |
| `VtfsClusterFlags`, `VtfsUniqueClusters` | 524,288 | Flag per cluster; the list of flagged ones |
| `VtfsClusterAABBs` | 524,288 | Each cluster's view-space box |
| `VtfsLightGrid` | 524,288 | Each cluster's (offset, count) into the index list |
| `VtfsLightIndexList` | 8,388,608 | The clusters' lights, one list after another |
| `VtfsLightsWorld`, `VtfsLightsColor`, `VtfsLightsView` | 1,048,576 | Each light's world position and range, colour, view position and range |
| `VtfsLightBounds` | 513 | 512 partial boxes, then the box of all lights |
| `VtfsKeysA/B`, `VtfsValuesA/B` | 1,048,576 | Morton codes and light indices, sorted from A into B and back, ending in A |
| `VtfsBvhNodes` | 33,825 | The BVH's inner nodes, level by level, root first |
| `VtfsIndirectArgs` | 1 | The light assignment's `dispatch_indirect` arguments at offset 0, the box overlay's `draw_indirect` arguments at 16 |

`VtfsAssignBvh` and `VtfsAssignAll` are dispatched indirectly, one workgroup per active cluster, from the counts `VtfsIndirectArgs` writes. The other passes over lights or clusters dispatch for the capacity, and their workgroups past this frame's count return at once; the sort and the BVH build always run their 12 and 3 passes, since the frame graph cannot repeat a pass a varying number of times.

## Changing it

The pipeline was made from the default one: the passes, buffers and sets above were added, and the G-buffer, `LightClusters` and `Lighting` passes removed. Edit `pipeline.json` and the shaders directly, as with any copy. Shaders recompile with `sk.shaders.compile_dir("shaders")`, and `sk.pipeline.check("pipeline.json")` validates the JSON without starting the engine.

**Shaders added over the default pipeline:**
- `vtfs.glsl`: the grid, capacities, Morton codes, BVH layout. `vtfs_compute.glsl` declares the compute passes' bindings, and `vtfs_lights.glsl` the forward passes' VTFS lights.
- `vtfs_*.comp`, `vtfs_mark_transparent.frag`: the passes above.
- `prepass_body.glsl`, `prepass.frag`, `prepass_masked.frag`, `sky.frag`, `forward_opaque.frag`, `forward_opaque_masked.frag`, `forward_skinned.vert`, `skinned_body.glsl`.

**Shaders changed:**
- `forward_body.glsl`: opaque and blended surfaces, the VTFS lights, debug views.
- `lights.glsl`: no clusters.
- `gbuffer.vert` and `gbuffer_skinned.vert`: invariant positions, the skinned one through `skinned_body.glsl`.
- `tonemap.frag`: view 8 is tone mapped.

**Removed:** `lighting.frag`, `light_clusters.comp`, `clusters.glsl`, `gbuffer.frag`, `gbuffer_masked.frag`, `gbuffer_body.glsl`.
