"""
How the air carries sound to the listener (play/sounds.py).

`air_audio(d)`: the world bus's gain and low-pass cutoff for the air's
density at the listener, `d` (a share of the density at the planet's
surface): silent at or below VACUUM, else from 0.25 rising with the
square root of the density to 1 at the surface, the cutoff from 400 Hz
to 20 kHz. `contact_volume(distance)`: in vacuum, how loud something
struck `distance` metres away is through the suit, from 1 touching to 0
at CONTACT_DISTANCE.
"""

VACUUM = 1e-4                # density share at or below which there is no air to carry sound
VACUUM_CUTOFF = 200.0        # Hz, the world bus's cutoff in vacuum
CONTACT_DISTANCE = 3.0       # m within which a strike is heard through the suit in vacuum


def air_audio(d):
    """(gain, cutoff Hz) of the world bus for a density share `d`."""
    if d <= VACUUM:
        return 0.0, VACUUM_CUTOFF
    share = min(1.0, d)
    return min(1.0, 0.25 + 0.75 * share ** 0.5), 400.0 + 19_600.0 * share ** 0.7


def contact_volume(distance):
    return max(0.0, 1.0 - distance / CONTACT_DISTANCE)
