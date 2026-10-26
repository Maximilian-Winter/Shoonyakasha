"""Planets drawn with procedural terrain on the planet pipeline: a cube-sphere
with quadtree level of detail built on the GPU, an atmosphere, clouds, an
ocean and a moon.

    from shoonyakasha.planet import cameras, settings, world

    world      PlanetWorld, the pipeline (PIPELINE) and compile_shaders
    cameras    GlobeCamera and FocusCamera
    settings   settings-menu items for a PlanetWorld
    orbits     the bodies' gravity and air, and orbits around them
    vec        vectors as tuples
    cube_sphere, quadtree, shadows, ground_textures

Importing this package does not require Vulkan or the native extension;
PlanetWorld takes the engine as an argument.
"""
