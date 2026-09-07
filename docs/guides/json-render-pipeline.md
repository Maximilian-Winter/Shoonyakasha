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

Change the ForwardPass swapchain output's `clear` color, then restart. This changes the background without rebuilding C++. Next, edit the fragment shader and rerun the shader compiler. Python examples compile on startup; C++ examples compile through CMake.

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
