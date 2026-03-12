"""
Shoonyakasha Python Demo — Verification Script

碼道驗證 — Verifying the Way of Code

This script demonstrates the full Python API for the Shoonyakasha engine.
It mirrors the C++ facade_test example using only Python.

Usage:
    python demo.py

Requirements:
    - Build with -DBUILD_PYTHON=ON
    - Ensure _shoonyakasha.pyd is in python/shoonyakasha/
    - Run from a directory containing pipeline.json and shader assets
"""

import math
import sys
import os

# Add the parent directory to path so we can import shoonyakasha
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..'))

import shoonyakasha as sk

print("=" * 60)
print("  Shoonyakasha Python Demo")
print("  शून्याकाश — Void-Space Engine")
print("=" * 60)
print()
print(f"  Module version: {sk.__version__}")
print(f"  NULL_ENTITY: {sk.NULL_ENTITY}")
print()
print("  Controls:")
print("    WASD   - Move camera")
print("    RMB    - Look around")
print("    ESC    - Toggle mouse capture")
print("    L      - Create point light at camera position")
print("    P      - Toggle physics")
print()
print("=" * 60)

# ── Create engine ─────────────────────────────────────────────
PARTICLE_COUNT = 50000

engine = sk.Engine(
    title="Python Demo — शून्याकाश",
    width=1920,
    height=1080,
    log_file="python_demo.log",
    hdr_environment_path="cubemaps_hdrs/kloofendal_28d_misty_8k.hdr",
    pipeline_json_path="pbr_ibl_pipeline_v3.json",
    render_graph_parameters={"particleCount": PARTICLE_COUNT},
)

light_count = 0
particle_time = 0.0
fps_timer = 0.0
fps_frames = 0


# ── Initialization ────────────────────────────────────────────
def on_init():
    global light_count
    print("[Python] on_init — setting up scene")

    # Create camera
    camera = engine.create_camera(
        pos=(0.0, 5.0, 15.0),
        fov=60.0,
        speed=8.0,
        near_plane=0.1,
        far_plane=500.0,
    )
    print(f"[Python] Camera entity: {camera}")

    # Create directional light
    engine.create_directional_light(
        direction=(-0.5, -1.0, -0.3),
        color=(1.0, 0.975, 0.95),
        intensity=3.0,
    )

    # Load glTF scene
    result = engine.load_gltf_scene("./NewSponza_Main_glTF_003.gltf")
    print(f"[Python] Loaded scene: {len(result.entities)} entities, "
          f"{result.total_vertices} vertices, "
          f"{result.total_textures} textures")

    # Scene info
    scene = engine.scene
    all_entities = scene.get_all_entities()
    print(f"[Python] Total entities: {len(all_entities)}")

    # Physics setup
    physics = engine.physics
    physics.gravity = (0.0, -9.81, 0.0)
    gx, gy, gz = physics.gravity
    print(f"[Python] Physics gravity: ({gx}, {gy}, {gz})")


# ── Per-frame update ──────────────────────────────────────────
def on_update(dt):
    global light_count, fps_timer, fps_frames

    fps_timer += dt
    fps_frames += 1
    if fps_timer >= 5.0:
        print(f"[FPS] {fps_frames / fps_timer:.1f}")
        fps_timer = 0.0
        fps_frames = 0

    inp = engine.input
    if inp.is_key_down(76):  # 'L' key
        scene = engine.scene
        cam = engine.camera_entity
        pos = scene.get_position(cam)

        engine.create_point_light(
            pos=pos,
            color=(1.0, 0.8, 0.6),
            intensity=5.0,
            range=20.0,
        )
        light_count += 1
        print(f"[Python] Point light #{light_count} at {pos}")


# ── Pre-render: drive particle simulation ─────────────────────
def on_pre_render(dt):
    global particle_time
    particle_time += dt

    # Static particle parameters
    engine.set_custom_float("particles.gravity", 1.5)
    engine.set_custom_uint("particles.count", PARTICLE_COUNT)
    engine.set_custom_float("particles.boundaryRadius", 15.0)
    engine.set_custom_float("particles.damping", 0.998)
    engine.set_custom_float("particles.spawnHeight", 0.5)

    # Attractor: warm glow in the atrium center
    engine.set_custom_vec4("particles.attractorPos", (0.0, 5.0, 0.0, 25.0))

    # Wind: gentle rotating swirl
    wind_angle = particle_time * 0.2
    engine.set_custom_vec4("particles.wind", (
        math.sin(wind_angle) * 0.4,
        0.1,
        math.cos(wind_angle) * 0.4,
        0.3,
    ))


# ── Key press ─────────────────────────────────────────────────
def on_key_pressed(key_code):
    if key_code == 80:  # 'P' key
        physics = engine.physics
        was_enabled = physics.enabled
        physics.enabled = not was_enabled
        state = "PAUSED" if was_enabled else "ENABLED"
        print(f"[Python] Physics {state}")


# ── Cleanup ───────────────────────────────────────────────────
def on_cleanup():
    print(f"[Python] Cleanup — created {light_count} dynamic lights")


# ── Register callbacks and run ────────────────────────────────
engine.set_on_init(on_init)
engine.set_on_update(on_update)
engine.set_on_pre_render(on_pre_render)
engine.set_on_key_pressed(on_key_pressed)
engine.set_on_cleanup(on_cleanup)

try:
    engine.run()
except Exception as e:
    print(f"Fatal error: {e}", file=sys.stderr)
    sys.exit(1)
