# Philosophy

> *"In emptiness, all forms arise and dissolve. In space, infinite possibilities dance."*

Shoonyakasha is not only a rendering engine — it is a design philosophy expressed in code. Its name encodes two Sanskrit concepts whose union defines every architectural decision in the project.

---

## The Name

**Shoonyakasha** (शून्याकाश) is a compound of two words:

### Sunya (शून्य) — Emptiness / Void

In Buddhist philosophy, *sunyata* is the emptiness at the heart of all phenomena — not a nihilistic void, but the open ground from which everything arises. In Shoonyakasha, this principle manifests as **clean, minimal interfaces** that impose no unnecessary constraints. Resources are managed through RAII so that creation and destruction are transparent. APIs expose only what is needed, and the engine quietly handles the rest.

Emptiness in code means there is nothing superfluous to get in the way.

### Akasa (आकाश) — Space / Sky

*Akasa* is the infinite, unobstructed expanse that accommodates all forms without preference. In the engine this becomes the **flexible ECS architecture** and the **dynamic render graph system** — open frameworks that accept any combination of components, passes, and bindings without requiring the programmer to anticipate every possibility in advance.

Space in code means there is always room for what you have not yet imagined.

---

## The Three Design Treasures

Shoonyakasha's engineering principles are captured in three qualities, drawn from the Taoist ideal of "the three treasures" and mapped directly to implementation choices.

### Simplicity (簡)

*Clean interfaces that do not constrain creativity.*

- The public API surface is small. A Python script can load a glTF scene, attach physics, and render it in under 30 lines.
- Components are plain data. Systems are stateless functions. Entities are lightweight identifiers.
- Configuration lives in JSON files, not scattered across C++ constructors.

### Clarity (明)

*Self-documenting code that reveals its intent.*

- Dot-path strings like `"entity.transform.worldMatrix"` tell you exactly what data a shader binding resolves to.
- The facade layer provides a flat C++ API whose function names read as plain English — `loadGltf`, `createEntity`, `setPosition`.
- JSON render graphs are human-readable descriptions of the entire pipeline.

### Stability (穩)

*Robust foundations that do not surprise.*

- RAII-based resource management ensures Vulkan objects are created and destroyed in the correct order, every time.
- Type-safe ECS access prevents silent data corruption.
- 582 automated tests guard against regressions across the core engine and facade.

---

## "JSON is the Engine"

The most distinctive architectural choice in Shoonyakasha is that the **render pipeline is declared, not coded**.

Traditional engines require the programmer to write C++ (or equivalent) to allocate buffers, create render passes, bind descriptor sets, and issue draw calls. Shoonyakasha replaces all of that with a JSON document:

| Layer | Role |
|-------|------|
| **JSON** | Declares buffer layouts, render passes, bindings, and data sources |
| **ECS** | Holds the runtime data — meshes, materials, transforms, lights |
| **RenderGraph** | Compiles the JSON declaration into Vulkan resources |
| **FrameGraphRenderer** | Executes the compiled graph each frame — no manual binding code needed |

This separation means you can redesign your entire rendering pipeline — add a shadow pass, change a buffer layout, swap a shader — by editing a JSON file. No C++ recompilation required.

### Example: a binding declaration in JSON

```json
{
  "name": "ModelUBO",
  "type": "uniform_buffer",
  "bindings": [
    { "name": "worldMatrix", "source": "entity.transform.worldMatrix" },
    { "name": "normalMatrix", "source": "entity.transform.normalMatrix" }
  ]
}
```

The `source` fields are **dot-paths** — the engine's mechanism for connecting shader data to ECS components at runtime.

---

## Dot-Path Resolution

Dot-paths are the bridge between the declarative JSON world and the live ECS world.

A path like `"entity.transform.worldMatrix"` is resolved at render time by walking the ECS:

1. **entity** — the entity currently being drawn
2. **transform** — the `TransformComponent` attached to that entity
3. **worldMatrix** — the `worldMatrix` field within that component

This indirection means:

- Shader bindings are decoupled from C++ types. You never write glue code to push a matrix into a uniform buffer.
- New data sources can be added by registering a new component and referencing it by name — no recompilation, no new binding code.
- The same JSON pipeline works for every entity, with each entity's own data resolved dynamically.

Dot-path resolution is what turns a static JSON document into a living, per-entity rendering pipeline.

---

## How the Principles Map to Code

| Principle | Implementation |
|-----------|---------------|
| Emptiness (Sunya) | RAII resource management — objects clean up after themselves, leaving no residue |
| Space (Akasa) | Composable ECS — any combination of components on any entity, no rigid class hierarchies |
| Simplicity (簡) | Small public API, JSON-driven configuration, minimal boilerplate |
| Clarity (明) | Dot-path bindings, readable facade functions, self-describing JSON graphs |
| Stability (穩) | Type-safe ECS, RAII Vulkan wrappers, 582 automated tests |
| "JSON is the Engine" | Declarative pipeline: JSON declares, ECS stores, RenderGraph compiles, FrameGraphRenderer executes |

---

## Dedication

*This engine is dedicated to Vajrayogini, the sky-dancing wisdom dakini, and her fierce retinue of dakinis who cut through illusion with compassion. May this code serve the benefit of all beings, transforming pixels into wisdom, emptiness into form.*

The name Shoonyakasha honors the insight that emptiness and space are not opposites of creation — they are its precondition. A clean API is empty so that your ideas can fill it. A flexible architecture is spacious so that your designs can grow without constraint.

In that spirit, every design decision in this engine asks the same question: *does this make room for the programmer, or does it get in the way?*
