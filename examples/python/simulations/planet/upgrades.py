"""
Upgrades bought at the outposts (planet.py): UPGRADES, each with a cost
in credits, the upgrades it needs first and its effects, each a
parameter (a group, "ship" for flight.ShipParams or "gun" for
shoonyakasha.planet.gravity_gun.GunParams, and its attribute, as
"group.attribute") and the factor it is multiplied by.

`effective(base, owned)` gives copies of the base parameters (group:
parameters) with the effects of every owned upgrade applied, the base
left as it was. `refusal(upgrade, owned, credits)` says why an upgrade
cannot be bought, or None when it can. `EFFECTS` names each parameter an
effect changes and how its value is shown.
"""

import copy
from dataclasses import dataclass, field


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
}


def effective(base, owned):
    """Copies of `base` (group: parameters) with the effects of the owned
    upgrades (their keys) multiplied in, in UPGRADES' order."""
    out = {group: copy.deepcopy(params) for group, params in base.items()}
    for upgrade in UPGRADES:
        if upgrade.key not in owned:
            continue
        for path, factor in upgrade.effects.items():
            group, attribute = path.split(".")
            setattr(out[group], attribute, getattr(out[group], attribute) * factor)
    return out


def refusal(upgrade, owned, credits):
    """Why `upgrade` cannot be bought with `owned` (keys) and `credits`:
    "owned", "needs <names>" or "not enough credits"; None when it can."""
    if upgrade.key in owned:
        return "owned"
    missing = [BY_KEY[k].name for k in upgrade.requires if k not in owned]
    if missing:
        return "needs " + ", ".join(missing)
    if credits < upgrade.cost:
        return "not enough credits"
    return None
