"""
The Slate's apps (slate.py) and its System and Start screens' sections,
from the game's state: Overview (where the player is, the pinned
contract or the objective, the suit, the car, what is near and what is
coming), the Log (what happened, the credits earned and spent, the ore
sold by kind), and Market, Map, Contracts, Workshop and Vehicle as the
lists of items the menu's tabs had (economy.py, contracting.py).

`where()` is where the player (or the car) is: (body, point planet space,
unit up from the body's centre), or None before there is either.
"""

import time

from shoonyakasha.planet import resources
from shoonyakasha.planet.vec import normalize, sub

import clock
import contracts
import fieldslate as fs
import hud as hud_module
import save
import slate
import trade
from explored_map import CELL
from fieldslate import AMBER, CORAL, HOLO, ICE, MINT, MIST
from objectives import progress_text
from play.core import foot, planet, state, world
from play import contracting, economy, exploring, spaceflight, survival, vehicle, walking

GAP = 17.0                       # canvas units between the panes
LOG_FILTERS = (("All", None), ("Money", "money"), ("Contracts", "contracts"), ("World", "world"),
               ("Finds", "finds"))
RESOURCE_COLOURS = {resources.FERRITE: AMBER, resources.ICE: fs.hex_colour("#9FD8F0")}


def where():
    """(body, point planet space, unit up) of the car while driving, else the player's feet; or None."""
    if state["driving"] and state["car"] is not None:
        body = spaceflight.car_body()
        point = vehicle.car_position()
    elif foot.player is not None and foot.bubble is not None:
        body = foot.bubble.body
        point = foot.bubble.to_world(foot.player.feet())
    else:
        return None
    return body, point, normalize(sub(point, body.centre()))


def along(here, point):
    """(metres along the ground, bearing in degrees or None) from `here` (where()) to `point`, planet space."""
    body, _, up = here
    to = normalize(sub(point, body.centre()))
    return hud_module.ground_distance(body.radius, up, to), hud_module.bearing(up, sub(point, here[1]))


def place_text(metres, degrees):
    return fs.distance(metres) + (" · %03d°" % round(degrees) if degrees is not None else "")


def nearest_outpost(here):
    body = here[0]
    found = [(along(here, o.point())[0], o) for o in state["outposts"] if o.body is body]
    return min(found, key=lambda f: f[0]) if found else None


def pane(surface, left, top, width, height):
    fs.pane(surface.mesh, left, top, width, height, fs.RADIUS, fs.PANE, fs.PANE_EDGE)


def row(surface, left, right, y, name, value, colour=ICE, size="body"):
    fs.write(surface.layer, name, (left, y), size, ICE)
    fs.write(surface.layer, value, (right, y), size, colour, "num", "right")


# ── Overview ───────────────────────────────────────────────────────────────

class Overview:
    name, icon = "Overview", "overview"

    def label(self):
        weather = "storm" if state["in_storm"] else "clear"
        temperature = state["temperature"]
        text = "Field slate · %s · %s" % (survival.hour_text(), weather)
        return text + (", %d °C" % round(temperature) if temperature is not None else "")

    def title(self):
        here = where()
        if here is None:
            return "Field slate"
        outpost = economy.at_outpost()
        if outpost is not None:
            return "On %s's pad" % outpost.name
        near = nearest_outpost(here)
        if near is None:
            return "On the %s" % here[0].name
        return "%s from %s" % (fs.distance(near[0]), near[1].name)

    def corner(self, surface, right, top):
        slate.credits_corner(surface, right, top, state["market"].credits,
                             ("Survey data", fs.credits(state["survey_data"])))

    def hints(self):
        return []

    def note(self):
        return "The world keeps running while the slate is open."

    def draw(self, surface, left, top, width, height):
        here = where()
        side = 0.31 * width
        main = width - side - GAP
        upper = 0.5 * height
        self.draw_pinned(surface, here, left, top, main, upper)
        self.draw_suit(surface, left + main + GAP, top, side, upper)
        lower_top, lower_h = top + upper + GAP, height - upper - GAP
        third = (width - 2.0 * GAP) / 3.0
        self.draw_car(surface, here, left, lower_top, third, lower_h)
        self.draw_near(surface, here, left + third + GAP, lower_top, third, lower_h)
        self.draw_coming(surface, here, left + 2.0 * (third + GAP), lower_top, third, lower_h)

    def draw_pinned(self, surface, here, left, top, width, height):
        layer, mesh = surface.layer, surface.mesh
        pane(surface, left, top, width, height)
        x, right = left + 26.0, left + width - 26.0
        c = contracting.pinned_contract()
        if c is not None:
            fs.label(layer, "Pinned contract · " + c.kind, (x, top + 36.0), HOLO)
            fs.pill(layer, hud_module.due_text(contracting.hours_left(c)) + " left", right, top + 36.0, AMBER,
                    "right")
            fs.write(layer, slate.fit_text(contracts.short_task(c), right - x, "heading", "text_medium"),
                     (x, top + 82.0), "heading", ICE, "text_medium")
            fraction = contracts.progress(c) if c.kind in ("delivery", "survey") else None
            if fraction is not None:
                fs.bar(mesh, x, top + 108.0, right - x, fraction, HOLO, 4.0)
            fs.write(layer, contracting.contract_done_text(c), (x, top + 132.0), "body", ICE, "num")
            fs.write(layer, "reward " + fs.credits(c.reward), (right, top + 132.0), "body", AMBER, "num", "right")
            target = contracting.contract_target(c)
            if target is not None:
                bx = x
                bx += surface.button(bx, top + height - 62.0, fs.button_width("Show on map"), "Show on map",
                                     show_map, kind="primary") + 12.0
                name = c.destination if c.kind in ("delivery", "courier") else "the marker"
                text = "Set waypoint · " + name
                surface.button(bx, top + height - 62.0, fs.button_width(text), text,
                               lambda t=target: set_waypoint(t))
                if here is not None and world.body_under(target) is here[0]:
                    fs.write(layer, place_text(*along(here, target)), (right, top + height - 44.0), "small", MIST,
                             "num", "right")
            return
        objectives = state["objectives"]
        goal = objectives.current()
        fs.label(layer, "Objective %d / %d" % (min(objectives.index + 1, len(objectives.goals)),
                                               len(objectives.goals)), (x, top + 36.0), HOLO)
        if goal is None:
            fs.write(layer, "Every objective done", (x, top + 82.0), "heading", ICE, "text_medium")
            fs.write(layer, "The planet is yours to explore.", (x, top + 112.0), "body", MIST)
            return
        fs.write(layer, goal.title, (x, top + 82.0), "heading", ICE, "text_medium")
        for k, line in enumerate(slate.wrap_text(goal.hint, right - x)[:3]):
            fs.write(layer, line, (x, top + 114.0 + 22.0 * k), "body", MIST)
        shown, target = objectives.progress(walking.game_numbers())
        if target != 1:
            fs.bar(mesh, x, top + height - 58.0, right - x, shown / target, HOLO, 4.0)
        fs.write(layer, progress_text(goal, shown, target), (x, top + height - 34.0), "body", ICE, "num")

    def draw_suit(self, surface, left, top, width, height):
        layer, mesh = surface.layer, surface.mesh
        pane(surface, left, top, width, height)
        x = left + 26.0
        fs.label(layer, "Suit", (x, top + 36.0))
        params, s = state["suit_params"], state["suit"]
        reserve = max(0.0, min(1.0, s.reserve / params.reserve))
        colour = MINT if reserve >= 0.995 else HOLO if reserve >= 0.5 else AMBER if reserve >= 0.2 else CORAL
        cx, cy = x + 44.0, top + 112.0
        fs.ring(mesh, cx, cy, 40.0, reserve, colour, 4.0)
        fs.write(layer, "%d" % round(100.0 * reserve), (cx, cy), "lead", ICE, "num", "center")
        tx = cx + 64.0
        temperature = state["temperature"]
        if temperature is not None:
            fs.write(layer, "%d °C" % round(temperature), (tx, cy - 26.0), 24, ICE, "num")
        fs.write(layer, "Comfort %d to %d °C" % (round(params.cold_limit), round(params.hot_limit)), (tx, cy + 4.0),
                 "body", MIST)
        fs.write(layer, "Health %d %%" % round(100.0 * s.health), (tx, cy + 28.0), "body",
                 ICE if s.health >= 0.995 else CORAL)

    def draw_car(self, surface, here, left, top, width, height):
        layer, mesh = surface.layer, surface.mesh
        pane(surface, left, top, width, height)
        x, right = left + 26.0, left + width - 26.0
        car = state["car"]
        fs.label(layer, "Hover car", (x, top + 34.0))
        if car is None:
            return
        if state["driving"]:
            fs.write(layer, "driving", (right, top + 34.0), "small", MIST, "num", "right")
        elif here is not None and spaceflight.car_body() is here[0]:
            fs.write(layer, place_text(*along(here, vehicle.car_position())), (right, top + 34.0), "small", MIST,
                     "num", "right")
        p = car.params
        row(surface, x, right, top + 70.0, "Fuel", "%s / %s" % (fs.grouped(car.fuel), fs.mass(p.fuel_capacity)))
        fs.bar(mesh, x, top + 86.0, right - x, car.fuel / p.fuel_capacity, HOLO, 3.0)
        row(surface, x, right, top + 116.0, "Hold", "%s / %s" % (fs.grouped(car.cargo_mass), fs.mass(p.cargo_capacity)))
        fs.bar(mesh, x, top + 132.0, right - x, car.cargo_mass / p.cargo_capacity, AMBER, 3.0)
        offer = economy.fuel_offer()
        by = top + height - 58.0
        if offer is not None:
            outpost, distance = offer
            price, fee, _ = trade.fuel_call(outpost, distance, False)
            cost = fee + price * (p.fuel_capacity - car.fuel)
            surface.button(x, by, right - x, "Call fuel", economy.call_fuel, "~" + fs.credits(cost))
        elif economy.car_outpost() is not None:
            surface.button(x, by, right - x, "Fill the tank", economy.fill_tank)
        else:
            surface.button(x, by, right - x, "Call fuel", None, enabled=False)

    def draw_near(self, surface, here, left, top, width, height):
        layer, mesh = surface.layer, surface.mesh
        pane(surface, left, top, width, height)
        x, right = left + 26.0, left + width - 26.0
        fs.label(layer, "Near you", (x, top + 34.0))
        if here is None:
            return
        found = sorted(((along(here, p)[0], name, colour) for name, p, colour in exploring.poi_markers(here[0])),
                       key=lambda f: f[0])[:4]
        if not found:
            fs.write(layer, "Nothing found nearby yet.", (x, top + 72.0), "body", MIST)
        line = fs.tint(ICE, 0.08, fs.ON_SLATE)
        for k, (metres, name, colour) in enumerate(found):
            y = top + 72.0 + 44.0 * k
            fs.dot(mesh, x + 4.0, y, 4.0, colour, hollow=name == "Signal")
            fs.write(layer, name, (x + 20.0, y), "body", ICE)
            fs.write(layer, fs.distance(metres), (right, y), "small", MIST, "num", "right")
            if k:
                mesh.face([(x, y - 22.5), (right, y - 22.5), (right, y - 21.5), (x, y - 21.5)], [line] * 4)

    def draw_coming(self, surface, here, left, top, width, height):
        layer = surface.layer
        pane(surface, left, top, width, height)
        x, right = left + 26.0, left + width - 26.0
        fs.label(layer, "Coming", (x, top + 34.0))
        lines = coming()
        if not lines:
            fs.write(layer, "Nothing on the way.", (x, top + 72.0), "body", MIST)
        y = top + 74.0
        for tag, colour, text in lines:
            w = fs.pill(layer, tag, x, y, colour)
            for k, part in enumerate(slate.wrap_text(text, right - x - w - 14.0)[:2]):
                fs.write(layer, part, (x + w + 14.0, y + 22.0 * k), "body", ICE)
            y += 66.0


def minutes(seconds):
    return "%d min" % max(1, round(seconds / 60.0))


def coming():
    """(tag, colour, text) of the storm and the meteor shower on the way or under way."""
    found = []
    storm, now = world.storm, world.time
    if storm is not None and now < storm.end:
        closed = sorted(state["storm_closed"])
        if state["in_storm"]:
            text = "You are in it; contract deadlines are held"
        elif now < storm.start:
            text = "Starts in %s, moving %d m/s" % (minutes(storm.start - now), round(storm.speed))
        else:
            text = "Under way for %s more" % minutes(storm.end - now)
        if closed:
            text += "; pads closed at %s" % ", ".join(closed)
        found.append(("Storm", CORAL, text))
    shower = state["shower"]
    if shower is not None:
        if shower.start > now:
            found.append(("Meteors", AMBER, "Next shower in about %s" % minutes(shower.start - now)))
        else:
            found.append(("Meteors", AMBER, "A shower is falling"))
    return found


def show_map():
    state["slate"].close(at_once=True)
    state["map_view"].set_open(True)


def set_waypoint(point):
    body = world.body_under(point)
    state["waypoint"] = (body, normalize(sub(point, body.centre())))
    state["hud"].toast("Waypoint set")


# ── Log ────────────────────────────────────────────────────────────────────

class Log:
    name, icon = "Log", "log"

    def __init__(self):
        self.filter = 0

    def label(self):
        return "Log · day %d" % (survival_day() + 1)

    def title(self):
        return "What happened"

    def corner(self, surface, right, top):
        layer = surface.layer
        x = right
        for k in reversed(range(len(LOG_FILTERS))):
            name = LOG_FILTERS[k][0]
            w = fs.text_width(name, "body", "text_medium") + 26.0
            x -= w
            active = k == self.filter
            box = (x, top + 18.0, w, 32.0)
            if active or surface.over(*box):
                fs.pane(surface.mesh, *box, fs.RADIUS_SMALL, fs.tint(HOLO if active else ICE, 0.12, fs.ON_SLATE),
                        fs.tint(HOLO, 0.3, fs.ON_SLATE) if active else None)
            fs.write(layer, name, (x + 0.5 * w, top + 34.0), "body", ICE if active else MIST, "text_medium",
                     "center")
            surface.region(*box, lambda k=k: setattr(self, "filter", k))
            x -= 6.0

    def hints(self):
        return []

    def note(self):
        return "Toasts you missed end up here."

    def draw(self, surface, left, top, width, height):
        layer, mesh = surface.layer, surface.mesh
        side = 0.33 * width
        main = width - side - GAP
        pane(surface, left, top, main, height)
        entries = state["log"].shown(LOG_FILTERS[self.filter][1])
        x, right = left + 26.0, left + main - 26.0
        line = fs.tint(ICE, 0.08, fs.ON_SLATE)
        rows = int((height - 30.0) // 45.0)
        for k, e in enumerate(entries[:rows]):
            y = top + 34.0 + 45.0 * k
            fs.write(layer, "d%d %s" % (e["day"] + 1, e["hour"]), (x, y), "small", MIST, "num")
            fs.dot(mesh, x + 112.0, y, 4.0, e["colour"])
            room = right - x - 130.0
            if e["detail"]:
                room -= fs.write(layer, e["detail"], (right, y), "small", e["colour"], "num", "right") + 14.0
            fs.write(layer, slate.fit_text(e["text"], room), (x + 128.0, y), "body", ICE)
            if k:
                mesh.face([(x, y - 22.5), (right, y - 22.5), (right, y - 21.5), (x, y - 21.5)], [line] * 4)
        if not entries:
            fs.write(layer, "Nothing yet.", (x, top + 34.0), "body", MIST)
        sx = left + main + GAP
        ledger = state["ledger"]
        so_far = 196.0
        pane(surface, sx, top, side, so_far)
        x, right = sx + 26.0, sx + side - 26.0
        fs.label(layer, "So far", (x, top + 30.0))
        cells = walking.game_numbers()["cells"]
        rows_ = (("Earned", fs.credits(ledger.earned), MINT), ("Spent", fs.credits(ledger.spent), CORAL),
                 ("Contracts", "%d done · %d failed" % (state["contracts_done"], state["contracts_failed"]), ICE),
                 ("Surveyed", "%s km²" % fs.grouped(cells * CELL * CELL / 1e6, 2), ICE))
        for k, (name, value, colour) in enumerate(rows_):
            row(surface, x, right, top + 62.0 + 34.0 * k, name, value, colour)
        top2 = top + so_far + GAP
        sold = sorted(ledger.sold_by.items(), key=lambda kv: -kv[1])
        h2 = 62.0 + 50.0 * max(1, min(5, len(sold)))
        pane(surface, sx, top2, side, h2)
        fs.label(layer, "Ore sold by kind", (x, top2 + 30.0))
        if not sold:
            fs.write(layer, "None sold yet.", (x, top2 + 62.0), "body", MIST)
        most = sold[0][1] if sold else 1.0
        for k, (resource, kg) in enumerate(sold[:5]):
            y = top2 + 60.0 + 50.0 * k
            row(surface, x, right, y, resources.name(resource), fs.mass(kg))
            fs.bar(mesh, x, y + 15.0, right - x, kg / most, RESOURCE_COLOURS.get(resource, ICE), 3.0)


def survival_day():
    return clock.day(survival.day_time())


def log_toast(text, colour, detail, kind):
    """A toast written into the log, at the day's time."""
    state["log"].add(survival_day(), survival.hour_text(), text, detail or "", colour, kind)


# ── The lists of items ─────────────────────────────────────────────────────

def items_apps():
    """Market, Map, Contracts, Workshop and Vehicle, each a list of items."""
    def outpost_title():
        outpost = economy.car_outpost() or economy.at_outpost()
        return outpost.name if outpost is not None else "No outpost here"

    map_items = [{"note": lambda: "M opens the full map; a left click on it sets the waypoint."},
                 {"button": "Open the map", "keys": "M", "do": show_map, "hint": "The explored ground, north up."}]
    return [slate.ItemsApp("Market", "market", "Market", outpost_title, lambda: economy.outpost_items(),
                           "Ore settled on a pad sells by itself."),
            slate.ItemsApp("Map", "map", "Map · " + planet.name, "Around you", lambda: map_items),
            slate.ItemsApp("Contracts", "contracts", "Contracts", "Contracts", lambda: contracting.contract_items(),
                           "Storms hold your deadlines while you're inside them."),
            slate.ItemsApp("Workshop", "workshop", "Workshop", "Upgrades", lambda: economy.upgrade_items()),
            slate.ItemsApp("Vehicle", "vehicle", "Vehicle", "Hover car", lambda: economy.car_items())]


def slate_apps():
    found = items_apps()
    return [Overview(), found[0], found[1], found[2], found[3], found[4], Log()]


# ── Saves ──────────────────────────────────────────────────────────────────

def save_line(data):
    """"day 3 09:56 · 2,000 cr · goals 2/6 · saved 21:41" of a save's data."""
    t = data.get("time", 0.0) + (data.get("day_offset") or 0.0)
    hour = clock.hour(t)
    game = data.get("game", {})
    credits = data.get("market", {}).get("credits", 0.0)
    saved = time.strftime("%d %b %H:%M", time.localtime(data.get("saved_at", 0.0)))
    return "day %d %02d:%02d · %s · goals %d/%d · saved %s" % (
        clock.day(t) + 1, int(hour) % 24, int(hour * 60.0) % 60, fs.credits(credits), game.get("objective", 0),
        len(state["objectives"].goals), saved)


def save_where(data):
    player = data.get("player") or {}
    return "On the %s" % (player.get("on") or planet.name)


def save_card(surface, left, top, width, data, button=None):
    """A save's card: where, its line, and `button` (text, do) at its right. Returns its height."""
    h = 78.0
    fs.pane(surface.mesh, left, top, width, h, fs.RADIUS, fs.tint(ICE, 0.05, fs.ON_SLATE),
            fs.tint(HOLO, 0.25, fs.ON_SLATE))
    fs.write(surface.layer, save_where(data), (left + 22.0, top + 26.0), "lead", ICE, "text_medium")
    fs.write(surface.layer, save_line(data), (left + 22.0, top + 52.0), "small", MIST, "num")
    if button is not None:
        text, do = button
        bw = fs.button_width(text)
        surface.button(left + width - 20.0 - bw, top + 0.5 * (h - fs.BUTTON_HEIGHT), bw, text, do)
    return h


def saves_section(title, button, note):
    @slate.section
    def draw(surface, left, top, width, height):
        layer = surface.layer
        fs.write(layer, title, (left, top + 14.0), 24, ICE, "display")
        data = state["last_save"]
        fs.label(layer, "1 save" if data is not None else "No save yet", (left + width, top + 20.0), MIST, "right")
        if data is not None:
            save_card(surface, left, top + 52.0, width, data, button)
        fs.write(layer, note, (left, top + height - 10.0), "body", MIST)
    return draw


def autosave_note():
    data = state["last_save"]
    return save.describe(data) if data is not None else "No save yet"
