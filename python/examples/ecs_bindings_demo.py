"""
Shoonyakasha Low-Level ECS Bindings Demo

Demonstrates engine.ecs: attaching custom Python-defined components to
entities and registering custom per-frame systems that run through the
same SystemManager/priority ordering as built-in systems.

Usage:
    python ecs_bindings_demo.py

Requirements:
    - Build with -DBUILD_PYTHON=ON
    - Run from this directory (so pipeline.json and shaders/ resolve)
"""

import sys
import os

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..'))

import shoonyakasha as sk

engine = sk.Engine(
    title="Shoonyakasha ECS Bindings Demo",
    width=1280, height=720,
    pipeline_json_path="pipeline.json",
)


class Health:
    """A plain Python class - no base class required to be a component."""

    def __init__(self, hp=100.0):
        self.hp = hp
        self.max_hp = hp


class Regen:
    def __init__(self, rate=5.0):
        self.rate = rate


entities = []


def on_init():
    engine.create_camera(pos=(0.0, 5.0, 15.0), fov=60.0)

    # A few entities with custom components attached.
    for i in range(3):
        e = engine.scene.create_entity(f"Unit{i}")
        engine.ecs.set_component(e, "Health", Health(hp=50.0 + i * 10))
        engine.ecs.set_component(e, "Regen", Regen(rate=2.0 + i))
        entities.append(e)

    # A regen system: runs every frame, finds every entity with both
    # Health and Regen components, and heals it over time.
    def regen_system(dt):
        for entity in engine.ecs.find_entities_with_component("Health"):
            if not engine.ecs.has_component(entity, "Regen"):
                continue
            health = engine.ecs.get_component(entity, "Health")
            regen = engine.ecs.get_component(entity, "Regen")
            health.hp = min(health.max_hp, health.hp + regen.rate * dt)

    engine.ecs.add_system("Regen", regen_system, priority=50)

    # A deliberately broken system, to demonstrate auto-disable: it raises
    # every frame, gets caught (traceback printed) and reported as a
    # failure, and disables itself after 3 consecutive failures.
    def flaky_system(dt):
        raise RuntimeError("boom")

    engine.ecs.add_system("Flaky", flaky_system, priority=60, max_consecutive_failures=3)


def on_update(dt):
    if engine.ecs.has_system("Flaky") and not engine.ecs.is_system_enabled("Flaky"):
        print(f"[Python] 'Flaky' auto-disabled after "
              f"{engine.ecs.get_system_failure_count('Flaky')} consecutive failures")
        engine.ecs.remove_system("Flaky")


engine.set_on_init(on_init)
engine.set_on_update(on_update)
engine.run()
