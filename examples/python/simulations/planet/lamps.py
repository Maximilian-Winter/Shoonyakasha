"""
The examples' local lights (planet.py, walker.py), as
shoonyakasha.planet.world.Light values for PlanetWorld.lights. Colours are
the illuminance at 1 m in the sun's units (world.SUN_ILLUMINANCE), ranges
metres, cones the inner and outer half angles in degrees.

`lights_on(world, camera)` whether the lights are on: the sun below
LIGHTS_ON_ELEVATION seen from `camera` (planet space). `outpost_lights`
a light OUTPOST_LIGHT_HEIGHT over each outpost's pad. `headlights` two
spots at `offsets` (right, up, back) from a vehicle's centre in its frame,
along its nose dipped by `dip` degrees. `suit_lamp` a spot SUIT_LAMP from
the eye in the view's frame, along the view (the gun's viewmodel is
outside its cone).
"""

import math

from shoonyakasha.planet import world as planet_world
from shoonyakasha.planet.vec import add, dot, normalize, scale, sub

LIGHTS_ON_ELEVATION = -2.0       # degrees
OUTPOST_LIGHT_HEIGHT = 10.0      # m above the pad
OUTPOST_LIGHT_COLOUR, OUTPOST_LIGHT_RANGE = (150.0, 135.0, 115.0), 60.0
HEADLIGHT_COLOUR, HEADLIGHT_RANGE, HEADLIGHT_CONE = (800.0, 800.0, 750.0), 200.0, (10.0, 20.0)
SUIT_LAMP = (-0.12, 0.08, 0.0)   # m from the eye along the view's right, up and forward
SUIT_LAMP_COLOUR, SUIT_LAMP_RANGE, SUIT_LAMP_CONE = (80.0, 78.0, 72.0), 45.0, (14.0, 26.0)


def lights_on(world, camera):
    body = world.body_under(camera)
    radial = normalize(body.relative(camera))
    return dot(world.sun_direction(), radial) < math.sin(math.radians(LIGHTS_ON_ELEVATION))


def outpost_lights(outposts):
    lights = []
    for o in outposts:
        point = o.point()
        up = normalize(sub(point, o.body.centre()))
        lights.append(planet_world.Light(add(point, scale(up, OUTPOST_LIGHT_HEIGHT)), OUTPOST_LIGHT_COLOUR,
                                         OUTPOST_LIGHT_RANGE))
    return lights


def headlights(centre, right, up, forward, offsets, dip):
    d = math.radians(dip)
    beam = normalize(sub(scale(forward, math.cos(d)), scale(up, math.sin(d))))
    inner, outer = (math.radians(a) for a in HEADLIGHT_CONE)
    return [planet_world.Light(add(centre, add(add(scale(right, r), scale(up, u)), scale(forward, -b))),
                               HEADLIGHT_COLOUR, HEADLIGHT_RANGE, beam, inner, outer) for r, u, b in offsets]


def suit_lamp(eye, right, up, forward):
    at = add(eye, add(add(scale(right, SUIT_LAMP[0]), scale(up, SUIT_LAMP[1])), scale(forward, SUIT_LAMP[2])))
    inner, outer = (math.radians(a) for a in SUIT_LAMP_CONE)
    return planet_world.Light(at, SUIT_LAMP_COLOUR, SUIT_LAMP_RANGE, forward, inner, outer)
