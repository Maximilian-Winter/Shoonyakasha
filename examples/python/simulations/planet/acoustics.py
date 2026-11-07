"""
How the air carries sound to the listener (play/sounds.py).

`air_audio(d)`: the world bus's gain and low-pass cutoff for the air's
density at the listener, `d` (a share of the density at the planet's
surface): silent at or below VACUUM, else from 0.25 rising with the
square root of the density to 1 at the surface, the cutoff from 400 Hz
to 20 kHz. `contact_volume(distance)`: in vacuum, how loud something
struck `distance` metres away is through the suit, from 1 touching to 0
at CONTACT_DISTANCE.

`sound_delay(distance)`: seconds sound takes to travel `distance` metres
of air, at SOUND_SPEED. `distance_cutoff(distance)`: the low-pass cutoff
the air leaves a sound at after `distance` metres, NEAR_CUTOFF within
NEAR_DISTANCE falling as a power of the distance to FAR_CUTOFF at
FAR_DISTANCE, and on no lower than LOWEST_CUTOFF.
`rumble_volume(distance, large)`: in vacuum, how loud a meteor's impact
`distance` metres away is through the ground and the feet, from 1 at the
impact to 0 at RUMBLE_REACH (a large meteor's) or SMALL_RUMBLE_REACH.
"""

import math

VACUUM = 1e-4                # density share at or below which there is no air to carry sound
VACUUM_CUTOFF = 200.0        # Hz, the world bus's cutoff in vacuum
CONTACT_DISTANCE = 3.0       # m within which a strike is heard through the suit in vacuum
SOUND_SPEED = 343.0          # m/s
NEAR_DISTANCE, NEAR_CUTOFF = 10.0, 18_000.0     # m, Hz
FAR_DISTANCE, FAR_CUTOFF = 5_000.0, 2_000.0     # m, Hz
LOWEST_CUTOFF = 300.0        # Hz
RUMBLE_REACH = 1_500.0       # m a large meteor's impact is felt through the ground in vacuum
SMALL_RUMBLE_REACH = 300.0   # m a small one's is


def air_audio(d):
    """(gain, cutoff Hz) of the world bus for a density share `d`."""
    if d <= VACUUM:
        return 0.0, VACUUM_CUTOFF
    share = min(1.0, d)
    return min(1.0, 0.25 + 0.75 * share ** 0.5), 400.0 + 19_600.0 * share ** 0.7


def contact_volume(distance):
    return max(0.0, 1.0 - distance / CONTACT_DISTANCE)


def sound_delay(distance):
    return distance / SOUND_SPEED


def distance_cutoff(distance):
    if distance <= NEAR_DISTANCE:
        return NEAR_CUTOFF
    power = math.log(NEAR_CUTOFF / FAR_CUTOFF) / math.log(FAR_DISTANCE / NEAR_DISTANCE)
    return max(LOWEST_CUTOFF, NEAR_CUTOFF * (distance / NEAR_DISTANCE) ** -power)


def rumble_volume(distance, large):
    return max(0.0, 1.0 - distance / (RUMBLE_REACH if large else SMALL_RUMBLE_REACH))
