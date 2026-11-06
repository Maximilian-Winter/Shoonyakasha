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
  events.py           # new: meteor showers: scheduling, paths, craters kept, heat (§11); storms (§12.2)
  save.py             # extend: walker's save; planet.py's save_game / load_game likewise
python/shoonyakasha/planet/
  resources.py        # new: resource ids and a NumPy mirror of resources.glsl (for tests)
  rocks.py            # extend: rock_colour by resource
  nearby_trees.py     # extend: NearbyTree.resource
  clock.py            # new: day phase, sun direction
  world.py            # extend: craters (Crater, set_craters), the chunks near them built again
  wind.py             # new: wind field (§12.1)
python/shoonyakasha/pipelines/planet/shaders/
  resources.glsl      # extend: a resource id beside the richness
  vegetation_scatter.comp  # extend: ore rocks carry the resource id
  craters.glsl        # new: craters after the flat sites, §11.2
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

Iridium (id 8) comes only from meteorites (§11): `RESOURCE_IRIDIUM` in `resources.glsl`, which the shaders never choose, and `resources.IRIDIUM`, with its entries in `ore_colours.glsl` and `rocks.ORE_TINTS` / `SPECK_COLOURS` (black, glassy; violet specks), in `rocks.DENSITIES` and in `trade.VALUES`. The walker's Outpost tab shows its price.

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
    effects: dict = field(default_factory=dict)    # "group.attribute": factor, or ("add", amount)
    parts: dict = field(default_factory=dict)      # part (pois.PARTS): count, spent when bought

UPGRADES = [
    Upgrade("gun_force_1", "Field emitter I", 1_500.0, (), {"gun.hold_force": 1.5, "gun.pull_range": 1.3}),
    Upgrade("gun_charge_1", "Capacitor bank", 2_500.0, ("gun_force_1",), {"gun.charged_speed": 1.4},
            {"coil": 1}),
    Upgrade("hold_1", "Cargo racks", 2_000.0, (), {"ship.cargo_capacity": 1.5, "ship.hold_m3": 1.5,
                                                   "car.cargo_capacity": 1.5, "car.hold_m3": 1.5}),
    Upgrade("tank_1", "Auxiliary tank", 3_000.0, (), {"ship.fuel_capacity": 1.5, "ship.dry_mass": 1.05,
                                                      "car.fuel_capacity": 1.5}),
    Upgrade("engine_1", "Main engine tuning", 6_000.0, ("tank_1",), {"ship.isp_main": 1.15,
                                                                      "car.drive_burn": 1.0 / 1.15},
            {"injector": 2}),
    Upgrade("suit_1", "Thermal liner", 1_000.0, (), {"suit.cold_limit": ("add", -10.0), "suit.reserve": 1.5}),
    Upgrade("suit_2", "Heat exchanger", 3_500.0, ("suit_1",), {"suit.hot_limit": ("add", 10.0),
                                                               "suit.reserve": 1.5}, {"coil": 1}),
]
```

**Groups:**
- `ship`: `flight.ShipParams`, `planet.py`;
- `car`: `hovercar.CarParams`, `walker.py`;
- `gun`: `gravity_gun.GunParams`, both games;
- `suit`: `suit.SuitParams`, `walker.py`.

**Functions:**
- `effective` and `effects_for`: each game applies only the effects on the groups it has.
- `for_groups`: each game offers only the upgrades with an effect on its groups. `planet.py`'s `SHIP_UPGRADES` and the walker's `WALKER_UPGRADES` come from it.
- `EFFECTS`: each changed parameter's name on the card, and how its value is shown.
- `refusal(upgrade, owned, credits, parts=None)`: returns "owned", "needs …" (upgrades, or parts in a game that has them), "not enough credits", or None when the upgrade can be bought.

Planned upgrades not in the table:
- **Scanner:** a Wide-band scanner (1,200 cr), with `scanner.range` ×2. It needs a scanner parameter object first; `scanner.py` sets its reach with the module constants `AHEAD`, `STEP` and `STRIP`.
- **Jetpack:** Jetpack bottles (900 cr), with the jet's burn ×0.5. They need a jet parameter object first; `character.py` drains `Character.jet_fuel` with the module constant `JET_BURN`.
- **Long-range package (§12):** 15,000 cr, needs the tank and engine upgrades, fuel capacity ×1.6, and 2 injectors and a nav core.

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
- **`walker.py`** (`save.py`, `VERSION` 2, `walker_save.json` and `walker_save.npz`, each written through a temporary file, the grids first) stores:
  - the clock, the player, the crates and loose rocks, the taken rocks
  - the credits and supplies, the objectives and the game's numbers
  - the waypoint and the car (its fuel and cargo)
  - the upgrades owned and the day's offset
  - the suit, the points of interest's states, the parts, the survey data and which crates are pods (§8, §9)
  - the contracts, the offers accepted today, the pin, the reputations and which crates are couriers' (§10)
  - the explored map's measured cells
  - the meteorites' craters, the next shower's start, which props are hot and since when, and which is the last meteorite (§11)
  - the storm under way or to come, and each contract's held-back time (§12.2)
  - the body the car is reckoned around (`"body"`) and its space mode (`"space"`: the ship's position and velocity from the planet's centre, orientation, spin, assist and throttle; none when hovering) (§13.2)

  Those of §8–§11 are keys a version-2 save may lack; loading one without them starts them as a new game has them.

  It autosaves every `AUTOSAVE_SECONDS` (120 s), on getting out of the car parked on an outpost's pad, on Quit and as the window closes. `--check` saves and loads, on foot and driving.

The keys of §11 and §12.2:

```json
{
  "modifiers": [ { "type": "crater", "body": "planet", "direction": [0, 0, 1], "radius": 30, "depth": 5.4, "rim": 1.8, "time": 15000.0 } ],
  "events": { "next_shower": 21000.0, "storm": { "centre": [0, 0, 1], "axis": [1, 0, 0], "speed": 9.0, "radius": 12000.0, "start": 18000.0, "end": 21600.0, "body_radius": 100000.0 } },
  "hot": [null, [15000.0, 180.0]],
  "meteorite": 1
}
```

`hot` and `meteorite` go with `props` (a prop's time of impact and cooling seconds, and the last meteorite's index). `next_shower` is the next shower's start, or the start of one under way; loaded, that shower is planned anew around the camera, starting at that time or 60 s after the load, whichever is later.

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

`walker.py`'s save (`save.py`) is of version 2 too. `save.read` migrates a version-1 save (`save.migrate`):
- the car gets a full tank (`fuel` None) and an empty hold;
- prop records without a resource become ferrite for ore, else 0;
- `"upgrades"` starts empty, and the day starts at the new game's hour (`day_offset` None).

---

# Phase 2 — A living world

## 7. Day and night

### 7.1 Sun motion

The planet stays non-rotating, so the physics, orbits and bubble stay inertial. The sun moves instead, around a fixed tilted axis (`clock.py` in the example):

```python
DAY_LENGTH = 40 * 60.0               # s of the world's clock
AXIS_TILT = math.radians(18.0)

def sun_direction(time, up, east, north):
    axis = normalize(add(scale(north, math.cos(AXIS_TILT)), scale(up, math.sin(AXIS_TILT))))
    noon = normalize(sub(up, scale(axis, dot(up, axis))))
    return rotate(noon, axis, -2.0 * math.pi * time / DAY_LENGTH)
```

- **Shape of the day:** the site sees noon at time 0, with the sun 72° up in the south. The sun rises in the east at 06:00 and sets in the west at 18:00, and reaches 72° below at midnight. `hour(time)` and `time_of(hour, now)` convert between the two.
- **Where it's set:** `planet.py` keeps the day's offset from the world's clock (`day_time`). With the day cycle on (a menu toggle, on with `--play`), `update_day` sets `PlanetWorld.sun_elevation` / `sun_azimuth` from `sun_direction` each frame.
- **Moving the hour:** Z / X move an hour, and so does the menu's Time of day. The save keeps the cycle and the offset.
- **Moon:** it is lit by the same sun direction.
- **Seasons** (not built): precess the tilt slowly over many days.

### 7.2 Horizon shadows under a moving sun

The stored horizon is the elevation of the terrain *along the sun's azimuth*, and `horizonShadow` (`shadow.glsl`) compares it with the sun's own elevation. So only the azimuth needs new horizons.

`PlanetWorld.sun_moved` measures them again in three cases:
- none were measured yet;
- the sun has moved `HORIZON_SUN_MAX` (5°) in all;
- with the sun above `HORIZON_AZIMUTH_FLOOR` (−6°) at the viewpoint, its azimuth there has turned by more than `horizon_tolerance(elevation)`. That tolerance is 2°, easing to 8° as the sun climbs from 30° to 70°, where shadows are short.

The rule is global, decided at the viewpoint, not per chunk. Over a day at 16× warp, `--day-check` counts 76 rebuilds. A 0.5° step would have made 707. The 5° cap accounts for most of the 76.

If the stale azimuths ever show far from the viewpoint, store the horizon for several azimuths per vertex instead: 8 × uint8 sines packed in two `uint`s, interpolated by the sun's azimuth at runtime, so the sun never forces a rebuild. That costs 8× the horizon samples in `chunk_build.comp` and changes the vertex layout.

### 7.3 Night

- **Exposure** (`clock.exposure_scale`): `PlanetWorld.exposure_scale` multiplies the menu's exposure. With the day cycle on it follows the sun's elevation at the camera: 1 down to 0°, rising in its logarithm to `NIGHT_SCALE` (6) at −10° and below. An auto-exposure pass (average log luminance, adapting over ~2 s) would also respond to what is on screen.
- **Stars** (`atmosphere.frag`): on a grid of cells over a cube's faces in planet space, with `STAR_SHARE` of the cells holding a star of hashed place, brightness and colour, a spot about a pixel across. They fade out as the sky's own light along the ray rises past a few thousandths of the sun's illuminance (`STAR_SKY_FADE`), so they show at night and in late twilight, and from space.
- **The ground at night:** dark but for the local lights. There is no moonlight or light from the planet.
- **Lights** (`lights.glsl`, `PlanetWorld.lights`): the local lights go on the end of `AtmosphereParams` (`lightCount`, then `lightPosition`, `lightDirection` and `lightColour`, 8 each). That block is already bound wherever the surfaces are lit.
  - **Culling:** each frame `upload_lights` sends the 8 nearest the camera, of those within their range plus 2 km, as positions relative to the camera.
  - **Shading:** `localLights(albedo, relativePos, normal, twoSided)` sums each light's colour over the distance squared, faded to 0 at its range, and within a spot's cone. `terrain.frag` (both bodies), `vegetation.frag` (foliage two-sided) and `scene.frag` add it to the sun and sky. There are no light shadows.
  - **Which lights** (`update_lights` in `planet.py`, on with the sun 2° below the horizon at the camera): one 10 m over each outpost's pad; the ship's two headlights, dipped 6° below its nose; and on foot a suit lamp beside the eye along the view, mounted so the gun's viewmodel stays outside its beam.
  - **Brightness:** the intensities were chosen against the night exposure (6×).
  - **Cost:** about 0.1 ms per frame in `--day-check`.
- **Ore specks:** both the scattered ore rocks' specks (`vegetation.frag`) and the loose rocks' (`SPECK_GLOW`, the Screens pass) are emissive light in the HDR image. The atmosphere pass then applies the exposure, so the night exposure brightens them with everything else and ore fields read in the dark.

## 8. Temperature and the suit

### 8.1 Local temperature

The suit is the walker's (`walker.py`); its rules are plain Python in `suit.py`.

```python
def local_temperature(temperature, moisture, hour, wind_speed):
    dry = 1.0 - max(0.0, min(1.0, moisture))
    swing = SWING_WET + SWING_DRY * dry                                   # 4 + 14 x dryness
    diurnal = swing * math.cos(2.0 * math.pi * (hour - WARMEST_HOUR) / 24.0)   # warmest at 15:00
    chill = WIND_CHILL * wind_speed if temperature < CHILL_BELOW else 0.0     # 0.4 per m/s below 10 C
    return temperature + diurnal - chill
```

- **Inputs:** `walker.here_temperature` feeds it the climate under the camera (`PlanetWorld.ground_climate`, with the lapse rate for height), the local hour where the camera is (`clock.local_hour`) and the wind's speed 10 m above the ground at the camera (`PlanetWorld.wind_at`, §12.1).
- **The moon** (`moon_temperature`): no air, so −150 °C in the shade, rising to +90 °C as the sun's sine of elevation rises to 0.2.
- **Display:** the objective card shows TEMP.

### 8.2 Suit reserve

```python
@dataclass
class SuitParams:
    cold_limit: float = -5.0             # degrees C: below, the suit spends its reserve
    hot_limit: float = 38.0
    reserve: float = 1.0                 # its capacity
    drain: float = 0.0008                # reserve a second per degree beyond the limits
    recharge: float = 0.05               # reserve a second, sheltered
    heal: float = 0.02                   # health a second at an outpost

def step_suit(state, params, temperature, sheltered, at_outpost, dt):
    if sheltered:
        state.reserve = min(params.reserve, state.reserve + params.recharge * dt)
        if at_outpost:
            state.health = min(1.0, state.health + params.heal * dt)
        return False
    state.reserve -= beyond(params, temperature) * params.drain * dt
    if state.reserve < 0.0:
        state.health = max(0.0, state.health + HEALTH_DRAIN * state.reserve)   # half the deficit
        state.reserve = 0.0
    return state.health <= 0.0
```

- **Sheltered:** in the car, or on an outpost's pad (`walker.at_outpost`). Health recovers only on a pad.
- **Effects** (`suit.effects`, the walker's HUD tint):
  - below half the reserve, a frost-blue (or heat-orange) tint grows from the screen's edges, up to 0.35 alpha;
  - below a fifth, the walk slows to 0.6 and there is no running;
  - the card shows SUIT, with the health once it is below full.
- **Zero health** (`wake_at_outpost`): the player comes to on the pad of the nearest outpost on its body (`outposts.nearest`), charged `RECOVERY_FEE` (250 cr, as far as the credits go). What the gun held stays where the player fell, and the suit is full again.
- **Saving:** the save keeps the reserve and the health.
- **Upgrades:** the Thermal liner and the Heat exchanger (§5) change `SuitParams` additively.
- **Not built:**
  - a deployable heater (a buyable prop the gun can carry) as shelter;
  - breathing sounds;
  - getting to the moon, which the walker has no way to do yet.

## 9. Points of interest

### 9.1 Placement from the hash

The points of interest are the walker's (`walker.py`), on the planet; their rules are plain Python in `pois.py`.

**The grid:** each cube face is cut into `CELLS` × `CELLS` (64 × 64) cells, about 2.5 km across on the planet. Each cell has one candidate (`candidate`):
- a direction hashed (`zlib.crc32`) from the body, face, cell and the planet's seed, kept 15 % away from the cell's edges;
- and a roll, 0 to 1.

**Measuring:** `Pois.want` queues the candidates within `POI_REACH` (3 km) of the player, again once the player has moved 500 m. `walker.probe_pois` measures them with the extra probes: height, climate, ore, resource, and the water level there. The explored map yields the probes while candidates wait. A candidate is valid above 2 m and out of the water. Slope isn't checked.

### 9.2 Kinds

`kind_for(roll, height, climate, ore)` adds up the chances of the kinds whose `where` holds, in table order, and returns the first whose sum passes the roll:

| Kind | Chance, where | What it is | Use | Reward |
|---|---|---|---|---|
| Cargo pod | 6 %, everywhere | an orange 1.2 m crate (300 kg prop) by a beacon pole | carry it with the gun onto an outpost's pad | 600 cr once settled there |
| Survey beacon | 4 %, everywhere | a mast | E within 5 m | reveals the map within 1.5 km (`ExploredMap.reveal`), survey data 50–200 cr |
| Wreck | 3 %, 6 % above 1,200 m | a hull of boxes | E held 3 s at each of 3 spots | a part each: coil, injector or nav core (by a hash) |
| Abandoned rig | 2 %, where the ore is above 0.3 | a frame and two crates | E at each crate | a part and 2 ore rocks of the local resource at grade 0.75 |
| Geode field | 3 %, below 2 °C or moisture below 0.3 | 6 loose ore rocks of the local resource at grade 0.95, made within 120 m | — | the rocks |
| Rock arch | 2 %, moisture below 0.3 or above 1,200 m | two pillars and a span | E within 25 m | survey data 50–200 cr |

- **No flattened sites:** everything stands on the terrain, sunk 0.3 m. The terrain's flat sites (at most 8) are the outposts'.
- **Drawing:** within 2 km a POI is drawn from boxes (`LAYOUTS`) in a clearing of the plants and rocks (`CLEAR_RADIUS`, `PlanetWorld.set_clearings`). The boxes have no collision.
- **Discovery:** a POI is discovered within `SCAN_RANGE` (600 m), or within 1.5 km inside the view's cone; line of sight isn't checked. It then shows on the compass and the map by name, greyed once looted. Undiscovered ones within 3 km show as "Signal".
- **Using:** E acts on the nearest unused spot in reach (`SPOTS`), with a hint on the HUD.
- **Saving:** the save keeps each POI's `discovered`, `looted` and `done` (the spots used) by key (body, face, cx, cy), and which crates are pods.
- **Selling only when settled:** ore rocks and pods sell on a pad only once they've settled, asleep in the physics or resting.

### 9.3 Survey data

- **Earning:** every map cell explored around the player (`ExploredMap.walked`; revealed cells don't count) is 0.5 cr of survey data, and a beacon or an arch gives 50–200 cr.
- **Selling:** it's held as a value and sold from the Outpost tab at any outpost (`trade.survey_sale`, `Market.sell_survey`). Its price falls with the outpost's supply of it as ore's does: `SURVEY_HALF` is 2,000 cr, under the supply key `"survey"`.
- **Parts:** they're part of the upgrades' price (`Upgrade.parts`, §5): the Capacitor bank takes a coil and Main engine tuning two injectors.
- **Suit upgrades:** Thermal liner (cold limit −10 °C, reserve ×1.5) and Heat exchanger (a coil; hot limit +10 °C, reserve ×1.5) use additive effects (`("add", value)`). `planet.py` has no parts, so its upgrades need none.

## 10. Contracts

Contracts are in `contracts.py` (generation, the task's text and progress, fragile damage, the record) and `walker.py` (the Contracts tab, the card, the mechanics, the save).

### 10.1 Kinds

| Kind | Task | Reward |
|---|---|---|
| Delivery | bring 200, 400 or 800 kg of a planet resource at grade 0.3, 0.5 or 0.7 or better to the offering outpost | its value at that grade (`trade.VALUES`, `GRADE_POWER`) × 1.4 + 100 |
| Courier | carry a fragile crate (0.8 m, 60 kg) to another outpost on the planet | 150 + 4 per km along the ground |
| Recovery | bring the cargo pod (0.9 m, 200 kg) 1–4 km out back to the outpost | 400 + 60 per km out |
| Survey | explore 80 % of the map's cells within 300 m of a marker 1–3 km out | 300 |

Each reward is times 1 + 0.1 × the outpost's reputation, rounded to 10 cr. A contract is due by the dawn after its last day, two days after the offer's. The moon's supply run waits for the moon (Phase 3).

### 10.2 Generation

`offers(outpost, day, seed, outposts, radius, reputation)` gives three offers for an outpost (a name and a direction), drawn from a `random.Random` seeded with `zlib.crc32` of the outpost's name, the day and the world's seed, so they are the same all day and after a reload. Python's `hash()` of a string changes from one run to the next, so the seed comes from `zlib.crc32`, as `outposts.py` and `rocks.py` seed theirs. The kinds are weighted 3 : 2 : 2 : 2 (delivery, courier, recovery, survey); a courier with no other outpost becomes a delivery. The day is `clock.day`, which turns at 06:00 at the site. Only the planet's outposts offer contracts. Offers accepted are left out for the rest of the day.

### 10.3 State, the tab and the card

- **State:** `Contract.state` goes `offered → active → (completed | failed | expired | abandoned)`. Up to 3 are active at once.
- **Contracts tab:** with the car or the player on an outpost's pad, its offers, each with Accept; the active contracts, each with Pin; and Abandon the pinned. A contract accepted while none is pinned is pinned, and when the pinned one ends the pin moves to the first still active.
- **Card, compass and map:** the pinned contract replaces the objective on the card (its task, a bar for a delivery's kilograms or a survey's share, and the hours left) and is marked "Contract" on the compass and the map: a delivery's and a courier's destination; a recovery's pod until it is in the hold or 20 m from its point, then the outpost; a survey's marker.
- **Delivery:** ore that settles on the destination's pad, or that the car's cargo brings there (Sell the car's cargo), is taken in place of being sold.
- **Courier:** the crate goes into the car's hold when the car is on the pad and it fits, else down 2 m ahead of the player. `PlanetPhysics.watch_strikes` reports only strikes by dynamic bodies, so the crate's hits are read from its velocity instead: each frame its speed falls, the change of its velocity is a strike. In the hold the car's bumps (`bump_car`) strike it at the speed into the obstacle. Each strike adds `damage += max(0, v − 2)² / 40`; at 1 the crate breaks, it is removed and the contract fails.
- **Recovery:** the pod is a point of interest with a fixed kind (`Poi.fixed_kind`, `Pois.place`), measured and made like the others; the save's POI record leaves it out and it is placed again from the contract on load.
- **Delivering a crate:** a courier's crate or a recovery's pod settled on its destination's pad, or in the hold with the car slower than 1 m/s on that pad. Either loads into the hold through the cargo net.
- **Survey:** every second the share of the explored map's cells within 300 m of the marker that are measured (`ExploredMap.share`); at 80 % it completes.
- **Reputation:** a completed contract pays its reward and raises its outpost's `reputation` by one; failed, expired or abandoned, it lowers it by one, to no lower than −5. Each point raises the outpost's offers' rewards by 10 % and its prices by 2 % (`trade.REPUTATION_PRICE`, in `base_price`).
- **Saving:** the save keeps the active contracts (`record`, `restore`), today's offers accepted, the pin, the reputations by outpost name, the counts, and which crates are couriers' (which are pods, as before); a crate in the hold is a cargo item with its `"contract"` and `"volume"`.

---

# Phase 3 — Events and the moon

## 11. Meteor showers

### 11.1 Scheduling

`events.py` (the walker's) plans the showers; `walker.py` runs them on the planet (`update_showers`).

```python
@dataclass
class Impact:
    time: float                  # the world's clock when it strikes
    direction: tuple             # unit, from the body's centre
    mass: float                  # kg of its meteorite or rock
    grade: float
    speed: float                 # m/s as it strikes
    angle: float                 # degrees below the horizon as it strikes
    bearing: float               # degrees clockwise from north it travels towards
    crater: tuple = None         # (radius, depth, rim) for the large one

@dataclass
class Shower:
    start: float                 # the world's clock its impacts are drawn from
    centre: tuple                # unit direction of its centre
    impacts: list                # sorted by time

def schedule(now, rng, player, radius, start=None):
    if start is None:
        start = now + max(MIN_GAP, rng.expovariate(1.0 / MEAN_GAP))   # 5 min at least, 25 min on average
    centre = along_ground(player, rng.uniform(0.0, 360.0), rng.uniform(AREA_NEAR, AREA_FAR), radius)   # 3-10 km
    ...
```

- **Composition:** 1 large impact at the centre (a meteorite of 2–6 t, a crater 40–80 m across, its radius growing with the mass, 0.18 of the radius deep with a rim 0.06 of it high) and 4–10 small ones (a rock of 15–60 kg each, no crater) within 1–2 km of the centre, all at times drawn over 40 s from the start. Every meteor of a shower comes from about the same bearing (within 10°), at 2–4 km/s and 25–60° below the horizon as it strikes, each with a grade of 0.4–0.8.
- **On land:** the impacts' ground is measured with the extra probes. While the large impact's ground lies under the sea, a lake or a river, the shower is planned again with the same start, up to `SHOWER_TRIES` (4) times.
- **Warning** 60 s before the start, once the large impact's ground is known: a toast ("Meteor shower inbound: bearing 040, 6.2 km") and a marker on the compass at the centre until the last impact. The next shower is planned once the last has struck.
- Under `--check` no shower is planned; the check gives its own.

### 11.2 Terrain modifiers (craters)

`sites.glsl` holds `TERRAIN_SITES` (8) flat sites in the `terrain` block: `sites[i]` (direction, height) and `siteShapes[i]` (flat radius, blend radius, body index, the body's radius). `terrainWithSites` mixes the ground towards the site's height within the blend radius, and `world.Site` / `PlanetWorld.set_sites` fill them.

`craters.glsl` holds `TERRAIN_CRATERS` (16) craters after them in the `terrain` block: `craters[i]` (direction, radius) and `craterShapes[i]` (depth, rim height, body index, the body's radius); a radius of 0 leaves the entry unused. `world.Crater(body, direction, radius, depth, rim)` and `PlanetWorld.set_craters` (at most `MAX_CRATERS`) fill them. In units of the radius `r`, a crater's shape is the smooth minimum of its bowl and its rim, faded out by `CRATER_REACH` (2) radii:

```glsl
float craterShape(float r, float depth, float rim) {
    if (r >= CRATER_REACH) return 0.0;
    float bowl = depth * (r * r - 1.0);                 // depth below the ground at the centre, 0 at the radius
    float x = (r - 1.0) / CRATER_RIM_WIDTH;             // 0.25 radii
    float ridge = rim * exp(-x * x);
    return craterSmoothMin(bowl, ridge, max(CRATER_SMOOTHNESS * depth, 1e-3))   // 0.3 of the depth
         * (1.0 - smoothstep(1.2, CRATER_REACH, r));
}
```

`terrainWithCraters(h, dir, body, smallest)` adds the shapes of a body's craters to its ground after the flat sites, in the chunks (`chunk_build.comp`; for the normals, without the craters smaller than two vertex spacings, as the moon's own craters), the probes, the collider probes and the height grid. Distances are chords, as in `sites.glsl`.

**Invalidation** when `set_craters` adds or removes a crater on a body whose tree is made:
- `PlanetLod.requeue_near(body, direction, 2 × radius)` queues every built chunk whose bounding sphere comes within two radii of the line out from the body's centre along the crater's direction, ahead of the queue, nearest first. Each chunk keeps its old vertices until its job builds it again, as with the sun's rebuild.
- The terrain version goes up, so the probes asked for before are dropped. The ground patches are measured again, and the bubble's trunks and static rocks are cleared and made again from the next lists (`on_foot`).
- The horizon shadows of chunks beyond two radii are not measured again until the sun's next rebuild of every chunk.
- **Vegetation and rocks:** the scatter (`scatterCandidate`, for the trees, the grass and the rocks) skips candidates within a crater's radius (`terrainInCrater`).

The walker keeps the meteorites' craters with the time each was dug (`events.keep`), at most `MAX_CRATERS` (16). When that many are kept, the oldest whose centre lies farther than 20 km along the ground from the camera goes, or the oldest of all when none does. They are saved in `modifiers` (§6).

### 11.3 The meteor itself

- **Trajectory:** `events.Path` flies each meteor back from where it strikes, in doubles, under the body's gravity in 0.05 s steps, until it is 30 km above the radius; its flight lasts the steps taken (8–27 s on the planet). Its position at a time is between two steps' points.
- **Visuals:** glowing quads (`viewmodel.Glows`, the Glow pass: additive, after the atmosphere, depth-tested), placed relative to the camera each frame (`draw_showers`):
  - a head: a camera-facing disc 25 m across (half that for a small meteor), at least 24 pixels of the screen's height across, three times its colour (the sum is clamped at white, which widens its bright core), brightening over its first 1.5 s;
  - a trail: 8 streaks along the last 2 s of its path, twice its colour, narrowing and fading towards its end.

  A meteor striking near the camera comes almost straight at it, so its trail shows little.

  A small meteor is 0.6 as bright as the large one.
- **Impact** (`strike`):
  - a flash: the HUD covers the screen with a warm white of alpha 0.6, full within 2 km and falling with the square of the distance beyond, fading over 0.6 s
  - the view shaken within 3 km, by up to 1.5° at the impact and none at 3 km, fading over 1.5 s
  - a dust plume: 6 discs (2 for a small impact) rising and growing from the blast's radius to three times it, fading over 20 s, and fading out within three of their sizes from the camera

  The small impacts' flash and shake are 0.6 as strong. The props in the bubble within 3 blast radii (the crater's radius, or 3 m for a small impact) are pushed away and up, at up to 6 m/s at the impact and none at that reach.
- **The meteorite:** an ore boulder of Iridium (§1.1) at the large impact's grade and mass, starting 2 m above the ground measured there before the impact and flying down at 5 m/s into the crater until its ground is measured (props.py's flying props). It glows (a disc three times its radius across) while hot, fading evenly over 3 minutes (`events.heat`).
  - While hot, the gun refuses to grab it with "TOO HOT" (`gravity_gun.Gun`'s `too_hot`, given by `OnFoot.too_hot`).
  - It is an ore boulder, so it breaks into chunks (`Rocks.breaks`); the chunks of a hot meteorite are as hot (`Rocks.on_split`).
  - The compass marks it while it lies whole.
- **Small impacts:** glowing rocks of iridium at their mass, with no crater, cooling over 1 minute.

### 11.4 Racing for it (later)

Other prospectors: an NPC ship flies from the nearest outpost to the crater and lands. If it arrives first and loads the meteorite, the meteorite is gone. Its flight is `flight.Ship` with a scripted controller: climb, fly a heading, descend with assist.

## 12. Wind and storms

### 12.1 Wind field

`shoonyakasha/planet/wind.py`, and `PlanetWorld.wind_at(body, point, height=None)` for `wind_speed` (m/s at 1 km) at the world's clock:

```python
def wind_at(point, up, time, speed, height):
    direction = prevailing(time)                  # BASE_DIRECTION turned about TURN_AXIS once every 6 h
    tangent = sub(direction, scale(up, dot(direction, up)))
    share = length(tangent)                       # weaker towards where the direction is straight up or down
    along = scale(tangent, 1.0 / share)
    steady = speed * share * profile(height)      # ln(h / 0.1) / ln(1000 / 0.1), h at least 1 m
    return scale(along, steady + gust(point, time, along, steady))
```

- **Height:** `height` is above the ground. `PlanetWorld.wind_at` takes it as the height above the ground under the viewpoint when it isn't given (above the radius while that isn't known). The profile is 0.25 at 1 m, 0.5 at 10 m and 1 at 1 km.
- **Gusts:** 0.25 of the steady speed times value noise (`noise3`) over 200 m, carried along with the wind.
- **The clouds** (`PlanetWorld.blow_clouds`) drift along the same prevailing direction, at the profile's speed in the middle of the cloud layer. Their coverage noise's offset moves against that direction turned by `CLOUD_ROTATION` (clouds.glsl's), and their detail noise's drift is that offset turned back.
- **Ship:** `Ship.step(..., wind=None)` and `Ship.aerodynamic_force(planet, wind)` drag and lift on the velocity less the wind (`planet.py`'s `ship_wind`). Translation assist holds the ship against them as it does against gravity.
- **Hover car:** `Car.step(..., wind=None)`. Its drive's drag stands for the still air's, so the wind adds the difference between the air's drag on its velocity through the air and on its velocity: `AIR_DRAG` × (|v − w|(v − w) − |v| v), along the ground, times its share of the way from its cushion to 20 m up. On its cushion the wind doesn't move it; in the air it drifts and feels head- and tailwinds.
- **Props:** `Props.drag` (in the physics) and `Props.fly` (out of it) drag them on their velocity less the wind, the same for every mass; friction and their weight decide whether they move. In the physics, props slower than 5 m/s through the air feel no drag.
- **The suit:** the wind chill (§8.1) takes the wind's speed 10 m above the ground at the camera.
- **The moon:** no wind (no air).

### 12.2 Storms

`wind.Storm` is a moving disk: a centre (a unit direction at its start) moving along the ground at `speed` about an `axis`, a `radius`, a `start` and an `end` on the world's clock. Its strength rises from 0 over its first 10 minutes (`FADE`) and falls back over its last 10; `Storm.at(direction, time)` is that strength inside its radius less its edge (a quarter of the radius), falling to 0 across the edge. `PlanetWorld.storm` holds one (or None). Where it is:
- **Wind:** the steady speed is up to 2.5 times higher (`STORM_WIND`), and the gusts' share up to 0.5 (`STORM_GUST`).
- **Clouds:** `upload_storm` gives the cloud shader its centre, radius, edge and strength (CloudParams' `storm` and `stormShape`); the coverage rises towards 0.95 (`STORM_COVERAGE`) inside it (`cloudStorm`), so the storm is seen from outside it and its clouds shade the ground under it.
- **Haze:** around a camera in it, the atmosphere pass covers the scene with a grey (the sun's illuminance times 0.05, by the sun's height) with an extinction of `STORM_HAZE` (1/1,500 per metre) times how much of the storm is at the camera, out to 50 km.

The walker (`update_storms`):
- **Planning** (`events.schedule_storm`): a gap of 40 minutes on average (10 at least) after the last ends; it lasts 40–80 minutes, has a radius of 8–15 km and starts 20–35 km upwind of the camera, give or take 30°, moving straight towards where the camera was at 6–14 m/s.
- **Outposts:** each on the planet is announced ("Storm approaching Base: landing pads closed") 5 minutes before the storm covers it. While covered it is closed: it buys, sells, fuels (or sends fuel) and takes contracts no more, and nothing on its pad sells or is delivered. Once the storm has passed it or ended, another toast says its pads are open.
- **Deadlines:** while the camera is in the storm, each active contract's deadline is held back (`Contract.paused`, the seconds it has been held).
- **Rain:** up to 160 streaks (as many and as bright as how much of the storm is at the camera) fall at 9 m/s with the wind through a 16 m box around the camera (`viewmodel.Glows`).
- **The car:** with the wind at it over 20 m/s it is pushed sideways by 1.5 m/s² per m/s beyond, times a noise of the clock, by its share of the way from its cushion to 20 m up (`buffet_car`).
- **Props:** an asleep prop is dragged only when the drag beats 0.7 of its weight (`props.STICK`), so a storm scatters light loose props and leaves heavy ones asleep.
- **Compass:** the storm's centre from 5 minutes before its start to its end.
- **Save:** `events.storm` is the storm, under way or to come (`events.storm_record`); without it a new one is planned.

## 13. The moon expedition

### 13.1 Why go

- **Ilmenite** (30 cr/kg) and **ice** are found only on the moon. Meteor showers there are more frequent: with no atmosphere, small meteors don't burn up.
- **Trade:** the moon's outposts (Lowrim, Selene Deep) already pay `OTHER_BODY` (1.8×) for planet ore. With demand (§3.1) they pay more for planet goods (amber, quartz).
- **Refinery:** one of them offers the `"refinery"` service, turning ice into fuel at 3 kg of ice per kg of fuel. That undercuts its fuel price of 3.0 cr/kg (`FUEL_PRICE["moon"]`).

### 13.2 What it takes

**The walker's space car.** The walker has no ship; its hover car flies to the moon in space mode (G at least 250 m above the ground): a `flight.Ship` with `hovercar.space_params`, so it has the ship's gravity of both bodies, the moon's sphere of influence, translation assist (T) and ground contacts. Its space drive burns the car's tank at `CarParams.space_isp` (3,000 s) for every thruster:

| | Stock | `tank_1` |
|---|---|---|
| dry mass | 1,800 kg | 1,800 kg |
| tank | 100 kg | 150 kg |
| Δv, empty hold (`hovercar.delta_v`) | ≈ 1,590 m/s | ≈ 2,355 m/s |

That covers one way: to a low orbit, the transfer and landing on the moon. Space mode ends by G within 300 m of the ground, or on touching down slower than 2 m/s, over the body under the car. From 5 km up the time warp goes to 4, 16 or 64 times (`,` `.`). The HUD shows the body the orbit is reckoned around, the assist, the throttle, the warp, the periapsis and apoapsis, and the Δv left.


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
- `--crater-check` (`planet.py`): a crater 70 m across and 6 m deep under the camera at preset 1, among the trees, dug with the camera 500 m back.
  - before it, some of the trees and rocks listed around a player put down there lie within its radius
  - within 300 frames, no chunk is queued or waiting for its heights
  - the probe at its centre comes down by its depth
  - every vertex of a chunk drawn before and after within half its radius lies at least half its depth lower, and every vertex beyond two radii is where it was
  - no gap between chunks of different levels is wider than its skirt (`CrackCheck`'s measurement)
  - after it, a player put down in it 20 m from its centre lists trees and rocks around, none within its radius
- `--day-check` (new): run one day at 16× warp.
  - chunk rebuilds caused by the sun number a few per chunk per day
  - the frame time stays within budget
- `--wind-check` (`planet.py`), with `wind_speed` set so the steady wind is 15 m/s where it is measured:
  - the ship hovering 100 m above the site with assist and no controls stays within 2 m of where it was for 20 s, with the wind there averaging at least 12 m/s and the drag at least 100 N
  - crates of 5 kg and 500 kg dropped from 20 m, 15 m upwind of the site, come down (their descent stopping after a fall of 10 m); the light one at least 3 m downwind of where it was dropped, the heavy one within 1.5 m

## 15. Milestones

1. **Resources.** Resource id in `resources.glsl`, a probe column, scatter rocks by resource with tints and specks, the value and density by resource. `--resource-check`.
2. **Prices by resource.** `trade.py` supply by resource and demand, outposts' demand from their surroundings, the trade card per resource. `--trade-check`.
3. **Hold limits.** `cargo_capacity` / `hold_m3`, the cargo net refusing what doesn't fit, running dry (the fuel call). `--cargo-check`, `--fuel-check`.
4. **Upgrades.** Table, effective parameters, buying at outposts. `--upgrade-check`.
5. **Save version 2.** New fields, the migration from version 1, autosave on landing. `--save-check`.
6. **Day and night.** Moving sun, azimuth-only horizon rebuild, exposure by the sun's elevation, stars. `--day-check`.
   6b. **Lights.** Headlights, a suit lamp and outpost lights, with a forward light loop in `terrain.frag`, `vegetation.frag` and `scene.frag`. Capture shots `lights_*`.
7. **Suit and temperature** (`walker.py`). Local temperature, reserve, shelter, effects, recovery at an outpost. Milestones 1–6b's features came to `walker.py` first: day and night and the lights, the car's fuel and fuel call, its hold and cargo net with an Outpost tab, the upgrades, and save version 2.
8. **Points of interest** (`walker.py`). Hash placement, kinds, discovery, salvage parts, survey data; parts in the upgrades' prices and the suit's upgrades.
9. **Contracts** (`walker.py`). Four kinds on the planet (no moon), the Contracts tab, the pinned contract on the card, the compass and the map, fragile crates, reputation in rewards and prices.
10. **Meteor showers.**
   10a. **Craters.** `craters.glsl` beside the flat sites, `world.Crater` / `set_craters`, the chunks near a crater built again (`PlanetLod.requeue_near`), the scatter kept out of it. `--crater-check`.
   10b. **Showers** (`walker.py`). Scheduling (`events.py`), meteor visuals, impacts, meteorites of iridium (id 8) too hot to grab at first, the craters kept and saved. `--check`'s shower.
11. **Wind and storms.**
   11a. **Wind.** `wind.py` and `PlanetWorld.wind_at`, the clouds drifting with it, the ship's, the car's and the props' drag in it, the suit's chill. `--wind-check`.
   11b. **Storms** (`walker.py`). `wind.Storm` and `PlanetWorld.storm` (the wind, the clouds' cover and the haze in it), planning (`events.schedule_storm`), outposts closing, deadlines held, rain, the car buffeted, light props scattered, the save. `--check`'s storm.
12. **The moon expedition.**
   12a. **The space car** (`walker.py`). The hover car's space mode (G): a `flight.Ship` of `hovercar.space_params` with a space drive, the moon's pull, the time warp, the space readout, landing back into the hover car on the planet or the moon; the walker on the moon. `--check`'s space mode and moon landing.
   12b. **The moon economy.** Moon resources, the refinery, long-range upgrades, the transfer planner.

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
