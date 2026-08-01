"""{project} — a Shoonyakasha starter.

Run it:

    python main.py

Shaders are compiled on startup, so editing shaders/basic.frag and running
again is enough to see the change — there is no separate build step.
"""

import shoonyakasha as sk


def main():
    # Compile any shader whose .spv is older than its source. Cheap when nothing
    # changed, and it means the .spv files can never silently drift.
    built = sk.shaders.compile_dir("shaders")
    if built:
        print("compiled %d shader(s)" % len(built))

    # Catch pipeline mistakes here, with the pass name and the offending key,
    # rather than as an exception from the C++ compiler.
    for problem in sk.pipeline.validate("pipeline.json"):
        print(problem)

    engine = sk.Engine(
        title="{project}",
        width=1280,
        height=720,
        pipeline_json_path="pipeline.json",
    )

    def on_init():
        # Paths are resolved against the shared assets/ directory.
        engine.load_gltf_scene("models/Box.gltf")

        engine.create_camera(position=(0.0, 1.5, 5.0), fov=60.0)
        engine.create_directional_light(
            direction=(-0.4, -1.0, -0.3), color=(1.0, 0.97, 0.92), intensity=3.0)

    engine.set_on_init(on_init)
    engine.run()


if __name__ == "__main__":
    main()
