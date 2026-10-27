"""Watch nuclei split, react and fuse.

    python viewer.py                  # fission: a neutron into U-235
    python viewer.py --scene chain    # 64 U-235 nuclei and one neutron
    python viewer.py --scene fusion   # deuterium and tritium: He-4 and a neutron
    python viewer.py --scene fusion --reaction dd --speed 0.9
    python viewer.py --scene chain --duration 20 --screenshot chain.png

Keys: 1 fission, 2 chain reaction, 3 fusion, R restart, SPACE pause,
+ and - run faster or slower. Hold the right mouse button and use WASD,
Q and E to fly the camera.

The nucleons run on the GPU every frame (pipeline/, which repeats
shaders/step.comp, the same shader the offline tools use), under the
scenarios' setting physics.SCENARIO: Coulomb strong enough that uranium
splits soon after capturing a neutron, screened so that nuclei side by side
do not push each other apart. Every few frames the nucleons are read back.
A free neutron near a uranium nucleus is turned toward it
(events.steer_kicks); a uranium nucleus that has captured a neutron turns
its heat into stretching (events.CaptureRule); a nucleus that has split is found
(events.FissionTracker), and each fragment gives off a neutron and radiates
part of its heat (events.emission_kicks). All are sent to the GPU as kicks
the pipeline applies once. In fusion, which needs no rule, each reaction is
printed (events.ReactionTracker).
"""

import argparse
import math
import time

import numpy as np
import shoonyakasha as sk
from shoonyakasha.mathutil import look_rotation

import events as ev
from physics import HERE, NUCLEON, SCENARIO

PIPELINE = HERE / "pipeline"
SCENARIO_CONFIGS = HERE / "configs" / "scenario"
CAPACITY = 16384             # "Nucleons" elementCount in pipeline/data.json
SUBSTEPS = 8                 # Step{s} passes in pipeline/passes/20_physics.json, two a step
LOOK_EVERY = 4               # frames between readbacks for the fission tracker
RATE = 20.0                  # simulation time units a second at time scale 1
DT = 0.02                    # every step's length; frames below RATE / (SUBSTEPS * DT) fps run slower
CAPTURE_RADIUS = 10.0        # free neutrons this close to a uranium nucleus's centre are turned toward it
SCENES = ("fission", "chain", "fusion")
REACTIONS = {"dt": ("H-2", "H-3"), "dd": ("H-2", "H-2")}
GLOW = {"fission": 6.0, "chain": 6.0, "fusion": 0.3}   # kinetic energy at which a nucleon glows fully


def nucleus(name, center=(0.0, 0.0, 0.0), rotation=None, nucleus_id=1):
    """A precomputed nucleus as NUCLEON records, turned by `rotation` and moved to `center`."""
    rows = np.load(SCENARIO_CONFIGS / ("%s.npy" % name))
    out = np.zeros(len(rows), dtype=NUCLEON)
    positions = rows[:, :3] @ (rotation.T if rotation is not None else np.eye(3))
    out["position"] = positions + np.asarray(center, dtype=np.float32)
    out["type"] = rows[:, 3]
    out["nucleus"] = nucleus_id
    return out


def random_rotation(rng):
    q = rng.normal(size=4)
    w, x, y, z = q / np.linalg.norm(q)
    return np.array([[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
                     [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
                     [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]])


def neutron(position, velocity):
    out = np.zeros(1, dtype=NUCLEON)
    out["position"] = position
    out["velocity"] = velocity
    return out


def build(scene, speed, rng, spacing=21.0, lattice=4, reaction="dt"):
    """The nucleons of `scene` and where the camera looks from and at. The
    chain reaction's nuclei sit on a cube of `lattice` nuclei a side,
    `spacing` apart. Fusion's two nuclei (REACTIONS[reaction]) close at
    `speed`, their total momentum zero."""
    if scene == "fission":
        parts = [nucleus("U-235"), neutron((-14.0, 0.6, 0.0), (speed, 0.0, 0.0))]
        return np.concatenate(parts), (0.0, 6.0, 32.0), (0.0, 0.0, 0.0)
    if scene == "chain":
        side = (np.arange(lattice) - (lattice - 1) / 2.0) * spacing
        corner = side[0]
        parts = []
        for i, offset in enumerate(np.array(np.meshgrid(side, side, side)).reshape(3, -1).T):
            parts.append(nucleus("U-235", offset, random_rotation(rng), nucleus_id=i + 1))
        parts.append(neutron((corner - 14.0, corner + 0.6, corner), (speed, 0.0, 0.0)))
        eye = tuple(-corner * np.array([2.7, 2.0, 5.6]))
        return np.concatenate(parts), eye, (0.0, 0.0, 0.0)
    if scene == "fusion":
        first, second = REACTIONS[reaction]
        a = nucleus(first, (-8.0, 0.0, 0.0), random_rotation(rng), nucleus_id=1)
        b = nucleus(second, (8.0, 0.0, 0.0), random_rotation(rng), nucleus_id=2)
        a["velocity"] = (speed * len(b) / (len(a) + len(b)), 0.0, 0.0)
        b["velocity"] = (-speed * len(a) / (len(a) + len(b)), 0.0, 0.0)
        return np.concatenate([a, b]), (0.0, 4.0, 22.0), (0.0, 0.0, 0.0)
    raise ValueError(scene)


def parse_args():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--scene", choices=SCENES, default="fission")
    parser.add_argument("--speed", type=float, default=None,
                        help="incoming neutron speed (fission, chain) or the two nuclei's closing speed (fusion)")
    parser.add_argument("--reaction", choices=sorted(REACTIONS), default="dt",
                        help="fusion's nuclei: dt deuterium and tritium, dd two deuterium")
    parser.add_argument("--spacing", type=float, default=21.0, help="distance between the chain reaction's nuclei")
    parser.add_argument("--lattice", type=int, default=4, help="the chain reaction's nuclei a side of its cube")
    parser.add_argument("--duration", type=float, default=0.0, help="quit after this many seconds (0: run until closed)")
    parser.add_argument("--screenshot", metavar="PNG", help="save a frame at --duration, then quit")
    parser.add_argument("--width", type=int, default=1600)
    parser.add_argument("--height", type=int, default=900)
    return parser.parse_args()


def main():
    args = parse_args()
    sk.shaders.compile_dir(HERE / "shaders")
    engine = sk.Engine(title="Nuclear simulation", width=args.width, height=args.height,
                       pipeline_json_path=str(PIPELINE))
    state = {"scene": args.scene, "time_scale": 1.0, "paused": False, "frame": 0, "seconds": 0.0,
             "sequence": 0, "events": 0, "neutrons": 0, "last_report": time.perf_counter()}
    rng = np.random.default_rng(1)

    def set_params():
        p = SCENARIO
        for key, value in (("dCore", p.d_core), ("dUnlike", p.d_unlike), ("dProtons", p.d_protons),
                           ("dNeutrons", p.d_neutrons), ("a", p.a), ("r0", p.r0), ("kc", p.kc),
                           ("minDistance", p.min_distance), ("screenStart", p.screen_start),
                           ("screenEnd", p.screen_end), ("dt", 0.0), ("damping", 0.0), ("mass", p.mass),
                           ("radius", 0.5), ("glowEnergy", GLOW[state["scene"]])):
            engine.set_custom_float("nuclear." + key, value)
        engine.set_custom_uint("nuclear.isolate", 0)

    def load(scene):
        speed = args.speed if args.speed is not None else {"fission": 1.0, "chain": 1.0, "fusion": 0.5}[scene]
        nucleons, eye, target = build(scene, speed, rng, args.spacing, args.lattice, args.reaction)
        if len(nucleons) > CAPACITY:
            raise ValueError("%d nucleons; the pipeline holds %d" % (len(nucleons), CAPACITY))
        state.update(scene=scene, count=len(nucleons), tracker=ev.FissionTracker(), capture=ev.CaptureRule(),
                     reactions=ev.ReactionTracker(),
                     events=0, neutrons=0, captures=0, eye=eye, target=target, read_frame=-1, sim_time=0.0,
                     owed=0.0, frame_times={}, loaded_frame=state["frame"], frame_step=0.0)
        engine.set_buffer("Nucleons", nucleons.tobytes())
        engine.set_custom_uint("nuclear.count", len(nucleons))
        engine.scene.set_position(state["camera"], eye)
        engine.scene.set_rotation(state["camera"], look_rotation(eye, target))
        set_params()
        print("%s: %d nucleons" % (scene, len(nucleons)))

    def look():
        """Read the nucleons back every few frames; kick nuclei that split."""
        if state["frame"] % LOOK_EVERY == 0:
            engine.request_readback("Nucleons")
        result = engine.get_readback("Nucleons")
        if result is None or result["frame"] == state["read_frame"] or result["frame"] < state["loaded_frame"]:
            return
        state["read_frame"] = result["frame"]
        # The simulation time of the readback, and of the start of this
        # frame's steps, when kicks sent now are applied.
        read_time = state["frame_times"].get(result["frame"], state["sim_time"])
        land_time = state["sim_time"] - state["frame_step"]
        nucleons = np.frombuffer(result["data"], dtype=NUCLEON)[:state["count"]]
        fissions, labels = state["tracker"].update(nucleons["position"])
        if state["scene"] == "fusion":
            reaction = state["reactions"].update(nucleons, read_time, labels)
            if reaction:
                print("reaction: %s -> %s" % (" + ".join(reaction[0]), " + ".join(reaction[1])))
        kicks = np.zeros(CAPACITY, dtype=ev.KICK)
        kicks["scale"] = 1.0
        mine = kicks[:state["count"]]
        for fragments in fissions:
            _, emitted, radiated = ev.emission_kicks(nucleons, fragments, per_fragment=1, speed=2.5,
                                                     radiate=0.5, kicks=mine)
            state["events"] += 1
            state["neutrons"] += len(emitted)
            print("split: %s, %d neutrons given off, %.1f units radiated"
                  % (" + ".join(str(len(f)) for f in fragments), len(emitted), radiated))
        _, captures, stretched = state["capture"].update(nucleons, labels, read_time, now=land_time, kicks=mine)
        state["captures"] += captures
        _, turned = ev.steer_kicks(nucleons, labels, radius=CAPTURE_RADIUS, kicks=mine)
        if not fissions and not stretched and not turned:
            return
        engine.set_buffer("Kicks", kicks.tobytes())
        state["sequence"] += 1
        engine.set_custom_uint("nuclear.kickSequence", state["sequence"])

    def on_init():
        state["camera"] = engine.create_camera((0.0, 6.0, 32.0), fov=50.0, speed=12.0)
        engine.set_custom_uint("nuclear.kickSequence", 0)
        set_params()
        load(state["scene"])

    def on_key(key):
        scenes = {ord("1"): "fission", ord("2"): "chain", ord("3"): "fusion"}
        if key in scenes:
            load(scenes[key])
        elif key == sk.keys.R:
            load(state["scene"])
        elif key == sk.keys.SPACE:
            state["paused"] = not state["paused"]
        elif key in (sk.keys.EQUAL, sk.keys.KP_ADD):
            state["time_scale"] = min(state["time_scale"] * 1.5, 4.0)
        elif key in (sk.keys.MINUS, sk.keys.KP_SUBTRACT):
            state["time_scale"] = max(state["time_scale"] / 1.5, 0.1)
        set_params()

    def on_update(dt):
        state["frame"] += 1
        state["seconds"] += dt
        # Steps are always DT long: a frame runs its substeps once enough
        # time has gathered, and otherwise runs them with dt 0, which leaves
        # the nucleons as they are.
        if not state["paused"]:
            state["owed"] = min(state["owed"] + RATE * state["time_scale"] * dt, SUBSTEPS * DT)
        step = DT if state["owed"] >= SUBSTEPS * DT else 0.0
        state["owed"] -= step * SUBSTEPS
        engine.set_custom_float("nuclear.dt", step)
        state["frame_step"] = step * SUBSTEPS
        state["sim_time"] += state["frame_step"]
        # Readbacks name frames from 0: this is frame state["frame"] - 1.
        times = state["frame_times"]
        times[state["frame"] - 1] = state["sim_time"]
        times.pop(state["frame"] - 1 - 64, None)
        look()
        now = time.perf_counter()
        if now - state["last_report"] >= 2.0:
            print("%s, %.0f s (%.0f time units): %d captures, %d splits, %d neutrons given off, time scale %.2f%s"
                  % (state["scene"], state["seconds"], state["sim_time"], state["captures"], state["events"],
                     state["neutrons"], state["time_scale"],
                     ", paused" if state["paused"] else ""))
            state["last_report"] = now
        if args.duration and state["seconds"] >= args.duration:
            if args.screenshot and not state.get("captured"):
                engine.capture_screenshot(args.screenshot)
                state["captured"] = True
                print("saved", args.screenshot)
            else:
                engine.request_quit()

    engine.set_on_init(on_init)
    engine.set_on_key_pressed(on_key)
    engine.set_on_update(on_update)
    engine.run()


if __name__ == "__main__":
    main()
