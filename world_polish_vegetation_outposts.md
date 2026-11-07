# World Polish: Terrain, Vegetation and Outposts — Implementation Guide

Fifth guide for Shoonyakasha. It polishes what the planet already has rather than adding systems: an erosion filter and geology in the terrain function, scanned assets from Poly Haven alongside generated trees with variants, octahedral impostors for views from above, forests that sit in the ground, and outposts built from a small procedural kit.

It builds on: `terrain.glsl` and the probes, the climate and biome weights, `vegetation.py` with its cards, atlas and side-view impostors, the hash scatter (`vegetation_scatter.comp`, cells per face, kinds with instance budgets), the modifier list for flattened sites and craters, rock promotion with convex hulls in Jolt, and the ground textures downloaded from Poly Haven with a `CREDITS.md`.

Function names for outside libraries (meshoptimizer, Poly Haven's API) are from memory; check them against the versions you use.

---

## 0. Overview

| Area | Change | Why it matters |
|---|---|---|
| Terrain shape | Erosion filter after mountains and hills | Branching gullies and ridges everywhere, from orbit down to walking |
| Terrain colour | Strata on rock, scree under cliffs, snow by aspect, wet beach band | Geology and climate read at a glance |
| Water | Lakes as a modifier with their own water level | Inland water without a river system |
| Assets | Poly Haven scans (CC0) through its API: hero rocks, desert plants, forest-floor debris | Scans match the scanned ground textures |
| Trees | A better generator (EZ-Tree's approach) and 6–8 variants per species per seed | Forests stop looking cloned; each seed grows its own |
| Far trees | Octahedral impostors | Crowns read correctly from the car at 100–300 m and from above |
| Forests | Edges, per-instance variation, ground contact, layered wind | Trees sit in the world instead of on it |
| Outposts | A procedural kit by purpose, beacons, Jolt hulls | Outposts look like what they buy and work as landmarks at night |

### Module layout

```
python/shoonyakasha/planet/
  assets/
    polyhaven.py              # API client: list, download, cache, CREDITS.md
    mesh_prep.py              # decimation, LODs, convex hulls, texture downsizing
    asset_kinds.py            # scanned models registered as scatter kinds
  tree_generator.py           # branch growth, leaves, variants per seed
  vegetation_impostors.py     # octahedral capture (replaces side view)
  outposts/
    kit.py                    # parts: meshes, hulls, sockets, lights
    layout.py                 # grammar: purpose -> parts on the site
pipelines/planet/shaders/
  erosion.glsl                # the erosion filter
  geology.glsl                # strata, scree, aspect snow, wet sand
  impostor_oct.glsl           # octahedral sampling
```

---

## 1. Terrain: the erosion filter

### 1.1 What it is

Rune Skovbo Johansen's *Fast and Gorgeous Erosion Filter* (2026) is a filter on a height function. It carves branching gullies and leaves sharp ridges, and it is evaluated at each point on its own, without a simulation or neighbour lookups. That is exactly what `terrain.glsl` needs: chunks, probes, collision patches and the horizon shadows all call the same function, so they all stay consistent.

Source: the blog post at blog.runevision.com (March 2026) and the Shadertoy links in it. Port the GLSL into `erosion.glsl`. Check the licence stated with the code before shipping.

### 1.2 Where it goes in the terrain function

```glsl
float terrainHeight(vec3 dir) {
    vec3 p = dir + T.seedOffset;
    float c = continents(p);                     // as now
    float h = mountains(p, c) + hills(p, c);     // as now, before the fine detail

    // Erosion: carve the large and medium forms, not the detail
    float landMask = smoothstep(-20.0, 60.0, h); // leave the sea floor alone
    h = mix(h, erosionFilter(p, h, E), landMask * E.strength);

    h += detail(p, h);                           // the 80 m to 10 m detail, after erosion
    return applyModifiers(dir, h, 0u, R);        // sites, outposts, craters, lakes
}
```

Notes:
- **Order:** erode the forms that should carry gullies (mountains, hills), then add the fine detail, then the modifiers. Erosion applied after the modifiers would cut into pads and craters.
- **Its inputs:** the filter works from the base height and its slope. Your eroded fBm already sums noise gradients; if the filter wants the gradient of the base, pass the accumulated one rather than taking finite differences, which would cost 3–4 more terrain evaluations per vertex.
- **Scale:** on a 100 km planet with 3,000 m mountains, gullies around 200–800 m across look right from the car and from orbit. Expose the filter's frequency, strength and branching as `TerrainParams` (and the settings menu's Planet section) so you can tune it live.
- **Cost:** measure `BuildChunks` before and after. The horizon shadows evaluate the terrain at 20 distances per vertex, so a filter that doubles the terrain's cost doubles that too. If it's too slow, use the filter in the vertex heights and horizons but drop its finest octave from the horizon samples, which are far away anyway.
- **Probes and CPU mirrors:** anything that mirrors the terrain on the CPU (tests, `--verify`) needs the filter too. Port it to the NumPy mirror in the same pass and extend the terrain test.

### 1.3 Check

`--verify` and `--crack-check` must still pass. Add a visual preset: 2 km above a mountain band at a low sun, where gullies show best.

---

## 2. Terrain: geology and climate detail

All in `geology.glsl`, called from `terrain.frag` when it builds the ground layers.

### 2.1 Strata

```glsl
vec3 strataColour(vec3 rockBase, float height, vec3 dir) {
    float warp = 40.0 * fbm(dir * 600.0, 3, 2.0, 0.5);          // bends the bands
    float band = fract((height + warp) / 23.0);                   // ~23 m bands
    float shade = 0.85 + 0.15 * sin(6.2831 * band) + 0.08 * hash1(floor((height + warp) / 23.0));
    return rockBase * shade;
}
```

Apply only to the rock layer, and only where it's steep (the layer's weight already says so). Use two band widths (23 m and 7 m) for a natural rhythm.

### 2.2 Scree

Scree gathers at the foot of cliffs. Where the slope is moderate (15–30°) and the terrain a short distance uphill is steep, blend in a grey gravel layer. Uphill steepness needs one more sample: the slope at `dir + uphill × 20 m` (uphill from the normal's tangent part). Do it in the chunk build and store it as a weight in the climate `vec4`'s spare channel, so the fragment shader doesn't evaluate the terrain.

### 2.3 Snow by aspect

```glsl
float aspect = dot(normalize(normal - up * dot(normal, up)), sunHorizontalDir);   // -1 shaded, +1 sunny
float snowLine = baseSnowLine + 300.0 * aspect;        // sunny slopes lose snow 300 m higher
```

Use the sun's *average* horizontal direction over the day (the equator-ward direction from §7 of the mechanics guide), not the current one, so snow doesn't move during the day.

### 2.4 Wet beach band

Between 0 and ~2 m above sea level: sand darkened by 35 % and with a higher specular term, with a noisy upper edge. The ocean shader's foam line, if you add one, sits on it.

### 2.5 Lakes

A lake is a modifier, like a crater:

- **Finding basins:** at outpost and POI generation time, probe a coarse grid around inland candidates; a basin is a cell lower than all its neighbours by at least 15 m.
- **The modifier:** type `lake`, centre, radius and water level (the basin's rim height minus a metre). It flattens nothing; it only tells the ocean pass to draw water there.
- **Drawing:** the ocean pass draws chunks whose height range reaches below the sea level. Extend it: a chunk also gets an ocean draw if it intersects a lake, with that lake's level instead of 0 (`ChunkInstances` gets a water level per instance). The ocean's shading stays the same.
- **Shores:** the wet band (§2.4) follows the lake's level too.

Rivers need a network that a per-point height function can't produce cheaply. Leave them out.

---

## 3. Assets from Poly Haven

### 3.1 Licence and API

All Poly Haven assets (models, textures, HDRIs) are CC0: any use, commercial included, no attribution required, redistribution allowed. Only the website's own content (logos, example renders, user submissions, text) is protected, and scraping the website needs permission. Fetch assets through the public API under its API terms, as `ground_textures.py` already does.

```python
# assets/polyhaven.py
API = "https://api.polyhaven.com"

def list_models(categories):
    """Asset ids of type models in any of `categories` (e.g. "rocks", "nature")."""
    return requests.get(f"{API}/assets", params={"t": "models", "c": ",".join(categories)},
                        headers={"User-Agent": USER_AGENT}).json()

def files(asset_id):
    """The asset's files by format and resolution (gltf: {"1k": {...}, "2k": {...}})."""
    return requests.get(f"{API}/files/{asset_id}", headers={"User-Agent": USER_AGENT}).json()

def download_gltf(asset_id, resolution="1k"):
    """The glTF and the files it includes, into the cache; returns the .gltf path."""
    ...
```

- `USER_AGENT`: a fixed name for your downloader (e.g. `"shoonyakasha-assets"`), as their API terms ask.
- Cache under the same folder as the ground textures. Write `CREDITS.md` with each asset's name and page, even though credit isn't required.
- Pick assets by id in a table (`asset_kinds.py`), not by category queries at runtime: the world must not change when Poly Haven adds an asset.

### 3.2 Preparing a scanned model

Run once after download, cached next to it:

1. **Textures:** take 1K. Leaves and plants with alpha go into the foliage atlas pipeline you already have (cut out, mean-normalised to 0.5, transparent texels filled with the mean).
2. **Decimate** to LODs, e.g. LOD0 ~6k, LOD1 ~1.5k, LOD2 ~400 triangles for a boulder; plants by their silhouette. meshoptimizer's `simplify` (in the C++ engine, bound to Python) keeps borders and UVs well; quadric decimation from Open3D or trimesh works too.
3. **Normals:** bake LOD0's detail into a normal map for LOD1/LOD2 if the scan's own normal map isn't enough (usually it is).
4. **Scale and origin:** to metres, origin at the base centre, up +Y, the same convention as `vegetation.py`'s unit meshes (or record the true height and normalise to 1 like your kinds).
5. **Convex hull** for rocks: from LOD2, at most 64 points (Jolt's limit is higher, but small hulls are faster), cached as `.npy`.
6. **Impostor:** render LOD0 into the octahedral impostor atlas (§5).

### 3.3 Scanned models as scatter kinds

```python
@dataclass(frozen=True)
class AssetKind:
    key: str                     # "nq_rock_03", "forest_log_01"
    asset_id: str                # Poly Haven id
    biomes: dict                 # biome -> chance per candidate (like biomePlants)
    sizes: tuple                 # (min, max) scale factor around the scan's own size
    align_to_slope: float        # 0: upright, 1: follows the ground's normal
    sink: float                  # m pushed into the ground, so no edge floats
    collider: str                # "hull" | "none" | "capsule"
    promotable: bool             # rocks the gun can lift
```

- They join the scatter as new kinds with their own instance budget. The cell hash and draw order stay unchanged for existing kinds, so adding kinds doesn't move existing trees. Give asset kinds their own hash stream (e.g. hash of the cell with a different salt).
- **Desert (Namaqualand):** rocks as hero boulders at low density, desert plants in their growth states as variants of a kind (pick the state by moisture).
- **Forest floor:** logs, stumps, branches and debris under forest cover only, 20–60 m apart; ferns at higher moisture.
- **Hero rocks** are promotable rocks with their hull; the gun's mass from the hull's volume times `DENSITY`, as in `rocks.py`.
- **Align and sink:** logs follow the slope (`align_to_slope = 1`), upright plants don't; sink everything by a few centimetres plus the slope times its radius.

---

## 4. Generated trees and variants

### 4.1 Approach

EZ-Tree (MIT, by Dan Greenheck) is a recursive branch generator with parameters per level: children count, angle, length, radius, taper, gnarliness (random bending per section), force (a direction branches bend towards: up for light, down for weight), twist, and leaves along the outer levels. Port the approach to Python in `tree_generator.py` (it's a few hundred lines), keeping your conventions: unit height, bark tubes, leaf cards showing the atlas, crown-centred normals.

```python
@dataclass
class Level:
    children: int            # branches from each parent branch
    start: float             # 0..1 along the parent where children begin
    angle: float             # degrees from the parent
    length: float            # relative to the parent
    radius: float            # relative to the parent
    taper: float             # radius at the tip / at the base
    gnarliness: float        # random bend per section, radians
    sections: int            # segments along the branch
    segments: int            # sides of the tube

@dataclass
class Species:
    levels: list[Level]
    force: tuple             # direction in tree space (0, 1, 0) up, (0, -1, 0) droop
    force_strength: float
    twist: float
    leaves: dict             # card kind, count per branch, size, start, angle
    bark: str                # bark tile
```

Growth in outline:

```python
def grow(species, rng):
    branches = [Branch(origin=(0, 0, 0), direction=(0, 1, 0), length=1.0, radius=species.trunk_radius, level=0)]
    tubes, cards = [], []
    while branches:
        b = branches.pop()
        lv = species.levels[b.level]
        points = []
        p, d = b.origin, b.direction
        for s in range(lv.sections + 1):
            points.append((p, b.radius * lerp(1.0, lv.taper, s / lv.sections)))
            d = normalize(add(d, scale(species.force, species.force_strength / lv.sections)))
            d = rotate_random(d, lv.gnarliness, rng)
            p = add(p, scale(d, b.length / lv.sections))
        tubes.append((points, lv.segments))
        if b.level + 1 < len(species.levels):
            nxt = species.levels[b.level + 1]
            for k in range(nxt.children):
                t = lerp(nxt.start, 1.0, (k + rng.random()) / nxt.children)
                origin, direction, radius = point_on(points, t)
                around = 2.0 * math.pi * (k / nxt.children) + rng.uniform(-0.3, 0.3) + species.twist * t
                child_dir = tilt(direction, math.radians(nxt.angle), around)
                branches.append(Branch(origin, child_dir, b.length * nxt.length, radius * nxt.radius, b.level + 1))
        if b.level >= species.leaf_level:
            cards += place_leaf_cards(points, species.leaves, rng)
    return mesh(tubes, cards)
```

### 4.2 Species and variants

- Keep your kinds (spruce, pine, round broadleaf, tall broadleaf, acacia) as `Species` presets, and add a few if the biomes call for them (a birch-like one for taiga edges, a palm-like one for tropical coasts).
- **Variants:** for each species, grow 6–8 trees from `seed = hash(world_seed, species, k)`. Each gets its own mesh in the vegetation buffers and its own impostor cell. The scatter picks the variant from the cell hash (`variant = hash % count`), drawn after the existing draws so nothing else moves.
- **Per seed:** a new world seed grows new variants. Cache them by seed so starting the same world again doesn't regrow them.
- **Budget:** variants multiply impostor cells and vertex data, not instances. 5 species × 8 variants × (mesh ~3k triangles + one octahedral impostor) is small.

### 4.3 Checks

Extend `test_planet_vegetation.py`: every variant one unit tall, whole triangles of its own vertices, cards on atlas cards, crowns' normals pointing out, the same seed growing the same tree twice.

---

## 5. Octahedral impostors

### 5.1 Why

The side-view impostor is right for a walker seeing trees at the horizon. From the car at 100–300 m you look down at 20–60°, and a side view rotated to face the camera shows the wrong silhouette. An octahedral impostor stores the tree from many directions over the upper hemisphere and shows the views nearest the actual one.

### 5.2 Layout

Each tree variant gets an N × N grid of views (N = 8 → 64 views; N = 6 → 36), each view a 128- or 256-texel square. Direction to grid cell via the **hemi-octahedral** mapping (y up):

```glsl
vec2 hemiOctEncode(vec3 d) {                 // d unit, d.y >= 0
    d /= abs(d.x) + abs(d.y) + abs(d.z);
    return vec2(d.x + d.z, d.x - d.z) * 0.5 + 0.5;          // 0..1
}
vec3 hemiOctDecode(vec2 uv) {
    vec2 f = uv * 2.0 - 1.0;
    vec3 d = vec3((f.x + f.y) * 0.5, 0.0, (f.x - f.y) * 0.5);
    d.y = 1.0 - abs(d.x) - abs(d.z);
    return normalize(d);
}
```

### 5.3 Capture (in `vegetation_impostors.py`)

For each grid cell (i, j): the direction `hemiOctDecode((i + 0.5) / N, (j + 0.5) / N)`; render the tree orthographically along it, framed by the tree's bounding sphere (the same size in every view), into its cell: albedo with alpha, normal (in tree space), and depth (distance along the view, for parallax). Twice the resolution then averaged down, with your alpha test, as now. Bark mask too, if the shader uses it.

### 5.4 Drawing

The vertex shader turns the quad to face the camera, as now. The fragment shader:

1. Direction from the tree to the camera, in the tree's frame (undo its turn about up), clamped to y ≥ 0.05.
2. Its grid position `g = hemiOctEncode(dir) * (N - 1)`; the three nearest views are the corners of the triangle containing `g` in the cell `floor(g)` (split along the diagonal), with barycentric weights.
3. For each of the three: project the fragment's point on the quad into that view's frame, sample albedo, normal and depth. Using each view's depth to shift its sample along the view (one step of parallax) removes most of the ghosting when blending.
4. Blend by the weights; alpha test the blended alpha; light with the blended normal turned back into world space.

Cost: three samples of three textures. Use it for trees beyond the meshes' range (250 m as now) and for the shadow cascades from the sun's direction (one view, no blending, is enough there).

### 5.5 Check

A capture test: the view straight down shows the crown's outline; a view from the side matches the old side-view impostor; the same direction always lands in the same cell.

---

## 6. Forests that sit in the ground

### 6.1 Edges

The scatter knows the cover noise that makes clearings. Its gradient finds edges:

- At each candidate, compare cover with cover a few cells away in four directions. Where the forest weight crosses its threshold within ~15 m, it's an edge.
- On edges: raise the bush chance (×3), allow young trees (scale 0.4–0.7 of their species' range), and lower the tall trees' chance a little. Real forests have this skirt.
- Do the same at the treeline (by temperature) and along beaches.

### 6.2 Per-instance variation

- **Dryness tint:** low moisture moves foliage towards yellow-olive (mix the plant colour 25 % towards a dry colour by `smoothstep(0.35, 0.15, moisture)`).
- **Lean:** on slopes, tilt trees up to 4° downhill; on exposed ridges (high, steep, cold) up to 6° downwind. Turn the instance's up vector, and the impostor's frame with it.
- **Treeline:** let the temperature threshold for trees waver with a noise of ±1.5 °C, and shrink trees near it (scale × smoothstep), so the line breaks up into stunted trees and gaps.

### 6.3 Ground contact

- **Darkening under trees:** the terrain fragment shader darkens the ground by forest density. Cheap version: the forest canopy weight already in the ground layers, squared, times 0.35 on the albedo, plus a small per-cell noise. Better: a low-resolution "tree density" texture around the camera filled by the scatter pass (count instances per 4 m cell within 300 m), blurred, sampled by the terrain shader.
- **Leaf litter:** a ground layer under forest (brown, from a Poly Haven forest-floor texture) weighted by canopy.
- **Grass fades under trees:** the grass scatter's chance drops with that density.
- **Sink:** trunks start 10–20 cm below the surface on slopes so no root floats.

### 6.4 Wind in layers

All driven by the wind field the clouds and the audio use (speed, direction, gusts):

| Layer | Moves | Frequency | Amplitude |
|---|---|---|---|
| Trunk bend | the whole tree, bending more with height (height²) | 0.1–0.3 Hz, per-tree phase | up to 1–2 % of height at 15 m/s |
| Branch sway | each branch's vertices, by its distance from the trunk | 0.5–1 Hz | a few cm |
| Leaf flutter | card corners | 3–6 Hz | ~1 cm |

Store per vertex: its distance from the trunk (0 for the trunk, 1 at a branch tip) and a phase. The vertex shader adds the three layers; the impostors get the trunk bend only.

---

## 7. Outposts

### 7.1 The kit

Each part: a mesh (from boxes, cylinders and beams like `cockpit.py`, or a re-materialed piece from Kenney's Space Kit or Quaternius's Modular Sci-Fi MegaKit, both CC0 glTF), a convex hull or a few for Jolt, sockets (where other parts attach), lights, and a footprint.

| Part | Size | Notes |
|---|---|---|
| Landing pad | 24 × 24 m | edge lights, a painted H, the trade sensor over it |
| Habitat module | 6 × 3 × 3 m | stackable, windows lit at night |
| Dome | 8–12 m | the research post's centre |
| Container | 6 × 2.4 × 2.6 m | stacked 1–3 high, colour per stack |
| Fuel tank | 4 m dia × 8 m | on legs, pipes to the pad |
| Ore heap | 6–10 m | a mining camp's; a mound mesh in the resource's tint |
| Conveyor | 2 m wide, 10–30 m | heap to a hopper |
| Comms mast | 20–35 m | blinking beacon at the top |
| Solar array | 8 × 4 m | rows facing the sun's average direction |
| Barrier | 4 m segments | lines the site's edge |
| Trade terminal | 1 × 2 m | the interaction point for the slate's Market and Workshop |

Materials in the UI palette: worn off-white panels, the car's yellow on handles and stripes, dark grey frames, cyan for lights and screens. One shared material atlas for the kit keeps draw calls low.

### 7.2 Layout by purpose

The outpost's purpose comes from its economy: what it buys most decides what it is.

| Purpose | Chosen when | Centre | Ring |
|---|---|---|---|
| Mining camp | it pays well for ore found near it | ore heaps, conveyor, hopper | containers, habitat, barrier |
| Fuel depot | it sells fuel cheaply | tank farm with pipes | pad ×2, habitat |
| Research post | it buys survey data | dome, antennas | solar arrays, habitat |
| Trade hub | the largest outpost | pad ×2, containers | everything, smaller |

A small grammar on the flattened site:

```python
def layout(outpost, rng):
    parts = [place("pad", at=(0, 0), heading=outpost.approach_heading)]
    ring = Ring(inner=16.0, outer=outpost.flat_radius - 6.0)
    for part in PURPOSES[outpost.purpose].centre:
        parts.append(ring.place_near_centre(part, rng, avoid=parts))
    for part, count in PURPOSES[outpost.purpose].ring:
        for _ in range(count):
            spot = ring.free_spot(part.footprint, rng, avoid=parts, keep_clear=approach_corridor(outpost))
            if spot:
                parts.append(place(part, at=spot, heading=facing_centre(spot) + rng.uniform(-10, 10)))
    parts.append(place("mast", at=ring.edge_spot(rng)))
    parts += barrier_along(ring.outer, gaps=[approach_corridor(outpost)])
    return parts
```

- Seeded by `(world_seed, outpost id)`: the same outpost always looks the same.
- Keep an approach corridor clear for landing (the heading with the flattest, lowest terrain).
- All parts sit on the flattened area; the site's flat radius grows with the purpose's needs (30–60 m).

### 7.3 Beacons and night

- Each mast has a beacon: an emissive sprite drawn after the atmosphere like the orbit path, a constant few pixels in size whatever the distance (the far-object marker approach), blinking every 2 s, phase per outpost.
- Visible within 30 km in air (fading with the atmosphere's transmittance) and from orbit on the night side. Outposts become landmarks to fly by.
- Pad edge lights and habitat windows switch on at dusk (sun elevation at the site below 3°).
- Point lights from the night section of the mechanics guide (§7.3 there) light the pad and the ground around it.

### 7.4 Physics and interaction

- Each part's hulls become static Jolt bodies when the bubble reaches the outpost; containers and barriers can be dynamic and asleep, so the gun can push them (a fun thing to find).
- The pad's sensor and the terminal's trigger zone feed the slate ("on a pad", "at a terminal").
- Ore delivered on foot: an ore drop bin next to the terminal, a Jolt sensor like the car's cargo net.

### 7.5 Distance

Beyond ~2 km an outpost becomes a few merged meshes (one per material), beyond ~8 km its beacon alone. Merge the parts' meshes per outpost at build time into one vertex buffer per LOD.

---

## 8. Checks

- `--verify`, `--crack-check`, `--morph-check`: unchanged, and must pass with the erosion filter on.
- `--erosion-check`: the terrain's mean, extremes and the share of steep ground before and after the filter at 10,000 directions, printed; the GPU height at those directions equals the NumPy mirror's.
- `--impostor-check`: for each variant, render the mesh and its impostor from 12 directions above 20°; the silhouettes' overlap (IoU) above 0.85.
- `--outpost-check`: every outpost's parts within its flat radius, none overlapping, the approach corridor clear, the pad level within 2 cm, and every part's hull resting (no Jolt body moving after 5 s).
- `--asset-check`: every asset kind's files present, LODs within their triangle budgets, hulls within 64 points, `CREDITS.md` listing each.
- Visual presets for captures: a gully band at low sun, a forest edge from the car at 150 m, a desert with Namaqualand plants, each outpost purpose at dusk, an outpost's beacon from orbit at night.

---

## 9. Milestones

1. **Erosion filter.** Ported into `terrain.glsl` and the NumPy mirror, parameters in the menu, cost measured; `--erosion-check`.
2. **Tree generator and variants.** `tree_generator.py`, species presets, 8 variants per species per seed, the scatter picking variants; extended vegetation tests.
3. **Outposts.** The kit, layouts by purpose, beacons, lights at dusk, Jolt hulls, sensors; `--outpost-check`.
4. **Octahedral impostors.** Capture, drawing with three-view blend and parallax, shadows; `--impostor-check`.
5. **Geology.** Strata, scree, snow by aspect, wet beaches.
6. **Forests in the ground.** Edges, variation, lean, treeline, darkening and litter, layered wind.
7. **Poly Haven assets.** API client, preparation, asset kinds for deserts and forest floors, hero rocks; `--asset-check`.
8. **Lakes.** Basin search, the lake modifier, water drawn at a lake's level.

---

## 10. Pitfalls

- **Erosion cost multiplies.** The horizon shadows and the probes evaluate the terrain many times per vertex. Measure `BuildChunks` with the filter on before tuning anything else.
- **Moving existing trees.** New kinds, variants or edge rules must draw from the hash after (or separately from) the existing draws, or every saved world's forests and taken rocks shift. Version the scatter like the save file.
- **Impostor ghosting** when blending three views without parallax; and popping when the camera crosses a grid cell, if the blend isn't continuous. Check both from the car, circling a tree.
- **Scan sizes.** A Poly Haven scan at full resolution can be hundreds of thousands of triangles and 8K textures. Never load an unprocessed scan into the game; the preparation step's output is what ships.
- **Floating assets on slopes.** Sink and slope alignment per kind; check on 30° slopes.
- **Outposts on bad ground.** The site search must check the flattened area's surroundings too, or a dome ends up against a cliff the flattening created. Keep the ease radius wide.
- **Too many beacons and markers.** Beacons are light in the world, not HUD markers; the compass shows only known outposts.

---

## 11. References

- Rune Skovbo Johansen — *Fast and Gorgeous Erosion Filter* (blog.runevision.com, March 2026), Shadertoy: https://www.shadertoy.com/view/wXcfWn and https://www.shadertoy.com/view/sf23W1
- 80.lv article on the filter — https://80.lv/articles/fast-terrain-erosion-filter-that-emulates-erosion-without-simulation
- Poly Haven (CC0) — https://polyhaven.com, licence: https://polyhaven.com/license, collections: https://polyhaven.com/collections
- Namaqualand scan library (CG Channel) — https://www.cgchannel.com/2024/10/download-poly-havens-free-namaqualand-3d-scan-library/
- EZ-Tree (MIT) — https://github.com/dgreenheck/ez-tree
- Kenney Space Kit (CC0) — https://kenney-assets.itch.io/space-kit
- Quaternius Modular Sci-Fi MegaKit (CC0) — https://quaternius.com/packs/modularscifimegakit.html
- meshoptimizer — https://github.com/zeux/meshoptimizer
- Octahedral impostors: Ryan Brucks, *Octahedral Impostors* (Shader Bits blog, 2018), and the open-source Unity implementation by xraxra (`IMP`) for a reference shader
- Previous guides: `planet_flight_mvp.md`, `on_foot_and_gravity_gun.md`, `game_and_world_mechanics.md`, `audio_with_miniaudio.md`
