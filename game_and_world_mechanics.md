# Game and World Mechanics — Implementation Guide

Third guide for the Shoonyakasha planet game. It builds on the two example programs in `examples/python/simulations/planet/`:

- **`planet.py`**: the free camera, the ship (`flight.py`: a rigid body that burns fuel and weighs its fuel and cargo, with Δv on the ORBIT display), the player on foot with the gravity gun, the cargo net beside the landed ship, the ore scanner (`scanner.py`), trading at outposts (`trade.py`), and a save to `planet_save.json` (`save_game` / `load_game`).
- **`walker.py`**: the player on foot with the gravity gun, the hover car (`hovercar.py`, no fuel or mass), the HUD's compass and objective card (`hud.py`, `objectives.py`), the explored map (`explored_map.py`), ore sold where it comes to rest on an outpost's pad, the menu (`terminal.py`), toasts, and a save to `walker_save.json` / `.npz` (`save.py`) with autosave.

Both share the planet and moon with climate and biomes on the GPU, probes with latency, the Jolt bubble, scatter rocks with an ore richness (`resources.glsl`) that come loose and split (example `rocks.py`, `world.taken_rocks`), props with records, the outposts from a survey (`outposts.py`: Base, Kestrel, Halden and Varga on the planet; Lowrim and Selene Deep on the moon), and credits with per-outpost supply (`trade.py`).

Direction taken: **prospector-trader first** (the economy and routes are the core), with world mechanics (time of day, temperature, weather, events) chosen because they change where it pays to go and how hard it is to get there.

Names of new modules and functions are suggestions.

---

## 0. Overview

| Phase | Adds | Playable goal |
|---|---|---|
| 1. Ore loop | Resource types by biome, outpost demand and prices by resource, hold limits, upgrades, save fields for all of it | Pick a biome to prospect by what an outpost wants, come back richer |
| 2. Living world | Day and night, temperature and the suit, points of interest, survey data, contracts | Take a contract whose route crosses a cold pass at dusk |
| 3. Events and the moon | Meteor showers with craters, wind and storms, the moon expedition with refuelling | Race to a fresh crater; plan a transfer to the moon for rare metals |

### Modules

```
examples/python/simulations/planet/
  game_data.py        # new: tunable tables: resources, upgrades, POI kinds, contracts
  trade.py            # extend: prices by resource and outpost demand
  flight.py           # extend: hold capacity (mass and volume) on Ship
  outposts.py         # extend: demand per resource on Outpost
  rocks.py            # extend: resource per rock, speck colour per resource
  upgrades.py         # new: owned upgrades -> parameter changes
  suit.py             # new: temperature, suit reserve, effects
  poi.py              # new: points of interest from the hash, discovery
  contracts.py        # new: generation, state, rewards
  events.py           # new: scheduler, meteor showers, storms
  save.py             # extend: walker's save; planet.py's save_game / load_game likewise
python/shoonyakasha/planet/
  resources.py        # new: resource ids and a NumPy mirror of resources.glsl (for tests)
  rocks.py            # extend: rock_colour by resource
  nearby_trees.py     # extend: NearbyTree.resource
  clock.py            # new: day phase, sun direction
  modifiers.py        # new: terrain modifiers (craters), invalidation
  wind.py             # new: wind field
python/shoonyakasha/pipelines/planet/shaders/
  resources.glsl      # extend: a resource id beside the richness
  vegetation_scatter.comp  # extend: ore rocks carry the resource id
  sites.glsl          # extend into the modifier list (craters beside flat sites), §11.2
```

### The game clock

Everything time-based (prices recovering, contracts expiring, day and night, events) runs on one clock, `world.time` (`PlanetWorld.time`), which runs with the time warp and is already in both saves (`"time"`). Slow processes are computed lazily from elapsed time, as `trade.supply` already does, rather than per frame.

---

# Phase 1 — The ore loop

## 1. Resources

### 1.1 Table

| id | Resource | Where (planet) | Where (moon) | Rock tint | Speck colour | Base value (cr/kg at grade 1) |
|---|---|---|---|---|---|---|
| 0 | none | | | | | |
| 1 | Ferrite | the mountains, where no other resource wins | | rust brown | orange | 2 |
| 2 | Cuprite | dry, temperate ground (about 3 to 24 °C) | | grey-green | green | 3 |
| 3 | Quartz | cold ground (below about 2 °C) | | pale grey | white-blue | 4.67 |
| 4 | Amber | warm, wet ground (above about 10 °C, moisture above 0.5) | | dark brown | gold | 6 |
| 5 | Evaporite (salts) | warm, dry ground (above about 12 °C, moisture below 0.4) | | off-white | pink | 1.67 |
| 6 | Ice | | ore craters whose centre lies towards the poles | blue-grey | pale blue | 1.33 (fuel feedstock) |
| 7 | Ilmenite | | the other ore craters | near black | silver | 10 |
| 8 | Iridium | meteorites only (§11) | meteorites only | black, glassy | violet | 40 |

§1.2 gives the exact rules for where each resource lies. Ids 0 to 7 are in `resources.glsl` and `python/shoonyakasha/planet/resources.py`. The scatter stores a rock's id in bits 16 and up of its `NearbyTrees` entry and in the whole-number part of its instance tint.

The rock tints and speck colours of ids 0 to 7 are in `ore_colours.glsl` (`ORE_TINTS`, `ORE_SPECKS`), and in `rocks.ORE_TINTS` and `rocks.SPECK_COLOURS` in `python/shoonyakasha/planet/rocks.py`. An ore rock's colour is turned from the body's rock colour towards its tint by `ORE_TINT_SHARE` (0.35) times 1 + its grade. Id 0's entries are the colours an ore rock without a resource gets.

The values of ids 1 to 7 are `trade.VALUES` in the example's `trade.py` (§1.3).

Each rock weighs by its resource's density: `rocks.DENSITIES`, indexed by id, and `rocks.density(resource)` in `python/shoonyakasha/planet/rocks.py`. A plain rock (id 0) has `rocks.DENSITY`, 2,600 kg/m³.

| id | Resource | Density (kg/m³) |
|---|---|---|
| 1 | Ferrite | 3,400 |
| 2 | Cuprite | 3,000 |
| 3 | Quartz | 2,650 |
| 4 | Amber | 1,100 |
| 5 | Evaporite | 2,200 |
| 6 | Ice | 920 |
| 7 | Ilmenite | 4,700 |
| 8 | Iridium | 8,000 |

The example's fixed rocks get their static hull and their mass at that density. `Rocks.grabbable`, a promoted rock's mass, a boulder's chunks and the cargo all follow from it. Ilmenite boulders are about 1.8 times as heavy as plain ones of the same size, and amber and ice rocks less than half.

Iridium (id 8) is not in the code. The tables above and in §1.1 give the values to use when §11 adds it: entries in `resources.glsl` and `resources.py`, in `ore_colours.glsl` and `rocks.ORE_TINTS` / `SPECK_COLOURS` (black, glassy; violet specks), in `rocks.DENSITIES` and in `trade.VALUES`. Until then the colour and density lookups clamp id 8 to ilmenite's entries.

### 1.2 The resource field on the GPU

`resources.glsl` gives one richness, 0 to 1, which is the grade:
- `planetOre(dir, height)`: veins (ridged noise, about 450 m across) times patches (fBm, about 1.7 km across) times `0.4 + 0.6 · terrainMountainMask(dir)`, and 0 below sea level.
- `moonOre(dir)`: the largest `MOON_ORE_OCTAVES` crater sizes. Each crater has a hashed mineral value, and craters above `MOON_ORE_LOW` hold ore over their floor and rim. `moonOreCrater(dir, out crater)` gives the same richness and the unit direction of the richest crater's centre (`dir` where there is none).

Where the richness is above 0, a resource id goes with it. The ids and constants are in `resources.glsl`, and `python/shoonyakasha/planet/resources.py` has the same ones.

On the planet, `planetResource(dir, temperature, moisture)` takes the base climate (`climateAt`, without the settings' offsets) and `terrainMountainMask(dir)`. Each of the five planet resources gets a weight. GLSL's `smoothstep` is undefined when `edge0 >= edge1`, so falling edges use `resourceFalling(low, high, x) = 1 − smoothstep(low, high, x)`.

```glsl
// resources.glsl
const int PLANET_RESOURCES = 5;              // ferrite to evaporite
const float RESOURCE_WAVELENGTH = 0.11;      // of a unit of direction: about 11 km on the planet
const float RESOURCE_NOISE = 0.6;

void planetResourceWeights(float temperature, float moisture, float mountains, out float w[PLANET_RESOURCES]) {
    w[0] = 0.55 * smoothstep(0.3, 0.9, mountains);                                        // ferrite
    w[1] = resourceFalling(0.3, 0.5, moisture) * smoothstep(3.0, 12.0, temperature)
         * resourceFalling(16.0, 24.0, temperature);                                      // cuprite
    w[2] = resourceFalling(-4.0, 2.0, temperature);                                       // quartz
    w[3] = smoothstep(0.5, 0.75, moisture) * smoothstep(10.0, 20.0, temperature);         // amber
    w[4] = resourceFalling(0.25, 0.4, moisture) * smoothstep(12.0, 20.0, temperature);    // evaporite
}

float planetResourceNoise(vec3 dir, int k) {
    vec3 p = (dir + terrain.seed.xyz) / RESOURCE_WAVELENGTH + vec3(float(k) * 17.3, float(k) * 5.9, 2.7);
    return fbm3(p, 3, 2.0, 0.5);
}

// The resource of the highest weight times 1 + RESOURCE_NOISE times its noise; ferrite where they tie.
uint planetResourcePick(float w[PLANET_RESOURCES], float noise[PLANET_RESOURCES]) {
    uint best = RESOURCE_FERRITE;
    float bestWeight = w[0] * (1.0 + RESOURCE_NOISE * noise[0]);
    for (int k = 1; k < PLANET_RESOURCES; ++k) {
        float weight = w[k] * (1.0 + RESOURCE_NOISE * noise[k]);
        if (weight > bestWeight) {
            bestWeight = weight;
            best = RESOURCE_FERRITE + uint(k);
        }
    }
    return best;
}
```

So ferrite is the default in the mountains. Quartz is the cold ore, amber the warm and wet, evaporite the warm and dry, and cuprite the dry and temperate. Ore lies mostly in the mountains, and with the default terrain the planet's ore is about 42 % ferrite, 20 % quartz, 17 % cuprite, 15 % amber and 6 % evaporite.

On the moon, `moonResource(dir)` is ice (6) where the richest crater's centre has `abs(crater.y) > MOON_ICE_LATITUDE` (0.55), otherwise ilmenite (7). The whole crater has one resource. With the default moon the ore is about 48 % ice and 52 % ilmenite.

Plumbing:
- **Probes:** `chunk_build.comp` writes each probe's resources as (richness, resource id, 0, 0). `PlanetWorld` has the richness in `ground_ore` / `extra_ores` and the id in `ground_resource` / `extra_resources`.
- **Scatter:** `vegetation_scatter.comp` makes rocks ore above `ORE_ROCKS` (0.3). `rockResource` gives an ore rock its resource from the candidate's base climate, as the probes do. `packedRockColour` turns its colour towards the resource's tint in `ore_colours.glsl` (`oreTint`). The resource id is in bits 16 and up of word 5 of the rock's `NearbyTrees` entry, and in the whole-number part of the instance's tint (`vegetationPackTint`, read back with `vegetationTint` and `vegetationResource`). `vegetation.frag` draws an ore rock's specks in `oreSpeckColour` of its resource.
- **Python:** `NearbyTree.resource`, `StaticRock.resource` and `Prop.resource` hold the id. `rocks.ORE_TINTS` and `rocks.SPECK_COLOURS` are the tables of `ore_colours.glsl`. `rock_colour(body, grade=0.0, resource=FERRITE)` gives a rock's colour, and `speck_colour(resource)` its specks'. The example `rocks.py` makes each ore rock's specks an emissive entity in `speck_colour(resource)` times `SPECK_GLOW`. A prop's saved record keeps its `resource`, and a record without one loads as ferrite when its grade is above 0.
- **Breaking:** only ore boulders split (`Rocks.breaks`, `Rocks.split`). The chunks take the boulder's resource along with its grade.
- **Test:** `tests/python/test_planet_resources.py` runs the shader over 50,000 random directions, and the GPU's planet resources must agree with `resources.planet_pick` on more than 99.9 % of the ore points. Each of ferrite to evaporite must be more than 5 % of the ore, with ferrite the most common, quartz colder than amber, amber wetter than the ore's mean and evaporite drier. The moon's resources must be `resources.moon_resource` of their crater's latitude, with ice and ilmenite each more than 5 %. The ids and constants must match the shader's.

### 1.3 What a rock is worth

Cargo items carry `"resource"` next to `"grade"`. The cargo net (`load_cargo` in `planet.py`) and the walker's pads (`objectives.sale_items`) take it from the loose rock. `trade.base_price` starts from:

```python
# trade.py
VALUES = {FERRITE: 2.0, CUPRITE: 3.0, QUARTZ: 4.67, AMBER: 6.0, EVAPORITE: 1.67, ICE: 1.33, ILMENITE: 10.0}
GRADE_POWER = 1.5

price = VALUES.get(item.get("resource", FERRITE), 0.0) * item["grade"] ** GRADE_POWER
```

So high grades pay more than linearly: grade 0.5 pays 35 % of grade 1. An item without a resource, from a save made before resources, sells as ferrite. The other-body and distance factors and the supply (still per body) apply on top, as in §3.2. The trade card lists each resource's price at grade 1, and the walker's sale toast names the resource.

## 2. Cargo

### 2.1 Items and the hold

`Ship.cargo` (`flight.py`) is a list of dicts with `"body"`, `"direction"` (where the ore was mined), `"mass"`, `"grade"` and `"resource"`. `Ship.cargo_mass` sums the masses, and `Ship.mass()` adds the cargo to the dry mass and fuel. The walker's items for `trade.sale` have the same keys (`objectives.sale_items`). Items are one per rock, and each knows the body and place it was mined at, which `trade.base_price` uses.

The hold has two limits: `ShipParams.cargo_capacity` (8,000 kg) and `ShipParams.hold_m3` (4 m³):

```python
# flight.py
PACKING = 1.6                   # a load of loose rock's volume over the rocks' own

def load_volume(mass, resource):
    return mass / density(resource) * PACKING           # shoonyakasha.planet.rocks.density

class Ship:
    @property
    def cargo_volume(self):
        return sum(load_volume(item["mass"], item.get("resource", FERRITE)) for item in self.cargo)

    def can_take(self, mass, resource):
        p = self.params
        return (self.cargo_mass + mass <= p.cargo_capacity
                and self.cargo_volume + load_volume(mass, resource) <= p.hold_m3)
```

Ferrite, cuprite and ilmenite fill the hold by their mass. Quartz (about 6.6 t), evaporite (5.5 t), amber (2.75 t) and ice (2.3 t) fill it by their volume.

### 2.2 Loading

- **`planet.py`:** loose ore rocks in the cargo net beside the landed ship (`NET_CENTRE`, `NET_HALF`, with corner posts) are loaded when the player boards or the ship takes off. Rocks that `Ship.can_take` refuses stay in the net. The card's LAST note then says how many were left, and on foot a WILL NOT FIT row counts them before boarding. Plain rocks (grade 0) are never loaded. The trade card shows the cargo's mass, its volume out of the hold's and its pieces, and the left display its mass and volume. `--cargo-check` preloads 2 t of ice, so its largest rock is left out by volume.
- **`walker.py`:** there is no hold. Ore is sold where it comes to rest on a pad (`objectives.sale_items`, `REST_SPEED` 0.5 m/s). The hover car gets a hold only if the walker should carry ore between outposts.

## 3. Outposts and prices

### 3.1 Data

`outposts.Outpost` has `name`, `body`, `direction`, `height`, `fuel_price` (`FUEL_PRICE`: home 0.8, planet 1.0, moon 3.0 cr/kg), its site radii, and `supply` (body name: (kg bought, world time)).

Additions:

```python
# Outpost additions
demand: dict = field(default_factory=dict)   # resource id -> price multiplier (0.5 .. 2.5)
services: frozenset = frozenset({"fuel"})    # "fuel", "upgrades", "repair", "contracts", "refinery", "cartography"
reputation: float = 0.0
```

Demand comes from the outpost's surroundings: it pays little for what is mined nearby and well for what is far away. Sample the resource field with probes in rings around it, using `outposts.ring` (the survey's ring of points), and set `demand[r] = clamp(1.8 − 1.4 · local_share[r], 0.5, 2.5)`.

### 3.2 Price model

`trade.py` today:
- An outpost pays `VALUES[resource] · grade ** GRADE_POWER` per kg (§1.3). That is multiplied by `OTHER_BODY` (1.8) for ore from the other body, or else by `1 + DISTANCE_PREMIUM` (0.5) per 100 km from where the ore was mined.
- The price is divided by `1 + s / SUPPLY_HALF`, where `s` is the kg of that body's ore the outpost has bought. `SUPPLY_HALF` is 4,000 kg.
- `s` decays towards 0 with `SUPPLY_RECOVERY` (900 s), computed lazily from the time stored with it.
- A sale integrates the price over the rising supply, `base · SUPPLY_HALF · ln((1 + (s + m)/H) / (1 + s/H))`. Splitting a sale therefore pays no more.

Changes for resources: key `supply` by resource id instead of body name, and multiply the base price by the outpost's demand. The curve, its integration and the lazy recovery stay.

```python
def base_price(outpost, item):
    resource = item.get("resource", FERRITE)
    price = (VALUES.get(resource, 0.0) * item["grade"] ** GRADE_POWER
             * outpost.demand.get(resource, 1.0))
    if item["body"] != outpost.body.name:
        return price * OTHER_BODY
    distance = ground_distance(outpost.body.radius, outpost.direction, item["direction"])
    return price * (1.0 + DISTANCE_PREMIUM * distance / 100_000.0)

def supply(outpost, resource, time):
    kg, since = outpost.supply.get(resource, (0.0, time))
    return kg * math.exp(-max(time - since, 0.0) / SUPPLY_RECOVERY)
```

`sale` keeps its form, with `item["resource"]` in place of `item["body"]` as the supply's key. `SUPPLY_RECOVERY` (900 s) is the time constant to tune against the length of a day (§7).

### 3.3 The trade card

`planet.py` shows an outpost's card when the ship is landed on its pad: Enter sells the whole cargo (`Market.sell`), and Space fills the tanks as far as the credits go (`Market.buy_fuel`).

Extend it to list the cargo by resource, with the price of the first kg (`trade.price`) coloured by demand, and the whole sale's value (`Market.value`). Add per-resource selling, the outpost's reputation and its services.

## 4. Fuel and mass

`flight.py` has this already:
- **Mass:** `ShipParams` has `dry_mass` 14,000 kg, `fuel_capacity` 6,000 kg, `isp_main` 450 s and `isp_rcs` 250 s. `Ship.mass()` is the dry mass plus fuel plus cargo.
- **Inertia:** `ShipParams.inertia` scaled by the mass over the mass full of fuel with no cargo.
- **Burn:** `Ship.fuel_flow(thrust)` burns the main engine's forward thrust (`max(-thrust[2], 0)`) at `isp_main` and the other thrusters at `isp_rcs`.
- **Running low:** with too little fuel left for a step, the thrust is cut to what the fuel lasts for. The torque that turns the ship needs no fuel.
- **Δv:** `Ship.delta_v()` is `isp_main · g0 · ln(m / (m − fuel))`, shown as DELTA-V on the ORBIT display.
- **Check:** `--fuel-check`.

With the defaults, Δv is ≈ 1,570 m/s with an empty hold and ≈ 1,270 m/s with 4 t of ore. Orbit at 40 km takes ~600 m/s plus losses. The transfer to the moon takes 162 m/s (`test_planet_flight.py`), and the moon landing ≈ 200 m/s.

**Running dry** (`trade.fuel_call`, `Market.call_fuel`, `call_fuel` in `planet.py`): landed off every pad, or on a closed orbit with the periapsis above `CALL_ORBIT_FLOOR` (25 km on the planet, 5 km on the moon), Space (D-pad down) calls for fuel from the nearest outpost on the body.
- **Price:** its fuel price times `CALL_FACTOR` (3) on the ground or `CALL_ORBIT_FACTOR` (6) in orbit, plus a fee of `CALL_FEE` (200 cr) and `CALL_FEE_PER_KM` (2 cr) per km along the ground.
- **Arrival:** after `CALL_DELAY` (20 s) plus the distance at `CALL_SPEED` (400 m/s), on the world's clock.
- **Delivery:** it fills the tanks as far as the credits left after the fee go, and the fee is paid only if some fuel comes. A ship that has left orbit, or moved more than `CALL_REACH` (50 m) from where it called, misses it and pays nothing.
- **Card:** a card offers the call below `CALL_SHOWN` (a quarter) of the tanks, and counts down while the fuel is on its way.
- **No visuals:** nothing flies in.
- **Check:** `--fuel-check` ends with a call in orbit.

Remaining:
- **The walker's hover car:** `hovercar.Car` has no fuel or mass. Fuel for it would be an `isp`-like burn on its `THRUST` and lift.

## 5. Upgrades

### 5.1 The table

`upgrades.py` in the example:

```python
@dataclass(frozen=True)
class Upgrade:
    key: str
    name: str
    cost: float                                    # credits
    requires: tuple = ()                           # keys of the upgrades needed first
    effects: dict = field(default_factory=dict)    # "group.attribute": factor

UPGRADES = [
    Upgrade("gun_force_1", "Field emitter I", 1_500.0, (), {"gun.hold_force": 1.5, "gun.pull_range": 1.3}),
    Upgrade("gun_charge_1", "Capacitor bank", 2_500.0, ("gun_force_1",), {"gun.charged_speed": 1.4}),
    Upgrade("hold_1", "Cargo racks", 2_000.0, (), {"ship.cargo_capacity": 1.5, "ship.hold_m3": 1.5}),
    Upgrade("tank_1", "Auxiliary tank", 3_000.0, (), {"ship.fuel_capacity": 1.5, "ship.dry_mass": 1.05}),
    Upgrade("engine_1", "Main engine tuning", 6_000.0, ("tank_1",), {"ship.isp_main": 1.15}),
]
```

The groups are `ship`, `flight.ShipParams`, and `gun`, `gravity_gun.GunParams`. `EFFECTS` gives each changed parameter's name on the card and how its value is shown. `refusal(upgrade, owned, credits)` returns "owned", "needs …", "not enough credits", or None when the upgrade can be bought.

Planned upgrades not in the table:
- **Scanner:** a Wide-band scanner (1,200 cr), with `scanner.range` ×2. It needs a scanner parameter object first; `scanner.py` sets its reach with the module constants `AHEAD`, `STEP` and `STRIP`.
- **Suit (§8.2):** a Thermal liner (1,000 cr), with the cold limit −10 °C and the reserve ×1.5. A Heat exchanger (3,500 cr, needs the liner), with the hot limit +10 °C and the reserve ×1.5. These need `SuitParams` and additive effects.
- **Jetpack:** Jetpack bottles (900 cr), with the jet's burn ×0.5. They need a jet parameter object first; `character.py` drains `Character.jet_fuel` with the module constant `JET_BURN`.
- **Long-range package (§12):** 15,000 cr, needs the tank and engine upgrades, fuel capacity ×1.6.
- **Parts (§9):** parts from wrecks, as an extra cost.

### 5.2 Applying and buying

`effective(base, owned)` deep-copies the base parameters (group: parameters) and multiplies in the effects of each owned upgrade, in table order. `planet.py` keeps the owned keys in `state["owned"]` and the effective parameters in `state["params"]`. `apply_upgrades(owned)` recomputes them and hands them to the current ship (`Ship.params`) and gun (`Gun.p`). `new_ship`, `load_game` and `spawn_player` give every later ship and gun the same parameters.

On an outpost's pad, Page Up / Page Down (D-pad left / right) turn the outpost's card through its pages: trade first, then one page per upgrade. An upgrade's page shows:
- the credits and the upgrade's cost;
- each parameter it changes, from now to after (or, once owned, the value and what it was);
- what it needs first, and whether it can be bought.

Enter (D-pad up) on an upgrade's page buys it (`buy_upgrade`). The trade page counts the upgrades owned.

A save stores the owned keys (`"upgrades"`), and loading applies them before it makes the ship. `--upgrade-check` buys four upgrades at Base, tries one before the upgrade it needs, and checks the ship's and the gun's parameters through a save and load.

## 6. Save and load

Both programs already save:
- **`planet.py`** (`save_game`, `SAVE_VERSION` 2, `planet_save.json`, written to a `.tmp` file and then `os.replace`d) stores:
  - the clock and the terrain parameters
  - the mode, the free camera and the player's feet
  - the ship (`ship_record`: fuel, cargo, crashed) and where it is parked
  - the taken rocks and the prop records
  - the scanner's contacts
  - the market (credits and the outposts' supplies)
  - the upgrades owned
- **`walker.py`** (`save.py`, `VERSION` 1, `walker_save.json` and `walker_save.npz`, each written through a temporary file, the grids first) stores:
  - the clock, the player, the crates and loose rocks, the taken rocks
  - the credits and supplies, the objectives and the game's numbers
  - the waypoint and the car
  - the explored map's measured cells

  It autosaves every `AUTOSAVE_SECONDS` (120 s), on Quit and as the window closes. `--save-check` exists.

Later phases add to the records as they add the state, each with a new version and its migration:

```json
{
  "parts": { "coil": 1 },
  "outposts": { "Halden": { "reputation": 2.5 } },
  "suit": { "reserve": 0.8, "health": 1.0 },
  "pois": { "discovered": [], "looted": [] },
  "contracts": { "active": [], "offers_day": 3 },
  "modifiers": [ { "type": "crater", "body": "planet", "direction": [0, 0, 1], "radius": 60, "depth": 9, "time": 15000 } ],
  "events": { "next_shower": 21000.0 }
}
```

`planet.py`'s save is of version 2 (`SAVE_VERSION`). Since version 1 it has gained:
- `"upgrades"`, the owned keys (§5);
- `"resource"` on each cargo item (§1.3);
- `"resource"` on each prop's record (`Props.record`).

`load_game` migrates a version-1 save as it reads it (`migrate_save`):
- cargo items without a resource become ferrite;
- prop records without one become ferrite when their grade is above 0, else 0;
- `"upgrades"` starts empty.

Any other version is refused. The supply stays keyed by body name. Keying it by resource id (§3.2) is a later version's migration, which would drop the body-keyed supplies.

With `--play`, `autosave_on_landing` saves to `planet_save.json` each time the ship lands on an outpost's pad after flying. It doesn't save in the sandbox, where the places and checks put the ship on pads. `--save-check` ends by loading its save as one of version 1.

`walker.py`'s save (`save.py`, `VERSION` 1) is unchanged. Its loose rocks' records carry `"resource"` through `Props.record`, and records without one load as ferrite.

---

# Phase 2 — A living world

## 7. Day and night

### 7.1 Sun motion

The planet stays non-rotating, so the physics, orbits and bubble stay inertial. The sun moves instead, around a fixed tilted axis.

```python
# clock.py
DAY_LENGTH = 40 * 60.0               # s of game time per day
AXIS_TILT = math.radians(18.0)

def sun_direction(t, site_up, site_east, site_north):
    """Sun direction in planet space. The site sees noon at t = 0 (mod day)."""
    axis = normalize(add(scale(site_north, math.cos(AXIS_TILT)), scale(site_up, math.sin(AXIS_TILT))))
    noon = normalize(sub(site_up, scale(axis, dot(site_up, axis))))
    angle = 2.0 * math.pi * (t / DAY_LENGTH)
    return rotate(noon, axis, -angle)

def local_hour(direction, sun):
    """0..24: the local solar time at a direction on the body."""
    ...
```

- The tilt gives variety in sun angles, and long days and long nights at high latitudes.
- **Seasons** (optional): precess the tilt slowly over many days.
- The moon is lit by the same sun direction.

### 7.2 Horizon shadows under a moving sun

`PlanetWorld` measures the chunks' horizons again whenever the sun moves more than `HORIZON_SUN_STEP` (0.5°, `world.py`) from `horizon_sun`. With a 40-minute day the sun moves 0.15° per second, so that is a full rebuild every ~3.3 s.

1. **Only azimuth matters.** The stored horizon is the elevation of the terrain *along the sun's azimuth*. `horizonShadow` (`shadow.glsl`) compares it against the sun's elevation at runtime, so elevation changes need no rebuild.
   - Track, per chunk, the *local* azimuth it was built for. Rebuild only when the sun's local azimuth at the chunk's centre has moved more than a tolerance.
   - `horizonShadow` softens over about 1° near the camera and about 4° beyond 60 km, so tolerances of ~2° near and ~8° far are invisible.
   - Near noon at high sun, the azimuth moves fast while the shadows are short, so raise the tolerance with sun elevation.
2. **Several azimuths per vertex** (if 1 isn't enough). Store the horizon for 8 azimuths (8 × uint8 sines packed in two `uint`s), interpolate by the sun's azimuth at runtime, and never rebuild for the sun. This costs 8× the horizon samples in `chunk_build.comp` and changes the vertex layout.

### 7.3 Night

- **Exposure:** auto-exposure (average log luminance via a downsampled pass, adapting over ~2 s). The minimum is to ramp exposure with the sun's elevation at the camera.
- **Starfield:** in the sky pass where the atmosphere's transmittance is high (procedural points from a hash on directions, fixed in planet space).
- **Lights:**
  - ship headlights (spotlights from the ship's front, which also light the terrain)
  - a **suit lamp** (a spot attached to the camera)
  - outpost lights (point lights at the pad)

  The lighting is sun + sky, so this needs a small forward light loop in `terrain.frag`, `vegetation.frag` and `scene.frag`: up to 8 lights in a uniform buffer, culled per frame on the CPU.
- **Ore specks** glow with `SPECK_GLOW` on the Screens pass. Raising their emissive strength as the sun sets lets ore fields read from the ship at low altitude.

## 8. Temperature and the suit

### 8.1 Local temperature

```python
def local_temperature(climate_T, moisture, hour, wind_speed):
    # Daily swing: large in dry places, small in wet ones
    swing = 4.0 + 14.0 * (1.0 - clamp(moisture, 0.0, 1.0))
    diurnal = swing * math.cos(2.0 * math.pi * (hour - 15.0) / 24.0)   # warmest mid-afternoon
    chill = 0.0 if climate_T > 10.0 else 0.4 * wind_speed               # wind chill when cold
    return climate_T + diurnal - chill
```

`climate_T` and `moisture` come from the probe under the player (`PlanetWorld.ground_climate()`). It includes the lapse rate with height. The walker HUD shows the result next to speed: `TEMP −14 °C`.

### 8.2 Suit reserve

```python
@dataclass
class SuitParams:
    cold_limit: float = -5.0       # °C: below, the suit spends reserve
    hot_limit: float = 38.0
    reserve: float = 1.0           # capacity (upgrades multiply)
    drain_per_degree: float = 0.0008   # reserve per second per degree beyond the limit
    recharge: float = 0.05         # per second when sheltered

@dataclass
class SuitState:
    reserve: float = 1.0
    health: float = 1.0

def step_suit(state, params, temperature, sheltered, dt):
    if sheltered:
        state.reserve = min(params.reserve, state.reserve + params.recharge * dt)
        return
    beyond = max(0.0, params.cold_limit - temperature, temperature - params.hot_limit)
    state.reserve -= beyond * params.drain_per_degree * dt
    if state.reserve < 0.0:
        state.health -= -state.reserve * 0.5     # health drains from the deficit
        state.reserve = 0.0
```

- **Sheltered** means inside the ship or the car, within an outpost's pad reach, or near a deployable heater (a buyable prop the gun can carry).
- **Effects** as reserve runs out: a frost or heat-haze vignette, breathing sound, and slower movement below 20 %.
- **Zero health:** you wake up at the nearest outpost (`outposts.nearest`) and pay a credit fee. Loose cargo carried by the gun is dropped where you fell.
- **The moon:** no air, so the temperature is sun-or-shade (e.g. +90 °C in sun, −150 °C in shade). Until the suit is upgraded, its limits keep moon walks short.

## 9. Points of interest

### 9.1 Placement from the hash

POIs use a coarse grid per cube face, with one candidate per cell and the same deterministic hash approach as the vegetation. At 64 × 64 cells per face, a cell is ~2.5 km on the planet (radius 100 km) and ~0.6 km on the moon (radius 25 km).

```python
def poi_candidate(body, face, cx, cy, seed):
    h = hash4(body, face, cx, cy, seed)
    u = (cx + 0.15 + 0.7 * frac(h * 0.123)) / 64 * 2 - 1
    v = (cy + 0.15 + 0.7 * frac(h * 0.457)) / 64 * 2 - 1
    roll = frac(h * 0.789)
    return direction_from_face(face, u, v), roll, h
```

The candidate's kind is decided from `roll` against the chances of its biome, which is known once a probe returns the climate there. Validity (slope below a limit, not in the sea, not in a lake) is checked with probes when the player comes within scanner range. Invalid candidates are dropped.

### 9.2 Kinds

| Kind | Chance (by biome) | What it is | Interaction | Reward |
|---|---|---|---|---|
| Cargo pod | everywhere, 6 % | a crashed crate with a beacon | carry it to an outpost or into the cargo (gun) | contract target, or goods to sell |
| Survey beacon | 4 % | an old mast | activate (F) | reveals the map within 1.5 km, survey data |
| Wreck | 3 %, more in mountains | a broken ship hull (a few boxes and beams) | salvage (F at marked spots, 3 s each) | upgrade parts: coil, injector, nav core |
| Abandoned rig | 2 %, near ore veins | a small flattened site with a frame and crates | open crates | ore, parts |
| Geode field | 3 % in tundra/desert | a cluster of high-grade small rocks | mine | rich rocks of the local resource |
| Arch / spire | 2 % in desert/mountains | a rock formation (a few large convex rocks) | scan | survey data (scenic) |

- **Flattened sites:** rigs and wrecks are flat sites like the outposts' (`world.Site`, `sites.glsl`, at most `TERRAIN_SITES` = 8). They go in the modifier list (§11.2). Small kinds (pods, beacons, geodes) sit on the terrain as props or static meshes.
- **Discovery:** a POI shows on the compass once it's within scanner range (600 m base). It also shows once seen: in view and within 1.5 km, with line of sight checked by a few probes. Undiscovered POIs within 3 km show as a faint "signal" bracket without a name.
- **State:** `discovered` and `looted`, per POI id = (body, face, cx, cy), in the save. Looted POIs stay visible but greyed.

### 9.3 Survey data

- Every newly explored map cell (`explored_map.Grid`) earns 0.5 cr of survey data, and every scanned POI 50–200 cr. Survey data is a cargo item of zero mass.
- It sells at outposts with the `"cartography"` service, with the same supply curve as ore (§3.2).

## 10. Contracts

### 10.1 Kinds

| Kind | Task | Reward basis |
|---|---|---|
| Delivery | bring `q` kg of resource R (min grade) to outpost B | market value × 1.4 + distance bonus |
| Courier | carry a fragile crate from A to B by hand or in the cargo, without hard impacts | distance bonus × fragility |
| Recovery | bring the cargo pod at a POI to outpost B | fixed + distance |
| Survey | reveal 80 % of the cells within 1 km of a marker | per cell + fixed |
| Supply run (moon) | deliver goods to a moon outpost | large; needs the long-range package |

### 10.2 Generation

Offers per outpost are seeded by `(outpost, day index, seed)`, so they are the same all day, after a reload, and refresh at dawn. Python's `hash()` of a string changes from one run to the next, so the seed comes from `zlib.crc32`, as `outposts.py` and `rocks.py` already seed theirs:

```python
def offers(outpost, day, world_seed):
    rng = random.Random(zlib.crc32(f"{outpost.name}:{day}:{world_seed}".encode()))
    wanted = [k for k, d in outpost.demand.items() if d > 1.2]
    kinds = ["delivery", "courier", "recovery", "survey"] if wanted else ["courier", "recovery", "survey"]
    weights = [5, 2, 2, 2] if wanted else [2, 2, 2]
    out = []
    for _ in range(3):
        kind = rng.choices(kinds, weights=weights)[0]
        if kind == "delivery":
            r = rng.choice(wanted)
            q = rng.choice([200, 400, 800])
            out.append(Contract(kind, outpost.name, resource=r, quantity=q,
                                min_grade=rng.choice([0.3, 0.5, 0.7]),
                                destination=outpost.name, deadline_days=2,
                                reward=round(RESOURCES[r].value * q * 1.4 * 0.6, -1)))
        ...
    return out
```

### 10.3 State and the objective card

- `Contract.state` goes `offered → active → (completed | failed | expired)`.
- Up to 3 contracts are active at once. The objective card (`objectives.py`, `hud.py`) shows the pinned one, and the compass marks its target (destination outpost, POI or survey marker).
- **Fragile crate:** strikes are watched with `sk.PlanetPhysics.watch_strikes`. Each strike adds `damage += max(0, v − 2)²·k`, and above a threshold the crate breaks: the contract fails and the crate splits into junk.
- **Reputation:** completed contracts raise the outpost's `reputation`. Higher reputation unlocks better offers, better prices (`demand × (1 + 0.02 · rep)`) and the outpost's upgrades.

---

# Phase 3 — Events and the moon

## 11. Meteor showers

### 11.1 Scheduling

```python
@dataclass
class Shower:
    start: float            # world.time
    centre: tuple           # body-relative direction of the impact area
    body: str
    impacts: list           # [(time, direction, size)] sorted by time

def schedule_next(now, rng, player_dir):
    gap = rng.expovariate(1.0 / (25 * 60.0))             # mean 25 min of game time
    start = now + max(5 * 60.0, gap)
    # Impact area 3-10 km from where the player will probably be (on land)
    ...
```

- **Warning** 60 s before: a toast and an audio cue ("Meteor shower inbound, bearing 040"), and a compass marker for the area.
- **Composition:** 1 large impact (a meteorite of 2–6 t, crater 40–80 m) and 4–10 small ones (no crater, a glowing rock each), spread over ~40 s within 1–2 km of the centre.

### 11.2 Terrain modifiers (craters)

`sites.glsl` holds `TERRAIN_SITES` (8) flat sites in the `terrain` block: `sites[i]` (direction, height) and `siteShapes[i]` (flat radius, blend radius, body index, the body's radius). `terrainWithSites` mixes the ground towards the site's height within the blend radius, and `world.Site` / `PlanetWorld.set_sites` fill them.

Craters join them in one modifier list with a type:

```glsl
// modifiers.glsl (generalising sites.glsl)
struct Modifier {
    vec4 dirRadius;     // xyz direction (body-relative unit), w = radius (m)
    vec4 params;        // x: type | body << 8; y: height or depth; z: rim height or blend radius; w: the body's radius
};
layout(std140, set = 1, binding = 7) uniform Modifiers { uvec4 count; Modifier m[32]; } MOD;

// Polynomial smooth minimum of a and b, blended over k
float smin(float a, float b, float k) {
    float h = clamp(0.5 + 0.5 * (b - a) / k, 0.0, 1.0);
    return mix(b, a, h) - k * h * (1.0 - h);
}

float applyModifiers(vec3 dir, float h, uint body) {
    for (uint i = 0u; i < MOD.count.x; ++i) {
        Modifier md = MOD.m[i];
        if ((uint(md.params.x) >> 8u) != body) continue;
        float d = length(dir - md.dirRadius.xyz) * md.params.w;      // chord, as sites.glsl
        uint type = uint(md.params.x) & 255u;
        if (type == 1u) {                       // flat site
            if (d < md.params.z) h = mix(md.params.y, h, smoothstep(md.dirRadius.w, md.params.z, d));
        } else if (type == 2u) {                // crater
            float r = d / md.dirRadius.w;
            if (r >= 2.0) continue;
            float bowl = md.params.y * (r * r - 1.0);                    // negative inside
            float rim  = md.params.z * exp(-pow((r - 1.0) / 0.25, 2.0));
            float shape = r < 1.0 ? smin(bowl, rim, 0.3 * md.params.y)
                                  : rim * (1.0 - smoothstep(1.2, 2.0, r));
            h += shape;
        }
    }
    return h;
}
```

`moon.glsl` has a crater profile (`MOON_RIM_WIDTH`, `MOON_CRATER_SIZE`), which keeps new craters consistent with the moon's.

**Invalidation** when a crater is added:
- Bump the terrain version, so probes asked for before it are ignored.
- Queue a rebuild for every built chunk whose bounding sphere comes within `2 × radius` of the crater, nearest first. Keep the old vertices until the new ones arrive, as the horizon rebuild does.
- Horizon shadows of chunks within a few km may change slightly; the next azimuth rebuild catches them.
- Request new collider patches where the crater touches them.
- **Vegetation and rocks:** the scatter skips candidates inside a crater's radius (reading the same modifier list), and static rocks and trunks there are removed from the bubble.

Keep at most ~16 craters per body. When the list is full, drop the oldest crater farther than 20 km from the player, along with its record. Craters are saved in `modifiers`.

### 11.3 The meteor itself

- **Trajectory:** entry at 30 km altitude, 2–4 km/s, 25–60° to the horizon. It is a straight line plus gravity, computed in doubles and drawn relative to the camera.
- **Visuals:**
  - a glowing head: a camera-facing quad with an additive radial gradient, sized from its distance so it stays at least a few pixels across
  - a trail: a line strip of the last 2 s of positions with fading alpha, drawn like the orbit path (after the atmosphere, depth-tested, additive)

  Brightness scales with the meteor's size.
- **Impact:**
  - a flash: a short additive full-screen tint scaled by 1/distance²
  - a shock sound delayed by distance / 340 m/s
  - camera shake within 3 km
  - a dust plume: a few large soft camera-facing quads rising and fading for 20 s

  Bodies in the bubble within `3 × radius` get an outward impulse.
- **The meteorite:** a Prop at the crater's centre, resource Iridium, glowing hot (emissive fading over 3 minutes).
  - While hot, the gun refuses to grab it with "TOO HOT".
  - It is an ore boulder, so it breaks into chunks (`Rocks.breaks`).
- **Small impacts:** glowing iridium rocks lying on the ground, with no crater, cooling faster.

### 11.4 Racing for it (later)

Other prospectors: an NPC ship flies from the nearest outpost to the crater and lands. If it arrives first and loads the meteorite, the meteorite is gone. Its flight is `flight.Ship` with a scripted controller: climb, fly a heading, descend with assist.

## 12. Wind and storms

### 12.1 Wind field

```python
# wind.py
def wind_at(body, position, t, base_dir, base_speed):
    """Wind (m/s, planet space) at a point: a prevailing direction turning slowly,
    stronger with height, plus gusts."""
    up = normalize(body.relative(position))
    h = max(1.0, length(body.relative(position)) - body.radius)
    speed = base_speed * (math.log(h / 0.1) / math.log(1000.0 / 0.1))   # log profile, = base at 1 km
    tangent = normalize(sub(base_dir, scale(up, dot(base_dir, up))))
    gust = 0.25 * speed * noise3(scale(position, 1.0 / 200.0), t * 0.3)
    return scale(tangent, speed + gust)
```

- The clouds' wind uses the same base direction and speed at cloud height.
- **Ship:** `Ship.aerodynamic_force` uses the air velocity `v − wind` instead of `v`.
- **Hover car:** its grip and drag act on `v − wind`.
- **Props:** light props (< 50 kg) get `F = −½ ρ C_d A |v_rel| v_rel` in the bubble step.
- **The moon:** no wind (no air).

### 12.2 Storms

- A storm is a moving disk: a centre direction, a radius of 8–15 km, a speed and a lifetime. Inside it:
  - cloud coverage rises towards 0.95
  - the wind is ×2.5, with stronger gusts
  - visibility drops: the haze density in the atmosphere pass rises locally around the camera
- Effects:
  - the hover car and the ship's assist get random lateral forces above a wind threshold
  - outposts warn "storm approaching, landing pads closed", and contracts' deadlines pause
  - rain (camera-space streak particles)
- Storms scatter loose small props.

## 13. The moon expedition

### 13.1 Why go

- **Ilmenite** (30 cr/kg) and **ice** are found only on the moon. Meteor showers there are more frequent: with no atmosphere, small meteors don't burn up.
- **Trade:** the moon's outposts (Lowrim, Selene Deep) already pay `OTHER_BODY` (1.8×) for planet ore. With demand (§3.1) they pay more for planet goods (amber, quartz).
- **Refinery:** one of them offers the `"refinery"` service, turning ice into fuel at 3 kg of ice per kg of fuel. That undercuts its fuel price of 3.0 cr/kg (`FUEL_PRICE["moon"]`).

### 13.2 What it takes

- **Δv:** with `tank_1`, `engine_1` and `longrange`:

  | | Value |
  |---|---|
  | fuel capacity | 6,000 × 1.5 × 1.6 = 14,400 kg |
  | dry mass | 14,000 × 1.05 = 14,700 kg |
  | Isp | 450 × 1.15 = 517.5 s |
  | Δv, full tanks, empty hold | 517.5 · g0 · ln(29,100 / 14,700) ≈ 3,470 m/s |

  The stock ship's ≈ 1,570 m/s covers orbit (~600 m/s plus losses), the transfer (162 m/s) and a moon landing (≈ 200 m/s). It doesn't cover the trip back.
- **The suit** (§8.2): moon walks in sunlight need the heat exchanger, and in shade the thermal liner.
- **A transfer planner** on the ORBIT display:
  - the Hohmann phase angle the moon must lead by, `π − n_moon · t_transfer`
  - the current phase angle
  - the time until the burn ("BURN IN 04:12")
  - the Δv needed against `Ship.delta_v()`

  It also needs a prograde/retrograde marker hold in the assist (point the nose and hold).

```python
def hohmann_plan(gm, r1, r2, moon_angular_rate):
    a = 0.5 * (r1 + r2)
    t_transfer = math.pi * math.sqrt(a ** 3 / gm)
    dv1 = math.sqrt(gm / r1) * (math.sqrt(2.0 * r2 / (r1 + r2)) - 1.0)
    lead = math.pi - moon_angular_rate * t_transfer       # radians the moon must lead
    return dv1, t_transfer, lead
```

From 40 km this gives the 162 m/s and 53 minutes that `test_a_hohmann_transfer_from_a_low_orbit_reaches_the_moon` (`test_planet_flight.py`) checks.

---

## 14. Checks

Existing flags in `planet.py`, among others: `--cargo-check`, `--fuel-check`, `--trade-check`, `--save-check`, `--scan-check`, `--landing-check`, `--crack-check`. Extensions and new checks:

- `--trade-check` (extend):
  - sell 4,000 kg of one resource at one outpost in 1 kg steps and in one sale; the revenues agree within 0.1 %
  - after `SUPPLY_RECOVERY` of game time, the supply is 1/e of what it was
- `--fuel-check` (extend): a scripted climb from rest on the pad to a 40 km orbit, once with an empty hold and once with a 4 t hold. Fuel used matches the rocket equation (Δv used against ln of the mass ratio) within 3 %.
- `--cargo-check` (extend): rocks beyond the hold's limits stay in the net.
- `--resource-check` (new):
  - the GPU's resource ids and grades at 10,000 random directions equal the NumPy mirror's
  - every resource 1–5 appears on the planet, and 6–7 on the moon
- `--save-check` (extend): play a scripted sequence (mine, load, sell, buy an upgrade, take a contract, add a crater), then save, regenerate and load.
  - every listed state is equal
  - the crater's chunks are rebuilt
  - a version-1 save migrates
- `--crater-check` (new): add a crater 500 m from the camera.
  - within N frames, every chunk within 2 radii is rebuilt
  - no chunk shows a crack against the crater's edge (`CrackCheck`'s measurement)
  - no vegetation or static rock lies inside its radius
- `--day-check` (new): run one day at 16× warp.
  - chunk rebuilds caused by the sun number a few per chunk per day
  - the frame time stays within budget
- `--wind-check` (new):
  - a hovering ship with assist holds position within 2 m in a 15 m/s wind
  - a 5 kg prop drifts, and a 500 kg one doesn't

## 15. Milestones

1. **Resources.** Resource id in `resources.glsl`, a probe column, scatter rocks by resource with tints and specks, the value and density by resource. `--resource-check`.
2. **Prices by resource.** `trade.py` supply by resource and demand, outposts' demand from their surroundings, the trade card per resource. `--trade-check`.
3. **Hold limits.** `cargo_capacity` / `hold_m3`, the cargo net refusing what doesn't fit, running dry (the fuel call). `--cargo-check`, `--fuel-check`.
4. **Upgrades.** Table, effective parameters, buying at outposts. `--upgrade-check`.
5. **Save version 2.** New fields, the migration from version 1, autosave on landing. `--save-check`.
6. **Day and night.** Moving sun, azimuth-only horizon rebuild, auto-exposure, stars, lights. `--day-check`.
7. **Suit and temperature.** Local temperature, reserve, shelter, effects, recovery at an outpost.
8. **Points of interest.** Hash placement, kinds, discovery, salvage parts, survey data.
9. **Contracts.** Generation, states, objective card integration, fragile crates, reputation.
10. **Meteor showers.** Modifier list, crater invalidation, meteor visuals, meteorites. `--crater-check`.
11. **Wind and storms.** Wind field, ship, car and prop drag, storms. `--wind-check`.
12. **The moon expedition.** Moon resources, the refinery, long-range upgrades, transfer planner.

## 16. Pitfalls

- **Balancing by feel too early.** Put every number in `game_data.py` and add a debug overlay showing credits per minute over the last 10 minutes.
- **Price exploits:**
  - Selling in tiny batches is covered by the integrated price (`trade.sale`).
  - Buying and reselling at the same outpost: outposts don't sell the resources they buy, or sell them with a large spread.
  - Loading the same rock twice: the prop is removed (`Props.remove`) the frame it is loaded.
- **Probe budget:** resources, POI validity, temperature under the player, meteor impact points and wind sampling all want probes. Batch them per frame into the world's probe pass, give each consumer a region, and keep the count measured on the debug card.
- **Terrain version bumps** (craters) invalidate every in-flight probe. Ship contacts and the player's collider patch must tolerate one or two frames without fresh data by keeping the old patch.
- **Sun-driven rebuilds:** without the azimuth-only rule, a short day means continuous chunk rebuilding. Measure with `--day-check` before shortening the day.
- **Save compatibility:** the seed regenerates everything not saved. A change to the scatter hash, the resource field or the POI grid changes what a saved world contains, so version those functions and migrate or warn.
- **Too many markers:** POIs, signals, contracts, outposts, meteors and the ship all want the compass. Cap what it shows (nearest N per type, distance-faded); the map (M) shows everything.

## 17. References

- Previous guides: `docs/plans/planet_flight_mvp.md`, `on_foot_and_gravity_gun.md`
- Hohmann transfer and the rocket equation: `tests/python/test_planet_flight.py` checks the numbers used here
- Auto-exposure: the log-average luminance approach (Reinhard et al. 2002, *Photographic Tone Reproduction for Digital Images*)
