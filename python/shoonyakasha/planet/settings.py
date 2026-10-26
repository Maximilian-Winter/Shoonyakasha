"""
Settings-menu items for a PlanetWorld, in the item format of
shoonyakasha.toolkit.overlay.SettingsMenu: a heading, then sliders, toggles
and buttons that read and change the world's settings.

    items = (settings.sky_items(world) + settings.cloud_items(world) + settings.ground_items(world)
             + settings.view_items(world) + settings.planet_items(world, edit, new_seed))

Each function takes `keys`, a dict from an item's name to the key names
that change it too, shown as caps beside it.
"""

from . import world as world_module


def _with_keys(items, keys):
    for item in items:
        name = item.get("slider") or item.get("toggle") or item.get("button")
        if keys and name in keys:
            item["keys"] = tuple(keys[name])
    return items


def _set_and(world, attribute, then):
    def setter(value):
        setattr(world, attribute, value)
        then()
    return setter


def sky_items(world, keys=None, site="the site"):
    """Exposure, the atmosphere's density, multiple scattering, ozone, and
    the sun's elevation and azimuth seen from `site`."""
    return _with_keys([
        {"heading": "Sky"},
        {"slider": "Exposure", "min": 0.05, "max": 2.0, "get": lambda: world.exposure,
         "set": _set_and(world, "exposure", world.upload_sky), "format": "%.2f",
         "hint": "The image's brightness before tone mapping."},
        {"slider": "Atmosphere density", "min": 0.25, "max": 4.0, "get": lambda: world.density,
         "set": _set_and(world, "density", world.upload_sky), "format": "x%.2f",
         "hint": "Scales the air and the haze: the sky's colour, the haze far off and the air's drag."},
        {"toggle": "Multiple scattering", "get": lambda: world.multiple_scattering,
         "set": _set_and(world, "multiple_scattering", world.upload_sky),
         "hint": "The light the air scatters more than once, from the multiple scattering table."},
        {"toggle": "Ozone", "get": lambda: world.ozone, "set": _set_and(world, "ozone", world.upload_sky),
         "hint": "The ozone layer's absorption of the light passing through it."},
        {"slider": "Sun elevation", "min": -20.0, "max": 90.0, "get": lambda: world.sun_elevation,
         "set": _set_and(world, "sun_elevation", world.upload_sky), "format": "%.0f deg",
         "hint": "The sun's height above the horizon, seen from %s." % site},
        {"slider": "Sun azimuth", "min": 0.0, "max": 360.0, "get": lambda: world.sun_azimuth,
         "set": _set_and(world, "sun_azimuth", world.upload_sky), "format": "%.0f deg",
         "hint": "The sun's direction seen from %s, from east towards north." % site},
    ], keys)


def cloud_items(world, keys=None):
    """The clouds on or off, their cover, density and wind."""
    return _with_keys([
        {"heading": "Clouds"},
        {"toggle": "Clouds", "get": lambda: world.clouds, "set": _set_and(world, "clouds", world.upload_clouds),
         "hint": "The cloud layer between 1.5 and 3.5 km, and its shadows."},
        {"slider": "Cloud cover", "min": 0.0, "max": 1.0, "get": lambda: world.cloud_coverage,
         "set": _set_and(world, "cloud_coverage", world.upload_clouds), "format": "%.2f",
         "hint": "The share of the sky the clouds cover."},
        {"slider": "Cloud density", "min": 0.005, "max": 0.1, "get": lambda: world.cloud_density,
         "set": _set_and(world, "cloud_density", world.upload_clouds), "format": "%.3f /m",
         "hint": "How much light a metre of cloud stops at full density."},
        {"slider": "Wind", "min": 0.0, "max": 60.0, "get": lambda: world.wind_speed,
         "set": lambda v: setattr(world, "wind_speed", v), "format": "%.0f m/s",
         "hint": "How fast the clouds drift, times the time warp."},
    ], keys)


def ground_items(world, keys=None):
    """The ground textures' strength and relief, the ocean, the shadows, the vegetation and the grass."""
    return _with_keys([
        {"heading": "Ground"},
        {"slider": "Ground textures", "min": 0.0, "max": 1.0, "get": lambda: world.texture_strength,
         "set": lambda v: setattr(world, "texture_strength", v), "format": "%.2f",
         "hint": "How much the ground's textures show over its colours; 0 leaves the colours alone. "
                 "Needs the textures (python -m shoonyakasha.planet.ground_textures)."},
        {"slider": "Ground relief", "min": 0.0, "max": 2.0, "get": lambda: world.texture_relief,
         "set": lambda v: setattr(world, "texture_relief", v), "format": "%.2f",
         "hint": "The strength of the textures' normal maps in the light."},
        {"toggle": "Ocean", "get": lambda: world.ocean, "set": world.set_ocean,
         "hint": "The sea's surface; off, the sea floor shows."},
        {"toggle": "Shadows", "get": lambda: world.shadows, "set": lambda on: setattr(world, "shadows", on),
         "hint": "The sun's shadow cascades, out to 20 km."},
        {"toggle": "Vegetation", "get": lambda: world.vegetation, "set": lambda on: setattr(world, "vegetation", on),
         "hint": "Trees and bushes where the ground is forest, grass or dry, within the vegetation's range."},
        {"slider": "Vegetation density", "min": 0.0, "max": 1.0, "get": lambda: world.vegetation_density,
         "set": lambda v: setattr(world, "vegetation_density", v), "format": "%.2f",
         "hint": "Times the biome's chance of a plant in each 10 m cell."},
        {"slider": "Vegetation range", "min": 500.0, "max": 5000.0, "get": lambda: world.vegetation_range,
         "set": lambda v: setattr(world, "vegetation_range", v), "format": "%.0f m",
         "hint": "How far from the camera trees grow; they shrink away over its last 15 %."},
        {"toggle": "Grass", "get": lambda: world.grass, "set": lambda on: setattr(world, "grass", on),
         "hint": "Tufts of grass near the camera, green in grass and forest, towards straw on dry ground."},
        {"slider": "Grass range", "min": 50.0, "max": 400.0, "get": lambda: world.grass_range,
         "set": lambda v: setattr(world, "grass_range", v), "format": "%.0f m",
         "hint": "How far from the camera grass grows; tufts shrink away over its last 30 %."},
    ], keys)


def view_items(world, keys=None):
    """The wireframe, the split factor and the colour mode, under no heading."""
    planet = world.planet

    def set_split(value):
        if planet.tree is not None:
            planet.tree.split_factor = value

    return _with_keys([
        {"toggle": "Wireframe", "get": lambda: world.wireframe, "set": world.set_wireframe,
         "hint": "The chunks' edges drawn over the terrain."},
        {"slider": "Split factor", "min": 0.5, "max": 4.0,
         "get": lambda: planet.tree.split_factor if planet.tree is not None else world_module.SPLIT_FACTOR,
         "set": set_split, "format": "%.2f",
         "hint": "A chunk splits within this many of its edge lengths of the camera; higher is finer and slower."},
        {"button": "Colour: terrain / face / level",
         "do": lambda: setattr(world, "colour_mode", (world.colour_mode + 1) % len(world_module.COLOUR_MODES)),
         "hint": "Colour the ground by its terrain, by cube face or by quadtree level."},
    ], keys)


class TerrainEdit:
    """A copy of the planet's terrain parameters that the menu changes,
    taken on by `apply`, which builds the planet again."""

    def __init__(self, world):
        self.world = world
        self.reset()

    def reset(self):
        """The copy made again from the planet's parameters."""
        self.values = {name: list(value) for name, value in self.world.planet.terrain.items()}

    def get(self, name, index):
        return lambda: self.values[name][index]

    def set(self, name, index):
        def setter(value):
            self.values[name][index] = value
        return setter

    def apply(self):
        self.world.planet.terrain = {name: list(value) for name, value in self.values.items()}
        self.world.regenerate()
        self.reset()


def planet_items(world, edit, new_seed, keys=None):
    """A new seed (`new_seed()`, at once), and the terrain's mountain and
    hill heights, continent frequency, sea level bias, ocean depth, detail
    height and erosion in `edit` (a TerrainEdit), which Apply takes on."""
    return _with_keys([
        {"heading": "Planet"},
        {"button": "New seed", "do": new_seed, "hint": "Builds a new planet from a random seed at once."},
        {"slider": "Mountain height", "min": 0.0, "max": 8000.0, "get": edit.get("mountains", 1),
         "set": edit.set("mountains", 1), "format": "%.0f m", "hint": "Taken on by Apply, below."},
        {"slider": "Hill height", "min": 0.0, "max": 600.0, "get": edit.get("hills", 1),
         "set": edit.set("hills", 1), "format": "%.0f m", "hint": "Taken on by Apply, below."},
        {"slider": "Continent frequency", "min": 0.5, "max": 4.0, "get": edit.get("continent", 0),
         "set": edit.set("continent", 0), "format": "%.2f",
         "hint": "More, smaller continents as it rises. Taken on by Apply, below."},
        {"slider": "Sea level bias", "min": -0.4, "max": 0.4, "get": edit.get("continent", 2),
         "set": edit.set("continent", 2), "format": "%+.2f",
         "hint": "Raises the land out of the sea as it rises. Taken on by Apply, below."},
        {"slider": "Ocean depth", "min": 0.0, "max": 4000.0, "get": edit.get("hills", 2),
         "set": edit.set("hills", 2), "format": "%.0f m", "hint": "Taken on by Apply, below."},
        {"slider": "Detail height", "min": 0.0, "max": 30.0, "get": edit.get("detail", 1),
         "set": edit.set("detail", 1), "format": "%.1f m",
         "hint": "The relief of the ground's finest bumps, 80 m and less across. Taken on by Apply, below."},
        {"slider": "Erosion", "min": 0.0, "max": 4.0, "get": edit.get("detail", 3),
         "set": edit.set("detail", 3), "format": "%.2f",
         "hint": "How much the slopes smooth the hills and the detail. Taken on by Apply, below."},
        {"button": "Apply: build the planet again", "do": edit.apply,
         "hint": "Builds the planet again with the settings above."},
    ], keys)
