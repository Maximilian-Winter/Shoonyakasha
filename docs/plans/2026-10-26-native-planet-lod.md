# Native planet level of detail

Status: done (2026-10-26). PlanetWorld's per-frame Python in the same descent: 0.22 ms (from 1.66 ms).

## Goal

Move the per-chunk work of `shoonyakasha.planet.world.PlanetWorld` from Python
to C++: the quadtree per body (selection, building order, eviction), the chunk
pool's slots and jobs, the heights coming back from the GPU, culling, and
writing the four draw buffers. Python keeps the settings, the sky and clouds,
the probes, the cameras and the user interface.

Measured before (2,400-frame descent of `planet.py --morph-check`, ~300 chunks
drawn, no profiler): PlanetWorld's Python 1.66 ms a frame, of which
`upload_drawn_chunks` 1.10 ms and the level of detail 0.48 ms; the GPU 3.1 to
4.3 ms a frame.

## Behaviour to keep

Everything the Python version does, in the same order, so that selections,
slot numbers, job stamps and buffer contents are identical frame for frame:

- `quadtree.QuadTree` and `Node`: bounds from `cube_sphere` (64-bit, same
  operation order), split within `split_factor * edge` of the bounding
  sphere, stay split until `merge_ratio` times that (a per-node flag for
  "split last update" in place of the key set), a node drawn until all four
  children are built, missing children sorted by distance (stable), building
  sorted by (not in view, distance) (stable), eviction of subtrees unused
  for 120 frames, children released before their parent.
- `ChunkPool`: free slots popped from the end of a list that starts
  2047 .. 0 and gets released slots appended; stamps counting jobs; pending
  heights per slot; no second job for a slot that has one this frame.
- The sun's rebuild queue: every built node of every body's tree, in
  `QuadTree.nodes()` order (a stack seeded with the roots), sorted by
  distance from the camera descending (stable), popped from the end.
- Culling: `in_view` (a cone around the view direction holding the screen's
  diagonal) and `hidden_by_horizon` against the body's floor.
- The buffers: `ChunkInstances` (per slot: centre less the camera in double
  then float, colour by mode, parent's split distance, level, body),
  `ChunkDraws`, `OceanDraws` (selected chunks whose low height is below 0 on
  a body with an ocean), `ShadowDraws` (selected chunks within the shadows'
  reach, in view or not); a count, padding to 16 bytes, then
  (indexCount, 1, 0, slot * verticesPerChunk, slot) per chunk.

## Shape

C++, `include/Planet/PlanetLod.h`, `src/Planet/PlanetLod.cpp`, namespace
`Shoonyakasha::Planet`, no engine dependencies:

- Nodes in one vector, children allocated as blocks of four, referred to by
  (index, generation) so that queued references to released nodes are
  recognised.
- `createTree(body, radius, low, high, maxLevel, splitFactor, heightPad)`,
  `dropTree(body)`, `update(view, bodies, frame, frozen)`,
  `queueRebuild(view, bodies)`, `takeHeights(rows)`, `writeDraws(view,
  bodies, colourMode, shadowReach, indexCount)`, the jobs of the frame,
  statistics, and snapshots of a tree's selected or all nodes.

Cython: `sk.PlanetLod` wraps it; `upload(engine)` hands the buffers to
`EngineAPI::setBuffer` without copying them into Python objects.

Python: `PlanetWorld` uses `sk.PlanetLod`; `Body.tree` and `PlanetWorld.pool`
become read-only views (selected and all nodes as snapshots, missing, split
factor, slots used, pending heights) for the checks and the debug cards.
`quadtree.py` stays as the reference the native version is tested against.

## Verification

- A Python test drives `sk.PlanetLod` and the Python reference (quadtree.py,
  the old pool and draw code) along a camera path with simulated heights
  coming back two frames late, and compares every frame: selected keys and
  slots, missing order, jobs, rebuilds, and the bytes of the four buffers.
- C++ unit tests for splitting and merging, eviction, a full pool and the
  horizon test.
- The planet example's self-checks, captures before and after, and timings.
