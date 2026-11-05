"""
The ores' resources (resources.glsl's RESOURCE_ ids), and in NumPy the
shader's choice of a resource from its inputs.

RESOURCES names each id. `planet_weights(temperature, moisture,
mountains)` gives the planet's resources' weights, ferrite first (an
array of PLANET_RESOURCES along the last axis), from the base temperature
(degrees C), the moisture and the mountains' mask, as
planetResourceWeights does; `planet_pick(weights, noise)` the id of the
highest weight times 1 + RESOURCE_NOISE times its noise (each resource's
planetResourceNoise), ferrite where they tie, as planetResourcePick does.
`moon_resource(crater_y)` is ice where the Y component of the richest ore
crater's centre (a unit direction) is beyond MOON_ICE_LATITUDE, ilmenite
elsewhere, as moonResource does.
"""

import numpy as np

NONE = 0
FERRITE = 1
CUPRITE = 2
QUARTZ = 3
AMBER = 4
EVAPORITE = 5
ICE = 6
ILMENITE = 7
RESOURCES = {NONE: "none", FERRITE: "Ferrite", CUPRITE: "Cuprite", QUARTZ: "Quartz", AMBER: "Amber",
             EVAPORITE: "Evaporite", ICE: "Ice", ILMENITE: "Ilmenite"}
PLANET_RESOURCES = 5             # ferrite to evaporite
RESOURCE_NOISE = 0.6
MOON_ICE_LATITUDE = 0.55


def smoothstep(low, high, x):
    t = np.clip((np.asarray(x, dtype=np.float64) - low) / (high - low), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def falling(low, high, x):
    """1 below `low`, falling to 0 at `high`."""
    return 1.0 - smoothstep(low, high, x)


def planet_weights(temperature, moisture, mountains):
    return np.stack([
        0.55 * smoothstep(0.3, 0.9, mountains),
        falling(0.3, 0.5, moisture) * smoothstep(3.0, 12.0, temperature) * falling(16.0, 24.0, temperature),
        falling(-4.0, 2.0, temperature),
        smoothstep(0.5, 0.75, moisture) * smoothstep(10.0, 20.0, temperature),
        falling(0.25, 0.4, moisture) * smoothstep(12.0, 20.0, temperature),
    ], axis=-1)


def planet_pick(weights, noise):
    scored = np.asarray(weights) * (1.0 + RESOURCE_NOISE * np.asarray(noise))
    return FERRITE + np.argmax(scored, axis=-1)


def moon_resource(crater_y):
    return np.where(np.abs(crater_y) > MOON_ICE_LATITUDE, ICE, ILMENITE)


def name(resource):
    return RESOURCES.get(int(resource), "none")
