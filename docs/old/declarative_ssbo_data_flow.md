# Declarative SSBO Data Flow System

*The next evolution of "JSON is the Engine"*

---

## Vision

Today, the engine's declarative pipeline handles **rendering** beautifully: JSON declares passes, buffer layouts, descriptor sets, and dot-path sources. UBOs like `CameraUBO` and `particleSimParams` are auto-created, auto-filled, and auto-bound — zero C++ plumbing.

But **compute data** — SSBOs that hold particle positions, simulation grids, physics state — is still manually created, manually initialized, and manually registered in C++. The SSBO is the last piece of hardcoded infrastructure.

This document proposes extending the declarative system to cover the **full data lifecycle**:

```
SOURCE (where data comes from)
  → BUFFER (GPU storage, declared in JSON)
    → COMPUTE (shader transforms it)
      → TARGET (where results go)
        → next graph / disk / CPU readback
```

Every step declared in JSON. Every data flow visible. Every connection automatic.

---

## Core Concepts

### 1. Declarative SSBO with Source & Target

Today's `bufferLayouts` declare field-level sources for UBOs (each field has a `source` dot-path). SSBOs are different — they hold **arrays of structs** managed by compute shaders, not individual named fields. The declaration model needs to reflect this:

```json
"bufferLayouts": {
  "particleSSBO": {
    "usage": "storage_buffer",
    "packing": "std430",
    "elementStruct": {
      "fields": [
        { "name": "position", "type": "vec4" },
        { "name": "velocity", "type": "vec4" }
      ]
    },
    "elementCount": { "source": "scene.custom.particles.count" },
    "source": {
      "type": "initializer",
      "frequency": "once",
      "fields": {
        "position": { "randomRange": { "min": [-12, 0, -5, 2], "max": [12, 8, 5, 5] } },
        "velocity": { "randomRange": { "min": [-0.5, 0.5, -0.5, 0.8], "max": [0.5, 1.5, 0.5, 1.2] } }
      },
      "seed": 42
    },
    "target": "scene.custom.particles.currentState"
  }
}
```

Key additions over today's system:
- **`elementStruct`** — declares the per-element struct layout (like the GLSL `Particle` struct)
- **`elementCount`** — how many elements, can reference a custom value
- **`source`** — where initial/ongoing data comes from
- **`target`** — where results are exposed after compute writes

### 2. Source Types

The `source` field describes where buffer data originates. Multiple source types serve different use cases:

| Source Type | Description | When |
|------------|-------------|------|
| `"initializer"` | Engine generates data (random, pattern, constant) | Buffer creation |
| `"custom"` | Pull from `scene.custom.*` values | Per frequency policy |
| `"file"` | Load from binary file on disk | Buffer creation |
| `"precomputed"` | Load from a previously saved compute result | Buffer creation |
| `"buffer_ref"` | Reference another buffer's output (cross-graph) | Per frequency policy |

#### Source: Initializer (procedural generation)

The engine generates initial data without any C++ code:

```json
"source": {
  "type": "initializer",
  "frequency": "once",
  "fields": {
    "position": {
      "randomRange": {
        "min": [-12.0, 0.0, -5.0, 2.0],
        "max": [12.0, 8.0, 5.0, 5.0]
      }
    },
    "velocity": {
      "constant": [0.0, 1.0, 0.0, 1.0]
    }
  },
  "seed": 42
}
```

Supported initializer strategies:
- **`randomRange`** — uniform random between min/max per component
- **`constant`** — every element gets the same value
- **`gaussian`** — normal distribution with mean/stddev
- **`grid`** — position on a regular grid (for cloth, fluid grids)
- **`custom`** — pull individual field values from `scene.custom.*`

#### Source: File (disk loading)

Load pre-computed or pre-baked data from disk:

```json
"source": {
  "type": "file",
  "path": "data/precomputed_particles.bin",
  "frequency": "once",
  "format": "raw"
}
```

#### Source: Buffer Reference (cross-graph sharing)

One graph's output feeds another graph's input:

```json
"source": {
  "type": "buffer_ref",
  "ref": "scene.custom.physics.collisionGrid",
  "frequency": "per_frame"
}
```

### 3. Target (Output Declaration)

The `target` field declares where a buffer's contents are **exposed** after compute shaders write to it:

```json
"target": "scene.custom.particles.currentState"
```

This does several things:
- **Registers the GPU buffer** under that name in the custom namespace
- **Makes it accessible** to other frame graphs via `buffer_ref` source type
- **Enables CPU readback** if the target has readback policies

#### Target with readback:

```json
"target": {
  "name": "scene.custom.particles.currentState",
  "readback": {
    "frequency": "every_n_frames",
    "n": 60,
    "callback": true
  }
}
```

#### Target with disk save (pre-computation):

```json
"target": {
  "name": "scene.custom.baked.lightProbes",
  "save": {
    "path": "data/baked_light_probes.bin",
    "trigger": "manual",
    "format": "raw"
  }
}
```

### 4. Update Frequency

Every source and target has a frequency policy controlling when data transfers happen:

| Frequency | Description | Use Case |
|-----------|-------------|----------|
| `"once"` | Only at buffer creation | Initial particle positions, static data |
| `"per_frame"` | Every frame | Simulation parameters, dynamic inputs |
| `"every_n_frames"` | Every N frames | LOD updates, periodic readback |
| `"manual"` | Only when explicitly triggered from C++ | Baking, debugging |
| `"on_change"` | When source data changes (dirty flag) | Infrequent parameter updates |

### 5. Transfer Direction & Memory Policies

Declare where data lives and how it moves:

```json
"memory": {
  "location": "device_local",
  "staging": "auto",
  "transferDirection": "cpu_to_gpu"
}
```

| Field | Options | Description |
|-------|---------|-------------|
| `location` | `"device_local"`, `"host_visible"`, `"host_coherent"` | Where the buffer lives |
| `staging` | `"auto"`, `"persistent"`, `"none"` | Staging buffer strategy |
| `transferDirection` | `"cpu_to_gpu"`, `"gpu_to_cpu"`, `"gpu_only"`, `"bidirectional"` | Data flow direction |

- **`cpu_to_gpu`** — Initial upload, parameter updates (default for sourced buffers)
- **`gpu_to_cpu`** — Readback for pre-computation, debugging, saving
- **`gpu_only`** — Compute read/write, no CPU involvement after init (most SSBOs)
- **`bidirectional`** — Both directions (rare, expensive)

---

## Cross-Graph Data Sharing

This is where the system becomes truly powerful. Multiple frame graphs can form a **data pipeline**:

```
FrameGraph A (Physics)          FrameGraph B (Rendering)
┌─────────────────────┐         ┌──────────────────────┐
│ collisionSSBO       │         │ particleSSBO         │
│   source: custom    │         │   source: buffer_ref │
│   target: custom.   │────────>│     ref: custom.     │
│     physics.grid    │         │     physics.grid     │
│                     │         │                      │
│ Compute: collision  │         │ Compute: simulate    │
│   detection         │         │   particles          │
└─────────────────────┘         │                      │
                                │ Render: draw         │
                                │   particles          │
                                └──────────────────────┘
```

Each graph declares its data dependencies. The engine resolves the order, ensures barriers, and handles the data flow. No C++ wiring needed.

### Chaining Example in JSON

**Graph A — Physics pre-pass:**
```json
{
  "name": "PhysicsGraph",
  "bufferLayouts": {
    "collisionGrid": {
      "usage": "storage_buffer",
      "packing": "std430",
      "elementStruct": {
        "fields": [
          { "name": "cellCount", "type": "uint" },
          { "name": "cellData", "type": "vec4" }
        ]
      },
      "elementCount": { "value": 4096 },
      "source": { "type": "initializer", "frequency": "once", "fields": { "cellCount": { "constant": 0 } } },
      "target": "scene.custom.physics.collisionGrid"
    }
  }
}
```

**Graph B — Particle rendering (references Graph A's output):**
```json
{
  "name": "ParticleRenderGraph",
  "bufferLayouts": {
    "collisionInput": {
      "usage": "storage_buffer",
      "packing": "std430",
      "source": {
        "type": "buffer_ref",
        "ref": "scene.custom.physics.collisionGrid",
        "frequency": "per_frame"
      }
    }
  }
}
```

---

## Pre-Computation Pipeline

A powerful use case: **bake expensive computations offline, save to disk, reload instantly.**

### Step 1: Compute and save

```json
{
  "name": "LightProbeBaker",
  "bufferLayouts": {
    "probeData": {
      "usage": "storage_buffer",
      "packing": "std430",
      "elementStruct": {
        "fields": [
          { "name": "shCoeffs", "type": "vec4", "arrayCount": 9 },
          { "name": "position", "type": "vec4" }
        ]
      },
      "elementCount": { "value": 256 },
      "source": { "type": "initializer", "frequency": "once", "fields": { "position": { "grid": { "min": [-10, 0, -5], "max": [10, 8, 5], "resolution": [8, 4, 8] } } } },
      "target": {
        "name": "scene.custom.baked.lightProbes",
        "save": { "path": "data/light_probes.bin", "trigger": "manual" }
      },
      "memory": { "transferDirection": "gpu_to_cpu" }
    }
  }
}
```

### Step 2: Load in production app

```json
{
  "bufferLayouts": {
    "lightProbes": {
      "usage": "storage_buffer",
      "packing": "std430",
      "source": {
        "type": "file",
        "path": "data/light_probes.bin",
        "frequency": "once"
      }
    }
  }
}
```

No recomputation. Instant load. Same declarative system.

---

## Implementation Roadmap

### Phase 1: Declarative SSBO with Initialization (Foundation)

**What:** Let JSON declare SSBOs with `elementStruct`, `elementCount`, and basic `source` initializers (`constant`, `randomRange`). The engine creates the buffer, generates initial data, and auto-binds it.

**Changes:**
- Extend `bufferLayouts` JSON parsing to handle `elementStruct` and `elementCount`
- Add SSBO creation in `createDotPathUBOs()` (rename to `createDotPathBuffers()`)
- Implement `randomRange` and `constant` initializers
- Auto-register created SSBOs for `autoBindBuffer` (same as dot-path UBOs)
- Add `frequency` field: `"once"` for initialization, `"per_frame"` for ongoing updates

**Result:** `ParticleData` SSBO declared entirely in JSON. `createParticles()` in C++ disappears. The app only calls `setCustom()` for sim params and the engine handles everything.

**Files:**
- `FrameGraphJson.cpp` — Parse new SSBO fields
- `FrameGraphCompiler.cpp` — Compile SSBO layouts
- `RenderGraph.cpp` — Create/initialize SSBOs from compiled layouts
- `DotPathResolver.h/cpp` — No changes (already has `scene.custom.*`)
- `pbr_ibl_pipeline_v3.json` — Declare particleSSBO
- `DeclarativeSponzaApp.cpp` — Remove `createParticles()` SSBO code

### Phase 2: Target & Cross-Graph Sharing

**What:** Add `target` field to expose buffer outputs. Add `buffer_ref` source type. Enable frame graph chaining.

**Changes:**
- Add `target` field to `BufferLayoutDesc`
- Register target buffers in `scene.custom.*` namespace as `GPUBuffer` references
- Add `buffer_ref` source resolution
- Implement cross-graph dependency tracking

**Result:** Multiple frame graphs can share SSBO data without C++ wiring.

### Phase 3: Transfer Policies & Readback

**What:** Add `memory` policies for CPU↔GPU transfer direction. Implement readback with staging buffers.

**Changes:**
- Add memory policy fields to `BufferLayoutDesc`
- Create staging buffers for readback
- Implement async readback with fence-based completion
- Add readback callback system

**Result:** GPU compute results can be read back to CPU for saving, debugging, or CPU-side logic.

### Phase 4: File I/O & Pre-Computation

**What:** Add `file` source type for loading binary data. Add `save` target option for writing compute results to disk.

**Changes:**
- Implement binary file loading into SSBOs
- Implement GPU→staging→file save pipeline
- Add manual trigger API for baking operations

**Result:** Full pre-computation pipeline. Bake offline, load instantly.

### Phase 5: Advanced Initialization Strategies

**What:** Add `gaussian`, `grid`, `custom` initializer types. Support complex procedural generation from JSON.

**Changes:**
- Implement additional initializer strategies
- Support per-field initialization with different strategies
- Add noise functions (Perlin, simplex) as initializer options

**Result:** Complex initial conditions without any C++ code.

---

## Example: Fully Declarative Particle System

With all phases complete, the entire particle system — from creation to simulation to rendering — is declared in JSON:

```json
{
  "bufferLayouts": {
    "particleSSBO": {
      "usage": "storage_buffer",
      "packing": "std430",
      "elementStruct": {
        "fields": [
          { "name": "position", "type": "vec4" },
          { "name": "velocity", "type": "vec4" }
        ]
      },
      "elementCount": { "source": "scene.custom.particles.count" },
      "source": {
        "type": "initializer",
        "frequency": "once",
        "seed": 42,
        "fields": {
          "position": { "randomRange": { "min": [-12, 0, -5, 2], "max": [12, 8, 5, 5] } },
          "velocity": { "randomRange": { "min": [-0.5, 0.5, -0.5, 0.8], "max": [0.5, 1.5, 0.5, 1.2] } }
        }
      },
      "memory": { "location": "device_local", "transferDirection": "gpu_only" }
    },
    "particleSimParams": {
      "usage": "uniform_buffer",
      "packing": "std140",
      "updateFrequency": "per_frame",
      "fields": [
        { "name": "deltaTime",      "type": "float", "source": "scene.time.delta" },
        { "name": "gravity",        "type": "float", "source": "scene.custom.particles.gravity" },
        { "name": "particleCount",  "type": "uint",  "source": "scene.custom.particles.count" },
        { "name": "boundaryRadius", "type": "float", "source": "scene.custom.particles.boundaryRadius" },
        { "name": "attractorPos",   "type": "vec4",  "source": "scene.custom.particles.attractorPos" },
        { "name": "wind",           "type": "vec4",  "source": "scene.custom.particles.wind" },
        { "name": "damping",        "type": "float", "source": "scene.custom.particles.damping" },
        { "name": "spawnHeight",    "type": "float", "source": "scene.custom.particles.spawnHeight" },
        { "name": "padding1",       "type": "float", "source": "const.0" },
        { "name": "padding2",       "type": "float", "source": "const.0" }
      ]
    }
  },
  "passes": [
    {
      "name": "ParticleSimulate",
      "type": "compute",
      "execution": {
        "type": "compute_dispatch",
        "workgroupSize": [256, 1, 1],
        "dispatch": { "x": { "parameter": "particleCount", "divisor": 256 }, "y": 1, "z": 1 },
        "bindDescriptorSets": true
      },
      "pipeline": { "computeShader": "shaders/particle_sim.comp.spv" },
      "descriptorSets": ["particleComputeSet"]
    },
    {
      "name": "ParticleRender",
      "type": "graphics",
      "execution": {
        "type": "draw",
        "vertexCount": { "parameter": "particleCount" },
        "instanceCount": 1,
        "bindDescriptorSets": true
      },
      "pipeline": {
        "vertexShader": "shaders/particle.vert.spv",
        "fragmentShader": "shaders/particle.frag.spv",
        "topology": "point_list",
        "blending": "additive"
      },
      "descriptorSets": ["cameraSet", "particleRenderSet"]
    }
  ]
}
```

**The C++ application code for particles reduces to:**

```cpp
void MyApp::init() {
    m_renderGraph->loadFromFile("pipeline.json");
    m_renderGraph->bindScene(m_scene.get());

    // Set initial custom values (the JSON sources reference these)
    auto& ctx = m_renderGraph->getSceneContext();
    ctx.setCustom("particles.count", 50000u);
    ctx.setCustom("particles.gravity", 1.5f);
    ctx.setCustom("particles.boundaryRadius", 15.0f);
    ctx.setCustom("particles.attractorPos", glm::vec4(0, 5, 0, 25));
    ctx.setCustom("particles.damping", 0.998f);
    ctx.setCustom("particles.spawnHeight", 0.5f);

    m_renderGraph->compile(extent, imageCount);
    // particleSSBO is auto-created, auto-initialized, auto-bound!
}

void MyApp::update() {
    auto& ctx = m_renderGraph->getSceneContext();

    // Only update dynamic values
    float angle = m_time * 0.2f;
    ctx.setCustom("particles.wind", glm::vec4(
        sinf(angle) * 0.4f, 0.1f, cosf(angle) * 0.4f, 0.3f));
}
```

No `VulkanBuffer`, no staging, no `memcpy`, no `registerStorageBuffer()`. Pure intent.

---

## Philosophical Alignment

This system embodies the engine's core philosophy:

- **Emptiness as Foundation** — Buffers arise from JSON declarations, not hardcoded structs
- **Non-action through Action** — The engine does the work; the application declares the intent
- **Harmony of Components** — Source, buffer, compute, target flow naturally
- **Transformation through Simplicity** — Complex data pipelines from simple JSON declarations

The particle SSBO was the last piece of manual Vulkan plumbing in the example app. With this system, even that dissolves into pure declaration — emptiness giving rise to form, as the dakinis intended.
