"""
Shoonyakasha Sprite/UI/Text Demo

Demonstrates the 2D rendering mechanics: world-space sprites,
screen-space UI panels, and bitmap-font text labels - all rendered
through the single "sprite_geometry" pass declared in sprite_pipeline.json.
Text labels are baked into one sprite entity per glyph, so they reuse the
exact same pass/shader as textured sprites and flat-colored UI panels.

Usage:
    python sprite_ui_demo.py

Requirements:
    - Build with -DBUILD_PYTHON=ON
    - Compile sprite.vert/sprite.frag to shaders/sprite.vert.spv /
      shaders/sprite.frag.spv (glslc sprite.vert -o shaders/sprite.vert.spv)
    - Run from this directory (so sprite_pipeline.json and shaders/ resolve)
    - Have a sprite.png / panel.png image available, or point texture_path
      at your own art
    - Have a .ttf/.otf font file available for the text label (e.g. any
      system font), or remove the create_text() call below
"""

import sys
import os

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', '..', 'python'))

import shoonyakasha as sk

engine = sk.Engine(
    title="Shoonyakasha Sprite/UI Demo",
    width=1280, height=720,
    pipeline_json_path="sprite_pipeline.json",
)


def on_init():
    scene = engine.scene

    # World-space sprite: billboarded quad placed in 3D space, projected
    # with the active 3D camera.
    engine.create_camera(pos=(0.0, 0.0, 5.0), fov=60.0)
    engine.create_sprite(
        world_pos=(0.0, 0.0, 0.0),
        texture_path="sprite.png",
        size=(1.5, 1.5),
        tint=(1.0, 1.0, 1.0, 1.0),
    )

    # Screen-space UI panel anchored to the top-left corner.
    panel = engine.create_ui_panel(
        anchor=sk.UI_ANCHOR_TOP_LEFT,
        offset_pixels=(110, 40),
        size_pixels=(200, 60),
        texture_path="",  # flat-colored panel (no texture)
        color=(0.1, 0.1, 0.15, 0.85),
    )

    # A second panel anchored to the bottom-right corner, textured.
    engine.create_ui_panel(
        anchor=sk.UI_ANCHOR_BOTTOM_RIGHT,
        offset_pixels=(-90, -50),
        size_pixels=(160, 80),
        texture_path="panel.png",
    )

    # Text label anchored to the top-left panel's position, baked from a
    # TTF font into one glyph sprite entity per character.
    engine.create_text(
        text="Score: 0",
        anchor=sk.UI_ANCHOR_TOP_LEFT,
        offset_pixels=(20, 60),
        font_path="font.ttf",
        font_size=28.0,
        color=(1.0, 1.0, 1.0, 1.0),
    )

    print(f"[Python] Created top-left panel entity: {panel}")


engine.set_on_init(on_init)
engine.run()
