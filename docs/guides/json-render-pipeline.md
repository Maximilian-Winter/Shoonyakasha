# JSON render pipelines

The pipeline file describes resources, passes, shaders, and the bindings between runtime data and GPU inputs. The engine compiles these declarations into a frame graph. JSON configures supported behavior; GLSL still implements shading, and new native execution/data sources may need C++.

## Start from a working pipeline

Run the [Python starter](../getting-started/python-quickstart.md) or [C++ FacadeTest](../getting-started/cpp-quickstart.md). The [starter pipeline](../../python/shoonyakasha/templates/pipeline.json) is a complete forward pipeline, paired with [vertex](../../python/shoonyakasha/templates/shaders/basic.vert) and [fragment](../../python/shoonyakasha/templates/shaders/basic.frag) shaders.

Its flow is:

```text
Camera ECS state -> CameraUBO -> cameraSet (set 0)
Entity transform/material -> MaterialPushConstants + materialSet (set 1)
                                            |
                                 ForwardPass: opaque_geometry
                                            |
                                   swapchain + depth
                                            |
                                         present
```

This describes one working application, not the only supported layout.

## How the starter fits together

1. `vertexFormats.standard` describes the attributes actually in the loaded vertex buffer. Locations and types must match the vertex shader.
2. `bufferLayouts.CameraUBO` uses `std140` and `per_frame` sources such as `scene.camera.view`. `MaterialPushConstants` uses per-entity sources and scalar packing.
3. `descriptorSetLayouts.cameraSet` binds the UBO through `autoBindBuffer`. `materialSet` declares texture slots. Pass `descriptorSets` order determines shader set indices.
4. `entityDataBindings.pbrOpaque` connects per-draw/material layouts to geometry rendering. `execution.entityDataBinding` selects it for the pass.
5. `resources` imports `swapchain` and declares a depth image. The pass writes both and marks its swapchain output `present: true`.
6. `pipeline` names compiled SPIR-V files and the depth/cull state. `execution.type: opaque_geometry` invokes the registered entity renderer.

The starter's 104-byte push-constant range matches its shader/layout. If you change fields, update the GLSL and range together; JSON does not rewrite shader interfaces.

## Make a first change

Change the ForwardPass swapchain output's `clear` color, then restart or [reload](#reload-while-running). This changes the background without rebuilding C++. Next, edit the fragment shader and rerun the shader compiler. Python examples compile on startup; C++ examples compile through CMake.

## Reload while running

`engine.reload_pipeline()` loads the pipeline JSON and the SPIR-V its passes name again and rebuilds the graph, waiting for the GPU. GLSL is compiled by `sk.shaders.compile_dir`, which rebuilds the shaders whose source or included files are newer than their SPIR-V. A key that does both:

```python
import shoonyakasha as sk

def on_key(key):
    if key == sk.keys.F5:
        try:
            sk.shaders.compile_dir("shaders")
            engine.reload_pipeline()
        except (sk.shaders.ShaderCompileError, RuntimeError) as error:
            print(error)

engine.set_on_key_pressed(on_key)
```

To reload on save, an ECS system can do the same when `compile_dir` returns compiled outputs or the JSON file's modification time changes.

Parameters, `set_custom_*` values, `set_buffer` data and pipeline images stay. Passes switched with `set_pass_enabled` or `apply_pipeline_preset` keep their state; the others take the JSON's `enabled`. A JSON file that does not load or a graph that does not compile raises `RuntimeError` with the reason, and the previous graph is compiled again; if that fails too, for example because its SPIR-V is gone, no frames are drawn until a reload succeeds. Call it from a key callback, `on_update`, an ECS system, a UI callback or `on_post_render`; from `on_init` and `on_pre_render` it raises. In C++, `EngineAPI::reloadPipeline()` returns the error message, empty on success.

For application-driven values, follow [custom uniforms](custom-shader-uniforms.md): set a named value during init/update, add a matching field source, bind the buffer, and consume it in GLSL.

## Add a pass

Use [BloomTest](../../examples/cpp/rendering/bloom_test/bloom_pipeline.json) for a complete multipass example. Declare the intermediate image, identify the producer's output and consumer's input, bind it as a sampled resource, and give the final output presentation responsibility. Inputs/outputs drive dependency and barrier analysis; descriptor binding alone is not a substitute for declaring access.

Blending into existing color uses `color_blend` with appropriate pipeline blend state. Presentation is independent: the final output can use `color_blend` and `present: true`. Do not insert an intermediate present transition before later passes use that image.

## Validate and debug

```python
import shoonyakasha as sk
sk.shaders.compile_dir("shaders")
for problem in sk.pipeline.validate("pipeline.json"):
    print(problem)
sk.pipeline.check("pipeline.json")  # raises ValueError on errors
```

The Python validator is a useful preflight, not a complete schema or proof of Vulkan correctness. The C++ parser/compiler and runtime validation are authoritative; see [validator differences](../reference/pipeline-json.md#validation-and-export).

The default application uses single-command-buffer execution. Queue declarations and native multi-queue APIs do not by themselves guarantee asynchronous execution through the facade.

Continue with the [JSON reference](../reference/pipeline-json.md), [compute/data flow](compute-and-data-flow.md), and [frame-graph architecture](../architecture/frame-graph-pipeline.md).
