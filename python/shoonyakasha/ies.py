"""IES light profiles for the default pipeline.

load() reads an IES LM-63 photometric file (type C photometry, the usual
kind for lamps and luminaires); atlas() turns up to 16 profiles into the
default pipeline's "lightProfiles" image, a 4 x 4 grid of tiles numbered 1
to 16 left to right, top to bottom. A point or spot light without a
rectangle follows profile N when its image slot is slot(N):

    from shoonyakasha import ies

    downlight = ies.load("downlight.ies")
    engine.set_pipeline_image_data("lightProfiles", ies.atlas([downlight]), format="R16_SFLOAT")
    engine.scene.set_light_source_image(light, ies.slot(1))

Vertical angle 0 points along the light's direction and 180 away from it;
horizontal angle 0 lies along the light's right axis and 90 along its up
axis. Each tile holds its profile divided by the profile's peak, so the
light's intensity is its intensity in its brightest direction.
"""

import re

TILES_ACROSS = 4
SLOT_OFFSET = 16


class Profile:
    """A light's intensity by direction, read from an IES file.

    vertical_angles and horizontal_angles are in degrees, as listed in the
    file; candela[h][v] is the intensity at horizontal_angles[h] and
    vertical_angles[v], the file's values times its candela multiplier,
    ballast factor and ballast-lamp factor. keywords holds the header's
    [KEYWORD] lines, without brackets.
    """

    def __init__(self, vertical_angles, horizontal_angles, candela, keywords=None):
        self.vertical_angles = list(vertical_angles)
        self.horizontal_angles = list(horizontal_angles)
        self.candela = [list(row) for row in candela]
        self.keywords = dict(keywords or {})

    @property
    def peak(self):
        """The highest intensity in the file, in candela."""
        return max(max(row) for row in self.candela)

    def sample(self, vertical, horizontal):
        """Intensities in candela at the given angles in degrees (NumPy
        arrays or numbers, broadcast together), interpolated linearly. The
        file's symmetry fills in horizontal angles it does not list; vertical
        angles outside its range give 0. Needs NumPy.
        """
        import numpy as np

        vertical, horizontal = np.broadcast_arrays(np.asarray(vertical, np.float64),
                                                   np.asarray(horizontal, np.float64))
        vs = np.asarray(self.vertical_angles, np.float64)
        hs = np.asarray(self.horizontal_angles, np.float64)
        cd = np.asarray(self.candela, np.float64)
        # Each listed horizontal angle's column, at the wanted vertical angles.
        flat_v = vertical.ravel()
        columns = np.stack([np.interp(flat_v, vs, row, left=0.0, right=0.0) for row in cd])
        if len(hs) == 1:
            return columns[0].reshape(vertical.shape)

        c = np.mod(horizontal.ravel(), 360.0)
        first, last = hs[0], hs[-1]
        if (first, last) == (0.0, 90.0):
            c = np.where(c > 180.0, 360.0 - c, c)
            c = np.where(c > 90.0, 180.0 - c, c)
        elif (first, last) == (0.0, 180.0):
            c = np.where(c > 180.0, 360.0 - c, c)
        elif (first, last) == (90.0, 270.0):
            c = np.where((c < 90.0) | (c > 270.0), np.mod(180.0 - c, 360.0), c)
        elif last - first < 360.0:
            # A full turn listed without its end: it closes on the first angle.
            c = np.where(c < first, c + 360.0, c)
            hs = np.append(hs, first + 360.0)
            columns = np.concatenate([columns, columns[:1]])
        index = np.clip(np.searchsorted(hs, c, side="right") - 1, 0, len(hs) - 2)
        h0, h1 = hs[index], hs[index + 1]
        t = np.clip((c - h0) / np.maximum(h1 - h0, 1e-9), 0.0, 1.0)
        points = np.arange(len(c))
        out = columns[index, points] * (1.0 - t) + columns[index + 1, points] * t
        return out.reshape(vertical.shape)


def parse(text):
    """The Profile in the text of an IES LM-63 file. Raises ValueError when
    the text is not one, or its photometry is not type C."""
    lines = text.splitlines()
    keywords = {}
    tilt_line = None
    for n, line in enumerate(lines):
        stripped = line.strip()
        if stripped.upper().startswith("TILT"):
            tilt_line = n
            break
        match = re.match(r"\[(\w+)\]\s*(.*)", stripped)
        if match:
            keywords[match.group(1).upper()] = match.group(2)
    if tilt_line is None:
        raise ValueError("no TILT= line: not an IES file")
    tilt = lines[tilt_line].split("=", 1)[1].strip().upper() if "=" in lines[tilt_line] else ""
    tokens = " ".join(lines[tilt_line + 1:]).replace(",", " ").split()
    try:
        numbers = [float(t) for t in tokens]
    except ValueError as error:
        raise ValueError("not an IES file: %s" % error)

    position = 0

    def take(count):
        nonlocal position
        if position + count > len(numbers):
            raise ValueError("the IES file ends early")
        values = numbers[position:position + count]
        position += count
        return values

    if tilt == "INCLUDE":
        take(1)                                     # lamp-to-luminaire geometry
        tilt_count = int(take(1)[0])
        take(2 * tilt_count)                        # tilt angles and factors
    (_lamps, _lumens, multiplier, vertical_count, horizontal_count,
     photometric_type, _units, _width, _length, _height) = take(10)
    ballast, ballast_lamp, _watts = take(3)
    if int(photometric_type) != 1:
        raise ValueError("photometric type %d: only type C is read" % int(photometric_type))
    vertical_count, horizontal_count = int(vertical_count), int(horizontal_count)
    if vertical_count < 1 or horizontal_count < 1:
        raise ValueError("the IES file lists no angles")
    vertical = take(vertical_count)
    horizontal = take(horizontal_count)
    scale = multiplier * ballast * ballast_lamp
    candela = [[value * scale for value in take(vertical_count)] for _ in range(horizontal_count)]
    return Profile(vertical, horizontal, candela, keywords)


def load(path):
    """The Profile in the IES file at path."""
    with open(path, "r", encoding="latin-1") as f:
        return parse(f.read())


def atlas(profiles, tile_size=128):
    """The lightProfiles image for up to 16 profiles, as a (4 * tile_size,
    4 * tile_size) float16 NumPy array for engine.set_pipeline_image_data
    with format="R16_SFLOAT".

    In each tile, rows run from vertical angle 0 at the top to 180 at the
    bottom and columns from horizontal angle 0 to 360, sampled at texel
    centres. Each profile is divided by its peak. Tiles not given are 1.
    Raises ValueError for more than 16 profiles or one without light.
    """
    import numpy as np

    profiles = list(profiles)
    if len(profiles) > TILES_ACROSS * TILES_ACROSS:
        raise ValueError("at most %d profiles" % (TILES_ACROSS * TILES_ACROSS))
    size = TILES_ACROSS * tile_size
    out = np.ones((size, size), np.float16)
    centres = (np.arange(tile_size) + 0.5) / tile_size
    vertical = (centres * 180.0)[:, None]
    horizontal = (centres * 360.0)[None, :]
    for n, profile in enumerate(profiles):
        peak = profile.peak
        if peak <= 0.0:
            raise ValueError("profile %d gives no light" % (n + 1))
        y, x = divmod(n, TILES_ACROSS)
        tile = profile.sample(vertical, horizontal) / peak
        out[y * tile_size:(y + 1) * tile_size, x * tile_size:(x + 1) * tile_size] = tile
    return out


def slot(n):
    """The image slot for scene.set_light_source_image that makes a light
    follow profile n, 1 to 16."""
    if not 1 <= n <= TILES_ACROSS * TILES_ACROSS:
        raise ValueError("profiles are numbered 1 to %d" % (TILES_ACROSS * TILES_ACROSS))
    return SLOT_OFFSET + n
