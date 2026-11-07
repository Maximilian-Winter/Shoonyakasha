"""
The slate's Map and Contracts apps (slate.py) and the contracts' cards
they share.

Map: the explored ground around the player (explored_map.render) north
up in a pane at the left, at the range of the tab chosen at the header's
right (FULL_RANGES; the mouse wheel steps through them), with the markers
(walking.markers) named and their distances, those beyond the edge held
on it, a storm's reach as a ring, the player's arrow, the scale bar and a
button clearing the waypoint; a click on the map sets the waypoint there
(`waypoint_offset`). At the right, the contracts: the active ones or the
offers of the outpost here, by the tab chosen over them.

Contracts: the active contracts at the left (the pinned first, each with
Pin, the pinned with Abandon), the offers of the outpost whose pad the
car or the player is on at the right, each with Accept.

A contract's card (`contract_card`): PINNED when it is, its kind and its
destination, FRAGILE for a courier's crate, its reward; its task in a few
words (contracts.short_task); a delivery's and a survey's progress with a
bar, a courier's crate's integrity with one, a recovery's where to; the
time left. An offer's card (`offer_card`): its kind and how far its
target is, its task, its reward and Accept.
"""

import math

from shoonyakasha.planet.vec import add, normalize, scale, sub

import contracts
import explored_map
import fieldslate as fs
import slate
from explored_map import CELL, FULL_CONTOURS, FULL_RANGES, UNEXPLORED_FULL
from fieldslate import AMBER, CORAL, HOLO, ICE, MINT, MIST
import hud as hud_module
from play.core import planet, state, world
from play import apps, contracting, economy, walking

MAP_PIXELS = 320                 # the map image's pixels across, stretched to its size
MAP_INTERVAL = 0.2               # s between the map's redraws
CARD_PAD = 22.0


def distance_name(metres):
    return explored_map.distance_label(metres)


class Map:
    name, icon = "Map", "map"

    def __init__(self):
        self.zoom = 1                 # index into FULL_RANGES
        self.side = 0                 # 0: the active contracts, 1: the offers here
        self.image = None             # the map's image element, made on first draw
        self.front = None             # a layer over it for the markers
        self.since = 1e9              # s since the image was last drawn
        self.drawn = None             # what the image was last drawn for
        self.shown = False

    def label(self):
        here = apps.where()
        body = here[0].name if here is not None else planet.name
        cells = walking.game_numbers()["cells"]
        return "Map · %s · north up · %s cells explored" % (body, format(cells, ","))

    def title(self):
        here = apps.where()
        near = apps.nearest_outpost(here) if here is not None else None
        return "Around %s" % near[1].name if near is not None else "Around you"

    def corner(self, surface, right, top):
        tabs(surface, right, top, [distance_name(m) for m in FULL_RANGES], self.zoom,
             lambda k: setattr(self, "zoom", k))

    def hints(self):
        return [(("LMB",), "Waypoint"), (("Wheel",), "Zoom")]

    def note(self):
        return "Storms hold your deadlines while you're inside them."

    def scroll(self, steps):
        self.zoom = max(0, min(len(FULL_RANGES) - 1, self.zoom - int(steps)))

    def hide(self):
        if self.shown:
            self.shown = False
            self.front.begin()
            self.front.end()
            self.image_visible(False)

    def image_visible(self, on):
        if self.image is not None:
            self.image[0].set_visible(self.image[1], on)

    def ensure(self, surface):
        if self.image is None:
            ui = surface.ui
            element = ui.create_image(surface.canvas, size=(MAP_PIXELS, MAP_PIXELS))
            ui.set_raycast_target(element, False)
            self.image = (ui, element)
            self.front = slate.FadeLayer(ui, surface.canvas)

    def draw(self, surface, left, top, width, height):
        self.ensure(surface)
        size = min(height, 0.6 * width)
        self.draw_map(surface, left, top, size)
        side_left = left + size + apps.GAP
        self.draw_contracts(surface, side_left, top, left + width - side_left, height)

    def draw_map(self, surface, left, top, size):
        ui, element = self.image
        front = self.front
        front.alpha, front.zoom = surface.layer.alpha, surface.layer.zoom
        front.begin()
        mesh = front.mesh
        here = apps.where()
        fs.pane(surface.mesh, left, top, size, size, fs.RADIUS, fs.hex_colour("#071219", 0.95), fs.PANE_EDGE)
        if here is None:
            self.image_visible(False)
            front.end()
            return
        body, point, up = here
        grid = state.ui.map.grid(body, up)
        at = grid.index(up)
        if at is None:
            self.image_visible(False)
            front.end()
            return
        metres = FULL_RANGES[self.zoom]
        inset = 3.0
        box = (left + inset, top + inset, size - 2.0 * inset)
        self.since += max(0.0, surface.clock_dt)
        key = (body.name, metres, round(at[0], 1), round(at[1], 1))
        if self.since >= MAP_INTERVAL or key != self.drawn:
            self.since, self.drawn = 0.0, key
            ui.set_texture_data(element, explored_map.render(grid, at, metres, MAP_PIXELS, unexplored=UNEXPLORED_FULL,
                                                             contour=FULL_CONTOURS.get(metres)))
        x0, y0, s = box
        if front.zoom is not None:
            cx, cy, z = front.zoom
            px, py = cx + z * (x0 - cx), cy + z * (y0 - cy)
            ui.set_rect(element, anchor_min=(0, 0), anchor_max=(0, 0), pivot=(0, 0), position=(px, py),
                        size=(s * z, s * z))
        else:
            ui.set_rect(element, anchor_min=(0, 0), anchor_max=(0, 0), pivot=(0, 0), position=(x0, y0), size=(s, s))
        self.image_visible(True)
        self.shown = True

        def pixel(where):
            px, py = explored_map.to_pixel(where, at, metres, s)
            return x0 + px, y0 + py

        storm = world.storm
        if storm is not None and body is planet and storm.start - 300.0 <= world.time < storm.end:
            centre = grid.index(storm.centre_at(world.time))
            if centre is not None:
                sx, sy = pixel(centre)
                radius = storm.radius / metres * 0.5 * s
                if radius < 4.0 * s:
                    ring = [(sx + radius * math.cos(a), sy + radius * math.sin(a))
                            for a in (2.0 * math.pi * k / 48 for k in range(48))]
                    for k in range(0, 48, 2):
                        a, b = ring[k], ring[(k + 1) % 48]
                        if inside(a, box) and inside(b, box):
                            mesh.segment(a, b, CORAL, 1.4)
                    lx, ly = clamp((sx, sy), box, 40.0)
                    fs.write(front, "STORM · %d m/s" % round(storm.speed), (lx, ly), "small", CORAL, "text_medium",
                             "center")
        labels = []
        for name, position, colour in walking.markers(body):
            where = grid.index(normalize(sub(position, body.centre())))
            if where is None:
                continue
            px, py = pixel(where)
            metres_to = math.hypot(where[0] - at[0], where[1] - at[1]) * CELL
            if inside((px, py), box):
                hud_module.marker(mesh, name, px, py, 6.0 if name in ("Waypoint",) else 5.0, colour)
                labels.append(("%s %s" % (name.upper(), distance_name(metres_to)), (px + 10.0, py), colour))
            else:
                ex, ey = clamp((px, py), box, 14.0)
                angle = math.atan2(py - (y0 + 0.5 * s), px - (x0 + 0.5 * s))
                tip = (ex + 7.0 * math.cos(angle), ey + 7.0 * math.sin(angle))
                side = (ex - 5.0 * math.sin(angle), ey + 5.0 * math.cos(angle))
                other = (ex + 5.0 * math.sin(angle), ey - 5.0 * math.cos(angle))
                mesh.face([tip, side, other], [fs.with_alpha(colour, 0.7)] * 3)
        for text, (lx, ly), colour in explored_map.separate(explored_map.merge(labels)):
            if inside((lx, ly), box):
                fs.write(front, slate.fit_text(text, x0 + s - lx - 6.0, "small", "num"), (lx, ly), "small", colour,
                         "num")
        heading = hud_module.bearing(up, walking.view_basis()[2]) or 0.0
        cx, cy = x0 + 0.5 * s, y0 + 0.5 * s
        fs.dot(mesh, cx, cy, 18.0, fs.tint(ICE, 0.35, fs.ON_SLATE), hollow=True, width=1.0)
        a = math.radians(heading)
        sin, cos = math.sin(a), math.cos(a)

        def turn(dx, dy):
            return cx + dx * cos - dy * sin, cy + dx * sin + dy * cos
        mesh.face([turn(0.0, -9.0), turn(6.0, 7.0), turn(0.0, 3.0), turn(-6.0, 7.0)], [ICE] * 4)
        bar_metres, bar_px = explored_map.scale_bar(metres, s)
        bx, by = x0 + 18.0, y0 + s - 22.0
        mesh.face([(bx, by - 1.0), (bx + bar_px, by - 1.0), (bx + bar_px, by + 1.0), (bx, by + 1.0)], [ICE] * 4)
        fs.write(front, distance_name(bar_metres), (bx + bar_px + 8.0, by), "small", ICE, "num")
        front.end()

        def click(box=box, grid=grid, at=at, metres=metres):
            p = surface.point
            if p is None:
                return
            where = explored_map.from_pixel((p[0] - box[0], p[1] - box[1]), at, metres, box[2])
            waypoint_cell(grid, body, where)
        surface.region(x0, y0, s, s, click)

    def draw_contracts(self, surface, left, top, width, height):
        here = economy.at_outpost()
        offers = contracting.contract_offers(here)
        names = ["Active %d / %d" % (len(state.contracts.active), contracts.MAX_ACTIVE),
                 "Offers at %s %d" % (here.name, len(offers)) if here is not None else "Offers"]
        tabs(surface, left, top, names, self.side, lambda k: setattr(self, "side", k), align="left", size="body")
        y = top + 56.0
        room = top + height - y - (fs.BUTTON_HEIGHT + 16.0 if state.progress.waypoint is not None else 0.0)
        if self.side == 0:
            draw_active(surface, left, y, width, room)
        else:
            draw_offers(surface, here, offers, left, y, width, room)
        if state.progress.waypoint is not None:
            text = "Clear waypoint"
            bw = fs.button_width(text)
            surface.button(left + width - bw, top + height - fs.BUTTON_HEIGHT, bw, text,
                           lambda: setattr(state.progress, "waypoint", None))


class Contracts:
    name, icon = "Contracts", "contracts"

    def label(self):
        here = economy.at_outpost()
        if here is None:
            return "Contracts · not on a pad"
        return "Contracts · at %s · rep %+d" % (here.name, round(state.contracts.reputation.get(here.name, 0.0)))

    def title(self):
        return "Contracts"

    def corner(self, surface, right, top):
        slate.credits_corner(surface, right, top, state.economy.market.credits)

    def hints(self):
        return []

    def note(self):
        return apps.sentence(state.contracts.note) or "Storms hold your deadlines while you're inside them."

    def draw(self, surface, left, top, width, height):
        half = 0.5 * (width - apps.GAP)
        fs.label(surface.layer, "Active %d / %d" % (len(state.contracts.active), contracts.MAX_ACTIVE), (left, top + 10.0))
        draw_active(surface, left, top + 30.0, half, height - 30.0)
        here = economy.at_outpost()
        x = left + half + apps.GAP
        fs.label(surface.layer, "Offered at %s today" % here.name if here is not None else "Offers",
                 (x, top + 10.0))
        draw_offers(surface, here, contracting.contract_offers(here), x, top + 30.0, half, height - 30.0)


def draw_active(surface, left, top, width, height):
    active = sorted(state.contracts.active, key=lambda c: c.id != state.contracts.pinned)
    if not active:
        fs.write(surface.layer, "No contracts yet: take one on an outpost's pad.", (left, top + 16.0), "body", MIST)
    y = top
    for c in active:
        if y > top + height - 100.0:
            break
        y += contract_card(surface, c, left, y, width) + 14.0


def draw_offers(surface, here, offers, left, top, width, height):
    layer = surface.layer
    if here is None:
        fs.write(layer, "Bring the car or yourself onto an outpost's pad.", (left, top + 16.0), "body", MIST)
        return
    if not offers:
        fs.write(layer, "No more offers here today; new ones at dawn.", (left, top + 16.0), "body", MIST)
        return
    y = top
    for c in offers:
        if y > top + height - 80.0:
            break
        y += offer_card(surface, c, left, y, width) + 12.0


def tabs(surface, x, top, names, chosen, choose, align="right", size="body"):
    """A row of tabs from `x` (their right with "right"), the chosen one lit; a click chooses."""
    layer = surface.layer
    widths = [fs.text_width(n, size, "text_medium") + 26.0 for n in names]
    if align == "right":
        x -= sum(widths) + 6.0 * (len(names) - 1)
    for k, (name, w) in enumerate(zip(names, widths)):
        active = k == chosen
        box = (x, top + 18.0, w, 34.0)
        if active or surface.over(*box):
            fs.pane(surface.mesh, *box, fs.RADIUS_SMALL, fs.tint(HOLO if active else ICE, 0.12, fs.ON_SLATE),
                    fs.tint(HOLO, 0.3, fs.ON_SLATE) if active else None)
        fs.write(layer, name, (x + 0.5 * w, top + 35.0), size, ICE if active else MIST, "text_medium", "center")
        surface.region(*box, lambda k=k: choose(k))
        x += w + 6.0


def contract_card(surface, c, left, top, width):
    """The card of the active contract `c`; returns its height."""
    layer, mesh = surface.layer, surface.mesh
    pinned = c.id == state.contracts.pinned
    height = 160.0 if c.kind in ("delivery", "survey", "courier") else 136.0
    fs.pane(mesh, left, top, width, height, fs.RADIUS, fs.tint(ICE, 0.05 if pinned else 0.03, fs.ON_SLATE),
            fs.tint(HOLO, 0.3, fs.ON_SLATE) if pinned else fs.PANE_EDGE)
    x, right = left + CARD_PAD, left + width - CARD_PAD
    y = top + 26.0
    if pinned:
        x += fs.pill(layer, "Pinned", x, y, HOLO) + 10.0
    x += fs.label(layer, "%s · %s" % (c.kind, c.destination), (x, y)) + 10.0
    if c.kind == "courier":
        fs.pill(layer, "Fragile", x, y, CORAL)
    fs.write(layer, fs.credits(c.reward), (right, y), "body", AMBER, "num", "right")
    x = left + CARD_PAD
    fs.write(layer, slate.fit_text(contracts.short_task(c), right - x, "lead", "text_medium"), (x, top + 56.0),
             "lead", ICE, "text_medium")
    due = "due in %s" % hud_module.due_text(contracting.hours_left(c))
    y = top + 84.0
    if c.kind in ("delivery", "survey"):
        fs.bar(mesh, x, y - 1.5, right - x, contracts.progress(c), HOLO, 3.0)
        y += 20.0
        fs.write(layer, contracting.contract_done_text(c), (x, y), "small", ICE, "num")
        fs.write(layer, due, (right, y), "small", ICE, "num", "right")
    elif c.kind == "courier":
        integrity = max(0.0, 1.0 - c.damage / contracts.BREAK_DAMAGE)
        w = fs.write(layer, "Crate integrity", (x, y), "small", ICE)
        value = fs.write(layer, "%d %%" % round(100.0 * integrity), (right, y), "small", ICE, "num", "right")
        fs.bar(mesh, x + w + 12.0, y - 1.5, right - x - w - value - 24.0, integrity,
               MINT if integrity > 0.5 else AMBER if integrity > 0.25 else CORAL, 3.0)
        y += 20.0
        fs.write(layer, due, (right, y), "small", ICE, "num", "right")
    else:
        fs.write(layer, contracting.contract_done_text(c), (x, y), "small", ICE, "num")
        fs.write(layer, due, (right, y), "small", ICE, "num", "right")
    y = top + height - 22.0
    if pinned:
        text_button(surface, x - 6.0, y, "Abandon", CORAL, contracting.abandon_pinned)
    else:
        text_button(surface, x - 6.0, y, "Pin", HOLO, lambda c=c: contracting.pin_contract(c))
    return height


def offer_card(surface, c, left, top, width):
    """The card of the offer `c`; returns its height."""
    layer, mesh = surface.layer, surface.mesh
    height = 84.0
    fs.pane(mesh, left, top, width, height, fs.RADIUS, fs.tint(ICE, 0.03, fs.ON_SLATE), fs.PANE_EDGE)
    x, right = left + CARD_PAD, left + width - CARD_PAD
    bw = fs.button_width("Accept")
    surface.button(right - bw, top + 0.5 * (height - fs.BUTTON_HEIGHT), bw, "Accept",
                   lambda c=c: contracting.accept_contract(c))
    reward_right = right - bw - 16.0
    rw = fs.write(layer, fs.credits(c.reward), (reward_right, top + 0.5 * height), "body", AMBER, "num", "right")
    fs.label(layer, "%s · %s" % (c.kind, offer_where(c)), (x, top + 28.0))
    room = reward_right - rw - 16.0 - x
    fs.write(layer, slate.fit_text(contracts.task(c), room), (x, top + 56.0), "body", ICE)
    return height


def offer_where(c):
    """How far an offer's target is from the player, or where it goes."""
    here = apps.where()
    if c.kind in ("recovery", "survey") and here is not None and here[0] is planet:
        point = add(planet.centre(), scale(c.target, planet.radius))
        return fs.distance(apps.along(here, point)[0])
    return "to %s" % c.destination


def text_button(surface, x, y, text, colour, do):
    """A text button: `text` in `colour` from `x`, its middle at `y`, lit while the pointer is over it."""
    w = fs.text_width(text, "body", "text_medium") + 12.0
    box = (x, y - 14.0, w, 28.0)
    if surface.over(*box):
        fs.fill(surface.mesh, fs.rounded_rect(*box, fs.RADIUS_SMALL), fs.tint(colour, 0.12, fs.ON_SLATE))
    fs.write(surface.layer, text, (x + 6.0, y), "body", colour, "text_medium")
    surface.region(*box, do)
    return w


def inside(p, box):
    x0, y0, s = box
    return x0 <= p[0] <= x0 + s and y0 <= p[1] <= y0 + s


def clamp(p, box, margin):
    x0, y0, s = box
    return (min(max(p[0], x0 + margin), x0 + s - margin), min(max(p[1], y0 + margin), y0 + s - margin))


def waypoint_cell(grid, body, where):
    """The waypoint at the cell coordinates `where` of `body`'s grid."""
    state.progress.waypoint = (body, grid.direction(where[0], where[1]))


def waypoint_offset(east, north):
    """The waypoint `east` and `north` metres from where the player is, on the body's grid."""
    here = apps.where()
    if here is None:
        return False
    body, _, up = here
    grid = state.ui.map.grid(body, up)
    at = grid.index(up)
    if at is None:
        return False
    waypoint_cell(grid, body, (at[0] + east / CELL, at[1] + north / CELL))
    return True
