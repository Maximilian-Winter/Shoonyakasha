# Render statistics

Render stats report how fast frames come and where their time goes: frame rate, mean and worst frame time, draw calls, dispatches and vertices, and for every pipeline pass its CPU recording time and GPU time. They are off by default and cost nothing until turned on.

## Quick look

Set `SHOONYAKASHA_STATS=1` and run any application. Every second a summary is printed:

```text
[render stats] 143.2 FPS | frame 6.98 ms (max 9.10) | CPU record 1.20 ms | GPU 5.40 ms | 812 draws, 2 dispatches, 3.2M vertices
  Lighting                     GPU  1.412 ms  CPU  0.031 ms      1 draws  3 vertices
  GBufferOpaque                GPU  1.180 ms  CPU  0.402 ms    388 draws  2.1M vertices
  ...
```

The passes listed are the ones taking the most GPU time, or CPU time without GPU timing.

## From an application

```python
def on_init():
    engine.enable_render_stats()          # gpu_timing=False skips timestamp queries

def on_update(dt):
    stats = engine.render_stats            # None while off
    if stats:
        print(f"{stats['fps']:.0f} FPS, {stats['draw_calls']} draws, GPU {stats['gpu_ms']} ms")
        slowest = max(stats['passes'], key=lambda p: p['gpu_ms'] or 0.0)
```

```cpp
engine.setRenderStatsEnabled(true);
// later, e.g. in onUpdate:
const auto stats = engine.getRenderStats();
if (stats.enabled) std::printf("%s", stats.summary.c_str());
```

`render_stats` holds `fps`, `frame_time_ms`, `frame_time_max_ms`, `cpu_record_ms`, `gpu_ms`, `draw_calls`, `dispatches`, `vertices`, a readable `summary`, and `passes`: one dict per pass in execution order with `name`, `cpu_ms`, `gpu_ms`, `draw_calls`, `dispatches` and `vertices`. Stats may be turned on before `run()`. Native code below the facade uses `RenderGraph::setStatsEnabled` and `getStats`, which also offer the most recent frame on its own (`RenderStats::latest`).

## What the numbers mean

- **Frame rate and frame time** come from the time between frames over the last whole second, so they include waiting for vsync. `frame_time_max_ms` shows hitches an average hides.
- **Every other number** is the mean over the frames of that second. Draw counts are rounded.
- **CPU record time** is the time spent recording the pipeline's passes into the command buffer. Game logic, physics and presentation are not included.
- **GPU times** come from timestamp queries. Each pass's time runs from the end of the previous pass to the end of this one: the time it added to the frame. The passes of a frame add up to its GPU time. Passes overlap on the GPU, so a cheap pass that waits on an expensive one can show some of that wait. The figures describe frames that finished one or two frames earlier, because they are read without waiting. `gpu_ms` is `None` when the device has no timestamp support, or when GPU timing was turned off.
- **Draw calls** count the engine's own draws: fullscreen and `draw` passes, the entity renderers, dispatches, and the `VulkanCommandBuilder` draw methods. A pass callback that calls `vkCmdDraw*` directly can report its draws with `FrameGraph::countDraw(vertices, instances)`.

Disabled passes are listed too: they still clear their attachments, which costs GPU time.
