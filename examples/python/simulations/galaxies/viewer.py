"""Watch two galaxies collide.

    python viewer.py
    python viewer.py --inclination 0 150        # the second disc turning against the orbit
    python viewer.py --duration 30 --screenshot merger.png

Keys: R restart, SPACE pause, + and - run faster or slower, C the
cinematic camera (a slow orbit around the galaxies' centre of mass), G
colour by galaxy or by age. Hold the right mouse button and use WASD, Q and
E to fly the camera.

The galaxies run on the GPU every frame (pipeline/, which repeats
shaders/step.comp, the same shader check.py runs): their centres, then a
million stars and gas particles in their potentials. The particles are
drawn as glowing points into an HDR image, bloomed and tone mapped. The
galaxies are read back every frame for the camera and the printed separation. The
model's orbital plane, xy, is drawn as the horizontal plane.
"""

import argparse
import math
import time

import numpy as np
import shoonyakasha as sk
from shoonyakasha.mathutil import look_rotation

import model as m

HERE = m.HERE
PIPELINE = HERE / "pipeline"
CAPACITY = 1 << 20           # "Particles" elementCount in pipeline/data.json
STEPS = 8                    # steps a frame: Step{s} passes in pipeline/passes/10_sim.json, three a step
SFR_WINDOW = 50.0            # Myr over which the star formation rate is averaged
RATE = 8.0                   # simulation time units a second at time scale 1 (38 Myr)
MAX_DT = 0.05                # the longest step; frames below RATE / (STEPS * MAX_DT) fps run slower
ORBIT_SPEED = 0.05           # the cinematic camera's turn, radians a second

# Model coordinates to world coordinates: the orbital plane (model xy) becomes world xz, y up.
TO_WORLD = np.array([[1.0, 0.0, 0.0], [0.0, 0.0, -1.0], [0.0, 1.0, 0.0]])


def to_world(scene):
    """The scene's particles and galaxy records turned into world coordinates."""
    particles = scene.particles.copy()
    particles["position"] = particles["position"] @ TO_WORLD
    particles["velocity"] = particles["velocity"] @ TO_WORLD
    records = scene.records.copy()
    for field in ("position", "previous", "velocity", "spin"):
        records[field] = records[field] @ TO_WORLD
    return particles, records


def parse_args():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--count", type=int, default=CAPACITY, help="stars and gas particles, at most %d" % CAPACITY)
    parser.add_argument("--inclination", type=float, nargs=2, default=(0.0, 30.0), metavar=("FIRST", "SECOND"),
                        help="each disc's tilt from the orbit's axis, degrees; above 90 it turns against the orbit")
    parser.add_argument("--ratio", type=float, default=1.0, help="the second galaxy's mass over the first's")
    parser.add_argument("--pericentre", type=float, default=10.0, help="closest approach of the first pass, kpc")
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
    params = m.Params()
    state = {"time_scale": 1.0, "paused": False, "seconds": 0.0, "cinematic": args.cinematic, "color_mode": 0,
             "orbit_angle": 0.0, "last_report": time.perf_counter(), "frames": 0, "frame": 0}
    display = {"pointSize": 2.0, "brightness": 0.07, "exposure": 0.8, "bloom": 0.25}

    def set_params():
        engine.set_custom_float("galaxy.softening", params.softening)
        engine.set_custom_float("galaxy.logLambda", params.log_lambda)
        engine.set_custom_float("galaxy.gasRelax", params.gas_relax)
        engine.set_custom_float("galaxy.sfDensity", params.sf_density)
        engine.set_custom_float("galaxy.sfEfficiency", params.sf_efficiency)
        for key, value in display.items():
            engine.set_custom_float("galaxy." + key, value)
        engine.set_custom_uint("galaxy.colorMode", state["color_mode"])

    def load():
        scene = m.merger(count=args.count, inclination=args.inclination, ratio=args.ratio,
                         pericentre=args.pericentre)
        particles, records = to_world(scene)
        engine.set_buffer("Particles", particles.tobytes())
        engine.set_buffer("Galaxies", records.tobytes())
        engine.set_buffer("Stats", bytes(16))
        engine.set_custom_uint("galaxy.count", len(particles))
        engine.set_custom_uint("galaxy.galaxyCount", len(scene.galaxies))
        engine.set_custom_float("galaxy.gasMass", scene.gas_mass)
        state.update(galaxies=scene.galaxies, records=records, sim_time=0.0, owed=0.0, gas_mass=scene.gas_mass,
                     formed=0, history=[(0.0, 0)], frame_times={}, loaded_frame=state["frame"])
        eye = np.array([0.0, 70.0, 110.0])
        engine.scene.set_position(state["camera"], eye)
        engine.scene.set_rotation(state["camera"], look_rotation(eye, np.zeros(3)))
        set_params()
        print("merger: %d particles, inclinations %s, mass ratio %g, pericentre %g kpc"
              % (len(particles), tuple(args.inclination), args.ratio, args.pericentre))

    def centre_of_mass():
        records = state["records"][:len(state["galaxies"])]
        masses = records["mass"].astype(np.float64)
        return (records["position"] * masses[:, None]).sum(0) / masses.sum()

    def fresh(result):
        """Whether a readback was taken since the scene was loaded."""
        return result is not None and result["frame"] > state["loaded_frame"]

    def look():
        result = engine.get_readback("Galaxies")
        if fresh(result):
            state["records"] = np.frombuffer(result["data"], dtype=m.GALAXY).copy()
        result = engine.get_readback("Stats")
        if fresh(result) and result["frame"] in state["frame_times"]:
            state["formed"] = int(np.frombuffer(result["data"], dtype=np.uint32)[0])
            history = state["history"]
            history.append((state["frame_times"][result["frame"]], state["formed"]))
            while len(history) > 2 and (history[-1][0] - history[1][0]) * m.TIME_MYR >= SFR_WINDOW:
                history.pop(0)

    def star_formation_rate():
        """Solar masses a year over the last SFR_WINDOW Myr."""
        (t0, n0), (t1, n1) = state["history"][0], state["history"][-1]
        if t1 <= t0:
            return 0.0
        return (n1 - n0) * state["gas_mass"] * 1e10 / ((t1 - t0) * m.TIME_MYR * 1e6)

    def fly_cinematic(dt):
        state["orbit_angle"] += ORBIT_SPEED * dt
        centre = centre_of_mass()
        a = state["orbit_angle"]
        eye = centre + np.array([130.0 * math.sin(a), 55.0, 130.0 * math.cos(a)])
        engine.scene.set_position(state["camera"], eye)
        engine.scene.set_rotation(state["camera"], look_rotation(eye, centre))

    def on_init():
        state["camera"] = engine.create_camera((0.0, 70.0, 110.0), fov=50.0, speed=30.0)
        load()

    def on_key(key):
        if key == sk.keys.R:
            load()
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
        now = time.perf_counter()
        if now - state["last_report"] >= 2.0:
            r = state["records"]
            separation = float(np.linalg.norm(r["position"][0] - r["position"][1]))
            print("%.0f Myr: separation %.1f kpc, %d stars formed, %.1f solar masses a year, time scale %.2f, "
                  "%.0f fps%s"
                  % (state["sim_time"] * m.TIME_MYR, separation, state["formed"], star_formation_rate(),
                     state["time_scale"], state["frames"] / (now - state["last_report"]),
                     ", paused" if state["paused"] else ""))
            state["last_report"] = now
            state["frames"] = 0
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
