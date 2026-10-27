"""Watch galaxies collide.

    python viewer.py                            # two equal disc galaxies merge
    python viewer.py --inclination 0 150        # the second disc turning against the orbit
    python viewer.py --scene ring               # a companion falls through a disc: a ring galaxy
    python viewer.py --scene replay             # the merger with every particle massive (precompute_replay.py)
    python viewer.py --duration 30 --screenshot merger.png

Keys: 1 the merger, 2 the ring galaxy, 3 the self-gravity replay, R
restart, SPACE pause, + and - run faster or slower, C the cinematic camera
(a slow orbit around the centre of the galaxies near the heaviest,
model.grid_centre, or the origin in the replay), G colour by galaxy or by
age, F1 settings, F2 the controls, P a screenshot into galaxy_captures/, H
the explanation cards, [ and ] the previous and next card. Hold the right
mouse button and use WASD, Q and E to fly the camera. The screen
(interface.py) shows readings, the scene's explanation cards (story.py)
and key hints.

The galaxies run on the GPU every frame (pipeline/, which repeats
shaders/step.comp, the same shader check.py runs): their centres, then a
million stars and gas particles in their potentials. The particles are
drawn as glowing points into an HDR image, bloomed and tone mapped. The
galaxy records and the count of stars formed are read back every frame for
the camera and the readings. The model's xy plane (the merger's orbital
plane, the ring target's disc) is drawn as the horizontal plane.

The replay (replay.py) plays recordings/merger.npy, made by
precompute_replay.py, into the pipeline buffer "Replay" with
sk.streams.play, and draws it with the ReplayStars pass in place of the
simulation and the Stars pass.
"""

import argparse
import math
import time

import numpy as np
import shoonyakasha as sk
from shoonyakasha.mathutil import look_rotation

import model as m
from interface import Interface
from replay import Replay

HERE = m.HERE
PIPELINE = HERE / "pipeline"
CAPACITY = 1 << 20           # "Particles" elementCount in pipeline/data.json
STEPS = 8                    # steps a frame: Step{s} passes in pipeline/passes/10_sim.json, three a step
SFR_WINDOW = 50.0            # Myr over which the star formation rate is averaged
RATE = 8.0                   # simulation time units a second at time scale 1 (38 Myr)
MAX_DT = 0.05                # the longest step; frames below RATE / (STEPS * MAX_DT) fps run slower
ORBIT_SPEED = 0.05           # the cinematic camera's turn, radians a second
MERGED_KPC = 1.0             # centres closer than this for MERGED_MYR count as merged
MERGED_MYR = 30.0
SCENES = ("merger", "ring", "replay")
EYES = {"merger": (0.0, 70.0, 110.0), "ring": (0.0, 32.0, 58.0), "replay": (0.0, 55.0, 85.0)}
ORBITS = {"merger": (130.0, 55.0), "ring": (85.0, 40.0), "replay": (100.0, 45.0)}  # cinematic radius, height
RECORDING = HERE / "recordings" / "merger"
REPLAY_BRIGHTNESS = 10.0     # the replay's 65,536 stars are drawn this much brighter than the million


def to_world(scene):
    """The scene's particles and galaxy records turned into world coordinates."""
    particles = scene.particles.copy()
    particles["position"] = particles["position"] @ m.TO_WORLD
    particles["velocity"] = particles["velocity"] @ m.TO_WORLD
    records = scene.records.copy()
    for field in ("position", "previous", "velocity", "spin"):
        records[field] = records[field] @ m.TO_WORLD
    return particles, records


def parse_args():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--scene", choices=SCENES, default="merger")
    parser.add_argument("--count", type=int, default=CAPACITY, help="stars and gas particles, at most %d" % CAPACITY)
    parser.add_argument("--inclination", type=float, nargs=2, default=(0.0, 30.0), metavar=("FIRST", "SECOND"),
                        help="merger: each disc's tilt from the orbit's axis, degrees; above 90 it turns "
                             "against the orbit")
    parser.add_argument("--ratio", type=float, default=1.0, help="merger: the second galaxy's mass over the first's")
    parser.add_argument("--pericentre", type=float, default=10.0, help="merger: closest approach of the first pass, kpc")
    parser.add_argument("--cinematic", action="store_true", help="start with the cinematic camera")
    parser.add_argument("--duration", type=float, default=0.0, help="quit after this many seconds (0: run until closed)")
    parser.add_argument("--screenshot", metavar="PNG", help="save a frame at --duration, then quit")
    parser.add_argument("--width", type=int, default=1600)
    parser.add_argument("--height", type=int, default=900)
    return parser.parse_args()


def main():
    args = parse_args()
    if not 0 < args.count <= CAPACITY:
        raise SystemExit("--count must be from 1 to %d" % CAPACITY)
    sk.shaders.compile_dir(HERE / "shaders")
    engine = sk.Engine(title="Galaxy collision", width=args.width, height=args.height,
                       pipeline_json_path=str(PIPELINE))
    defaults = m.Params()
    # How the merger is set up (on restart), the rules' constants and what is
    # drawn (at once); the settings menu (F1) changes them.
    settings = {"tilt": args.inclination[1], "ratio": args.ratio, "pericentre": args.pericentre,
                "log_lambda": defaults.log_lambda, "gas_relax": defaults.gas_relax,
                "sf_density": defaults.sf_density, "sf_efficiency": defaults.sf_efficiency,
                "sf_compression": defaults.sf_compression,
                "explanations": True, "pause_on_cards": not args.duration, "readings": True, "key_hints": True,
                "exposure": 0.8, "bloom": 0.25, "brightness": 0.07, "point_size": 2.0}
    state = {"scene": args.scene, "time_scale": 1.0, "paused": False, "seconds": 0.0, "cinematic": args.cinematic,
             "color_mode": 0, "orbit_angle": 0.0, "last_report": time.perf_counter(), "frames": 0, "frame": 0}
    replay = Replay(engine, RECORDING)

    def set_params():
        params = defaults.with_(log_lambda=settings["log_lambda"], gas_relax=settings["gas_relax"],
                                sf_density=settings["sf_density"], sf_efficiency=settings["sf_efficiency"],
                                sf_compression=settings["sf_compression"])
        engine.set_custom_float("galaxy.softening", params.softening)
        engine.set_custom_float("galaxy.logLambda", params.log_lambda)
        engine.set_custom_float("galaxy.gasRelax", params.gas_relax)
        engine.set_custom_float("galaxy.sfDensity", params.sf_density)
        engine.set_custom_float("galaxy.sfEfficiency", params.sf_efficiency)
        engine.set_custom_float("galaxy.sfCompression", params.sf_compression)
        brightness = settings["brightness"] * (REPLAY_BRIGHTNESS if state["scene"] == "replay" else 1.0)
        for key, value in (("pointSize", settings["point_size"]), ("brightness", brightness),
                           ("exposure", settings["exposure"]), ("bloom", settings["bloom"])):
            engine.set_custom_float("galaxy." + key, value)
        engine.set_custom_uint("galaxy.colorMode", state["color_mode"])

    def build(name):
        if name == "ring":
            return m.ring(count=args.count)
        return m.merger(count=args.count, inclination=(args.inclination[0], settings["tilt"]),
                        ratio=settings["ratio"], pericentre=settings["pericentre"])

    def load(name):
        if name == "replay":
            load_replay()
            return
        replay.stop()
        engine.set_pass_enabled("Step{s}", True)
        engine.set_pass_enabled("Stars", True)
        engine.set_pass_enabled("ReplayStars", False)
        scene = build(name)
        particles, records = to_world(scene)
        engine.set_buffer("Particles", particles.tobytes())
        engine.set_buffer("Galaxies", records.tobytes())
        engine.set_buffer("Stats", bytes(16))
        engine.set_custom_uint("galaxy.count", len(particles))
        engine.set_custom_uint("galaxy.galaxyCount", len(scene.galaxies))
        engine.set_custom_float("galaxy.gasMass", scene.gas_mass)
        separation = float(np.linalg.norm(records["position"][1] - records["position"][0]))
        state.update(scene=name, galaxies=scene.galaxies, records=records, sim_time=0.0, owed=0.0,
                     gas_mass=scene.gas_mass, formed=0, history=[(0.0, 0)], frame_times={},
                     loaded_frame=state["frame"], followed=records["position"][0].astype(np.float64),
                     count=len(particles), separation=separation, closest=separation, closing=0.0, passes=0,
                     pass_myr=[],
                     apocentres=0, approaching=True, merged=False, merged_since=None, sfr=0.0, formed_mass=0.0)
        eye = np.array(EYES[name])
        target = records["position"][0] if name == "ring" else m.grid_centre(records, len(scene.galaxies))
        engine.scene.set_position(state["camera"], eye)
        engine.scene.set_rotation(state["camera"], look_rotation(eye, target))
        set_params()
        state["interface"].new_scene(name)
        if name == "ring":
            print("ring: %d particles, a companion of 1/5 the mass falling through the disc" % len(particles))
        else:
            print("merger: %d particles, inclinations %s, mass ratio %g, pericentre %g kpc"
                  % (len(particles), (args.inclination[0], settings["tilt"]), settings["ratio"],
                     settings["pericentre"]))

    def load_replay():
        if not replay.available:
            print("replay: no recording at %s.npy; make it with python precompute_replay.py" % RECORDING)
            if "galaxies" not in state:
                load("merger")
            return
        replay.stop()
        meta = replay.start()
        engine.set_pass_enabled("Step{s}", False)
        engine.set_pass_enabled("Stars", False)
        engine.set_pass_enabled("ReplayStars", True)
        engine.set_custom_uint("galaxy.replayCount", replay.count)
        state.update(scene="replay", sim_time=0.0, loaded_frame=state["frame"], replay=meta)
        eye = np.array(EYES["replay"])
        engine.scene.set_position(state["camera"], eye)
        engine.scene.set_rotation(state["camera"], look_rotation(eye, np.zeros(3)))
        set_params()
        state["interface"].new_scene("replay")
        print("replay: %d bodies, %d stars recorded, %d frames %.1f Myr apart; pericentres %s, merged at %s Myr"
              % (meta["bodies"], meta["stars"], meta["frames"], meta["frame_myr"],
                 [(p["myr"], p["kpc"]) for p in meta["pericentres"]], meta["merged_myr"]))

    def fresh(result):
        """Whether a readback was taken since the scene was loaded."""
        return result is not None and result["frame"] > state["loaded_frame"]

    def look():
        result = engine.get_readback("Galaxies")
        if fresh(result):
            state["records"] = np.frombuffer(result["data"], dtype=m.GALAXY).copy()
            follow_passes()
            if state["scene"] == "ring" and not state["cinematic"]:
                follow_target()
        result = engine.get_readback("Stats")
        if fresh(result) and result["frame"] in state["frame_times"]:
            state["formed"] = int(np.frombuffer(result["data"], dtype=np.uint32)[0])
            history = state["history"]
            history.append((state["frame_times"][result["frame"]], state["formed"]))
            while len(history) > 2 and (history[-1][0] - history[1][0]) * m.TIME_MYR >= SFR_WINDOW:
                history.pop(0)
        state["sfr"] = star_formation_rate()
        state["formed_mass"] = state["formed"] * state["gas_mass"] * 1e10

    def follow_passes():
        """The separation (and the least so far) and closing speed of the first two galaxies, and from its change the
        passes (closest approaches), apocentres (farthest) and whether they have merged."""
        r = state["records"]
        d = (r["position"][1] - r["position"][0]).astype(np.float64)
        v = (r["velocity"][1] - r["velocity"][0]).astype(np.float64)
        separation = float(np.linalg.norm(d))
        state["closing"] = -float(d @ v) / max(separation, 1e-6) * m.VELOCITY_KMS
        myr = state["sim_time"] * m.TIME_MYR
        growing = separation > state["separation"]
        if state["approaching"] and growing and separation > MERGED_KPC:
            state["passes"] += 1
            state["pass_myr"].append(myr)
        elif not state["approaching"] and not growing and state["passes"] >= 1:
            state["apocentres"] += 1
        if separation != state["separation"]:
            state["approaching"] = not growing
        state["separation"] = separation
        state["closest"] = min(state["closest"], separation)
        if separation < MERGED_KPC:
            if state["merged_since"] is None:
                state["merged_since"] = myr
            state["merged"] = state["merged"] or myr - state["merged_since"] >= MERGED_MYR
        else:
            state["merged_since"] = None

    def star_formation_rate():
        """Solar masses a year over the last SFR_WINDOW Myr."""
        (t0, n0), (t1, n1) = state["history"][0], state["history"][-1]
        if t1 <= t0:
            return 0.0
        return (n1 - n0) * state["gas_mass"] * 1e10 / ((t1 - t0) * m.TIME_MYR * 1e6)

    def follow_target():
        """Move the camera with the ring scene's target galaxy, so it stays in view as it drifts."""
        target = state["records"]["position"][0].astype(np.float64)
        eye = np.array(engine.scene.get_position(state["camera"])) + target - state["followed"]
        engine.scene.set_position(state["camera"], eye)
        state["followed"] = target

    def fly_cinematic(dt):
        state["orbit_angle"] += ORBIT_SPEED * dt
        centre = np.zeros(3) if state["scene"] == "replay" else m.grid_centre(state["records"], len(state["galaxies"]))
        a = state["orbit_angle"]
        radius, height = ORBITS[state["scene"]]
        eye = centre + np.array([radius * math.sin(a), height, radius * math.cos(a)])
        engine.scene.set_position(state["camera"], eye)
        engine.scene.set_rotation(state["camera"], look_rotation(eye, centre))

    def on_init():
        state["camera"] = engine.create_camera(EYES[state["scene"]], fov=50.0, speed=30.0)
        state["interface"] = Interface(engine, state, settings, load=load, restart=lambda: load(state["scene"]),
                                       apply=set_params)
        load(state["scene"])

    def on_key(key):
        if state["interface"].key(key):
            set_params()
            return
        scenes = {ord("1"): "merger", ord("2"): "ring", ord("3"): "replay"}
        if key in scenes:
            load(scenes[key])
        elif key == sk.keys.R:
            load(state["scene"])
        elif key == sk.keys.SPACE:
            state["paused"] = not state["paused"]
        elif key == sk.keys.C:
            state["cinematic"] = not state["cinematic"]
        elif key == sk.keys.G:
            state["color_mode"] = 1 - state["color_mode"]
        elif key in (sk.keys.EQUAL, sk.keys.KP_ADD):
            state["time_scale"] = min(state["time_scale"] * 1.5, 4.0)
        elif key in (sk.keys.MINUS, sk.keys.KP_SUBTRACT):
            state["time_scale"] = max(state["time_scale"] / 1.5, 0.1)
        set_params()

    def on_update(dt):
        state["seconds"] += dt
        state["frames"] += 1
        state["frame"] += 1
        if state["scene"] == "replay":
            update_replay(dt)
            return
        # The frame's steps share the simulation time owed, each at most MAX_DT.
        if not state["paused"]:
            state["owed"] = min(state["owed"] + RATE * state["time_scale"] * dt, STEPS * MAX_DT)
        step = min(state["owed"] / STEPS, MAX_DT)
        state["owed"] -= step * STEPS
        engine.set_custom_float("galaxy.dt", step)
        engine.set_custom_float("galaxy.time", state["sim_time"])
        state["sim_time"] += step * STEPS
        # Readbacks name frames from 0: this is frame state["frame"] - 1.
        times = state["frame_times"]
        times[state["frame"] - 1] = state["sim_time"]
        times.pop(state["frame"] - 1 - 64, None)
        look()
        if state["cinematic"]:
            fly_cinematic(dt)
        state["interface"].update(dt)
        now = time.perf_counter()
        if now - state["last_report"] >= 2.0:
            print("%s, %.0f Myr: separation %.1f kpc, %d passes%s, %d stars formed, %.1f solar masses a year, "
                  "time scale %.2f, %.0f fps%s"
                  % (state["scene"], state["sim_time"] * m.TIME_MYR, state["separation"], state["passes"],
                     ", merged" if state["merged"] else "", state["formed"], state["sfr"], state["time_scale"],
                     state["frames"] / (now - state["last_report"]), ", paused" if state["paused"] else ""))
            state["last_report"] = now
            state["frames"] = 0
        finish()

    def update_replay(dt):
        speed = 0.0 if state["paused"] else RATE * state["time_scale"]
        state["sim_time"], ahead = replay.update(dt, speed)
        engine.set_custom_float("galaxy.time", state["sim_time"])
        engine.set_custom_float("galaxy.replayAhead", ahead)
        if state["cinematic"]:
            fly_cinematic(dt)
        state["interface"].update(dt)
        now = time.perf_counter()
        if now - state["last_report"] >= 2.0:
            print("replay, %.0f Myr: time scale %.2f, %d frames played, %d late, %.0f fps%s"
                  % (state["sim_time"] * m.TIME_MYR, state["time_scale"], replay.player.played, replay.player.late,
                     state["frames"] / (now - state["last_report"]), ", paused" if state["paused"] else ""))
            state["last_report"] = now
            state["frames"] = 0
        finish()

    def finish():
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
