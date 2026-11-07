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

`reverb_from(distances)`: the reverb of the space around the listener,
from how far rays cast from it in every direction went before they hit
something (None for a ray that hit nothing within REVERB_REACH): (RT60 of
the low, mid and high bands in seconds, wet level). Its enclosure is the
share of rays that hit; the RT60 grows from OPEN_RT60 with the enclosure
and the square root of the mean distance hit (to REVERB_ROOM), and the
wet level from OPEN_WET with the enclosure. `occluded_audio(blocked)`:
(gain, cutoff Hz) for a sound with something between it and the
listener (OCCLUDED_GAIN, OCCLUDED_CUTOFF), or (1, None) without.
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
REVERB_REACH = 150.0         # m a reverb ray reaches
REVERB_ROOM = 50.0           # m of mean distance hit at which the RT60 stops growing with it
OPEN_RT60, ENCLOSED_RT60 = 0.2, 1.8          # s, nothing around; added fully enclosed
OPEN_WET, ENCLOSED_WET = 0.05, 0.4           # wet level, nothing around; added fully enclosed
BANDS = (1.2, 1.0, 0.6)      # the low, mid and high bands' RT60 as shares of the mid's
OCCLUDED_GAIN = 0.5
OCCLUDED_CUTOFF = 700.0      # Hz


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


def reverb_from(distances):
    hits = [d for d in distances if d is not None]
    enclosure = len(hits) / len(distances) if distances else 0.0
    size = math.sqrt(min(1.0, (sum(hits) / len(hits)) / REVERB_ROOM)) if hits else 0.0
    rt60 = OPEN_RT60 + ENCLOSED_RT60 * enclosure * size
    return tuple(rt60 * share for share in BANDS), OPEN_WET + ENCLOSED_WET * enclosure


def occluded_audio(blocked):
    return (OCCLUDED_GAIN, OCCLUDED_CUTOFF) if blocked else (1.0, None)
