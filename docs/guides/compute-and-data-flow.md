# Compute and GPU data flow

Use [ParticleTest](../../examples/cpp/compute/particle_test) for a small compute/render pipeline, [ParticleFlowExample](../../examples/cpp/compute/particle_flow_example) for a larger simulation, and [SSBODataFlowExample](../../examples/cpp/compute/ssbo_data_flow_example) for initialization, sharing, and readback. Build instructions and target names are in the [C++ catalog](../examples/cpp-examples.md).

## Connect simulation and rendering

1. Define an SSBO element layout and element count under `bufferLayouts`, usually with `std430` packing.
2. Initialize it through a layout-level `source` or provide a native buffer. Declare/import the graph resource and bind it through descriptor layouts.
3. Give the compute pass a compiled compute shader and `compute_dispatch` execution with group counts matching its local size.
4. Declare shader read/write accesses in the producer and shader-read accesses in consumers so dependency/barrier analysis sees the data flow.
5. Keep allocation count, draw/dispatch parameters, and shader-side count consistent. Updating a custom uniform alone does not resize the storage buffer.

## Buffer synchronization

Storage buffers rarely appear in a pass's inputs and outputs, so the compiler finds them through descriptor sets: a `uniform_buffer` or `storage_buffer` binding with `autoBindBuffer` names the buffer, and the pass's SPIR-V says which stages declare it and whether any may write it. Declare a buffer a shader only reads as `readonly buffer`; otherwise the pass counts as writing it, and the next pass to use the buffer waits for it. From that, each pass gets at most one memory barrier, placed only where a pass reads a buffer an earlier pass wrote, or writes one an earlier pass is still using, including the previous frame's last user. Uniform buffers the host fills need none.

Some passes record through code the graph cannot see: those with an execute callback, execution type `none`, `manual` or `scene_geometry`, the entity geometry types (their renderers bind vertex and index buffers), and those with a buffer binding without `autoBindBuffer`. They are taken to touch every buffer, graphics passes reading and compute passes reading and writing, so a buffer a compute pass writes is still visible to a renderer that binds it as vertex data. Give buffer bindings an `autoBindBuffer` where you can, so unrelated passes do not wait on each other.

For a side-effect-only compute pass, set `hasSideEffects: true` if otherwise unused outputs would allow culling. `queue: compute` runs a pass on the compute queue where the device has one without graphics; see [`queue`](../reference/pipeline-json.md#passes-and-pipeline-state).

## Indirect dispatches and draws

A compute pass can decide how much work a later pass does: it writes the arguments of a `dispatch_indirect` or `draw_indirect` pass into a storage buffer (see the [reference](../reference/pipeline-json.md#execution)). Bind that buffer to the writer with `autoBindBuffer` and without `readonly` in its shader, so the graph knows who writes it, and declare the writer first. The indirect pass then depends on it, waits for its writes, and keeps it from being culled.

A one-thread pass after the one that counts the work is the usual way to fill in the arguments, for instance a group count rounded up from an item count. The [Volume Tiled Forward Shading example](../../examples/python/rendering/volume_tiled/pipeline/README.md) dispatches one workgroup per active cluster that way.

## Per-pass control from Python

Python code runs between frames: in the `set_on_update`, `set_on_pre_render` and `set_on_post_render` callbacks and in ECS systems, not while a pass records. What it sets there reaches the passes of the next frame:

- **Which passes run:** `engine.set_pass_enabled(name, enabled)` turns a pass, or every instance of a repeated pass, on or off from the next frame.
- **Values:** `set_custom_*` values reach every pass whose layouts read them, uploaded once per frame. Values that differ from one pass to the next come from push constants bound to `pass.repeatIndex`, `pass.repeatCount`, `pass.extent` or `pass.texelSize`; a repeated pass can index a custom array with its repeat index.
- **Results:** `engine.request_readback(name)` and `engine.get_readback(name)` read a buffer or image a pass wrote (see [Reading back from Python](#reading-back-from-python)); `engine.get_pass_draw_stats(name)` gives a geometry pass's drawn and culled entities, and `engine.render_stats`, after `engine.enable_render_stats()`, each pass's CPU and GPU time.

## Initialization and sharing

[ssbo_pipeline.json](../../examples/cpp/compute/ssbo_data_flow_example/ssbo_pipeline.json) demonstrates seeded sphere/gaussian initialization and publishing a target named `particles.currentState`. A consumer can use a `buffer_ref` source referring to a target in `SharedBufferRegistry`. Native application code must arrange graph lifetimes, registration, and execution order.

Binary file sources load raw layout-compatible bytes; they are not a portable schema conversion. [ssbo_pipeline_from_file.json](../../examples/cpp/compute/ssbo_data_flow_example/ssbo_pipeline_from_file.json) illustrates the file source. Keep stride, field types, and count consistent with the file producer. A path ending in `.npy` loads the array data of a NumPy file, without its header; its data must be exactly the buffer's size, or the buffer starts as zeros, with a warning.

## Readback and saving

A target object can request periodic/manual readback and binary save. Image resources have corresponding target/readback/save policies for render-target capture. `StagingBufferManager` manages transfer staging and callbacks; the native example shows the callback and manual-trigger setup needed beyond JSON declarations.

### Reading back from Python

A buffer layout or image resource that declares a `readback` can be read from Python. `engine.request_readback(name)` copies it at the end of the next frame, whatever its declared frequency, and returns False when it declares no readback. `engine.get_readback(name)` returns the most recent copy, or None before the first:

```python
import numpy as np

def on_update(dt):
    r = engine.get_readback("Counter")          # a storage buffer, "frequency": "per_frame"
    if r:
        values = np.frombuffer(r["data"], dtype=np.uint32)

engine.request_readback("pattern")              # an image, "frequency": "manual"
r = engine.get_readback("pattern")              # a few frames later
if r:
    texels = np.frombuffer(r["data"], dtype=np.float32).reshape(r["height"], r["width"], 4)
```

The result is a dict: `data` (bytes), `frame` (the frame copied, counted from 0), and for an image `width`, `height` and `format` (its pipeline JSON name), for a buffer `element_count` and `element_stride`. A buffer's data is the whole buffer. An image's is mip 0, layer 0, as rows of tightly packed texels; a depth format gives its depth. A copy arrives once its frame's fence was waited on, as many frames later as there are frames in flight, and the result stays until a newer copy arrives or the pipeline is compiled again. Images whose format has no known texel size are not read back, with a warning. In C++, `EngineAPI::requestReadback` and `getReadback(name, Readback&)` do the same, and `RenderGraph::pollReadbacks` returns the copies completed when the frame began.

The frame-capture methods save the presented image, a separate facility. Do not assume an intermediate HDR target can be selected through `capture_screenshot`.

Readback frequency, memory placement, transfer direction, and buffer size affect correctness and cost. The parser accepting a policy does not prove a workflow has been exercised; use the matching example and GPU validation when changing it.

See [JSON policy reference](../reference/pipeline-json.md#initialization-memory-and-readback), [native frame graph](../api/cpp/frame-graph.md), and [capture](frame-capture.md).

## Running compute shaders directly

For precomputation (lookup tables, baked data) a compute context runs one shader on buffers and images and returns when the GPU is done, without a pipeline JSON or a frame loop. `sk.ComputeContext()` creates its own Vulkan device with no window; `engine.compute` is a context on a running engine's device, from `on_init` on.

```python
import struct
import numpy as np
import shoonyakasha as sk

with sk.ComputeContext() as ctx:
    kernel = ctx.kernel("square.comp")           # GLSL is compiled with glslc; a .spv is loaded as is
    values = np.linspace(-1, 1, 1000, dtype=np.float32)
    result = np.empty_like(values)
    ctx.run(kernel, [values, result], threads=values.size, push=struct.pack("<fI", 2.0, values.size))
    np.save("squares.npy", result)
```

A kernel's bindings, push constant size and workgroup size are read from its SPIR-V: `kernel.bindings` lists them as dicts of `binding`, `type` (`"storage"`, `"uniform"`, `"storage_image"` or `"sampled_image"`), `read_only` and `name`, ascending by binding number; images add `dim` (`"2d"`, `"3d"` or `"cube"`), `arrayed`, and `format`, a storage image's format qualifier as a pipeline JSON format name (None without one). Kernels may use storage buffers, uniform buffers, storage images and combined image samplers (`sampler2D` and so on), in set 0 only; anything else is rejected when the kernel is loaded.

`ctx.run(kernel, bindings, groups=..., threads=..., push=...)` takes one entry per binding, in the order of `kernel.bindings`:

- A `GpuBuffer` stays on the GPU between runs. `ctx.buffer(size)` makes one filled with zeros, `ctx.upload(array)` one holding a copy; `buf.upload(data, offset)`, `buf.to_numpy(dtype, shape)`, `buf.to_bytes()` and `buf.download_into(array)` copy in and out.
- An array (anything with the buffer protocol and contiguous memory) is copied to a buffer for the run, and copied back into afterwards when it is writable and the binding is not `readonly`.
- An image binding takes a `GpuImage`. `ctx.image(width, height, format, depth=1, layers=1)` makes one filled with zeros: 2D, a 2D array (`layers` > 1) or 3D (`depth` > 1), one mip level, with a pipeline JSON format name such as `"R32G32B32A32_SFLOAT"` (the default). `ctx.upload_image(array, format=None, kind="2d")` makes one from an array shaped `(height, width[, channels])`, or `(depth | layers, height, width[, channels])` with `kind="3d"` or `"array"`; without a format it takes the dtype's (float32 to `R32…_SFLOAT`, float16 to `R16…_SFLOAT`, uint8 to `R8…_UNORM`, other integers to `…_UINT` or `…_SINT`). `img.to_numpy()` returns the texels in that shape, and `img.upload()`, `img.to_bytes()` and `img.download_into()` copy tightly packed texels.

An image is bound as the shader declares it: a 2D image needs depth 1 and one layer, a 2D array depth 1, a 3D image depth > 1, a cube six square layers (+X, −X, +Y, −Y, +Z, −Z), a cube array 6 × n. A storage image whose format qualifier names another format than the image's is rejected. Sampled images are read with linear filtering, between texels and between mip levels, where the device can filter the format, nearest otherwise, clamped to the edge.

### Mip levels

`ctx.image(..., mips=n)` gives an image `n` mip levels, `mips=0` the full chain down to 1×1; level m is `max(1, size >> m)` in each dimension. `img.generate_mips()` fills levels 1 and up from level 0, each by halving the one before with linear filtering, and raises `RuntimeError` where the device cannot blit and filter the format; `ctx.upload_image(array, mips=0)` uploads level 0 and generates the rest. A storage image is bound at level 0 and a sampled image with all its levels; `img.level(m)` in `run`'s bindings binds level m alone, so a kernel can write each level itself:

```python
env = ctx.image(256, 256, "R16G16B16A16_SFLOAT", layers=6, mips=0)
for mip in range(env.mips):
    size = env.level_shape(mip)                   # (6, height, width, 4)
    ctx.run(prefilter, [source, env.level(mip)], threads=(size[2], size[1], 6),
            push=struct.pack("<f", mip / (env.mips - 1)))
```

`img.upload`, `img.to_numpy`, `img.to_bytes` and `img.download_into` take a `mip` argument (default 0), and `img.level_shape(mip)` and `img.level_nbytes(mip)` give its shape and size.

### Image files

`img.save(path, mip=0, layer=0, half=True)` writes one layer (of an array) or slice (of a 3D image) by the path's extension: `.exr` (uncompressed, 16-bit floats, or 32-bit with `half=False`), `.hdr` (Radiance RGBE) or `.npy` (float32, height × width × channels). Texels are converted to floats: UNORM formats to 0..1, sRGB formats decoded to linear, the packed B10G11R11 and A2B10G10R10 formats unpacked; integer formats raise `ValueError`. `sk.save_exr(path, array, half=True)` and `sk.save_hdr(path, array)` write NumPy arrays shaped `(height, width)` or `(height, width, channels)`. An `.exr` stores 1 channel as Y, 2 as R and G, 3 as R, G and B, 4 as R, G, B and A; an `.hdr` stores RGB, 1 channel as grey, 2 as red and green with blue zero, and drops alpha.

`groups` is the number of workgroups; `threads` is the number of invocations, divided by the local size and rounded up, so the shader must skip invocations past the end. Either is an int or up to three ints. `push` may be shorter than the kernel's push constants; the rest is zero.

`ctx.shutdown()`, or leaving the `with` block, frees the context's buffers, images and kernels; using one afterwards raises `RuntimeError`. The engine's context is shut down after `on_cleanup`. A context is used from one thread at a time; `engine.compute` from the engine's callbacks.

In C++ the same API is `Shoonyakasha::Compute::Context` in `Compute/Compute.h`, constructed without arguments for a device of its own or from a `VulkanDevice&`, with `EngineAPI::compute()` giving the engine's. `createImage` and `uploadImage` make `Image`s, and `run` takes a list of `Binding`s, each a `Buffer`, an `Image`, or `Binding(image, mip)` for one mip level. `Compute/ImageFiles.h` has `saveExr`, `saveHdr` and `saveImage`. `Compute/Npy.h` writes and reads `.npy` files (`saveNpy`, `loadNpy`) that `numpy.load` and `numpy.save` share:

```cpp
#include "Compute/Compute.h"
#include "Compute/Npy.h"

using namespace Shoonyakasha::Compute;

Context ctx;
Kernel kernel = ctx.loadKernel("square.comp.spv");
std::vector<float> values(1000, 0.5f);
Buffer in = ctx.upload(values.data(), values.size() * sizeof(float));
Buffer out = ctx.createBuffer(values.size() * sizeof(float));
struct { float scale; uint32_t count; } push{2.0f, uint32_t(values.size())};
ctx.runThreads(kernel, {in, out}, push.count, 1, 1, &push, sizeof push);
saveNpy("squares.npy", out.download<float>());
```

A `.npy` file saved this way can be the `source` of a pipeline JSON buffer, as above.
