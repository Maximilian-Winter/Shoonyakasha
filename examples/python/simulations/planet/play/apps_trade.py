"""
The slate's Market, Workshop and Vehicle apps (slate.py).

Market: the outpost whose pad the car or the player is on, whether its
pads are open and the reputation there; a table of what it pays for each
resource at grade 1 (trade.price, for ore found by the outpost; the
moon's ores and the meteorites' iridium too) with the demand, from how
much of each body's ore it has bought (trade.supply); the car's hold by
resource with what it would fetch and Sell the hold (Enter); the refinery
at outposts.REFINERY; the fuel's price, the tank, Fill and Sell survey
data.

Workshop: the upgrades the walker offers (WALKER_UPGRADES) in columns, the
gun's, the suit's and the car's, each owned, to be had now, or waiting
for the one it needs; the one chosen (a click) at the right with what it
changes from now to after, what it costs against what is held, what it
needs, and Buy (Enter); the parts held and the credits at the header's
right.

Vehicle: the car seen from above with its cargo net and what lies in
it, NET READY while something does; Waypoint to car and the fuel (a call
off the pads, filling on one); the fuel with the range at full speed and
the delta-v in space mode; the hold by resource and the room left; the
upgrades fitted to it.
"""

import math

from shoonyakasha.planet import resources
from shoonyakasha.planet.vec import dot, sub

import fieldslate as fs
import hovercar
import outposts
import pois
import slate
import trade
import upgrades
from fieldslate import AMBER, CORAL, HOLO, ICE, MINT, MIST
from shoonyakasha import keys
from play.core import bodies, planet, state, world
from play import apps, apps_map, economy, spaceflight, storms, vehicle
from play.economy import WALKER_UPGRADES
from play.vehicle import CAR_NET_HALF

ROW = 44.0                       # canvas units between the market's rows
GROUPS = (("Gravity gun", "gun"), ("Suit", "suit"), ("Hover car", "car"))
NODE = (300.0, 50.0, 18.0)       # an upgrade's node: width, height, gap below


DEMAND_COLOURS = {"High": AMBER, "Normal": HOLO, "Low": MIST}


# ── Market ─────────────────────────────────────────────────────────────────

class Market:
    name, icon = "Market", "market"

    def outpost(self):
        return economy.car_outpost() or economy.at_outpost()

    def label(self):
        if economy.car_outpost() is not None:
            return "Market · car on pad"
        return "Market · on foot" if economy.at_outpost() is not None else "Market · not on a pad"

    def title(self):
        outpost = self.outpost()
        return outpost.name if outpost is not None else "No outpost here"

    def corner(self, surface, right, top):
        slate.credits_corner(surface, right, top, state["market"].credits)

    def hints(self):
        return [(("Enter",), "Sell the hold")] if economy.car_outpost() is not None else []

    def note(self):
        return apps.sentence(state["trade_note"]) or "Ore settled on the pad sells by itself, at these prices."

    def key(self, key):
        if key in (keys.ENTER, keys.KP_ENTER) and economy.car_outpost() is not None:
            economy.sell_car_cargo()
            return True
        return False

    def draw(self, surface, left, top, width, height):
        layer = surface.layer
        outpost = self.outpost()
        if outpost is not None:
            x = left + fs.text_width(self.label().upper(), "label", "text_medium") + 12.0
            closed = storms.closed(outpost)
            x += fs.pill(layer, "Pads closed" if closed else "Pads open", x, top - 69.0, CORAL if closed else MINT)
            rep = round(state["reputation"].get(outpost.name, 0.0))
            fs.pill(layer, "Rep %+d" % rep, x + 8.0, top - 69.0, MINT if rep >= 0 else CORAL)
        main = 0.6 * width
        self.draw_prices(surface, outpost, left, top, main, height)
        side_left = left + main + apps.GAP
        side_w = left + width - side_left
        used = self.draw_hold(surface, outpost, side_left, top, side_w)
        self.draw_fuel(surface, outpost, side_left, top + used + apps.GAP, side_w)

    def draw_prices(self, surface, outpost, left, top, width, height):
        layer, mesh = surface.layer, surface.mesh
        apps.pane(surface, left, top, width, height)
        x, right = left + 26.0, left + width - 26.0
        if outpost is None:
            fs.write(layer, "Stand or park on an outpost's pad to see its prices.", (x, top + 34.0), "body", MIST)
            return
        price_x, demand_x = x + 0.55 * (right - x), x + 0.62 * (right - x)
        fs.label(layer, "Resource", (x, top + 30.0))
        fs.label(layer, "cr/kg", (price_x, top + 30.0), MIST, "right")
        fs.label(layer, "Demand", (demand_x, top + 30.0))
        moon = next(b for b in bodies if b is not planet)
        rows = ([(planet, r, "") for r in range(resources.FERRITE, resources.EVAPORITE + 1)]
                + [(moon, r, "moon") for r in (resources.ICE, resources.ILMENITE)]
                + [(planet, resources.IRIDIUM, "meteorite")])
        line = fs.tint(ICE, 0.08, fs.ON_SLATE)
        y = top + 66.0
        for body, r, tag in rows:
            item = {"body": body.name, "direction": outpost.direction, "mass": 1.0, "grade": 1.0, "resource": r}
            now = trade.price(outpost, item, world.time)
            text = trade.demand(outpost, item, world.time)
            colour = DEMAND_COLOURS[text]
            fs.dot(mesh, x + 5.0, y, 5.0, apps.resource_colour(r))
            w = fs.write(layer, resources.name(r), (x + 18.0, y), "body", ICE)
            if tag:
                fs.write(layer, tag, (x + 24.0 + w, y + 1.0), "small", MIST)
            fs.write(layer, "%.2f" % now, (price_x, y), "body", AMBER if text == "High" else ICE, "num", "right")
            fs.pill(layer, text, demand_x, y, colour)
            mesh.face([(x, y + 0.5 * ROW - 0.5), (right, y + 0.5 * ROW - 0.5), (right, y + 0.5 * ROW + 0.5),
                       (x, y + 0.5 * ROW + 0.5)], [line] * 4)
            y += ROW
        held = ["%s of the %s's ore" % (fs.mass(trade.supply(outpost, b.name, world.time)), b.name) for b in bodies]
        fs.write(layer, slate.fit_text("%s has bought %s." % (outpost.name, " and ".join(held)), right - x, "small"),
                 (x, y), "small", ICE)
        lines = slate.wrap_text("Prices at grade 1. They fall as %s stocks up and recover over about %d minutes. "
                                "Ore from far away or from the moon pays more."
                                % (outpost.name, round(trade.SUPPLY_RECOVERY / 60.0)), right - x, "small")
        for k, text in enumerate(lines):
            fs.write(layer, text, (x, y + 26.0 + 20.0 * k), "small", MIST)

    def draw_hold(self, surface, outpost, left, top, width):
        """The car's hold pane; returns its height."""
        layer, mesh = surface.layer, surface.mesh
        car = state["car"]
        rows = hovercar.ore_by_resource(car.cargo) if car is not None else []
        height = 150.0 + 34.0 * len(rows)
        apps.pane(surface, left, top, width, height)
        x, right = left + 24.0, left + width - 24.0
        fs.label(layer, "Car's hold", (x, top + 30.0), HOLO)
        if car is None:
            return height
        p = car.params
        fs.write(layer, "%s / %s · %.1f / %.1f m³" % (fs.grouped(car.cargo_mass), fs.mass(p.cargo_capacity),
                                                       car.cargo_volume, p.hold_m3), (right, top + 30.0), "small",
                 MIST, "num", "right")
        on_pad = economy.car_outpost()
        y = top + 64.0
        for r, kg, grade, items in rows:
            fs.dot(mesh, x + 5.0, y, 5.0, apps.resource_colour(r))
            w = fs.write(layer, resources.name(r), (x + 18.0, y), "body", ICE)
            fs.write(layer, "%s · %.2f" % (fs.mass(kg), grade), (x + 28.0 + w, y), "small", MIST, "num")
            if on_pad is not None:
                fs.write(layer, fs.credits(trade.sale(on_pad, items, world.time)[0]), (right, y), "body", AMBER, "num",
                         "right")
            y += 34.0
        if not rows:
            fs.write(layer, "Empty", (x, y), "body", MIST)
            y += 34.0
        ore = economy.hold_ore()
        value = trade.sale(on_pad, ore, world.time)[0] if on_pad is not None and ore else 0.0
        enabled = on_pad is not None and bool(ore) and not storms.closed(on_pad)
        by = top + height - fs.BUTTON_HEIGHT - 20.0
        if on_pad is not None and on_pad.name == outposts.REFINERY:
            half = 0.5 * (right - x - 10.0)
            surface.button(x, by, half, "Sell the hold", economy.sell_car_cargo, fs.credits(value), "primary", enabled)
            surface.button(x + half + 10.0, by, half, "Refine the ice", economy.refine_ice)
        else:
            surface.button(x, by, right - x, "Sell the hold", economy.sell_car_cargo,
                           fs.credits(value) if enabled else None, "primary", enabled)
        return height

    def draw_fuel(self, surface, outpost, left, top, width):
        layer, mesh = surface.layer, surface.mesh
        car = state["car"]
        height = 170.0
        apps.pane(surface, left, top, width, height)
        x, right = left + 24.0, left + width - 24.0
        fs.label(layer, "Fuel", (x, top + 30.0))
        if outpost is not None:
            fs.write(layer, "%.2f cr/kg here" % outpost.fuel_price, (right, top + 30.0), "small", MIST, "num", "right")
        if car is None:
            return
        p = car.params
        apps.row(surface, x, right, top + 62.0, "Tank", "%s / %s" % (fs.grouped(car.fuel), fs.mass(p.fuel_capacity)))
        fs.bar(mesh, x, top + 80.0, right - x, car.fuel / p.fuel_capacity, HOLO, 3.0)
        by = top + height - fs.BUTTON_HEIGHT - 20.0
        on_pad = economy.car_outpost()
        half = 0.42 * (right - x)
        need = (p.fuel_capacity - car.fuel) * on_pad.fuel_price if on_pad is not None else 0.0
        surface.button(x, by, half, "Fill", economy.fill_tank, fs.credits(need) if on_pad is not None else None,
                       enabled=on_pad is not None and need > 0.5)
        survey = trade.survey_sale(outpost, state["survey_data"], world.time)[0] if outpost is not None else 0.0
        surface.button(x + half + 10.0, by, right - x - half - 10.0, "Sell survey data", economy.sell_survey,
                       fs.credits(survey) if outpost is not None else None,
                       enabled=outpost is not None and state["survey_data"] > 0.5)


# ── Workshop ───────────────────────────────────────────────────────────────

class Workshop:
    name, icon = "Workshop", "workshop"

    def __init__(self):
        self.chosen = None            # the key of the upgrade shown at the right

    def label(self):
        outpost = economy.at_outpost()
        return "Workshop · on %s's pad" % outpost.name if outpost is not None else "Workshop · not on a pad"

    def title(self):
        return "Upgrades"

    def corner(self, surface, right, top):
        x = slate.credits_corner(surface, right, top, state["market"].credits) - 30.0
        for part in reversed(pois.PARTS):
            n = state["parts"].get(part, 0)
            x -= fs.pill(surface.layer, "%s %d" % (pois.PART_NAMES[part], n), x, top + 40.0, ICE if n else MIST,
                         "right") + 8.0

    def hints(self):
        return [(("Enter",), "Buy")]

    def note(self):
        return apps.sentence(state["upgrade_note"]) or "Parts come from salvaging wrecks and opening rigs' crates."

    def key(self, key):
        if key in (keys.ENTER, keys.KP_ENTER):
            economy.buy_upgrade(self.upgrade())
            return True
        return False

    def upgrade(self):
        if self.chosen is None:
            first = next((u for u in WALKER_UPGRADES if u.key not in state["owned"]), WALKER_UPGRADES[0])
            self.chosen = first.key
        return upgrades.BY_KEY[self.chosen]

    def draw(self, surface, left, top, width, height):
        side = 0.3 * width
        columns = width - side - apps.GAP
        col_w = (columns - 2.0 * apps.GAP) / 3.0
        for k, (name, group) in enumerate(GROUPS):
            self.draw_column(surface, name, group, left + k * (col_w + apps.GAP), top, col_w)
        self.draw_detail(surface, left + columns + apps.GAP, top, side, height)
        self.draw_legend(surface, left, top + height - 8.0)

    def draw_column(self, surface, name, group, left, top, width):
        layer, mesh = surface.layer, surface.mesh
        fs.label(layer, name, (left, top + 10.0))
        y = top + 32.0
        previous = None
        line = fs.tint(ICE, 0.2, fs.ON_SLATE)
        for u in WALKER_UPGRADES:
            if group not in {path.split(".")[0] for path in upgrades.effects_for(u, (group,))}:
                continue
            h = NODE[1]
            owned = u.key in state["owned"]
            needs = [k for k in u.requires if k not in state["owned"]]
            chosen = u.key == self.chosen
            if previous is not None and previous in u.requires:
                colour = MINT if previous in state["owned"] else line
                mesh.face([(left + 30.0, y - NODE[2]), (left + 32.0, y - NODE[2]), (left + 32.0, y), (left + 30.0, y)],
                          [colour] * 4)
            hot = surface.over(left, y, width, h)
            fs.pane(mesh, left, y, width, h, fs.RADIUS_SMALL,
                    fs.tint(HOLO if chosen else ICE, 0.1 if chosen else (0.06 if hot else 0.035), fs.ON_SLATE),
                    fs.tint(HOLO, 0.55, fs.ON_SLATE) if chosen else fs.PANE_EDGE)
            cx, cy = left + 31.0, y + 0.5 * h
            if owned:
                fs.dot(mesh, cx, cy, 11.0, MINT)
                mesh.segment((cx - 5.0, cy), (cx - 1.5, cy + 4.0), fs.INK, 2.0)
                mesh.segment((cx - 1.5, cy + 4.0), (cx + 5.5, cy - 4.0), fs.INK, 2.0)
            elif needs:
                dashed_circle(mesh, cx, cy, 11.0, MIST)
            else:
                fs.dot(mesh, cx, cy, 11.0, HOLO, hollow=True, width=1.6)
            fs.write(layer, u.name, (left + 54.0, cy), "lead" if not needs else "body", ICE if not needs else MIST,
                     "text_medium")
            surface.region(left, y, width, h, lambda key=u.key: setattr(self, "chosen", key))
            previous = u.key
            y += h + NODE[2]

    def draw_detail(self, surface, left, top, width, height):
        layer, mesh = surface.layer, surface.mesh
        u = self.upgrade()
        fs.pane(mesh, left, top, width, height, fs.RADIUS, fs.PANE, fs.tint(HOLO, 0.25, fs.ON_SLATE))
        x, right = left + 24.0, left + width - 24.0
        group = next((g for g in ("gun", "suit", "car") if upgrades.effects_for(u, (g,))), "car")
        group_name = dict((g, n) for n, g in GROUPS)[group]
        fs.label(layer, "%s · tier %d" % (group_name, upgrades.tier(u)), (x, top + 30.0), HOLO)
        fs.write(layer, slate.fit_text(u.name, right - x, 24, "display"), (x, top + 66.0), 24, ICE, "display")
        owned = u.key in state["owned"]
        now = state["params"]
        after = now if owned else upgrades.effective(economy.base_params(), state["owned"] | {u.key})
        y = top + 108.0
        for path in upgrades.effects_for(u, (group,)):
            g, attribute = path.split(".")
            name, shown = upgrades.EFFECTS[path]
            fs.write(layer, slate.fit_text(name.capitalize(), 0.5 * (right - x), "body"), (x, y), "body", ICE)
            before, then = shown(getattr(now[g], attribute)), shown(getattr(after[g], attribute))
            if owned or before == then:
                fs.write(layer, before, (right, y), "body", ICE, "num", "right")
            else:
                w = fs.write(layer, then, (right, y), "body", MINT, "num", "right")
                fs.write(layer, before + " →", (right - w - 8.0, y), "small", MIST, "num", "right")
            y += 30.0
        line = fs.tint(ICE, 0.1, fs.ON_SLATE)
        mesh.face([(x, y - 6.0), (right, y - 6.0), (right, y - 5.0), (x, y - 5.0)], [line] * 4)
        y += 16.0
        credits = state["market"].credits
        rows = [(fs.credits(u.cost), "you have %s" % fs.grouped(credits), credits >= u.cost)]
        for part, n in u.parts.items():
            held = state["parts"].get(part, 0)
            rows.append(("%d %s" % (n, pois.PART_NAMES[part].lower()), "you have %d" % held, held >= n))
        for k in u.requires:
            rows.append((upgrades.BY_KEY[k].name, "owned" if k in state["owned"] else "needed first",
                         k in state["owned"]))
        for name, have, ok in rows:
            fs.write(layer, name, (x, y), "body", ICE)
            fs.write(layer, have, (right, y), "body", MINT if ok else CORAL, "text", "right")
            y += 28.0
        by = top + height - fs.BUTTON_HEIGHT - 24.0
        reason = upgrades.refusal(u, state["owned"], credits, state["parts"])
        outpost = economy.at_outpost()
        if owned:
            surface.button(x, by, right - x, "Owned", None, enabled=False)
        elif reason is not None:
            surface.button(x, by, right - x, slate.fit_text(reason.capitalize(), right - x - 30.0), None, enabled=False)
        elif outpost is None:
            surface.button(x, by, right - x, "Buy on an outpost's pad", None, enabled=False)
        else:
            surface.button(x, by, right - x, "Buy", lambda: economy.buy_upgrade(u), fs.credits(u.cost), "primary")

    def draw_legend(self, surface, left, y):
        layer, mesh = surface.layer, surface.mesh
        x = left
        fs.dot(mesh, x + 5.0, y, 5.0, MINT)
        x += 14.0 + fs.write(layer, "Owned", (x + 14.0, y), "small", ICE) + 18.0
        fs.dot(mesh, x + 5.0, y, 5.0, HOLO, hollow=True, width=1.4)
        x += 14.0 + fs.write(layer, "Can get now", (x + 14.0, y), "small", ICE) + 18.0
        dashed_circle(mesh, x + 5.0, y, 5.0, MIST, 6)
        fs.write(layer, "Needs the one above", (x + 14.0, y), "small", MIST)


def dashed_circle(mesh, cx, cy, radius, colour, dashes=8):
    """A circle of `dashes` dashes and as many gaps."""
    for k in range(dashes):
        a0, a1 = 2.0 * math.pi * (k + 0.25) / dashes, 2.0 * math.pi * (k + 0.75) / dashes
        mesh.segment((cx + radius * math.cos(a0), cy + radius * math.sin(a0)),
                     (cx + radius * math.cos(a1), cy + radius * math.sin(a1)), colour, 1.2)


# ── Vehicle ────────────────────────────────────────────────────────────────

class Vehicle:
    name, icon = "Vehicle", "vehicle"

    def label(self):
        return "Vehicle · %s" % ("driving" if state["driving"] else "parked")

    def title(self):
        return "Hover car"

    def corner(self, surface, right, top):
        car = state["car"]
        if car is None:
            return
        x = right
        offer = economy.fuel_offer()
        if offer is not None:
            outpost, distance = offer
            price, fee, _ = trade.fuel_call(outpost, distance, False)
            value = "~" + fs.credits(fee + price * (car.params.fuel_capacity - car.fuel))
            w = fs.button_width("Call fuel", value)
            x -= surface.button(x - w, top + 12.0, w, "Call fuel", economy.call_fuel, value) + 10.0
        elif economy.car_outpost() is not None:
            w = fs.button_width("Fill the tank")
            x -= surface.button(x - w, top + 12.0, w, "Fill the tank", economy.fill_tank) + 10.0
        here = apps.where()
        if not state["driving"] and here is not None and here[0] is spaceflight.car_body():
            metres = apps.along(here, vehicle.car_position())[0]
            text, value = "Waypoint to car", fs.distance(metres)
            w = fs.button_width(text, value)
            surface.button(x - w, top + 12.0, w, text, lambda: apps.set_waypoint(vehicle.car_position()), value)

    def hints(self):
        return []

    def note(self):
        return apps.sentence(state["car_note"]) or "The hold sells from the Market app when the car is on a pad."

    def draw(self, surface, left, top, width, height):
        car = state["car"]
        if car is None:
            return
        net = vehicle.in_net() if not state["driving"] else []
        if net:
            x = left + fs.text_width(self.label().upper(), "label", "text_medium") + 12.0
            fs.pill(surface.layer, "Net ready", x, top - 69.0, MINT)
        main = 0.5 * width
        self.draw_schematic(surface, net, left, top, main, height)
        side_left = left + main + apps.GAP
        side_w = left + width - side_left
        y = top
        y += self.draw_fuel(surface, car, side_left, y, side_w) + apps.GAP
        y += self.draw_hold(surface, car, side_left, y, side_w) + apps.GAP
        self.draw_fitted(surface, side_left, y, side_w)

    def draw_schematic(self, surface, net, left, top, width, height):
        layer, mesh = surface.layer, surface.mesh
        apps.pane(surface, left, top, width, height)
        cx = left + 0.5 * width
        line = HOLO
        body_w, body_h = 120.0, 190.0
        bx, by = cx - 0.5 * body_w, top + 50.0
        fs.stroke(mesh, fs.rounded_rect(bx, by, body_w, body_h, 44.0), line, 2.0)
        fs.pane(mesh, cx - 36.0, by + 26.0, 72.0, 60.0, 12.0, fs.tint(HOLO, 0.1, fs.ON_SLATE),
                fs.tint(HOLO, 0.5, fs.ON_SLATE))
        for side in (-1.0, 1.0):
            px = cx + side * (0.5 * body_w + 8.0) - 9.0
            fs.stroke(mesh, fs.rounded_rect(px, by + 100.0, 18.0, 60.0, 8.0), line, 1.6)
        # The net, to scale behind the car: CAR_NET_HALF (right, back) metres either side.
        scale = 22.0                  # canvas units a metre
        nw, nh = 2.0 * CAR_NET_HALF[0] * scale, 2.0 * CAR_NET_HALF[1] * scale
        nx, ny = cx - 0.5 * nw, by + body_h + 46.0
        fs.label(layer, "Cargo net · %.0f × %.0f m" % (2.0 * CAR_NET_HALF[0], 2.0 * CAR_NET_HALF[1]),
                 (cx, ny - 16.0), AMBER, "center")
        fs.fill(mesh, fs.rounded_rect(nx, ny, nw, nh, 4.0), fs.tint(ICE, 0.05, fs.ON_SLATE))
        corners = [(nx, ny), (nx + nw, ny), (nx + nw, ny + nh), (nx, ny + nh)]
        for a, b in zip(corners, corners[1:] + corners[:1]):
            steps = int(max(abs(b[0] - a[0]), abs(b[1] - a[1])) / 8.0)
            for k in range(0, steps, 2):
                p0 = (a[0] + (b[0] - a[0]) * k / steps, a[1] + (b[1] - a[1]) * k / steps)
                p1 = (a[0] + (b[0] - a[0]) * (k + 1) / steps, a[1] + (b[1] - a[1]) * (k + 1) / steps)
                mesh.segment(p0, p1, AMBER, 1.4)
        for p in corners:
            fs.dot(mesh, p[0], p[1], 3.5, AMBER)
        if net:
            centre, right, _, back = vehicle.net_frame()
            for prop, point in net:
                d = sub(point, centre)
                px = cx + dot(d, right) * scale
                py = ny + 0.5 * nh + dot(d, back) * scale
                colour = apps.resource_colour(prop.resource) if prop.grade > 0.0 else AMBER
                size = max(4.0, min(12.0, prop.radius * scale))
                if prop.kind == "crate":
                    fs.fill(mesh, fs.rounded_rect(px - size, py - size, 2.0 * size, 2.0 * size, 2.0), colour)
                else:
                    hexagon = [(px + size * math.cos(math.pi * k / 3.0), py + size * math.sin(math.pi * k / 3.0))
                               for k in range(6)]
                    fs.fill(mesh, hexagon, colour)
        n = len(net)
        text = ("Nothing in the net." if n == 0 else
                "%d %s in the net load when you get in, each that fits the hold." % (n, "thing" if n == 1 else "things"))
        if state["driving"]:
            text = "The net loads when you get in by a parked car."
        fs.write(layer, slate.fit_text(text, width - 40.0), (cx, top + height - 40.0), "body", ICE, "text", "center")

    def draw_fuel(self, surface, car, left, top, width):
        layer, mesh = surface.layer, surface.mesh
        height = 150.0
        apps.pane(surface, left, top, width, height)
        x, right = left + 24.0, left + width - 24.0
        p = car.params
        fs.label(layer, "Fuel", (x, top + 30.0))
        fs.write(layer, "%s / %s" % (fs.grouped(car.fuel), fs.mass(p.fuel_capacity)), (right, top + 30.0), "body",
                 ICE, "num", "right")
        fs.bar(mesh, x, top + 50.0, right - x, car.fuel / p.fuel_capacity, HOLO, 4.0)
        burn = car.burn_rate(hovercar.CarControls(throttle=1.0))
        reach = car.fuel / burn * hovercar.MAX_SPEED if burn > 0.0 else 0.0
        apps.row(surface, x, right, top + 84.0, "Range at full speed", "~" + fs.distance(reach))
        apps.row(surface, x, right, top + 118.0, "Δv in space mode",
                 "%s m/s" % fs.grouped(hovercar.delta_v(p, car.fuel, car.cargo_mass)))
        return height

    def draw_hold(self, surface, car, left, top, width):
        layer, mesh = surface.layer, surface.mesh
        rows = hovercar.ore_by_resource(car.cargo)
        height = 130.0 + 34.0 * len(rows)
        apps.pane(surface, left, top, width, height)
        x, right = left + 24.0, left + width - 24.0
        p = car.params
        fs.label(layer, "Hold", (x, top + 30.0))
        fs.write(layer, "%s · %.2f m³" % (fs.mass(car.cargo_mass), car.cargo_volume), (right, top + 30.0), "body",
                 ICE, "num", "right")
        fs.bar(mesh, x, top + 50.0, right - x, 0.0, HOLO, 4.0)
        bx = x
        for r, kg, _, _ in rows:
            w = (right - x) * kg / p.cargo_capacity
            if w >= 1.0:
                fs.fill(mesh, fs.rounded_rect(bx, top + 50.0, w, 4.0, 2.0), apps.resource_colour(r))
            bx += w
        y = top + 84.0
        for r, kg, grade, _ in rows:
            fs.dot(mesh, x + 5.0, y, 5.0, apps.resource_colour(r))
            w = fs.write(layer, resources.name(r), (x + 18.0, y), "body", ICE)
            fs.write(layer, "%.2f" % grade, (x + 26.0 + w, y), "small", MIST, "num")
            fs.write(layer, fs.mass(kg), (right, y), "body", ICE, "num", "right")
            y += 34.0
        apps.row(surface, x, right, y, "Room left", "%s · %.2f m³" % (fs.mass(p.cargo_capacity - car.cargo_mass),
                                                                      p.hold_m3 - car.cargo_volume), MIST)
        return height

    def draw_fitted(self, surface, left, top, width):
        layer = surface.layer
        apps.pane(surface, left, top, width, 96.0)
        x = left + 24.0
        fs.label(layer, "Fitted", (x, top + 30.0))
        fitted = [u for u in WALKER_UPGRADES if u.key in state["owned"] and upgrades.effects_for(u, ("car",))]
        if not fitted:
            fs.write(layer, "Nothing yet: the Workshop app fits upgrades on a pad.", (x, top + 64.0), "body", MIST)
        for u in fitted:
            x += fs.pill(layer, u.name, x, top + 64.0, MINT) + 8.0


def apps_list():
    """The slate's apps in the rail's order."""
    return [apps.Overview(), Market(), apps_map.Map(), apps_map.Contracts(), Workshop(), Vehicle(), apps.Log()]

