"""
The contracts in play (contracts.py): the outposts' offers for the day,
accepting, pinning and abandoning them, delivering ore, couriers' crates
and pods, the couriers' crates struck and broken, the survey contracts'
shares measured, deadlines, rewards and reputations, and the Contracts app's items.
"""

import math
import zlib

from shoonyakasha.planet.vec import add, cross, dot, length, normalize, quat_from_basis, scale, sub
from shoonyakasha.planet import resources

import clock
import contracts
import pois
import hud as hud_module
from play.core import GOOD_TOAST, foot, planet, state
from play import economy, exploring, storms, survival

SURVEY_EVERY = 1.0              # s between measurements of a survey contract's share explored
PICKED_UP = 20.0                # m a recovery's pod has moved from its point once picked up
MIN_REPUTATION = -5.0           # an outpost's reputation goes no lower


def contract_day():
    """The contracts' day: clock.day of the day's time."""
    return clock.day(survival.day_time())


def contract_offers(outpost):
    """The offers of `outpost` on the planet today (contracts.offers, with
    its reputation), less those accepted; none elsewhere."""
    if outpost is None or outpost.body is not planet:
        return []
    found = contracts.offers((outpost.name, outpost.direction), contract_day(), exploring.poi_seed(),
                             [(o.name, o.direction) for o in state.site.outposts if o.body is planet], planet.radius,
                             state.contracts.reputation.get(outpost.name, 0.0))
    return [c for c in found if c.id not in state.contracts.offers_taken]


def outpost_named(name):
    return next((o for o in state.site.outposts if o.name == name), None)


def active_contract(contract_id):
    return next((c for c in state.contracts.active if c.id == contract_id), None)


def pinned_contract():
    return active_contract(state.contracts.pinned)


def pod_key(c):
    """The key of a recovery's pod's point of interest (pois.Pois.place), off the cube's faces."""
    return (planet.index, -2, zlib.crc32(c.id.encode()), 0)


def place_pod(c, done=False):
    """A recovery's pod as a point of interest at its target, of the pod's kind wherever it is."""
    poi = pois.Poi(pod_key(c), tuple(c.target), 0.0, fixed_kind="pod", done=[0] if done else [])
    state.exploring.pois.place(poi)


def crate_contract(crate):
    """The active contract a crate prop belongs to (a courier's crate or a recovery's pod), or None."""
    contract_id = state.contracts.couriers.get(id(crate))
    if contract_id is not None:
        return active_contract(contract_id)
    key = state.exploring.pods.get(id(crate))
    if key is None:
        return None
    return next((c for c in state.contracts.active if c.kind == "recovery" and pod_key(c) == key), None)


def forget_crate(crate):
    state.contracts.couriers.pop(id(crate), None)
    state.contracts.crate_velocity.pop(id(crate), None)
    state.exploring.pods.pop(id(crate), None)


def crate_item(c, mass, volume):
    """A contract's crate as an item of the car's hold."""
    origin = outpost_named(c.origin)
    return {"body": planet.name, "direction": tuple(origin.direction) if origin is not None else (0.0, 1.0, 0.0),
            "mass": mass, "grade": 0.0, "resource": resources.NONE, "volume": volume, "contract": c.id}


def contract_title(c):
    return "%s for %s" % (c.kind.capitalize(), c.origin)


def hours_left(c):
    """Game hours (clock.py) until the contract's deadline: the dawn after its last day, held back by its pause."""
    end = (c.deadline + 1) * clock.DAY_LENGTH - 0.25 * clock.DAY_LENGTH
    return max(0.0, (end - (survival.day_time() - c.paused)) * 24.0 / clock.DAY_LENGTH)


def contract_done_text(c):
    """How far the contract has come: the kg delivered, the share surveyed, the crate's damage, or where to."""
    if c.kind == "delivery":
        return "%.0f / %.0f kg" % (min(c.delivered, c.quantity), c.quantity)
    if c.kind == "survey":
        return "%d / %d %%" % (round(100.0 * c.explored), round(100.0 * contracts.SURVEY_SHARE))
    if c.kind == "courier":
        return "damage %d %%" % round(100.0 * c.damage / contracts.BREAK_DAMAGE)
    return "to %s" % c.destination


def contract_progress_text(c):
    return "%s, %.0f h left" % (contract_done_text(c), hours_left(c))


def contract_target(c):
    """Where the contract's marker stands (planet space): a delivery's and
    a courier's destination; a recovery's pod's point until the pod is in
    the hold or PICKED_UP from it, then its destination; a survey's
    target. None without a contract."""
    if c is None:
        return None
    if c.kind == "recovery":
        crate = next((p for p in foot.props.crates if crate_contract(p) is c), None)
        in_hold = state.vehicle.car is not None and any(i.get("contract") == c.id for i in state.vehicle.car.cargo)
        moved = False
        if crate is not None:
            body, position, _ = foot.prop_state(crate)
            moved = body is planet and poi_distance_to(c.target, add(body.centre(), position)) > PICKED_UP
        if not in_hold and not moved:
            return add(planet.centre(), scale(c.target, planet.radius))
    if c.kind == "survey":
        return add(planet.centre(), scale(c.target, planet.radius))
    outpost = outpost_named(c.destination)
    return outpost.point() if outpost is not None else None


def poi_distance_to(direction, point):
    """Metres along the ground from `point` (planet space) to the unit `direction`."""
    return planet.radius * math.acos(max(-1.0, min(1.0, dot(direction, normalize(planet.relative(point))))))


def contract_note(text, colour=GOOD_TOAST):
    state.contracts.note = text
    state.ui.hud.toast(text[0].upper() + text[1:], colour, kind="contracts")


def accept_contract(c):
    """An offer accepted at its outpost (at_outpost), open, while fewer than
    contracts.MAX_ACTIVE are active, and pinned when none is: a courier's
    crate put in the car's hold when the car is on the pad and it fits,
    else beside the player on foot (refused otherwise); a recovery's pod
    placed at its target."""
    outpost = economy.at_outpost()
    if outpost is None or outpost.name != c.origin:
        contract_note("accept a contract on its outpost's pad", hud_module.WARNING)
        return False
    if storms.closed(outpost):
        contract_note(storms.closed_note(outpost), hud_module.WARNING)
        return False
    if len(state.contracts.active) >= contracts.MAX_ACTIVE:
        contract_note("at most %d contracts at once" % contracts.MAX_ACTIVE, hud_module.WARNING)
        return False
    if c.kind == "courier" and not give_courier_crate(c, outpost):
        contract_note("no room for the crate: bring the car onto the pad, or yourself", hud_module.WARNING)
        return False
    c.state = "active"
    state.contracts.active.append(c)
    state.contracts.offers_taken.add(c.id)
    if state.contracts.pinned is None:
        state.contracts.pinned = c.id
    if c.kind == "recovery":
        place_pod(c)
    contract_note("accepted: %s" % contracts.task(c))
    return True


def give_courier_crate(c, outpost):
    """A courier's crate into the car's hold when the car is on `outpost`'s
    pad and it fits, else 2 m ahead of the player on foot. Whether it was."""
    car = state.vehicle.car
    volume = contracts.COURIER_SIZE ** 3
    if economy.car_outpost() is outpost and car.can_take(contracts.COURIER_MASS, None, volume):
        car.cargo.append(crate_item(c, contracts.COURIER_MASS, volume))
        return True
    player, bubble = foot.player, foot.bubble
    if player is None or state.vehicle.driving or bubble.body is not planet:
        return False
    up = player.up()
    forward = player.level(player.heading, up)
    right = normalize(cross(forward, up))
    spot = add(player.feet(), add(scale(forward, 2.0), scale(up, 0.5 * contracts.COURIER_SIZE + 0.3)))
    crate = foot.props.spawn_crate(bubble, spot, quat_from_basis(right, up, scale(forward, -1.0)),
                                   contracts.COURIER_SIZE, colour=contracts.COURIER_COLOUR,
                                   mass=contracts.COURIER_MASS)
    state.contracts.couriers[id(crate)] = c.id
    return True


def end_contract(c, how):
    """`c` ended "completed", "failed", "expired" or "abandoned": its
    outpost's reputation up one when completed, else down one (no lower
    than MIN_REPUTATION); its crate, pod and point of interest gone; the
    pin moved to the next active contract."""
    crates = [p for p in foot.props.crates if crate_contract(p) is c]
    c.state = how
    state.contracts.active = [a for a in state.contracts.active if a is not c]
    rep = state.contracts.reputation.get(c.origin, 0.0) + (1.0 if how == "completed" else -1.0)
    state.contracts.reputation[c.origin] = max(MIN_REPUTATION, rep)
    if how == "completed":
        state.contracts.done += 1
    else:
        state.contracts.failed += 1
    for crate in crates:
        forget_crate(crate)
        if foot.gun is not None and crate.body is not None and foot.gun.held == crate.body:
            foot.gun.drop()
        foot.props.remove(foot.bubble, crate, release=False)
    if state.vehicle.car is not None:
        state.vehicle.car.cargo = [i for i in state.vehicle.car.cargo if i.get("contract") != c.id]
    if c.kind == "recovery":
        state.exploring.pois.pois.pop(pod_key(c), None)
    if state.contracts.pinned == c.id:
        state.contracts.pinned = state.contracts.active[0].id if state.contracts.active else None


def complete_contract(c):
    """`c` done: its reward paid, then ended (end_contract)."""
    state.economy.market.credits += c.reward
    state.economy.earned += c.reward
    end_contract(c, "completed")
    contract_note("contract complete: %s, +%s cr" % (contract_title(c), format(int(round(c.reward)), ",")))


def fail_contract(c, how, why):
    end_contract(c, how)
    contract_note("contract %s: %s (%s)" % (how, contract_title(c), why), hud_module.WARNING)


def pin_contract(c):
    state.contracts.pinned = c.id


def abandon_pinned():
    c = pinned_contract()
    if c is None:
        contract_note("no contract pinned", hud_module.WARNING)
        return
    fail_contract(c, "abandoned", "%s's reputation -1" % c.origin)


def deliver(outpost, item):
    """An ore item at `outpost` taken by the first active delivery that
    wants it there (contracts.wants), completing it once it has its
    quantity. Whether one took it."""
    c = next((a for a in state.contracts.active if contracts.wants(a, item, outpost.name)), None)
    if c is None:
        return False
    c.delivered += item["mass"]
    if c.delivered >= c.quantity:
        complete_contract(c)
    else:
        state.ui.hud.toast("Delivered %.0f kg of %s: %.0f / %.0f kg"
                           % (item["mass"], resources.name(c.resource).lower(), c.delivered, c.quantity), GOOD_TOAST,
                           kind="contracts")
    return True


def deliver_couriers(held):
    """Each courier's crate settled on its destination's pad (pad_under) delivered."""
    for crate in list(foot.props.crates):
        if id(crate) not in state.contracts.couriers:
            continue
        c = crate_contract(crate)
        outpost = exploring.pad_under(crate, held)
        if c is not None and outpost is not None and outpost.name == c.destination:
            forget_crate(crate)
            foot.props.remove(foot.bubble, crate, release=False)
            complete_contract(c)


def strike_crate(c, speed):
    """A courier's crate struck at `speed` (contracts.strike); broken, its contract fails. Whether it broke."""
    if not contracts.strike(c, speed):
        return False
    fail_contract(c, "failed", "the crate broke")
    return True


def strike_hold(speed):
    """The car's bump at `speed` striking each courier's crate in its hold."""
    car = state.vehicle.car
    for contract_id in [i["contract"] for i in car.cargo if "contract" in i]:
        c = active_contract(contract_id)
        if c is not None and c.kind == "courier":
            strike_crate(c, speed)


def watch_couriers():
    """Each courier's crate struck by how much its velocity changed from
    last frame while its speed fell (a hit, not a throw)."""
    seen = state.contracts.crate_velocity
    for crate in list(foot.props.crates):
        contract_id = state.contracts.couriers.get(id(crate))
        if contract_id is None:
            continue
        _, _, velocity = foot.prop_state(crate)
        before = seen.get(id(crate))
        seen[id(crate)] = velocity
        c = active_contract(contract_id)
        if c is None or before is None or length(velocity) >= length(before):
            continue
        strike_crate(c, length(sub(before, velocity)))


def update_contracts(dt):
    """The outposts' reputations put on them (trade's prices); each active
    contract past its deadline (its day reckoned less the time it was
    held back, contracts.Contract.paused) expired; the survey contracts'
    shares measured every SURVEY_EVERY (ExploredMap.share), completing at
    contracts.SURVEY_SHARE; the couriers' crates watched; with the car on
    an open outpost's pad, the crates in its hold for there delivered."""
    for o in state.site.outposts:
        o.reputation = state.contracts.reputation.get(o.name, 0.0)
    if foot.player is None and not state.vehicle.driving:
        return
    for c in list(state.contracts.active):
        if clock.day(survival.day_time() - c.paused) > c.deadline:
            fail_contract(c, "expired", "past its deadline")
    state.contracts.survey_in -= dt
    if state.contracts.survey_in <= 0.0:
        state.contracts.survey_in = SURVEY_EVERY
        for c in [a for a in state.contracts.active if a.kind == "survey"]:
            c.explored = state.ui.map.share(planet, c.target, contracts.SURVEY_RADIUS)
            if c.explored >= contracts.SURVEY_SHARE:
                complete_contract(c)
    if foot.bubble is not None and foot.bubble.body is planet:
        watch_couriers()
    car, outpost = state.vehicle.car, economy.car_outpost()
    if car is not None and outpost is not None and not storms.closed(outpost) and car.speed() < 1.0:
        for item in [i for i in car.cargo if "contract" in i]:
            c = active_contract(item["contract"])
            if c is not None and c.kind in ("courier", "recovery") and c.destination == outpost.name:
                car.cargo.remove(item)
                complete_contract(c)


# ── The save ───────────────────────────────────────────────────────────────

def record():
    """The save's section: the active contracts, today's offers accepted,
    the pin, the reputations and the counts done and failed."""
    c = state.contracts
    return {"active": [contracts.record(a) for a in c.active],
            "taken": sorted(i for i in c.offers_taken if i.rsplit("/", 2)[1] == str(contract_day())),
            "pinned": c.pinned, "reputation": dict(c.reputation), "done": c.done, "failed": c.failed}


def restore(section):
    """The contracts as a save's section (`record`) has them, each
    recovery's pod placed (done when its crate is a prop or in the car's
    hold); the props and the car restored first."""
    c = state.contracts
    c.active = [contracts.restore(r) for r in section["active"]]
    c.offers_taken, c.pinned = set(section["taken"]), section["pinned"]
    c.reputation, c.done, c.failed = dict(section["reputation"]), section["done"], section["failed"]
    held = {i.get("contract") for i in state.vehicle.car.cargo}
    for a in c.active:
        if a.kind == "recovery":
            place_pod(a, done=pod_key(a) in state.exploring.pods.values() or a.id in held)
