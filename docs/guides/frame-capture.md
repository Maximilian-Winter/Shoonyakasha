# Frame capture

Capture reads the last presented swapchain frame, including final composition and UI. Use a post-render callback or input event after at least one frame has been presented.

```python
# During a running engine, after presentation:
ok = engine.capture_screenshot("shot.png")
if sk.video_recording_available():
    started = engine.start_recording("clip.mkv", fps=30, quality=18)
# Later:
finished = engine.stop_recording()
```

```cpp
bool ok = engine.captureScreenshot("shot.png");
Shoonyakasha::Facade::RecordingOptions options;
options.fps = 30;
bool started = engine.startRecording("clip.mkv", options);
// Later:
bool finished = engine.stopRecording();
```

Check the booleans and application log for failures. Recording is also finalized at shutdown. Python `is_recording` and `recorded_frame_count` are properties; C++ uses `isRecording()` and `getRecordedFrameCount()`.

## Formats and dependencies

Screenshots support `.png`, `.jpg`, `.bmp`, `.tga`, and `.hdr`. Saving the presented image to HDR does not recover pre-tonemapping scene radiance. For an intermediate HDR target, use [render-target readback](compute-and-data-flow.md).

Video is piped to ffmpeg instead of linking an encoder library. Discovery uses an explicit path, `FFMPEG`, `PATH`, and known install locations. Query `sk.find_ffmpeg()` / `findFfmpeg()` to inspect discovery. Defaults are 30 fps, quality 18, codec `libx264`, and automatic executable discovery; select a codec/container combination available in your ffmpeg installation, for example H.264 in `.mkv` or `.mp4`.

Readback is synchronous and stalls for GPU copies; recording reduces rendering throughput. The selected fps describes the encoded stream, not a guarantee that the application renders at that rate. Treat resize and encoder failure as cases to check in the application's log and recording status.

References: [Python Engine](../api/python/engine.md), [C++ EngineAPI](../api/cpp/engine-api.md), [InstancingTest](../../examples/cpp/api/instancing_test).
