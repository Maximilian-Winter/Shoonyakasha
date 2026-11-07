# On Foot, Physics Props and the Gravity Gun — Implementation Guide

Next step for the Shoonyakasha planet example: get out of the ship, walk on the planet and the moon, pick things up with a gravity gun, and build a small resource/trade loop on top.

Builds on what exists: doubles everywhere with the planet's centre at the origin, `PlanetWorld` probes with latency, `GroundPatch`, `flight.py`'s gravity and `surface_motion`, the hash-based vegetation scatter, the cockpit drawn in camera space, and the `--*-check` self-tests.

API names for Jolt are from memory of its current design. Check them against the version you vendor; the structure matters more than the exact signatures.

---

## 0. Overview

| Piece | Approach |
|---|---|
| Rigid body physics | Jolt Physics in C++, double precision build, exposed through a small Python API |
| Coordinate frame | A per-body **bubble**: body-centred, non-rotating, translated by an origin offset; rebased by translation only |
| Gravity | Jolt's gravity off (gravity factor 0); `GM/r²` towards the body's centre applied per body each step |
| Terrain collision | Two height-field patches (fine and coarse) built from a dedicated probe pass, rebuilt as the player moves |
| Trees | Trunk capsules from a filtered readback of the vegetation scatter near the player |
| Player | Jolt `CharacterVirtual` with up = radial, walk/run/jump/jetpack, boarding the ship |
| Gravity gun | Ray grab, cone pull, PD hold (linear and angular), punt with recoil, charged punt |
| Props | Crates spawned by code; rocks promoted from the scatter when touched |
| Far objects | Leave the bubble → two-body ballistic integrator in doubles; return when near the player |
| Game loop | Ore field in the climate pass, scanning from orbit, carrying ore to the ship, fuel and cargo mass, outposts with prices |

### Mode state machine

```
           Tab (debug)                 F near ship, ship landed & slow
 camera  <------------->  foot  <------------------------------------>  ship
                                       F in ship, landed & slow
```

- `camera`: free camera as now (debug, presets).
- `foot`: the player controller, gravity gun, bubble physics active.
- `ship`: as now. The bubble stays alive while the ship is landed so props and the player's things keep simulating; it is frozen or dropped when the ship leaves (§3.4).

### New modules

```
examples/planet/
  player.py            # first-person controller on top of the physics API
  gravity_gun.py       # grab / hold / punt
  props.py             # crates, promoted rocks, ballistic far objects, persistence
  cargo.py             # cargo zone, ore, fuel and mass (later)
python/shoonyakasha/planet/
  bubble.py            # bubble frame, rebase, gravity per body, terrain collider requests
  collider_probes.py   # dedicated probe pass for collision patches
include/Physics/
  PhysicsWorld.h/.cpp  # Jolt wrapper bound to Python as sk.Physics
pipelines/planet/shaders/
  collider_probes.comp
  vegetation_nearby.comp
```

---

## 1. Jolt in the engine

### 1.1 Build

- Vendor Jolt (CMake subproject), build with `JPH_DOUBLE_PRECISION=ON`. Positions become `RVec3` (doubles); velocities, rotations and shapes stay float.
- Fixed step: your flight already steps at 1/120 s. Step Jolt with the same step inside the same accumulator loop, so the ship, the player and props advance together.
- Collision layers (object layers → broad-phase layers):

| Layer | Contents | Collides with |
|---|---|---|
| `STATIC` | terrain patches, trunks, outpost geometry, rocks not yet promoted | `DYNAMIC`, `CHARACTER`, `KINEMATIC` |
| `DYNAMIC` | crates, promoted rocks, ore | everything |
| `KINEMATIC` | the landed ship | `DYNAMIC`, `CHARACTER` |
| `SENSOR` | cargo zone, outpost trade zone | `DYNAMIC` |

### 1.2 Python API surface (keep it small)

```python
phys = engine.physics                      # sk.Physics, one per engine

# Bodies
bid = phys.create_box(half_extents, position, rotation, motion="dynamic", layer="dynamic",
                      mass=None, density=600.0, friction=0.7, restitution=0.1)
bid = phys.create_sphere(radius, position, motion="dynamic", ...)
bid = phys.create_capsule(half_height, radius, position, rotation, motion="static", layer="static")
bid = phys.create_convex_hull(points, position, rotation, motion="dynamic", ...)
bid = phys.create_height_field(samples, size, spacing, origin, rotation)   # terrain patch
phys.set_shape(bid, ...)                   # replace a patch's shape in place
phys.remove(bid)

# State (positions in bubble coordinates, doubles)
phys.get_transform(bid)  -> (position, quaternion)
phys.set_transform(bid, position, quaternion, activate=True)
phys.get_velocity(bid)   -> (linear, angular)
phys.set_velocity(bid, linear, angular)
phys.add_force(bid, force)  ; phys.add_torque(bid, torque) ; phys.add_impulse(bid, impulse, point=None)
phys.set_motion_type(bid, "static" | "kinematic" | "dynamic")
phys.move_kinematic(bid, position, quaternion, dt)
phys.mass(bid) ; phys.inertia_world(bid) ; phys.is_active(bid)

# Queries
hit = phys.cast_ray(origin, direction, max_distance, layers=("dynamic", "static"))
#    -> (bid, point, normal, fraction) or None
bids = phys.overlap_sphere(centre, radius, layers=("dynamic",))
bids = phys.sensor_contents(sensor_bid)

# Character
cid = phys.create_character(radius=0.3, height=1.8, mass=80.0, max_slope_deg=50.0)
phys.character_set_up(cid, up)
phys.character_set_velocity(cid, v) ; phys.character_velocity(cid)
phys.character_position(cid) ; phys.character_set_position(cid, p)
phys.character_update(cid, dt, gravity)          # ExtendedUpdate (stick to floor, walk stairs)
phys.character_ground(cid) -> (state, normal, ground_velocity, ground_body)

# Step
phys.step(dt)                                     # one fixed step
phys.shift_origin(offset)                         # rebase: subtract offset from all body positions
```

Batch transforms for rendering into one call returning a NumPy array (`phys.get_transforms(bids)`), like your chunk readbacks, so a few hundred props don't cost hundreds of Python calls.

On the C++ side `shift_origin` loops over all bodies (`BodyInterface::SetPosition` with `EActivation::DontActivate`) and characters. With double precision this is cheap and only needed rarely (§2.2).

---

## 2. The bubble frame

### 2.1 Definition

- Each bubble belongs to one body (planet or moon): `bubble.body`.
- **Bubble coordinates** = position relative to the body's centre, minus `bubble.origin` (a `dvec3` near the player).
- Axes are the body's axes: **no rotation**. Rebasing is a pure translation, so velocities and orientations never change on rebase. Your bodies don't spin, so the body frame is the natural frame.
- World (planet-centred) position of anything in the bubble: `body.centre() + bubble.origin + p`.

Why bother with a bubble when Jolt runs in doubles? Jolt's broad phase and the height field are happiest near the origin, contact caching stays well-conditioned, and it gives one clear place where "the world of physics" starts and ends (the ballistic hand-off in §8).

```python
class Bubble:
    REBASE_DISTANCE = 500.0          # m from the origin before rebasing
    RADIUS = 2_000.0                 # m: beyond this, bodies leave the bubble (§8)

    def __init__(self, phys, body, around):
        self.phys = phys
        self.body = body             # shoonyakasha.planet Body (planet or moon)
        self.origin = around         # dvec3, body-relative

    def to_bubble(self, world_point):
        return sub(self.body.relative(world_point), self.origin)

    def to_world(self, p):
        return add(self.body.centre(), add(self.origin, p))

    def maybe_rebase(self, player_p, holding=None):
        if length(player_p) < self.REBASE_DISTANCE:
            return
        shift = player_p                         # move the origin to the player
        self.phys.shift_origin(shift)
        self.origin = add(self.origin, shift)
        if holding is not None:
            holding.shift(shift)                 # the gun's cached hold target, if any
```

### 2.2 When to rebase

- Rebase between steps, never inside one, and only when the player is more than `REBASE_DISTANCE` from the origin.
- All state that caches bubble coordinates (gun hold target, terrain patch centres, trunk positions, cargo zone) is shifted in the same call. Keep a list of "listeners" if it grows.

### 2.3 Gravity and the moving moon

Jolt gravity is set to zero (`PhysicsSystem::SetGravity(Vec3::sZero())`), and every dynamic body gets gravity as a force each step:

```python
def apply_gravity(bubble, bodies):
    gm = bubble.body.gm
    for bid in bodies:                        # active dynamic bodies only (phys.active_dynamic())
        p = add(bubble.origin, bubble.phys.position(bid))      # body-relative, doubles
        r2 = dot(p, p)
        g = scale(p, -gm / (r2 * math.sqrt(r2)))
        bubble.phys.add_force(bid, scale(g, bubble.phys.mass(bid)))
```

Do this in C++ for many bodies (`phys.apply_central_gravity(centre_in_bubble, gm)` iterating active bodies). The direction varies by 0.6° per kilometre on a 100 km planet, which you will actually see when a thrown crate flies far.

**On the moon** the bubble is moon-centred, so it accelerates with the moon (0.14 m/s² towards the planet). In that frame the planet's pull on a crate minus the frame's acceleration is only the planet's tide, which over a bubble of 2 km is negligible. So in the moon bubble: gravity = the moon's `GM/r²` alone, exactly like your surface handling for the ship. A crate at rest on the moon stays at rest without any special code.

Optionally add the tide (`planet_gravity(p) − planet_gravity(moon_centre)`) if you want long ballistic flights to match `flight.py` exactly.

### 2.4 The ship in the bubble

- While the ship is landed (or near the ground within the bubble), it gets a `KINEMATIC` body: a convex hull of `ShipParams.hull` + `gear` points (or a hand-made simplified hull of the F-117), moved each step with `move_kinematic` to the ship's pose from `flight.py`.
- The player can walk around it and crates can rest on its wings. Crates on the wing when it takes off will slide off, which is correct and funny.
- When the ship climbs out of the bubble's radius, remove its kinematic body. When the player is in the ship and the ship leaves, the bubble's dynamic bodies are either frozen (stored as records, §9.4) or dropped.

---

## 3. Terrain collision from probes

### 3.1 Two patches

| Patch | Grid | Spacing | Size | Use |
|---|---|---|---|---|
| Fine | 65 × 65 | 0.5 m | 32 m | the player, held and nearby props |
| Coarse | 65 × 65 | 4 m | 256 m | props thrown or rolling further |

Both are Jolt height fields (or mesh shapes, see below) in the tangent plane at their centre direction, like `GroundPatch`. A body beyond the coarse patch is either asleep (fine) or handed to the ballistic integrator (§8) once it leaves the bubble radius; while it's inside the bubble but outside both patches, keep it frozen (`set_motion_type static` until the patch reaches it) rather than letting it fall through the planet.

**Height field vs mesh:** Jolt's height field shape expects a regular grid along its local axes, with heights along its local up. The tangent-plane grid works if you store heights measured along the patch's up (not along each point's radial direction). Over 256 m on a 100 km planet the radial directions differ by 0.15°, which is an error of a few cm at the patch's edge. If that bothers you, use a `MeshShape` built from the actual 3D points (your `GroundPatch` already computes them). Rebuilding a 64 × 64 × 2-triangle mesh shape takes well under a millisecond.

### 3.2 A dedicated probe pass

The existing 176-probe workgroup is too small. Add `ColliderProbes`, a compute pass after `BuildChunks`:

```glsl
// collider_probes.comp — one invocation per probe
layout(local_size_x = 64) in;

layout(std430, binding = 0) readonly  buffer Requests { vec4  dirs[];  };  // xyz direction (double-derived), w = body
layout(std430, binding = 1) writeonly buffer Results  { float heights[]; };

layout(push_constant) uniform P { uint count; uint stamp; } pc;

void main() {
    uint i = gl_GlobalInvocationID.x;
    if (i >= pc.count) return;
    vec3 d = dirs[i].xyz;
    float h = (uint(dirs[i].w) == 0u) ? terrainHeight(d) : moonHeight(d);
    heights[i] = h;
}
```

- Request: 2 × 65² = 8,450 directions, written by Python from doubles (direction precision: cast the normalised double direction to float. At 0.5 m spacing on a 100 km radius that's 5e-6 rad, about 60× above float's resolution near 1.0, fine).
- Read back with the same latency handling as your probes: a patch is requested with a stamp, and the shape is only replaced when the result for that stamp arrives. The old shape stays until then.
- Same terrain function as `chunk_build.comp`, so the true terrain (not the morphed mesh); see §3.4.

### 3.3 When to rebuild

```python
def needs_new_patch(patch, player_p, velocity, latency_s):
    ahead = add(player_p, scale(velocity, latency_s))
    off = length(sub(ahead, patch.centre_bubble))
    return off > 0.25 * patch.size
```

- Centre the request at the predicted position (player position + velocity × latency), as `ground_patch_request` already does for the ship.
- Coarse patch: centre at the player too, but also re-centre around a fast-moving tracked prop if you want to follow a thrown rock.

### 3.4 Collider vs drawn mesh

Physics uses the true terrain function at 0.5 m. The drawn mesh at `MAX_LEVEL` 12 has 2.4 m quads and is morphed. Your detail noise goes down to 10 m wavelengths, so the two can differ by tens of centimetres: crates will visibly hover or sink, and the player's feet will too.

Options, cheapest first:

1. **Deeper LOD near the player:** allow levels 13–14 (1.2 m / 0.6 m quads) within the split distance of a small radius around the player in `foot` mode only. The quadtree already supports it; it's the pool size and the morph's ancestor depth (up to 4) to check.
2. **Drop the finest detail octaves from the collider** to match what the drawn mesh can represent at level 12 (low-pass the physics). Feet then match the mesh, but small bumps aren't felt.
3. Displacement in the terrain's vertex/tessellation stage near the camera. More work.

Start with 1 and add a check (§12) that measures `collider height − drawn height` under the player.

---

## 4. Tree trunks and nearby vegetation

The scatter is deterministic per cell. Instead of mirroring it on the CPU, filter the GPU's result.

```glsl
// vegetation_nearby.comp — after VegetationScatter
// Input: the scatter's per-kind instance buffers (position relative to camera, scale, kind, cell id)
// Output: up to MAX_NEARBY instances within RADIUS of the player, appended with an atomic counter.
layout(std430, binding = 3) buffer Nearby {
    uint count;
    uint pad[3];
    Plant plants[];          // vec4 position_rel_camera + height, uvec4 (kind, face, cellX, cellY)
} nearby;
```

- Python reads `Nearby` back each frame (latency as usual), converts camera-relative positions to bubble coordinates (camera position in doubles is known for the frame that produced it, so keep a small ring buffer of `frame → camera`).
- Diff by cell id against the trunks already in Jolt: add static capsules for new ones, remove those out of range.
- Trunk radius and height per kind come from your mesh parameters (spruce trunk at its scale, etc.). Bushes and grass get no collider.
- Rocks (§9.2) come through the same buffer with their own kind.

Radius: 40 m is plenty for walking and props; the coarse patch area doesn't need trees.

---

## 5. First-person controller

### 5.1 State

```python
@dataclass
class Player:
    character: int                 # phys character id
    heading: tuple                 # unit tangent vector: where the player faces, horizontally
    pitch: float = 0.0             # radians, clamped to ±89°
    jet_fuel: float = 1.0          # 0..1
    crouching: bool = False
```

### 5.2 Look (parallel transport of the heading)

```python
def look(player, up, dx, dy):
    # Yaw around local up
    player.heading = rotate(player.heading, up, -dx * LOOK_RADIANS_PER_PIXEL)
    # Re-project onto the tangent plane every frame: up changes as the player walks
    h = sub(player.heading, scale(up, dot(player.heading, up)))
    player.heading = normalize(h) if length(h) > 1e-6 else local_frame(up)[1]
    player.pitch = clamp(player.pitch - dy * LOOK_RADIANS_PER_PIXEL, -1.55, 1.55)

def eye_basis(player, up):
    right = normalize(cross(player.heading, up))
    forward = add(scale(player.heading, math.cos(player.pitch)), scale(up, math.sin(player.pitch)))
    return right, cross(right, forward), forward
```

Re-projecting the heading each frame is the parallel transport: walking 25 km around the moon keeps the player facing "the same way" along the surface, no snapping.

### 5.3 Movement step

```python
WALK, RUN, CROUCH = 4.0, 8.0, 2.0         # m/s
ACCEL_GROUND, ACCEL_AIR = 40.0, 6.0       # m/s² towards the wanted horizontal velocity
JUMP_HEIGHT = 1.1                         # m on the planet; see note
JET_ACCEL, JET_BURN = 9.0, 0.25           # m/s², fuel per second
JET_REFILL = 0.15                         # per second on the ground

def step_player(player, phys, bubble, controls, dt):
    p = phys.character_position(player.character)
    rel = add(bubble.origin, p)                         # body-relative
    up = normalize(rel)
    g = scale(up, -bubble.body.gm / dot(rel, rel))
    phys.character_set_up(player.character, up)

    state, normal, ground_v, _ = phys.character_ground(player.character)
    on_ground = state == "on_ground"

    right = normalize(cross(player.heading, up))
    wish = add(scale(player.heading, controls.forward), scale(right, controls.strafe))
    if length(wish) > 1.0:
        wish = normalize(wish)
    speed = CROUCH if player.crouching else (RUN if controls.run else WALK)
    wanted_h = scale(wish, speed)

    v = phys.character_velocity(player.character)
    v_rel = sub(v, ground_v)                            # relative to what we stand on
    v_up = dot(v_rel, up)
    v_h = sub(v_rel, scale(up, v_up))

    accel = ACCEL_GROUND if on_ground else ACCEL_AIR
    dv = sub(wanted_h, v_h)
    max_dv = accel * dt
    if length(dv) > max_dv:
        dv = scale(normalize(dv), max_dv)
    v_h = add(v_h, dv)

    if on_ground:
        v_up = 0.0
        if controls.jump:
            v_up = math.sqrt(2.0 * length(g) * JUMP_HEIGHT)
        player.jet_fuel = min(1.0, player.jet_fuel + JET_REFILL * dt)
    else:
        v_up += -length(g) * dt
    if controls.jet and player.jet_fuel > 0.0:
        v_up += JET_ACCEL * dt
        player.jet_fuel -= JET_BURN * dt

    v = add(ground_v, add(v_h, scale(up, v_up)))
    phys.character_set_velocity(player.character, v)
    phys.character_update(player.character, dt, g)      # ExtendedUpdate: stick to floor, stairs
```

Notes:
- **Jump height** computed from the local gravity is the same 1.1 m on both bodies. For moon fun, make the jump *velocity* fixed instead (`v = sqrt(2 · 4.905 · 1.1) ≈ 3.3 m/s`) so it goes 5× higher on the moon. I'd do the latter.
- **Moving ground:** `ground_v` makes the player ride the landed ship or a moving platform. On the moon the bubble frame already moves with the moon, so the ground is at rest there.
- The character's rotation should align its capsule with `up`: Jolt's `CharacterVirtual` uses its up for slope and floor tests; also set the shape's rotation to the quaternion from (0,1,0) to `up` if your Jolt version doesn't do it from `SetUp`.
- **Fall damage** (optional): landing speed above ~12 m/s on the planet hurts; on the moon you rarely reach it.

### 5.4 Camera

- Eye at `character position + up × (EYE_HEIGHT − half capsule)`, e.g. 1.65 m above the feet, 1.0 m crouched.
- Smooth eye height over ~0.1 s after stepping up stairs/rocks so steps don't jolt the view.
- Head bob: a small vertical sine with walking distance (amplitude 2–3 cm), off when airborne. Make it a setting.
- Camera world position = `bubble.to_world(eye)`, which goes into your existing camera-relative pipeline unchanged.

### 5.5 Boarding and exiting

- **Board:** in `foot` mode, if the player is within 3 m of the ship's boarding point (beside the cockpit, from `model_to_ship`) and presses **F**, switch to `ship`. Remove the character from the bubble (or disable it).
- **Exit:** in `ship` mode with `ship.on_ground` and speed below 1 m/s, **F** places the character at the boarding point on the ship's left, then lets `character_update` settle it on the ground. Re-create the bubble around it if there is none.
- **EVA** (later): exiting in orbit puts the player in a bubble of their own with no terrain patch; the ship becomes a kinematic body in it. Jetpack becomes your only thruster. Fun, but after the rest works.

### 5.6 Mode transitions table

| From → To | What happens |
|---|---|
| ship → foot | Bubble created/kept around the ship; ship becomes kinematic body; character spawned at boarding point |
| foot → ship | Character disabled; bubble stays while ship is in it; ship's kinematic body follows flight.py |
| ship leaves bubble radius | Dynamic bodies frozen to records (§9.4); bubble dropped; terrain patch requests stop |
| foot → camera (Tab) | Physics keeps running; the player stands there |
| camera → foot (Tab) | Back to the player, not to the camera's position |

---

## 6. Viewmodel

Same approach as `cockpit.py`: a mesh in camera space, drawn with the camera's rotation and no translation.

- Position: lower right, e.g. `(0.22, −0.2, −0.45)` in camera space.
- **Sway:** offset by the last frame's look delta (low-pass filtered), max a few cm, so the gun lags behind fast turns.
- **Bob:** follow the head bob with a phase offset.
- **Recoil:** on punt, kick back 6 cm and up 3°, spring back over 0.25 s.
- **Glow:** emissive tip whose intensity follows the gun state (idle, can-grab (target under reticle), holding, charging). That alone sells the gun.
- **Beam:** while holding, a few camera-relative line segments (or a thin quad strip) from the tip to the held object, wobbling slightly. Draw it in the overlay pass or as an unlit additive mesh.

Generate it from boxes and beams with your `oriented_box`/`beam` helpers to start, model it later.

Render it in the same pass as the cockpit with its own depth clear (or depth range), so it never clips into walls or the ground.

---

## 7. The gravity gun

### 7.1 Parameters

```python
@dataclass
class GunParams:
    grab_range: float = 4.0          # m: grab directly
    pull_range: float = 18.0         # m: pull towards the player within the cone
    pull_cone_deg: float = 6.0
    pull_force: float = 6_000.0      # N, at most
    hold_distance: float = 2.2       # m in front of the eye (+ the object's radius)
    hold_force: float = 9_000.0      # N, at most, including holding against gravity
    hold_frequency: float = 6.0      # Hz, the hold spring's natural frequency
    hold_torque_gain: float = 12.0   # 1/s
    punt_impulse: float = 1_600.0    # N·s, at most
    punt_speed: float = 30.0         # m/s, at most from a punt
    charge_time: float = 1.5         # s to full charge
    charged_speed: float = 240.0     # m/s at full charge, for light objects (see 7.6)
    charged_mass: float = 30.0       # kg: full charged speed up to this mass
    recoil_factor: float = 0.6       # share of the punt's impulse back into the player
    drop_distance: float = 1.2       # m: drop if the object is this far off the hold point
```

### 7.2 Lifting limit by weight, not mass

The hold force budget `hold_force` must cover the object's weight plus its acceleration. So the gun can lift `hold_force / g`:

| Body | g (m/s²) | Liftable |
|---|---|---|
| Planet | 4.905 | ~1,800 kg (but sluggish above a few hundred) |
| Moon | 0.981 | ~9,000 kg |

Make it a feature: the same rock you can barely drag on the planet floats on the moon. Show "TOO HEAVY" when `mass × g > 0.9 × hold_force`.

### 7.3 Grab and pull

```python
def update_gun(gun, phys, eye, forward, controls, dt):
    if gun.held is None:
        hit = phys.cast_ray(eye, forward, gun.p.pull_range, layers=("dynamic", "static_rock"))
        gun.target = hit
        if hit and controls.secondary:                       # right mouse: grab / pull
            bid, point, _, fraction = hit
            if is_static_rock(bid):
                bid = promote_rock(bid)                      # §9.2
            distance = fraction * gun.p.pull_range
            if distance <= gun.p.grab_range:
                gun.grab(bid, point)
            else:
                pull(phys, bid, eye, gun.p.pull_force)
    ...
```

```python
def pull(phys, bid, eye, max_force):
    p, _ = phys.get_transform(bid)
    to_eye = sub(eye, p)
    d = length(to_eye)
    v, _ = phys.get_velocity(bid)
    approach = dot(v, scale(to_eye, 1.0 / d))
    wanted = min(12.0, d * 2.0)                               # m/s towards the player
    force = scale(to_eye, phys.mass(bid) * (wanted - approach) * 4.0 / d)
    if length(force) > max_force:
        force = scale(normalize(force), max_force)
    phys.add_force(bid, force)
```

Also cone-search with `overlap_sphere` along the ray when the ray misses, so pulling isn't pixel-hunting.

### 7.4 Hold (linear PD with gravity compensation)

```python
def hold(gun, phys, eye, forward, g, dt):
    bid = gun.held
    p, q = phys.get_transform(bid)
    v, w = phys.get_velocity(bid)
    m = phys.mass(bid)

    target = add(eye, scale(forward, gun.p.hold_distance + gun.held_radius))
    omega = 2.0 * math.pi * gun.p.hold_frequency
    k = m * omega * omega
    c = 2.0 * m * omega                                    # critically damped
    # The target moves with the player: damp the velocity relative to the target's velocity
    v_target = gun.player_velocity                         # good enough; or differentiate target
    force = add(scale(sub(target, p), k), scale(sub(v_target, v), c))
    force = sub(force, scale(g, m))                        # cancel gravity
    if length(force) > gun.p.hold_force:
        force = scale(normalize(force), gun.p.hold_force)
    phys.add_force(bid, force)

    if length(sub(target, p)) > gun.p.drop_distance:
        gun.drop()                                         # snagged on something

    # Angular: keep the orientation it had relative to the view when grabbed
    q_target = quat_multiply(view_quaternion(forward, up_of_view), gun.relative_rotation)
    q_err = quat_multiply(q_target, quat_conjugate(q))
    if q_err[0] < 0.0:
        q_err = tuple(-c for c in q_err)                   # shortest way
    axis_angle = scale(q_err[1:], 2.0)                     # small-angle: 2·vector part ≈ axis·angle
    w_wanted = scale(axis_angle, gun.p.hold_torque_gain)
    # Blend the angular velocity towards w_wanted; simpler and steadier than a torque PD
    phys.set_velocity(bid, phys.get_velocity(bid)[0], lerp(w, w_wanted, min(1.0, 10.0 * dt)))
```

Notes:
- **Rotating the held object:** while holding, **R** + mouse turns `relative_rotation` (like HL2's rotate). Useful for stacking.
- **Scroll wheel** moves `hold_distance` between 1.5 and 6 m.
- **Collisions while holding:** the held object stays a normal dynamic body, so it pushes others and gets stopped by walls. Don't let it collide with the player's capsule (set a collision group filter while held) or you can stand on what you hold and fly. (Or allow it for a hidden feature.)
- Wake the body each step (`activate=True`), or Jolt puts it to sleep while held still.

### 7.5 Punt

```python
def punt(gun, phys, player, forward, charge=0.0):
    bid = gun.held or (gun.target and gun.target[0])
    if bid is None:
        return
    m = phys.mass(bid)
    if charge > 0.0:
        speed = gun.p.charged_speed * charge * min(1.0, gun.p.charged_mass / m)
    else:
        speed = min(gun.p.punt_speed, gun.p.punt_impulse / m)
    impulse = scale(forward, m * speed)
    phys.add_impulse(bid, impulse)
    # Recoil, capped so heavy punts don't launch the player across the map
    back = scale(impulse, -gun.p.recoil_factor / PLAYER_MASS)
    if length(back) > 8.0:
        back = scale(normalize(back), 8.0)
    phys.character_set_velocity(player.character, add(phys.character_velocity(player.character), back))
    gun.drop()
    gun.kick()                                             # viewmodel recoil
```

- **Punting a static target** (the ground, a cliff, a trunk): no object moves, but the recoil applies with the impulse as if from a heavy object. That's the gravity-gun jump: look down, punt, fly. On the moon it reaches tens of metres.
- **Left mouse:** punt. **Hold left mouse** while holding something: charge, release to fire (§7.6).

### 7.6 Charged punt and the orbiting crate

Numbers for the moon (radius 25 km, surface gravity 0.981 m/s²):
- circular speed at the surface: √(gR) ≈ 157 m/s
- escape speed: √(2gR) ≈ 222 m/s

So a full charge of 240 m/s on a light object escapes the moon; around 160–170 m/s horizontally from a high crater rim gives an orbit that may clear the terrain. The rolling ground (400 m) and crater rims mean a surface-grazing orbit usually hits something, which is part of the fun: aim slightly up, or launch from the highest rim you can find.

On the planet (circular speed at the surface 700 m/s) the charged punt is just a very hard throw, and the reentry/atmosphere drag (§8) slows it.

Make it a deliberate toy: full charge only for objects under `charged_mass`, a charge sound/glow ramp, a 2 s cooldown.

---

## 8. Far objects: leaving and re-entering the bubble

A crate thrown at 160 m/s leaves a 2 km bubble in under 15 s. Keep it alive as a **ballistic object** in doubles, outside Jolt.

```python
@dataclass
class Ballistic:
    body: object                 # the body it's reckoned around
    position: tuple              # body-relative, doubles
    velocity: tuple              # body-relative
    orientation: tuple
    angular_velocity: tuple      # body frame
    kind: str                    # mesh / prop type
    mass: float
    half_extent: float           # for drawing and the ground test
    tracked: bool = False        # show its orbit path

def step_ballistic(obj, physics, dt):
    gm = obj.body.gm
    r2 = dot(obj.position, obj.position)
    g = scale(obj.position, -gm / (r2 * math.sqrt(r2)))
    drag = air_drag(physics, obj)                 # density(h) like flight.py, Cd·A from size
    obj.velocity = add(obj.velocity, scale(add(g, scale(drag, 1.0 / obj.mass)), dt))
    obj.position = add(obj.position, scale(obj.velocity, dt))
    # orientation: integrate the constant angular velocity (no torques)
```

- **Leaving:** each step, a dynamic body farther than `Bubble.RADIUS` from the origin (or above the coarse patch's reach and moving away) is removed from Jolt and becomes a `Ballistic` with the same state (`bubble.origin + p`, same velocity, orientation, angular velocity).
- **Sphere of influence:** use `flight.reference` to switch `obj.body` between planet and moon, converting velocities with the bodies' relative velocity, just like the ship.
- **Ground:** each ballistic object gets one probe per frame at its direction (extend the extra probes or use the collider probe pass with a few slots). When its height above the probed terrain is below `half_extent`, it impacts:
  - Near the player (inside the bubble radius): re-enter the bubble as a dynamic body a little above the ground and let Jolt handle the bounce.
  - Far away: it comes to rest. Store it as a **resting record** (§9.4) at that spot. Optionally spawn an impact effect if it's in view.
- **Re-entering:** a ballistic object that comes within the bubble radius of the player is converted back into a Jolt body. That's how the orbiting crate comes back around and lands at your feet.
- **Drawing:** instanced mesh, position relative to camera in doubles as usual. Give tracked objects a small screen-space marker (like the ship's beacon idea) and draw their `flight.orbit_path` with the existing orbit pass. Select the tracked object with **T** on the last thrown one.
- **Time warp** applies to ballistic objects as it does to the ship. The bubble itself runs at 1× (clamp warp to 1 while in `foot` mode with active bodies, or step Jolt multiple times; clamping is simpler and less surprising).

Accuracy: semi-implicit Euler at 1/120 s held your 40 km orbit within 100 m over one revolution, which is plenty for crates. For long-lived orbiting objects, propagate them analytically (Kepler) when far from the player and no drag applies: you already compute orbit elements in `shoonyakasha.planet.orbits`.

---

## 9. Props

### 9.1 Crates and test objects

- `props.spawn_crate(position, size=0.6, density=500)` creates a box body plus a mesh entity (your `box()` helper with a crate colour or texture).
- Debug key: **G** drops a crate 2 m in front of the player. **Shift+G**: a stack of 10.
- Render loop: one batched `get_transforms` call, then for each prop `engine.scene.set_position(entity, sub(bubble.to_world(p), camera))` and rotation. For many props, move to instanced drawing like the vegetation.

### 9.2 Rocks promoted from the scatter

Add rock kinds to the vegetation scatter:

| Kind | Where | Size | Mass (2,600 kg/m³ × ~0.5 of the bounding box) |
|---|---|---|---|
| pebble cluster | everywhere on land, sparse | — (no collider) | — |
| small rock | desert, steppe, tundra, mountains, the moon | 0.2–0.6 m | 5–150 kg |
| boulder | mountains, rock layer, crater rims | 0.8–2.5 m | 350 kg – 10 t |

Meshes: a few convex variants (noise-displaced icosphere, flattened), each with a precomputed convex hull for Jolt.

**Promotion flow:**

1. Nearby rocks arrive through `vegetation_nearby.comp` (§4) with their cell id, transform and variant.
2. Within 40 m they exist in Jolt as **static** bodies (layer `STATIC`, tagged as rock with their cell id), so the player bumps into them and the gun's ray can hit them.
3. When the gun grabs one, or a dynamic body hits it with an impulse above a threshold (Jolt contact listener), it is **promoted**:
   - its static body becomes dynamic (`set_motion_type("dynamic")`) at the same transform;
   - a mesh entity is created for it at the same transform;
   - its cell id goes into the **taken set**, uploaded to the GPU, and the scatter skips that cell's rock from the next frame on.
4. Because the scatter skip and the entity creation happen in the same frame (or the entity is created one frame early), there's no flicker. If you see a frame of double rock, delay the taken-set upload by one frame.

**Taken set on the GPU:** an open-addressing hash table, e.g. 8,192 entries of `uvec2` keys (`body | face << 1 | cellX << 4`, `cellY`), linear probing, uploaded whole when it changes (tiny). The scatter does one lookup per rock candidate only (not per tree), so it's cheap.

### 9.3 Sleep and freezing

- Jolt sleeps bodies at rest; nothing to do while they're in the bubble.
- A body in the bubble that ends up outside both terrain patches while asleep stays asleep. If it wakes there (something hits it), freeze it (`static`) until a patch covers it again.

### 9.4 Resting records and persistence

Anything moved by the player and at rest outside the bubble becomes a record:

```python
@dataclass
class Record:
    body: str                  # "planet" | "moon"
    position: tuple            # body-relative doubles
    orientation: tuple
    kind: str                  # crate, rock variant, ore
    cell: tuple | None         # the scatter cell it came from (keeps the taken set)
    extra: dict                # ore grade, cargo label...
```

- Records within the bubble radius of the player become Jolt bodies again (asleep), and are drawn as instances when farther (up to some distance).
- Save `records` + `taken` + ship state + player position to a JSON file on quit (**F5** quicksave / **F9** load). The taken set and the records together are the whole persistent change to the world: everything else regenerates from the seed.
- A new seed (**N**) clears both (or keep them per seed in the save).

---

## 10. Game loop: ore, scanning, cargo, fuel, outposts

### 10.1 Ore field

Add a fourth climate-like value in `climate.glsl` (or a separate `resources.glsl`): **ore richness** 0..1.

```glsl
float oreRichness(vec3 dir, float height, float slope, float mountainMask) {
    float veins = ridged(dir * 220.0 + seedOffset * 3.1, 4, 2.1, 0.5);   // long narrow bands, ~km scale
    float patches = smoothstep(0.55, 0.85, fbm(dir * 60.0 + seedOffset * 1.7, 4, 2.0, 0.5));
    return clamp(veins * patches * (0.4 + 0.6 * mountainMask), 0.0, 1.0);
}
```

For the moon: richness on crater floors and rims of the larger crater sizes (you have the crater lattice: give each crater a hashed "mineral" value).

The probes return it alongside height and climate. The free camera card can show it ("ORE 0.42") as a debug row.

### 10.2 Ore in the world

- Where richness > 0.3, the rock scatter places **ore rocks** (a tinted variant with emissive specks) with density rising with richness.
- Ore rocks are props like others. Breaking a boulder: punting a small rock into an ore boulder above some impact speed splits it into 3–6 ore chunks (prepared fragment meshes, or a convex decomposition of the variant). Or give the player a **mining charge** prop to place and trigger. Keep it physical and simple.
- Each ore chunk has a mass and a grade (from richness).

### 10.3 Scanning from the ship

A **SCAN** page on the right MFD (toggle with **M** in the ship, cycling ORBIT / SCAN):

- In orbit: probe the ground track ahead (e.g. 32 probes per second along the predicted path, using the extra probes' slots or the collider probe pass while not on foot) and draw a scrolling strip of richness. Mark points above 0.6 and remember them as **contacts** (body, direction, richness).
- Below 5 km: point the HUD at the nearest contact with a diamond marker and distance, like the flight path marker. The `LandingCheck`'s ring search is a ready-made pattern for finding a landable flat spot near a contact.

### 10.4 Cargo

- A **cargo zone**: a Jolt sensor box behind the cockpit (or under the ship, a "cargo net" area on the ground next to the landing gear).
- Ore chunks inside the zone when the player presses **F** at the ship (or when the ship takes off) are **loaded**: removed from the bubble, added to `ship.cargo` (kind, mass, grade).
- The cargo display on the left MFD: mass, value estimate.

### 10.5 Fuel and mass

Extend `ShipParams` and `Ship`:

```python
dry_mass: float = 14_000.0
fuel_capacity: float = 6_000.0     # kg
isp_main: float = 450.0            # s
isp_rcs: float = 250.0             # s

# Ship
fuel: float = 6_000.0
cargo_mass: float = 0.0

def mass(self):
    return self.params.dry_mass + self.fuel + self.cargo_mass
```

- In `step`: use `self.mass()` everywhere `p.mass` is used, scale inertia with mass / initial mass, and burn fuel: `fuel -= (|main thrust| / (isp_main · g0) + Σ|rcs thrust| / (isp_rcs · g0)) · dt` with `g0 = 9.80665`. No fuel: no thrust (rotation assist can stay free, or not).
- Show **Δv remaining** = `isp_main · g0 · ln(m / (m − fuel))` on the ORBIT page. The 162 m/s Hohmann transfer to the moon becomes something you check against your fuel and cargo.
- With the defaults above, Δv ≈ 450 · 9.81 · ln(20/14) ≈ 1,570 m/s: enough to reach orbit (~600 m/s plus losses), go to the moon and back, but not to waste. A heavy ore load makes it tight.

### 10.6 Outposts and prices

- Generalise the flattened site to a list of **sites**: `(body, direction, flat_radius, ease_radius, height)`, passed to `terrain.glsl` / `moon.glsl` as a small uniform array (8–16 entries). The existing site becomes site 0.
- Place outposts at start by a seeded search: candidate directions, probe height and slope (like `LandingCheck`), keep flat dry spots far apart. 3–4 on the planet, 2 on the moon.
- Each outpost: a landing pad (your blocks) and a **trade zone** sensor. Landing on the pad with cargo opens a trade card (your `Card` / `SettingsMenu` style): sell ore, buy fuel.
- Prices per outpost and resource, varying with distance from where it's mined and with what the outpost has bought recently (a simple supply value that recovers over time). The moon buys planet goods and vice versa: there's the X-style trade loop in miniature.
- Credits shown on the cards. Contracts later ("deliver 500 kg of ore to Outpost Tycho by...").

---

## 11. Input map

| Key | Foot | Ship |
|---|---|---|
| W/A/S/D | walk | (as now) |
| Shift | run | |
| Ctrl | crouch | |
| Space | jump; hold for jetpack | |
| Mouse | look | (right mouse: pitch/yaw, as now) |
| Left mouse | punt (hold to charge while holding) | |
| Right mouse | grab / pull / drop | |
| Wheel | hold distance (while holding) | |
| R + mouse | rotate held object | chase / cockpit (R) |
| F | board ship / use | exit ship (landed, slow) |
| G / Shift+G | drop a crate / a stack (debug) | |
| T | track the last thrown object | |
| M | | ORBIT / SCAN page |
| Tab | free camera (debug) | free camera (debug) |
| F5 / F9 | quicksave / quickload | |

Gamepad: left stick move, right stick look, A jump/jet, right trigger punt (hold to charge), left trigger grab/drop, X use/board, bumpers hold distance. Feed both through the existing hint bar and controls sheet.

Note: right mouse currently means "look" in the free camera. In foot mode the mouse looks without a button (capture the cursor); release it when the settings menu is open.

---

## 12. Checks (in the style of the existing ones)

- `--prop-rest-check`: drop 20 crates and 10 rocks on slopes up to 30° on both bodies; after 10 s all asleep; over the next 30 s none moves more than 1 cm. Then rebase the bubble 5 times by moving the player; still none moves.
- `--walk-check`: walk a scripted 200 m circle on the planet and on the moon at walk and run speed; the feet stay within 5 cm of the probed terrain, never below it; and print the largest `collider − drawn mesh` height difference under the player (§3.4).
- `--hold-check`: grab a 50 kg crate, turn 360° in 2 s, walk 30 m, jump; the crate stays within `drop_distance` of the hold point throughout; across a rebase it doesn't jump.
- `--punt-check`: punt crates of 5, 50 and 500 kg; their speeds match `min(punt_speed, punt_impulse/m)` within 2 %; the recoil matches its cap.
- `--orbit-crate-check`: on the moon, put a 10 kg crate 3 km above the moon's radius (above the highest terrain) with circular speed horizontally, let it leave the bubble, fly one orbit as a ballistic object, and re-enter the bubble when it comes back over the player; its position after one period is within 50 m of the start; it ends as a Jolt body again.
- `--promotion-check`: grab every rock within 20 m one by one; each is drawn exactly once per frame (no doubles, no gaps) by comparing scatter instance counts and entity counts around the promotion frame.
- `--save-check`: move 10 rocks, throw a crate far, save, regenerate, load; the taken set and records match, and the scatter skips exactly those cells.

---

## 13. Milestones

1. **Jolt + bubble + crates.** Jolt built in double precision, the Python API, bubble on the planet and the moon, gravity per body, the fine terrain patch from the collider probe pass, crates dropped from the free camera. `--prop-rest-check` passes.
2. **Player.** `CharacterVirtual` with radial up, walking, jumping (fixed velocity, high on the moon), jetpack, head bob, eye smoothing. Boarding and exiting the ship; the landed ship as a kinematic body. `--walk-check`.
3. **Collider vs drawn mesh.** Deeper LOD near the player in foot mode until the feet don't float or sink visibly.
4. **Gravity gun.** Grab, pull, hold, rotate, punt, recoil, ground punt jump, viewmodel with glow and beam. `--hold-check`, `--punt-check`.
5. **Trees.** `vegetation_nearby.comp` readback, trunk capsules.
6. **Rocks.** Rock kinds in the scatter, static bodies near the player, promotion, the taken set. `--promotion-check`.
7. **Far objects.** Coarse patch, leaving and re-entering the bubble, ballistic integration with probes for impact, tracked object with orbit path. Charged punt. `--orbit-crate-check`. (The orbiting crate moment.)
8. **Persistence.** Records, taken set, save/load. `--save-check`.
9. **Ore and scanning.** Richness field, ore rocks, breaking boulders, SCAN page, contacts.
10. **Cargo, fuel, outposts.** Cargo zone, mass and fuel with Δv display, multiple flattened sites, trade cards, prices.

---

## 14. Pitfalls

- **Rebasing mid-step** or forgetting a cached bubble coordinate (hold target, patch centre, sensor positions). Shift everything in one place.
- **Gravity applied twice:** Jolt's global gravity must be zero, and `CharacterVirtual` gets gravity only through your `character_update(..., gravity)` call.
- **Sleeping held objects:** wake the held body each step.
- **Held object pushing the player** or the player standing on it: filter held-object vs player collisions.
- **Patch replacement under resting bodies:** replacing a height field can wake or slightly move bodies resting on it if the new heights differ (different centre, same function: differences should be tiny, but float-to-height-field quantisation can add a few mm). Keep patch spacing fixed and centres snapped to the grid so samples land on the same directions.
- **Static-to-dynamic promotion overlap:** a rock promoted while intersecting the terrain or a trunk gets pushed out violently. Lift it 1 cm along up when promoting, or let Jolt resolve with limited penetration recovery speed.
- **Tunnelling:** fast punts (and charged punts) through thin objects or the ground. Use Jolt's linear cast motion quality (`EMotionQuality::LinearCast`) for small fast bodies.
- **Probe budget:** collider probes, ballistic impact probes, scan probes and the existing ones compete for readback latency and GPU time. Give each consumer its own buffer region and stamp, and measure the pass.
- **Mouse capture:** foot mode looks without a button; make sure the menu, the controls sheet and focus loss release it.
- **Time warp in foot mode:** clamp to 1× while the bubble has awake bodies, or physics and the player get coarse steps and odd results.

---

## 15. References

- Jolt Physics (repository, samples, docs) — https://github.com/jrouwe/JoltPhysics
- Jolt Physics documentation (architecture, character, double precision) — https://jrouwe.github.io/JoltPhysics/
- Glenn Fiedler — *Fix Your Timestep!* — https://gafferongames.com/post/fix_your_timestep/
- Erin Catto — Box2D publications (sequential impulses, soft constraints; background for your own contact solver and for tuning) — https://box2d.org/publications/
- Previous guide: `planet_flight_mvp.md` (precision, LOD, atmosphere, flight model)
