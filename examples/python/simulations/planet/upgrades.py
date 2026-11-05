"""
Upgrades bought at the outposts (planet.py, walker.py): UPGRADES, each
with a cost in credits, the parts it costs too (pois.PARTS, from wrecks
and rigs), the upgrades it needs first and its effects, each a parameter
(a group, "ship" for flight.ShipParams, "car" for hovercar.CarParams,
"gun" for shoonyakasha.planet.gravity_gun.GunParams or "suit" for
suit.SuitParams, and its attribute, as "group.attribute") and the factor
it is multiplied by, or ("add", amount) added. An upgrade has effects for
each game's vehicle; a game applies those of the groups it has, and
offers those with any (`for_groups`).

`effective(base, owned)` gives copies of the base parameters (group:
parameters) with the effects of every owned upgrade on those groups
applied, the base left as it was. `effects_for(upgrade, groups)` are the
upgrade's effects on `groups`. `refusal(upgrade, owned, credits, parts)`
says why an upgrade cannot be bought, or None when it can; a game
without parts (planet.py) passes none and its upgrades need none.
`EFFECTS` names each parameter an effect changes and how its value is
shown.
"""

import copy
from dataclasses import dataclass, field


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
BY_KEY = {u.key: u for u in UPGRADES}

# Each effect's parameter: its name on the cards, and its value's format.
EFFECTS = {
    "gun.hold_force": ("GUN FORCE", lambda v: "%.1f kN" % (v / 1_000.0)),
    "gun.pull_range": ("GUN PULL RANGE", lambda v: "%.0f m" % v),
    "gun.charged_speed": ("CHARGED THROW", lambda v: "%.0f m/s" % v),
    "ship.cargo_capacity": ("HOLD MASS", lambda v: "%.1f t" % (v / 1_000.0)),
    "ship.hold_m3": ("HOLD VOLUME", lambda v: "%.1f m3" % v),
    "ship.fuel_capacity": ("FUEL TANKS", lambda v: "%.0f kg" % v),
    "ship.dry_mass": ("DRY MASS", lambda v: "%.1f t" % (v / 1_000.0)),
    "ship.isp_main": ("MAIN ENGINE ISP", lambda v: "%.0f s" % v),
    "car.cargo_capacity": ("HOLD MASS", lambda v: "%.2f t" % (v / 1_000.0)),
    "car.hold_m3": ("HOLD VOLUME", lambda v: "%.1f m3" % v),
    "car.fuel_capacity": ("FUEL TANK", lambda v: "%.0f kg" % v),
    "car.drive_burn": ("FUEL AT FULL THROTTLE", lambda v: "%.3f kg/s" % v),
    "suit.cold_limit": ("SUIT COLD LIMIT", lambda v: "%.0f C" % v),
    "suit.hot_limit": ("SUIT HOT LIMIT", lambda v: "%.0f C" % v),
    "suit.reserve": ("SUIT RESERVE", lambda v: "x%.2f" % v),
}


def effects_for(upgrade, groups):
    return {path: effect for path, effect in upgrade.effects.items() if path.split(".")[0] in groups}


def for_groups(groups):
    """The upgrades with an effect on any of `groups` (a game's)."""
    return [u for u in UPGRADES if effects_for(u, groups)]


def effective(base, owned):
    """Copies of `base` (group: parameters) with the effects of the owned
    upgrades (their keys) on its groups applied, in UPGRADES' order: a
    factor multiplied in, ("add", amount) added."""
    out = {group: copy.deepcopy(params) for group, params in base.items()}
    for upgrade in UPGRADES:
        if upgrade.key not in owned:
            continue
        for path, effect in effects_for(upgrade, out).items():
            group, attribute = path.split(".")
            value = getattr(out[group], attribute)
            setattr(out[group], attribute, value + effect[1] if isinstance(effect, tuple) else value * effect)
    return out


def parts_text(parts):
    """"1 coil, 2 injectors"."""
    return ", ".join("%d %s%s" % (n, part.replace("_", " "), "" if n == 1 else "s") for part, n in parts.items())


def refusal(upgrade, owned, credits, parts=None):
    """Why `upgrade` cannot be bought with `owned` (keys), `credits` and
    `parts` (part: count; None in a game without parts, whose upgrades
    then need none): "owned", "needs <names>", "needs <parts>" or "not
    enough credits"; None when it can."""
    if upgrade.key in owned:
        return "owned"
    missing = [BY_KEY[k].name for k in upgrade.requires if k not in owned]
    if missing:
        return "needs " + ", ".join(missing)
    if parts is not None:
        short = {p: n for p, n in upgrade.parts.items() if parts.get(p, 0) < n}
        if short:
            return "needs " + parts_text(short)
    if credits < upgrade.cost:
        return "not enough credits"
    return None
