"""
Buying and selling at the outposts: the car's fuel (filled on a pad,
called off the pads), selling the car's cargo and survey data, the
refinery, the upgrades and the parameters they give, and the items of the
slate's Vehicle, Market and Workshop apps.
"""

from shoonyakasha.planet.gravity_gun import GunParams
from shoonyakasha.planet.vec import length, normalize, sub
from shoonyakasha.planet import resources

import hovercar
import suit
import outposts
import trade
import upgrades
from trade import sale
import hud as hud_module
from play.core import GOOD_TOAST, bodies, foot, planet, state, world
from play import contracting, spaceflight, storms, vehicle

CALL_REACH = 50.0               # m the car may move from where fuel was called to and still get it
WALKER_UPGRADES = upgrades.for_groups(("car", "gun", "suit"))   # the upgrades the outposts here offer


def car_outpost():
    """The outpost whose pad the car is on, or None."""
    car = state["car"]
    if car is None:
        return None
    return next((o for o in state["outposts"] if o.at(vehicle.car_position())), None)


def fuel_offer():
    """(the nearest outpost on the body the car is over, metres along the ground to it)
    for a fuel call to the car, off every pad and on its cushion
    (hovercar.LEAVE_HEIGHT), or None."""
    car = state["car"]
    if car is None or car_outpost() is not None or car.height is None or car.height > hovercar.LEAVE_HEIGHT:
        return None
    here, body = normalize(car.position), spaceflight.car_body()
    found = [(o, outposts.ground_distance(body.radius, o.direction, here))
             for o in state["outposts"] if o.body is body]
    return min(found, key=lambda f: f[1]) if found else None


def car_note(text, colour=GOOD_TOAST):
    state["car_note"] = text
    state["hud"].toast(text[0].upper() + text[1:], colour, kind="money")


def fill_tank():
    """The car's tank filled at the outpost whose pad it is on, as far as the credits go; not while it is closed."""
    outpost, car = car_outpost(), state["car"]
    if outpost is None:
        car_note("the car is not on an outpost's pad", hud_module.WARNING)
        return
    if storms.closed(outpost):
        car_note(storms.closed_note(outpost), hud_module.WARNING)
        return
    kg, cost = state["market"].buy_fuel(car, outpost)
    if kg > 0.5:
        car_note("bought %.0f kg of fuel at %s for %s cr" % (kg, outpost.name, format(int(round(cost)), ",")))
    elif car.fuel >= car.params.fuel_capacity - 0.5:
        car_note("the tank is full")
    else:
        car_note("no credits for fuel", hud_module.WARNING)


def call_fuel():
    """Fuel called to the car from the nearest outpost (fuel_offer,
    trade.fuel_call), arriving after its delay and paid then; not while
    that outpost is closed."""
    car = state["car"]
    if state["fuel_call"] is not None:
        car_note("fuel is on its way")
        return
    offer = fuel_offer()
    if offer is None:
        car_note("fuel can be called to the car off the pads, on its cushion", hud_module.WARNING)
        return
    if car.fuel >= car.params.fuel_capacity - 0.5:
        car_note("the tank is full")
        return
    outpost, distance = offer
    if storms.closed(outpost):
        car_note(storms.closed_note(outpost), hud_module.WARNING)
        return
    price, fee, delay = trade.fuel_call(outpost, distance, False)
    if state["market"].credits <= fee:
        car_note("no credits for a fuel call", hud_module.WARNING)
        return
    state["fuel_call"] = {"outpost": outpost, "price": price, "fee": fee, "due": world.time + delay,
                          "spot": car.position}
    car_note("fuel called from %s, arriving in %.0f s" % (outpost.name, delay))


def update_fuel_call():
    """The called fuel put in the car once it is due, if the car is still
    within CALL_REACH of where it was called to; otherwise the call is off,
    unpaid. A toast when the car runs dry."""
    car = state["car"]
    if car is not None:
        if car.dry() and not state["was_dry"]:
            state["hud"].toast("The car is out of fuel: call fuel from the slate (Tab, Vehicle)", hud_module.WARNING)
        state["was_dry"] = car.dry()
    call = state["fuel_call"]
    if call is None or world.time < call["due"]:
        return
    state["fuel_call"] = None
    if car is None or length(sub(car.position, call["spot"])) > CALL_REACH:
        car_note("the fuel call missed the car", hud_module.WARNING)
        return
    kg, cost = state["market"].call_fuel(car, call["price"], call["fee"])
    if kg > 0.0:
        car_note("fuel delivered from %s: %.0f kg for %s cr" % (call["outpost"].name, kg,
                                                              format(int(round(cost)), ",")))
    else:
        car_note("no credits for the called fuel", hud_module.WARNING)


def car_items():
    """The Vehicle app's items: its fuel, where it is, filling the tank on a pad and calling fuel off them."""
    def fuel():
        car = state["car"]
        return "-" if car is None else "%.0f / %.0f kg" % (car.fuel, car.params.fuel_capacity)

    def where():
        outpost = car_outpost()
        if outpost is not None:
            return "on %s's pad" % outpost.name
        offer = fuel_offer()
        return "%.1f km from %s" % (offer[1] / 1000.0, offer[0].name) if offer is not None else "in the air"

    def quote():
        call = state["fuel_call"]
        if call is not None:
            return "Fuel from %s arrives in %.0f s." % (call["outpost"].name, max(0.0, call["due"] - world.time))
        offer = fuel_offer()
        if offer is None:
            return ""
        price, fee, delay = trade.fuel_call(offer[0], offer[1], False)
        return "A call costs %.2f cr/kg and a %s cr fee, and takes %.0f s." % (price, format(int(round(fee)), ","),
                                                                              delay)
    return [
        {"heading": "Car"},
        {"stat": "Fuel", "get": fuel},
        {"stat": "Cargo", "get": cargo_text},
        {"stat": "Where", "get": where},
        {"stat": "Credits", "get": lambda: "%s cr" % format(int(round(state["market"].credits)), ",")},
        {"button": "Fill the tank", "do": fill_tank,
         "hint": "With the car on an outpost's pad: its tank filled at the outpost's fuel price."},
        {"button": "Call fuel", "do": call_fuel,
         "hint": "With the car off the pads, on its cushion: fuel flown in from the nearest outpost, at %d times "
                 "its price and a fee by the distance, paid when it arrives." % round(trade.CALL_FACTOR)},
        {"note": quote},
        {"note": lambda: state["car_note"]},
    ]


def cargo_text():
    car = state["car"]
    if car is None or not car.cargo:
        return "empty"
    n = len(car.cargo)
    return "%.0f kg, %.2f / %.1f m3, %d piece%s" % (car.cargo_mass, car.cargo_volume, car.params.hold_m3, n,
                                                     "" if n == 1 else "s")


def hold_ore():
    """The ore in the car's hold: its items but the contracts' crates."""
    car = state["car"]
    return [] if car is None else [item for item in car.cargo if "contract" not in item]


def sell_car_cargo():
    """The ore in the car's hold at the outpost whose pad it is on: each
    item a delivery contract wants there delivered (`deliver`), the rest
    sold (trade.sale); the contracts' crates stay. Not while it is closed."""
    outpost, car = car_outpost(), state["car"]
    if outpost is None:
        state["trade_note"] = "the car is not on an outpost's pad"
        state["hud"].toast("The car is not on an outpost's pad", hud_module.WARNING, kind="money")
        return
    if storms.closed(outpost):
        state["trade_note"] = storms.closed_note(outpost)
        state["hud"].toast(state["trade_note"], hud_module.WARNING, kind="money")
        return
    ore = [item for item in hold_ore() if not contracting.deliver(outpost, item)]
    car.cargo = [item for item in car.cargo if "contract" in item]
    if not ore:
        state["trade_note"] = "no ore to sell"
        return
    credits, supplies = sale(outpost, ore, world.time)
    outpost.supply.update(supplies)
    state["market"].credits += credits
    kg = sum(item["mass"] for item in ore)
    state["sold_kg"] += kg
    state["earned"] += credits
    for item in ore:
        state["ledger"].sold(item["resource"], item["mass"])
    state["trade_note"] = "sold %.0f kg of ore at %s for %s cr" % (kg, outpost.name, format(int(round(credits)), ","))
    state["hud"].toast(state["trade_note"][0].upper() + state["trade_note"][1:], GOOD_TOAST, kind="money")


def outpost_items():
    """The Market app's items: the outpost whose pad the car is on, its price for
    each resource at grade 1 (iridium from the planet's meteorites), the
    car's cargo, what its ore would fetch, and selling it."""
    def here():
        outpost = car_outpost()
        return "%s, the %s" % (outpost.name, outpost.body.name) if outpost is not None else "the car is not on a pad"

    def price(body, resource):
        def text():
            outpost = car_outpost()
            if outpost is None:
                return "-"
            item = {"body": body.name, "direction": outpost.direction, "mass": 1.0, "grade": 1.0,
                    "resource": resource}
            return "%.2f cr/kg" % trade.price(outpost, item, world.time)
        return text

    def value():
        outpost, ore = car_outpost(), hold_ore()
        if outpost is None or not ore:
            return "-"
        return "%s cr" % format(int(round(state["market"].value(outpost, ore, world.time))), ",")

    planet_resources = range(resources.FERRITE, resources.EVAPORITE + 1)
    moon = next(b for b in bodies if b is not planet)
    prices = ([{"stat": resources.name(r) + ", grade 1", "get": price(planet, r)} for r in planet_resources]
              + [{"stat": resources.name(r) + " (moon), grade 1", "get": price(moon, r)}
                 for r in (resources.ICE, resources.ILMENITE)]
              + [{"stat": "Iridium (meteorites), grade 1", "get": price(planet, resources.IRIDIUM)}])
    return ("columns", [
        [{"heading": "Outpost"},
         {"stat": "Here", "get": here},
         {"stat": "Credits", "get": lambda: "%s cr" % format(int(round(state["market"].credits)), ",")},
         {"stat": "Car's cargo", "get": cargo_text},
         {"stat": "It would fetch", "get": value},
         {"button": "Refine the car's ice into fuel", "do": refine_ice,
          "hint": "At %s's refinery: the ice in the car's hold made fuel in its tank, %.0f kg of ice to a kg."
                  % (outposts.REFINERY, outposts.REFINE_ICE)},
         {"button": "Sell the car's cargo", "do": sell_car_cargo,
          "hint": "With the car on an outpost's pad: the ore in its hold sold there, or delivered to a "
                  "contract that wants it."},
         {"note": lambda: state["trade_note"]}],
        [{"heading": "Prices"}] + prices + [
         {"heading": "Survey data"},
         {"stat": "Held", "get": lambda: "worth %s cr" % format(int(round(state["survey_data"])), ",")},
         {"button": "Sell survey data", "do": sell_survey,
          "hint": "With the car or you on an outpost's pad: the survey data sold there, its price falling with "
                  "the data the outpost has bought."}],
    ])


def refine_ice():
    """At the refinery (outposts.REFINERY) whose pad the car is on, open:
    the ice in the car's hold (not a contract's) made fuel in its tank,
    outposts.REFINE_ICE kg of ice to a kg, as much as the tank takes."""
    outpost, car = car_outpost(), state["car"]
    note = None
    if outpost is None or not outpost.refinery:
        note = "the car is not on %s's pad, where the refinery is" % outposts.REFINERY
    elif storms.closed(outpost):
        note = storms.closed_note(outpost)
    ice = [i for i in car.cargo if i.get("resource") == resources.ICE and "contract" not in i]
    if note is None and not ice:
        note = "no ice in the car's hold"
    if note is None and car.fuel >= car.params.fuel_capacity - 0.05:
        note = "the tank is full"
    if note is not None:
        state["trade_note"] = note
        state["hud"].toast(note[0].upper() + note[1:], hud_module.WARNING, kind="money")
        return
    wanted = (car.params.fuel_capacity - car.fuel) * outposts.REFINE_ICE
    taken = 0.0
    for item in ice:
        take = min(item["mass"], wanted - taken)
        item["mass"] -= take
        taken += take
        if item["mass"] <= 1e-6:
            car.cargo.remove(item)
        if taken >= wanted:
            break
    car.fuel = min(car.params.fuel_capacity, car.fuel + taken / outposts.REFINE_ICE)
    state["trade_note"] = "refined %.0f kg of ice into %.1f kg of fuel" % (taken, taken / outposts.REFINE_ICE)
    state["hud"].toast(state["trade_note"][0].upper() + state["trade_note"][1:], GOOD_TOAST, kind="money")


def sell_survey():
    """The survey data held sold at the outpost whose pad the car or the player is on
    (trade.Market.sell_survey), while it is open."""
    outpost = at_outpost()
    if outpost is None:
        state["trade_note"] = "not on an outpost's pad"
        return
    if storms.closed(outpost):
        state["trade_note"] = storms.closed_note(outpost)
        return
    value = state["survey_data"]
    credits = state["market"].sell_survey(outpost, value, world.time)
    if value <= 0.0:
        state["trade_note"] = "no survey data to sell"
        return
    state["survey_data"] = 0.0
    state["earned"] += credits
    state["trade_note"] = "sold survey data at %s for %s cr" % (outpost.name, format(int(round(credits)), ","))
    state["hud"].toast(state["trade_note"][0].upper() + state["trade_note"][1:], GOOD_TOAST, kind="money")


def base_params():
    return {"car": hovercar.CarParams(), "gun": GunParams(), "suit": suit.SuitParams()}


def apply_upgrades(owned):
    """The upgrades `owned` (keys) and their effective parameters
    (upgrades.effective, the car's, the gun's and the suit's): the car's,
    the gun's and the suit's now, and those of every car made and every
    gun spawned after."""
    state["owned"] = set(owned)
    state["params"] = upgrades.effective(base_params(), state["owned"])
    state["suit_params"] = state["params"]["suit"]
    foot.gun_params = state["params"]["gun"]
    if foot.gun is not None:
        foot.gun.p = state["params"]["gun"]
    if state["car"] is not None:
        state["car"].params = state["params"]["car"]


def at_outpost():
    """The outpost whose pad the car or the player's feet are on, or None."""
    outpost = car_outpost()
    if outpost is None and foot.player is not None:
        feet = foot.bubble.to_world(foot.player.feet())
        outpost = next((o for o in state["outposts"] if o.at(feet)), None)
    return outpost


def buy_upgrade(upgrade):
    """`upgrade` bought at an outpost (at_outpost), open, with the credits, when upgrades.refusal allows it."""
    market = state["market"]
    outpost = at_outpost()
    if outpost is None:
        reason = "not at an outpost"
    elif storms.closed(outpost):
        reason = storms.closed_note(outpost)
    else:
        reason = upgrades.refusal(upgrade, state["owned"], market.credits, state["parts"])
    if reason is not None:
        state["upgrade_note"] = "%s: %s" % (upgrade.name, reason)
        state["hud"].toast(state["upgrade_note"], hud_module.WARNING, kind="money")
        return
    market.credits -= upgrade.cost
    for part, n in upgrade.parts.items():
        state["parts"][part] -= n
    apply_upgrades(state["owned"] | {upgrade.key})
    state["upgrade_note"] = "bought %s for %s cr" % (upgrade.name, format(int(round(upgrade.cost)), ","))
    state["hud"].toast(state["upgrade_note"][0].upper() + state["upgrade_note"][1:], GOOD_TOAST, kind="money")


def upgrade_text(upgrade):
    """A line on `upgrade`: its cost or state, and each effect from now to after."""
    now = state["params"]
    owned = upgrade.key in state["owned"]
    after = now if owned else upgrades.effective(base_params(), state["owned"] | {upgrade.key})
    changes = []
    for path in upgrades.effects_for(upgrade, now):
        group, attribute = path.split(".")
        name, shown = upgrades.EFFECTS[path]
        value = shown(getattr(now[group], attribute))
        changes.append("%s %s" % (name.lower(), value if owned else "%s to %s" % (value,
                                                                                shown(getattr(after[group],
                                                                                              attribute)))))
    reason = upgrades.refusal(upgrade, state["owned"], state["market"].credits, state["parts"])
    cost = format(int(round(upgrade.cost)), ",") + " cr" + (", " + upgrades.parts_text(upgrade.parts)
                                                            if upgrade.parts else "")
    head = "owned" if owned else cost + ("" if reason is None else " (" + reason + ")")
    return "%s; %s" % (head, "; ".join(changes))


def parts_held():
    held = {p: n for p, n in state["parts"].items() if n > 0}
    return upgrades.parts_text(held) if held else "none"


def upgrade_items():
    """The Workshop app's items: where, the credits and the parts held, and for
    each upgrade the walker offers (WALKER_UPGRADES) a button to buy it and
    a line of what it costs and changes, the gun's and the suit's in one
    column and the car's in another."""
    left = [{"heading": "At an outpost"},
            {"stat": "Where", "get": lambda: at_outpost().name if at_outpost() is not None else "not on a pad"},
            {"stat": "Credits", "get": lambda: "%s cr" % format(int(round(state["market"].credits)), ",")},
            {"stat": "Parts", "get": parts_held},
            {"note": lambda: "" if at_outpost() is not None else "Bring the car or yourself onto a pad."},
            {"note": lambda: state["upgrade_note"]}]

    def column(heading, groups):
        items = [{"heading": heading}]
        for upgrade in WALKER_UPGRADES:
            if not upgrades.effects_for(upgrade, groups):
                continue
            items += [{"button": upgrade.name, "do": lambda u=upgrade: buy_upgrade(u),
                       "hint": "Buy %s at the outpost whose pad the car or you are on%s." % (
                           upgrade.name, "" if not upgrade.requires else
                           "; it needs " + ", ".join(upgrades.BY_KEY[k].name for k in upgrade.requires))},
                      {"note": lambda u=upgrade: upgrade_text(u)}]
        return items
    return ("columns", [left, column("Gun and suit", ("gun", "suit")), column("Car", ("car",))])
