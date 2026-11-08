"""
Scanned rocks and plants (CC0, from Poly Haven's scenes) as vegetation kinds.

SCANS names each scan kind: the scene it comes from (SCENES: an archive
holding a .blend), the object in it, its triangle budget, its family
(where it grows: "desert" on dry ground, "forest" on the forest floor,
"rock" on rock and dry ground), the heights it is drawn at (m), whether
it is small (scattered on the scans' fine, near grid; else on their coarse,
far one), whether it is a rock (opaque, with a convex hull for its
collider) and how deep it sinks into the ground, as a share of its height.
CARD_SCANS are scanned twigs (the "card" family), which become foliage
cards of vegetation_textures' atlas rather than scattered scans; ALL is
both.
vegetation_scatter.comp holds the same table (its family, heights and
smallness per kind, in this order). The scans are prepared once (scan_export.py, with
Blender) into DIRECTORY/prepared/<kind>/: mesh.npz (positions in metres,
y up, its base's centre at the origin; normals, tangents with their
bitangent's sign, uvs, each vertex's material, triangle indices, and a
rock's hull points), materials.json (each material's albedo and normal
map, PNG, and whether it is cut out by its alpha) and meta.json, with
CREDITS.md listing them all.

The scenes' archives are found in the SHOONYAKASHA_SPACE_ASSETS
environment variable's directory, else assets/space_assets under the
current directory; DIRECTORY is SHOONYAKASHA_SCANS, else
shoonyakasha/space_assets in the user's cache (beside the ground's
textures).

`prepared(kind)` gives a kind's prepared data (Scan; of ALL), or None
when it is not prepared; `available()` whether every kind of SCANS is. `atlas()` packs every
prepared material's textures into an albedo atlas and a normal atlas of
CELL x CELL cells, ATLAS_COLUMNS a row (needs Pillow).
"""

import json
import os
from dataclasses import dataclass

import numpy as np

SCENES = {
    "namaqualand": ("namaqualand.zip", "Namaqualand.blend"),
    "pine_forest": ("pine_forest.zip", "polyhaven_pine_fir_forest.blend"),
    "verdant_trail": ("verdant_trail.zip", "verdant_trail.blend"),
}


@dataclass(frozen=True)
class ScanKind:
    name: str
    scene: str
    object: str
    triangles: int           # budget after decimation
    family: str              # "desert", "forest" or "rock"
    heights: tuple           # (lowest, highest) m it is drawn at
    small: bool = False      # on the near grid
    rock: bool = False
    sink: float = 0.0        # of its height pushed into the ground


SCANS = (
    ScanKind("nq_boulder_03", "namaqualand", "namaqualand_boulder_03_LOD3", 3000, "rock", (1.1, 1.8), rock=True,
             sink=0.12),
    ScanKind("nq_boulder_04", "namaqualand", "namaqualand_boulder_04_LOD3", 3000, "rock", (1.4, 2.4), rock=True,
             sink=0.12),
    ScanKind("nq_boulder_06", "namaqualand", "namaqualand_boulder_06_LOD3", 3000, "rock", (0.8, 1.4), rock=True,
             sink=0.12),
    ScanKind("nq_rocks_a", "namaqualand", "namaqualand_rocks_01_a_LOD3", 1000, "desert", (0.25, 0.5), small=True,
             rock=True, sink=0.15),
    ScanKind("nq_stones_b", "namaqualand", "namaqualand_stones_01_b_LOD3", 800, "desert", (0.25, 0.5), small=True,
             rock=True, sink=0.15),
    ScanKind("quiver_tree", "namaqualand", "quiver_tree_01_LOD3", 6000, "desert", (2.0, 3.6), sink=0.02),
    ScanKind("searsia", "namaqualand", "searsia_lucida_f_LOD0", 6000, "desert", (0.6, 1.2), sink=0.03),
    ScanKind("othonna", "namaqualand", "othonna_cerarioides_e_LOD0", 6000, "desert", (0.7, 1.3), sink=0.03),
    ScanKind("ursinia", "namaqualand", "flower_ursinia_b_LOD2", 1600, "desert", (0.12, 0.25), small=True, sink=0.05),
    ScanKind("fern_b", "pine_forest", "fern_01_b_lod0", 2400, "forest", (0.35, 0.7), small=True, sink=0.05),
    ScanKind("fern_d", "pine_forest", "fern_01_d_lod0", 1000, "forest", (0.3, 0.6), small=True, sink=0.05),
    ScanKind("dead_log", "pine_forest", "dead_tree_lod1", 3000, "forest", (0.25, 0.4), sink=0.25),
    ScanKind("dry_branches", "pine_forest", "dry_branches_medium_01_a_lod0", 2500, "forest", (0.25, 0.45),
             small=True, sink=0.2),
    ScanKind("vt_boulder", "verdant_trail", "boulder_01", 4000, "rock", (2.5, 4.5), rock=True, sink=0.12),
    ScanKind("vt_rock_face", "verdant_trail", "rock_face", 4000, "rock", (2.5, 4.5), rock=True, sink=0.15),
)
CARD_SCANS = (
    ScanKind("fir_twig_main", "pine_forest", "fir_twig_main_a", 4000, "card", (0.0, 0.0)),
    ScanKind("fir_twig_tip", "pine_forest", "fir_twig_tip_d", 2000, "card", (0.0, 0.0)),
)
ALL = SCANS + CARD_SCANS
NAMES = tuple(s.name for s in SCANS)
HULL_POINTS = 64             # a rock's hull has at most this many points
CELL = 512                   # texels a side of an atlas cell
ATLAS_COLUMNS = 8
FLAT_NORMAL = (128, 128, 255, 255)   # a cell's texels when its material has no normal map


def source_directory():
    return os.environ.get("SHOONYAKASHA_SPACE_ASSETS") or os.path.join(os.getcwd(), "assets", "space_assets")


def directory():
    chosen = os.environ.get("SHOONYAKASHA_SCANS")
    if chosen:
        return chosen
    base = os.environ.get("LOCALAPPDATA") or os.path.join(os.path.expanduser("~"), ".cache")
    return os.path.join(base, "shoonyakasha", "space_assets")


def prepared_directory(kind):
    return os.path.join(directory(), "prepared", kind)


@dataclass
class Scan:
    kind: ScanKind
    positions: np.ndarray    # (N, 3) m
    normals: np.ndarray      # (N, 3)
    tangents: np.ndarray     # (N, 4): xyz and the bitangent's sign
    uvs: np.ndarray          # (N, 2)
    materials: np.ndarray    # (N,) the vertex's material
    indices: np.ndarray      # (M,) uint32, three per triangle
    hull: np.ndarray         # (K, 3) m; empty for a plant
    textures: list           # per material: {"name", "albedo", "normal", "cutout"} (paths in its directory)

    @property
    def height(self):
        return float(self.positions[:, 1].max())


def prepared(kind):
    """The kind's prepared Scan (by ScanKind or name), or None."""
    if isinstance(kind, str):
        kind = next(s for s in ALL if s.name == kind)
    folder = prepared_directory(kind.name)
    try:
        data = np.load(os.path.join(folder, "mesh.npz"))
        with open(os.path.join(folder, "materials.json"), encoding="utf-8") as f:
            textures = json.load(f)
    except (OSError, ValueError):
        return None
    for t in textures:
        for key in ("albedo", "normal"):
            if t.get(key):
                t[key] = os.path.join(folder, t[key])
    return Scan(kind, data["positions"], data["normals"], data["tangents"], data["uvs"], data["materials"],
                data["indices"], data["hull"], textures)


def available():
    """Whether every kind of SCANS is prepared."""
    return all(os.path.exists(os.path.join(prepared_directory(s.name), "mesh.npz")) for s in SCANS)


def atlas(loaded=None):
    """(albedo RGBA uint8, normals RGBA uint8, {(kind name, material): cell}, columns, rows): each
    material of the prepared scans (`loaded`, by kind name; prepared() of each kind when None) with an
    albedo in its own cell, row-major from the top left, its albedo (sRGB, its alpha its cut-out) and
    normal map resized to CELL x CELL. Without any, one cell of white and flat normals."""
    from PIL import Image
    if loaded is None:
        loaded = {s.name: prepared(s) for s in SCANS}
    entries = [(name, k, t) for name, scan in loaded.items() if scan is not None
               for k, t in enumerate(scan.textures) if t.get("albedo")]
    columns = ATLAS_COLUMNS
    rows = max(1, -(-len(entries) // columns))
    albedo = np.full((rows * CELL, columns * CELL, 4), 255, np.uint8)
    normals = np.empty((rows * CELL, columns * CELL, 4), np.uint8)
    normals[:] = FLAT_NORMAL
    cells = {}
    for cell, (name, material, texture) in enumerate(entries):
        r, c = divmod(cell, columns)
        window = (slice(r * CELL, (r + 1) * CELL), slice(c * CELL, (c + 1) * CELL))
        albedo[window] = np.asarray(Image.open(texture["albedo"]).convert("RGBA").resize((CELL, CELL),
                                                                                         Image.LANCZOS))
        if texture.get("normal"):
            normals[window] = np.asarray(Image.open(texture["normal"]).convert("RGBA").resize((CELL, CELL),
                                                                                              Image.LANCZOS))
        cells[(name, material)] = cell
    return albedo, normals, cells, columns, rows
