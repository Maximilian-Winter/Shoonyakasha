"""
The walker's suit (walker.py): the temperature around the player and the
suit's reserve against it. Plain Python, no engine.

`local_temperature(temperature, moisture, hour, wind_speed)` is the air's
temperature (degrees C) from the climate's (the probe's under the player,
with its height's lapse) at the local `hour`: a daily swing of SWING_WET
plus SWING_DRY times the dryness (1 - moisture) about it, warmest at
WARMEST_HOUR, less WIND_CHILL per m/s of wind where the climate is
colder than CHILL_BELOW. `moon_temperature(sun_sine)` is the moon's, which
has no air: MOON_SHADE in the dark, rising to MOON_SUN as the sun's
sine of elevation rises to MOON_FULL_SUN.

`SuitParams`: the suit spends its reserve beyond `cold_limit` and
`hot_limit`, `drain` of it per second per degree beyond, from `reserve`
(its capacity); sheltered it recharges by `recharge` a second; health
recovers by `heal` a second at an outpost. `SuitState` holds the
`reserve` and `health` (0 to 1). `step_suit(state, params, temperature,
sheltered, at_outpost, dt)` moves them on by `dt` seconds: sheltered
(in the car, or on an outpost's pad), the reserve recharges and, at an
outpost, the health recovers; else the reserve drains by how far the
temperature is beyond the limits, and once it is empty the health drains
by HEALTH_DRAIN times what the reserve could not give. Returns whether the
health has run out.

`effects(state, params, temperature)`: (the screen tint's colour and
alpha, the movement's factor): frost-blue below the cold limit or
heat-orange above the hot one, its alpha rising to TINT_ALPHA as the
reserve falls from TINT_FROM to 0; movement SLOW_FACTOR below
SLOW_BELOW of the reserve.
"""

import math
from dataclasses import dataclass

SWING_WET, SWING_DRY = 4.0, 14.0         # degrees C of the daily swing, wet and its addition when dry
WARMEST_HOUR = 15.0
WIND_CHILL = 0.4                         # degrees C per m/s of wind
CHILL_BELOW = 10.0                       # degrees C
MOON_SUN, MOON_SHADE = 90.0, -150.0      # degrees C
MOON_FULL_SUN = 0.2                      # the sun's sine of elevation from which the moon's ground is at MOON_SUN
HEALTH_DRAIN = 0.5                       # health per unit of reserve the suit could not give
TINT_FROM, TINT_ALPHA = 0.5, 0.35
FROST, HEAT = (0.55, 0.78, 1.0), (1.0, 0.5, 0.2)
SLOW_BELOW, SLOW_FACTOR = 0.2, 0.6


@dataclass
class SuitParams:
    cold_limit: float = -5.0             # degrees C: below, the suit spends its reserve
    hot_limit: float = 38.0              # degrees C: above, too
    reserve: float = 1.0                 # its capacity
    drain: float = 0.0008                # reserve a second per degree beyond the limits
    recharge: float = 0.05               # reserve a second, sheltered
    heal: float = 0.02                   # health a second at an outpost


@dataclass
class SuitState:
    reserve: float = 1.0
    health: float = 1.0


def local_temperature(temperature, moisture, hour, wind_speed):
    dry = 1.0 - max(0.0, min(1.0, moisture))
    swing = SWING_WET + SWING_DRY * dry
    diurnal = swing * math.cos(2.0 * math.pi * (hour - WARMEST_HOUR) / 24.0)
    chill = WIND_CHILL * wind_speed if temperature < CHILL_BELOW else 0.0
    return temperature + diurnal - chill


def moon_temperature(sun_sine):
    x = max(0.0, min(1.0, sun_sine / MOON_FULL_SUN))
    return MOON_SHADE + (MOON_SUN - MOON_SHADE) * x


def beyond(params, temperature):
    """Degrees beyond the suit's limits (0 within them)."""
    return max(0.0, params.cold_limit - temperature, temperature - params.hot_limit)


def step_suit(state, params, temperature, sheltered, at_outpost, dt):
    if sheltered:
        state.reserve = min(params.reserve, state.reserve + params.recharge * dt)
        if at_outpost:
            state.health = min(1.0, state.health + params.heal * dt)
        return False
    state.reserve -= beyond(params, temperature) * params.drain * dt
    if state.reserve < 0.0:
        state.health = max(0.0, state.health + HEALTH_DRAIN * state.reserve)
        state.reserve = 0.0
    return state.health <= 0.0


def effects(state, params, temperature):
    share = state.reserve / params.reserve if params.reserve > 0.0 else 0.0
    colour = FROST if temperature < 0.5 * (params.cold_limit + params.hot_limit) else HEAT
    alpha = TINT_ALPHA * max(0.0, min(1.0, (TINT_FROM - share) / TINT_FROM))
    return (colour, alpha), (SLOW_FACTOR if share < SLOW_BELOW else 1.0)
