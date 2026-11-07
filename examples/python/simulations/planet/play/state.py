"""
The walker's state, one part per system, each a dataclass whose defaults
are a new game's: `State` holds them as `view`, `site`, `vehicle`,
`space`, `economy`, `progress`, `suit`, `exploring`, `contracts`,
`showers`, `storms`, `ui` (the objects made in on_init that draw or
hold the screens), `sound` (the audio, its clips and what the sounds
watch from frame to frame) and `session` (the start, loading, saving and
--check). play.core makes the one `state`.
"""

import random
from collections import Counter
from dataclasses import dataclass, field

from shoonyakasha.planet.gravity_gun import GunParams

import carview
import flight
import gamelog
import hovercar
import pois
import suit as suit_module
import upgrades
from objectives import Objectives
from trade import Market

AUTOSAVE_SECONDS = 120.0         # s of play between autosaves


@dataclass
class View:
    position: tuple = (0.0, 0.0, 0.0)  # the camera, planet space
    forward: tuple = (0.0, 0.0, 1.0)
    frame_ms: float = 16.0             # frame time, smoothed
    chase: object = field(default_factory=carview.ChaseCamera)
    chase_basis: tuple = None          # the chase camera's (right, up, forward) this frame
    captured: bool = False             # whether the mouse was last asked to be captured
    ignore_buttons: bool = False       # the mouse buttons held when it was captured ignored until let go
    bumpers: set = field(default_factory=set)   # the gamepad bumpers held last frame
    pad_x: bool = False                # the gamepad's X held last frame
    capture_toast: list = None         # [file name, frames to wait] of the toast for a screenshot taken


@dataclass
class Site:
    home: object = None                # the outpost at the blocks
    outposts: list = field(default_factory=list)   # every outpost, the home one first
    survey: tuple = None               # the outposts' and the lakes' surveys while the world measures them
    ship: tuple = None                 # (position, right, up, forward) of the parked ship, planet space
    ship_model: tuple = None           # (root entity, mesh entities)


@dataclass
class Vehicle:
    car: object = None                 # the flying car (hovercar.Car), once the ship's pose is known
    driving: bool = False
    controls: object = field(default_factory=hovercar.CarControls)
    poses: tuple = None                # its ((position, rotation) before, after) this frame, planet space
    physics_body: int = None           # its kinematic box in the bubble
    body: object = None                # the world body it is reckoned around; None: the planet
    net_posts: list = field(default_factory=list)   # its cargo net's corner posts
    fuel_call: dict = None             # the fuel on its way to it (call_fuel), or None
    was_dry: bool = False              # its tank was empty last frame
    note: str = ""                     # the last fuel bought, called or delivered


@dataclass
class Space:
    ship: object = None                # the car in space mode (flight.Ship), or None
    controls: object = field(default_factory=flight.Controls)
    time: float = 0.0                  # the world's clock the space mode's steps have reached
    warp: int = 0                      # index into WARPS
    hold: str = None                   # the nose hold: None, "prograde" or "retrograde"


@dataclass
class Economy:
    market: object = field(default_factory=Market)   # the credits and the outposts' supplies (trade.py)
    seen: dict = field(default_factory=dict)   # id of a loose ore rock: (body name, unit direction) where first seen
    sold_kg: float = 0.0
    earned: float = 0.0                # credits from sales
    ledger: object = field(default_factory=gamelog.Ledger)   # the credits earned and spent, the ore sold by resource
    owned: set = field(default_factory=set)   # the keys of the upgrades bought (upgrades.py)
    params: dict = field(default_factory=lambda: upgrades.effective(
        {"car": hovercar.CarParams(), "gun": GunParams(), "suit": suit_module.SuitParams()}, set()))
    parts: dict = field(default_factory=dict)   # part (pois.PARTS): count
    survey_data: float = 0.0           # credits' worth of survey data held
    trade_note: str = ""               # the last sale of the car's cargo
    upgrade_note: str = ""             # the last upgrade bought or refused


@dataclass
class Progress:
    objectives: object = field(default_factory=Objectives)
    waypoint: tuple = None             # (body, unit direction from its centre)
    waypoints: int = 0                 # waypoints reached
    ore_chunks: int = 0                # chunks of broken ore boulders seen
    ore_seen: int = 0                  # 1 once an ore rock was within ORE_SEEN_RANGE
    log: object = field(default_factory=gamelog.Log)   # what happened, for the slate's Log


@dataclass
class Suit:
    state: object = field(default_factory=suit_module.SuitState)   # the reserve and the player's health
    params: object = field(default_factory=suit_module.SuitParams)
    temperature: float = None          # degrees C where the camera is, once known (here_temperature)
    woken: int = 0                     # times the player woke at an outpost


@dataclass
class Exploring:
    pois: object = field(default_factory=pois.Pois)   # the points of interest known (pois.py)
    want_at: tuple = None              # the direction the candidates were last looked for around
    asked: tuple = None                # the extra probes asked for the candidates, and their Pois
    asking: list = field(default_factory=list)
    scenes: dict = field(default_factory=dict)   # key: [(entity, point in planet space)] of the points drawn
    cleared: list = field(default_factory=list)   # the keys of the points the world's clearings are around
    pods: dict = field(default_factory=dict)   # id of a cargo pod's crate: its point of interest's key
    pods_sold: int = 0
    walked_counted: int = None         # the explored map's walked cells already paid as survey data
    use_spot: tuple = None             # (poi, spot, distance) the use key acts on now
    use_progress: float = 0.0          # s the use key has been held on it
    use_latch: bool = False            # the use key must be let go before the next use


@dataclass
class Contracts:
    active: list = field(default_factory=list)   # the active contracts (contracts.Contract)
    offers_taken: set = field(default_factory=set)   # the ids of the offers accepted
    pinned: str = None                 # the id of the contract on the card, the compass and the map
    reputation: dict = field(default_factory=dict)   # outpost name: the player's reputation there
    done: int = 0
    failed: int = 0                    # failed, expired or abandoned
    couriers: dict = field(default_factory=dict)   # id of a courier's crate: its contract's id
    crate_velocity: dict = field(default_factory=dict)   # id of a courier's crate: its velocity last frame
    survey_in: float = 0.0             # s until the survey contracts' shares are measured again
    note: str = ""                     # the last contract accepted, ended or refused


@dataclass
class Showers:
    shower: object = None              # the next or current meteor shower (events.Shower)
    body: object = None                # the world body it falls on; None: the planet
    rng: object = field(default_factory=random.Random)
    tries: int = 0                     # times it was planned again for a large impact in water
    next_start: float = None           # a loaded save's world clock for the next shower's start
    impact_heights: dict = field(default_factory=dict)   # its impact index: the ground's height measured there (m)
    asked: tuple = None                # the extra probes asked for its impacts, and their indices
    asking: list = field(default_factory=list)
    warned: bool = False
    struck: int = 0                    # its impacts struck so far (they are in time order)
    paths: dict = field(default_factory=dict)   # its impact index: the meteor's path (events.Path)
    craters: list = field(default_factory=list)   # (world.Crater, world clock) of the meteorites' craters, oldest first
    hot: dict = field(default_factory=dict)   # id of a meteorite, its chunk or rock: (world clock it struck, cooling s)
    meteorite: object = None           # the last meteorite's prop
    flash: tuple = None                # (world clock, alpha) of the last impact's flash
    shake: tuple = None                # (world clock, amplitude in radians) of the last shake
    plumes: list = field(default_factory=list)   # (point, up, world clock, blast radius, light, rise share) of each puff
    heads_drawn: int = 0               # meteors' heads drawn last frame


@dataclass
class Storms:
    rng: object = field(default_factory=random.Random)
    warned: set = field(default_factory=set)   # names of the outposts the storm has been announced for
    closed: set = field(default_factory=set)   # names of the outposts it covers
    inside: bool = False               # the camera was in it last frame
    rain_drawn: int = 0                # rain streaks drawn last frame


@dataclass
class UI:
    hud: object = None                 # the head-up display (hud.Hud)
    map: object = None                 # the explored ground (explored_map.ExploredMap)
    map_view: object = None            # the minimap (explored_map.MapView)
    viewmodel: object = None
    slate: object = None               # the slate (slate.Slate)
    system: object = None              # the System screen (slate.SystemScreen): the pause
    start: object = None               # the start's screen (slate.SystemScreen)
    car_glows: object = None           # the car's engines' glow (carview.EngineGlows)
    meteor_glows: object = None        # the meteors', plumes' and hot props' quads (viewmodel.Glows)
    rain_glows: object = None          # the rain's streaks (viewmodel.Glows)


@dataclass
class Sound:
    audio: object = None               # engine.audio (sk.Audio), or None without sound
    entries: dict = field(default_factory=dict)   # name: its entry of sounds.SOUNDS or loops.LOOPS
    loops: dict = field(default_factory=dict)     # a loop's name: (voice, bus, spatial) while it plays
    exertion: float = 0.0              # 0 to 1, how hard the player works, eased (loops.update_breath)
    clips: dict = field(default_factory=dict)   # an entry of sounds.SOUNDS: its clips
    rng: object = field(default_factory=random.Random)
    played: Counter = field(default_factory=Counter)   # an entry: voices played of it
    held: int = None                   # the body the gun held last frame
    last_punt: tuple = None            # the gun's last_punt last frame
    velocities: dict = field(default_factory=dict)   # id of a prop: its velocity last frame
    hit_at: dict = field(default_factory=dict)       # id of a prop: `clock` at its last hit
    clock: float = 0.0                 # s of frames since the start
    stride: int = None                 # the player's footsteps counted
    air: float = 0.0                   # s the player has been off the ground
    shown: dict = field(default_factory=dict)        # "slate", "system": whether it was shown last frame
    density: float = 1.0               # the air's density at the camera, a share of the surface's
    air_set: tuple = None              # (gain, cutoff Hz) the world bus was last set to
    hush: bool = False                 # the next toast silent (sounds.alert)
    mix: str = None                    # the state the mix was last set for (sounds.mix_state)
    mix_seen: set = field(default_factory=set)   # the states the mix has been set for
    paused: bool = False               # the world and suit buses paused
    track: str = None                  # the music playing (music.track)
    track_wanted: str = None           # the track wanted, and for how long
    track_since: float = 0.0
    music_voice: int = 0


@dataclass
class Session:
    starting: bool = False             # the start's screen is up: no player until Continue or New game
    loading: dict = None               # a save's player record waiting for the ground under it
    market_record: dict = None         # a save's credits and supplies waiting for the outposts' survey
    autosave_in: float = AUTOSAVE_SECONDS
    last_save: dict = None             # the data of the save last written or read
    day_offset: float = None           # s added to the world's clock for the day (clock.py); None: START_HOUR
    check: dict = None                 # --check: its stage, time in it, and what it measured
    shot: bool = False                 # --shot's screenshot taken
    check_use: bool = False            # --check: the use key held


@dataclass
class State:
    view: View = field(default_factory=View)
    site: Site = field(default_factory=Site)
    vehicle: Vehicle = field(default_factory=Vehicle)
    space: Space = field(default_factory=Space)
    economy: Economy = field(default_factory=Economy)
    progress: Progress = field(default_factory=Progress)
    suit: Suit = field(default_factory=Suit)
    exploring: Exploring = field(default_factory=Exploring)
    contracts: Contracts = field(default_factory=Contracts)
    showers: Showers = field(default_factory=Showers)
    storms: Storms = field(default_factory=Storms)
    ui: UI = field(default_factory=UI)
    sound: Sound = field(default_factory=Sound)
    session: Session = field(default_factory=Session)

