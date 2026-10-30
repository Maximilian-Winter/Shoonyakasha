"""
The planet's biomes as biome.glsl classifies them, for naming the biome at
a point the probes measured (PlanetWorld.ground_climate), and the climate
settings' palettes.

A biome's weight comes from the temperature (degrees C) and the moisture
(0 .. 1) with the climate settings' offsets: four temperature bands
(edges TEMPERATURE_EDGES) by four moisture bands (edges MOISTURE_EDGES),
each edge a smoothstep TEMPERATURE_BLEND or MOISTURE_BLEND to either side,
mapped onto the biomes by TABLE. `weights` gives them; `dominant` the
biome with the greatest weight. Snow lies below SNOW_TEMPERATURE.

PALETTES are (name, flora hue turn, soil hue turn), in degrees, for the
climate settings' hue turns (biome.glsl's hueTurn).
"""

NAMES = ("desert", "savanna", "tropical forest", "steppe", "grassland", "deciduous forest", "taiga", "tundra")
DESERT, SAVANNA, TROPICAL, STEPPE, GRASSLAND, DECIDUOUS, TAIGA, TUNDRA = range(8)

TEMPERATURE_EDGES = (0.0, 7.0, 19.0)
MOISTURE_EDGES = (0.15, 0.32, 0.55)
TEMPERATURE_BLEND = 1.5
MOISTURE_BLEND = 0.04
SNOW_TEMPERATURE = -6.0

TABLE = (
    (TUNDRA, TUNDRA, TUNDRA, TUNDRA),
    (STEPPE, STEPPE, TAIGA, TAIGA),
    (DESERT, STEPPE, GRASSLAND, DECIDUOUS),
    (DESERT, DESERT, SAVANNA, TROPICAL),
)

PALETTES = (
    ("Earth", 0.0, 0.0),
    ("Crimson", 115.0, -25.0),
    ("Violet", 160.0, -150.0),
    ("Amber", 90.0, 20.0),
    ("Teal", -62.0, 40.0),
)


def _smoothstep(e0, e1, x):
    t = min(max((x - e0) / (e1 - e0), 0.0), 1.0)
    return t * t * (3.0 - 2.0 * t)


def _bands(x, edges, blend):
    s = [_smoothstep(e - blend, e + blend, x) for e in edges]
    return (1.0 - s[0], s[0] - s[1], s[1] - s[2], s[2])


def weights(temperature, moisture):
    """Each biome's weight (a list of 8 summing to 1) for a temperature
    (degrees C) and moisture, the climate settings' offsets already added;
    the moisture is clamped to 0 .. 1 first."""
    moisture = min(max(moisture, 0.0), 1.0)
    t = _bands(temperature, TEMPERATURE_EDGES, TEMPERATURE_BLEND)
    m = _bands(moisture, MOISTURE_EDGES, MOISTURE_BLEND)
    result = [0.0] * len(NAMES)
    for i in range(4):
        for j in range(4):
            result[TABLE[i][j]] += t[i] * m[j]
    return result


def dominant(temperature, moisture):
    """The index of the biome with the greatest weight."""
    w = weights(temperature, moisture)
    return max(range(len(w)), key=lambda b: w[b])


def snowy(temperature):
    """Whether snow lies at a temperature (on ground less steep than a cliff)."""
    return temperature < SNOW_TEMPERATURE


def card_rows(climate):
    """Rows for an info card: the dominant biome (with snow where it lies)
    and the temperature and moisture, for (temperature, moisture) with the
    settings' offsets; dashes for None."""
    if climate is None:
        return [("BIOME", "-"), ("CLIMATE", "-")]
    temperature, moisture = climate
    name = NAMES[dominant(temperature, moisture)]
    return [("BIOME", name + (", snow" if snowy(temperature) else "")),
            ("CLIMATE", "%.0f C, moisture %.2f" % (temperature, moisture))]
