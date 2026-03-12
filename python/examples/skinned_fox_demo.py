#
# skinned_fox_demo.py — Animated Fox with skeletal animation
#
# Python equivalent of examples/skinned_mesh_test
#
# Run from the skinned_mesh_test working directory so assets resolve:
#   cd examples/skinned_mesh_test
#   python ../../python/examples/skinned_fox_demo.py
#
# Add the parent directory to path so we can import shoonyakasha
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..'))
import shoonyakasha as sk

engine = sk.Engine(
    title="Skinned Fox \u2014 Python Demo",
    width=1280,
    height=720,
    log_file="skinned_fox_python.log",
    pipeline_json_path="skinned_pipeline.json",
    hdr_environment_path="cubemaps_hdrs/charolettenbrunn_park_4k.hdr",
)

# Track animated entities for key handling
animated_entities = []
anim_speed = 1.0


def on_init():
    # Camera: elevated, pulled back to see the fox
    engine.create_camera(
        pos=(0, 40, 200),
        fov=60.0,
        speed=50.0,
        near_plane=1.0,
        far_plane=2000.0,
    )

    # Warm directional light (sunlight)
    engine.create_directional_light(
        direction=(-0.5, -1.0, -0.3),
        color=(1.0, 0.975, 0.95),
        intensity=3.0,
    )

    # Warm golden point light (fill)
    engine.create_point_light(
        pos=(3.0, 1.0, 2.0),
        color=(1.0, 0.85, 0.7),
        intensity=5.0,
        range=20.0,
    )

    # Load the Fox model with skins + animations
    result = engine.load_gltf_scene(
        "models/Fox.glb",
        load_textures=True,
        load_materials=True,
        create_entities=True,
        load_skins=True,
        load_animations=True,
        name_prefix="fox",
    )

    print(f"\n=== Fox Loaded ===")
    print(f"  {result}")
    print(f"  Skeletons: {result.skeleton_count}")
    print(f"  Animation clips:")
    for i, (name, dur) in enumerate(result.animation_clips):
        print(f"    [{i}] {name} ({dur:.2f}s)")

    # Find all entities that have animation clips
    scene = engine.scene
    for entity in result.entities:
        if scene.get_animation_clip_count(entity) > 0:
            animated_entities.append(entity)
            # Start playing first clip, looping
            scene.set_animation_looping(entity, True)
            scene.play_animation(entity, 0)

    print(f"  Animated entities: {len(animated_entities)}")
    print()
    print("Controls:")
    print("  4 / 5 / 6   - Switch animation clip")
    print("  Space        - Pause / resume")
    print("  + / =        - Speed up (x1.5)")
    print("  -            - Slow down (x1.5)")
    print("  W/A/S/D/Q/E  - Move camera")
    print("  Right mouse  - Look around")
    print()


def on_key_pressed(key):
    global anim_speed
    scene = engine.scene

    for entity in animated_entities:
        clip_count = scene.get_animation_clip_count(entity)

        # Keys 4, 5, 6 → play clip 0, 1, 2
        if key == 52 and clip_count > 0:       # '4'
            scene.play_animation(entity, 0)
            print(f"Playing clip 0: {scene.get_animation_clip_name(entity, 0)}")

        elif key == 53 and clip_count > 1:     # '5'
            scene.play_animation(entity, 1)
            print(f"Playing clip 1: {scene.get_animation_clip_name(entity, 1)}")

        elif key == 54 and clip_count > 2:     # '6'
            scene.play_animation(entity, 2)
            print(f"Playing clip 2: {scene.get_animation_clip_name(entity, 2)}")

        # Space → toggle pause/resume
        elif key == 32:
            if scene.is_animation_playing(entity):
                scene.stop_animation(entity)
                print("Animation paused")
            else:
                current = scene.get_current_animation_clip(entity)
                if current >= 0:
                    scene.play_animation(entity, current)
                    print("Animation resumed")

        # + / = → speed up
        elif key in (61, 334):   # '=' or numpad '+'
            anim_speed = min(anim_speed * 1.5, 10.0)
            scene.set_animation_speed(entity, anim_speed)
            print(f"Animation speed: {anim_speed:.2f}x")

        # - → slow down
        elif key in (45, 333):   # '-' or numpad '-'
            anim_speed = max(anim_speed / 1.5, 0.1)
            scene.set_animation_speed(entity, anim_speed)
            print(f"Animation speed: {anim_speed:.2f}x")


engine.set_on_init(on_init)
engine.set_on_key_pressed(on_key_pressed)
engine.run()
