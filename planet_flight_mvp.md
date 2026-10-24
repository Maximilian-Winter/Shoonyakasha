# Planet-Scale Procedural Planet + Atmospheric Flight — MVP Implementation Guide

Target: custom Vulkan C++ engine with Python bindings.
Goal: fly a ship in cockpit view from the ground, through the atmosphere, into orbit around one procedurally generated planet. Fun prototype, not production.

---

## 0. Overview

Four problem areas, each with a standard solution:

| Area | MVP solution | Later upgrade |
|---|---|---|
| Geometry at planet scale | Cube-sphere, one quadtree per face, chunked LOD with skirts | CDLOD morphing, horizon culling, GPU-driven quadtree |
| Numeric precision | `double` world positions on CPU, camera-relative rendering, reverse-Z infinite projection | Double/emulated-double noise inputs at deep LOD |
| Atmosphere | Fullscreen single-scattering raymarch (Rayleigh + Mie) using depth buffer | Hillaire 2020 LUT model (transmittance, multi-scatter, sky-view, aerial perspective) |
| Flight & physics | 6DOF Newtonian + flight assist, inverse-square gravity, density-based drag | Lift model, reentry heating, orbits UI, multiple bodies |

**Scale recommendation:** start with a small planet, `R = 100 km` (range 50–200 km). Orbit is reachable in about a minute, curvature is visible in flight, float precision problems appear much later. Make `R` a runtime parameter so you can scale up later.

### Suggested module layout

```
planet/
  PlanetParams        // radius, seed, noise params, atmosphere params (exposed to Python)
  CubeSphere          // face frames, spherify mapping
  QuadTree / QuadNode // LOD selection, split/merge
  ChunkGenerator      // GPU compute dispatch, async job queue
  ChunkCache          // GPU buffers, LRU eviction
  TerrainRenderer     // draw visible chunks, camera-relative transforms
  HeightQuery         // CPU-side height sampling for collision
atmosphere/
  AtmosphereParams
  AtmospherePass      // fullscreen post-process (MVP) -> LUT pipeline (later)
flight/
  ShipState           // double precision position/velocity
  FlightModel         // forces, flight assist, integration
  CockpitView         // cockpit mesh in camera space, HUD
core/
  WorldOrigin         // floating-origin / camera-relative helper
  ReverseZ            // projection + depth state
```

---

## 1. Precision foundation (do this first)

Everything else depends on this. Get it right on an empty scene with one sphere.

### 1.1 Double precision world, camera-relative rendering

- All world-space positions that can be large (ship, chunk centers, planet center, sun) are `glm::dvec3` on the CPU.
- Each frame: `camPosW` (double). For every object: `relPos = float3(objPosW - camPosW)`. Only `relPos` goes to the GPU.
- The view matrix contains **rotation only**. Translation is already removed.
- Nothing on the GPU ever sees an absolute world coordinate (except where explicitly a *direction* on the unit sphere, e.g. noise input).

```cpp
struct WorldOrigin {
    glm::dvec3 camPos;
    glm::vec3 toCameraRelative(const glm::dvec3& p) const {
        return glm::vec3(p - camPos);   // subtract in double, then cast
    }
};

// Per chunk push constant
struct ChunkPush {
    glm::mat4 viewProjRotOnly;   // proj * rotation-only view
    glm::vec3 chunkOriginRel;    // chunkCenterW - camPosW, as float
    float     morphOrPad;
};
```

The vertex shader then does: `gl_Position = viewProjRotOnly * vec4(chunkOriginRel + inLocalPos, 1.0);`
Chunk vertices are stored **relative to the chunk center** (small numbers, full float precision).

### 1.2 Reverse-Z with infinite far plane (Vulkan)

Vulkan clip depth is `[0,1]` natively, so reverse-Z works without extensions or `gl_FragDepth` writes. With a `D32_SFLOAT` depth buffer this covers a cockpit at 0.05 m and a horizon thousands of km away in one pass. No logarithmic depth needed.

```cpp
// Right-handed view space (camera looks down -Z), Vulkan clip space (Y down, depth 0..1).
// Near plane maps to depth 1, infinity maps to depth 0.
glm::mat4 reverseZInfinitePerspective(float fovY, float aspect, float zNear) {
    float f = 1.0f / std::tan(fovY * 0.5f);
    glm::mat4 P(0.0f);
    P[0][0] = f / aspect;
    P[1][1] = -f;          // flip Y for Vulkan (omit if you use a negative viewport height)
    P[2][2] = 0.0f;
    P[2][3] = -1.0f;       // w_clip = -z_view
    P[3][2] = zNear;       // z_clip = zNear  -> depth = zNear / -z_view
    return P;
}
```

Pipeline state:
- `depthCompareOp = VK_COMPARE_OP_GREATER_OR_EQUAL`
- clear depth to `0.0f`
- depth format `VK_FORMAT_D32_SFLOAT`

When reconstructing view-space distance in post-processing (atmosphere): `viewDist_z = zNear / depth` (along the view axis); divide by the ray direction's z-component for distance along the ray.

### 1.3 Precision checklist

- [ ] No large translations in any matrix sent to the GPU.
- [ ] Physics in double.
- [ ] Chunk vertex data relative to chunk center.
- [ ] Noise input = unit sphere direction (float is fine until roughly LOD 14–16 at `R = 100 km`; see §2.6).

---

## 2. Planet geometry: cube-sphere + quadtree LOD

### 2.1 Cube faces

Six faces, each with an orthonormal frame `(normal, axisU, axisV)`. A point on a face is `normal + u*axisU + v*axisV` with `u, v ∈ [-1, 1]`.

```cpp
struct CubeFace { glm::dvec3 n, u, v; };
static const CubeFace kFaces[6] = {
    {{ 1,0,0}, {0,0,-1}, {0,1,0}},  // +X
    {{-1,0,0}, {0,0, 1}, {0,1,0}},  // -X
    {{0, 1,0}, {1,0, 0}, {0,0,-1}}, // +Y
    {{0,-1,0}, {1,0, 0}, {0,0, 1}}, // -Y
    {{0,0, 1}, {1,0, 0}, {0,1,0}},  // +Z
    {{0,0,-1}, {-1,0,0}, {0,1,0}},  // -Z
};
```

(Check the winding per face once with a wireframe view; flipping one axis per face is the usual bug.)

### 2.2 Cube → sphere mapping ("spherify")

Plain `normalize(p)` clusters vertices near cube corners. The spherified cube mapping distributes them much more evenly:

```glsl
vec3 spherify(vec3 p) {           // p on the cube surface, components in [-1,1]
    vec3 p2 = p * p;
    return vec3(
        p.x * sqrt(1.0 - p2.y*0.5 - p2.z*0.5 + p2.y*p2.z/3.0),
        p.y * sqrt(1.0 - p2.z*0.5 - p2.x*0.5 + p2.z*p2.x/3.0),
        p.z * sqrt(1.0 - p2.x*0.5 - p2.y*0.5 + p2.x*p2.y/3.0));
}
```

Implement the same function on CPU (double) and GPU (float).

### 2.3 Quadtree nodes

```cpp
struct NodeKey {            // unique, hashable
    uint8_t  face;
    uint8_t  level;
    uint32_t x, y;          // integer coords at this level, 0 .. 2^level - 1
};

struct QuadNode {
    NodeKey     key;
    glm::dvec2  uvMin;      // face-space bounds in [-1,1]
    double      uvSize;
    glm::dvec3  centerW;    // world-space center on the displaced surface (approx.)
    double      boundRadius;
    float       minH, maxH; // from generated heights (initially terrain amplitude bounds)
    QuadNode*   children[4] = {};
    ChunkHandle chunk;      // GPU data, may be pending
    enum class State { Empty, Pending, Ready } state = State::Empty;
};
```

Node edge length in meters (approximate): `edge ≈ R * (π/2) * uvSize / 2` — at level 0 a face edge is about a quarter of the circumference.

### 2.4 LOD selection (per frame, CPU)

```cpp
void selectNodes(QuadNode& n, const Camera& cam, std::vector<QuadNode*>& out) {
    if (!inFrustum(n, cam) || belowHorizon(n, cam)) return;

    double d    = distanceToNodeBounds(n, cam.posW);   // distance to bounding sphere surface, min 0
    double edge = nodeEdgeMeters(n);
    bool wantSplit = n.key.level < kMaxLevel && d < kSplitFactor * edge;    // kSplitFactor ~ 1.5–2.5

    if (wantSplit) {
        ensureChildren(n);                              // creates 4 nodes, requests chunks
        if (allChildrenReady(n)) {
            for (auto* c : n.children) selectNodes(*c, cam, out);
            return;
        }
    }
    if (n.state == QuadNode::State::Ready) out.push_back(&n);
    else requestChunk(n);
    // Merge: free children whose distance > kMergeFactor * edge (kMergeFactor ~ 1.2 * kSplitFactor)
    // lazily, via LRU in ChunkCache rather than immediately.
}
```

Key rules:
- **Keep the parent visible until all 4 children are ready.** No holes while chunks generate.
- **Hysteresis** between split and merge distances to avoid flicker.
- **Budget:** generate at most N chunks per frame (e.g. 4–16). Prioritize by distance.
- **Horizon culling:** a node is hidden if it is entirely behind the planet horizon as seen from the camera. Cheap test: treat the planet as a sphere of radius `R + minTerrainHeight` and the node as a bounding sphere; check if the node's bounding sphere is occluded. This is the single biggest win in orbit-to-low-altitude views.
- Max level: for `R = 100 km`, level ~14–16 gives sub-meter vertex spacing with 64×64 chunks. Start with `kMaxLevel = 12`.

### 2.5 Chunk generation on the GPU (compute)

Each chunk: `N×N` vertices (N = 33 or 65), plus a 1-vertex border for normals, plus skirt vertices.

Dispatch inputs (push constants):

```glsl
layout(push_constant) uniform ChunkGenParams {
    vec3  faceN;  float uvSize;
    vec3  faceU;  float radius;
    vec3  faceV;  float skirtDepth;
    vec2  uvMin;  vec2 pad;
    vec3  chunkCenterRel;   // chunk center in planet space (float; fine for small planets)
    float pad2;
} P;
```

Core of the compute shader:

```glsl
layout(local_size_x = 8, local_size_y = 8) in;

layout(std430, binding = 0) writeonly buffer Positions { vec4 outPos[]; };   // xyz local, w = height
layout(std430, binding = 1) writeonly buffer Normals   { vec4 outNrm[]; };

const int N = 65;           // vertices per side (without border)

vec3 surfaceDir(vec2 uv) {
    vec3 cubeP = P.faceN + uv.x * P.faceU + uv.y * P.faceV;
    return normalize(spherify(cubeP));     // spherify already lands on the unit sphere; normalize for safety
}

void main() {
    ivec2 id = ivec2(gl_GlobalInvocationID.xy);
    if (id.x >= N || id.y >= N) return;

    vec2  t   = vec2(id) / float(N - 1);
    vec2  uv  = P.uvMin + t * P.uvSize;
    vec3  dir = surfaceDir(uv);
    float h   = terrainHeight(dir);                 // meters, see §3
    vec3  pos = dir * (P.radius + h);

    // Normal via finite differences in uv space
    float e  = P.uvSize / float(N - 1);
    vec3 pU  = surfaceDir(uv + vec2(e, 0)); pU *= P.radius + terrainHeight(pU);
    vec3 pV  = surfaceDir(uv + vec2(0, e)); pV *= P.radius + terrainHeight(pV);
    vec3 nrm = normalize(cross(pU - pos, pV - pos));

    int idx = id.y * N + id.x;
    outPos[idx] = vec4(pos - P.chunkCenterRel, h);
    outNrm[idx] = vec4(nrm, 0.0);
}
```

Notes:
- The finite-difference version evaluates the noise 3× per vertex. Fine for an MVP. Later: generate an `(N+2)²` height grid first, then compute normals from neighbors in a second pass. Or use analytic noise derivatives (see §3.3).
- **Index buffer is shared** across all chunks (same topology). Only positions/normals differ.
- Use one big buffer with a sub-allocator for chunk data; one indirect draw per chunk or `vkCmdDrawIndexedIndirect` with a buffer of visible chunks.
- Read back `minH/maxH` per chunk (tiny buffer, a few frames late is fine) to tighten bounding volumes.

### 2.6 Cracks and seams

**MVP: skirts.** Add a ring of vertices along the 4 edges, duplicated and pushed toward the planet center by `skirtDepth ≈ 0.05–0.2 × nodeEdgeMeters`. Add triangles connecting edge → skirt. Hides T-junction cracks between LOD levels.

**Later: CDLOD** (Strugar). Vertices morph toward the parent-level grid position as the camera distance approaches the split threshold. Removes cracks *and* popping. Requires the height at the parent's coarser grid position, either stored per vertex (`morphTarget`) or sampled from a parent heightmap.

Face-to-face seams: since noise is sampled on the 3D unit sphere direction, adjacent faces produce identical heights along shared edges automatically. Only LOD-level differences cause cracks.

### 2.7 Precision at deep LOD

At high levels, adjacent vertices differ by tiny amounts in `dir` and `uv`. Symptoms: stair-stepping, noisy normals, wobbling vertices.

Options in order of effort:
1. Cap `kMaxLevel` where it still looks fine. Add detail via normal maps / shader noise instead of geometry.
2. Compute `uv` per vertex as `uvMin + t * uvSize` in double on the CPU per chunk, pass `uvMin` split into hi/lo floats.
3. Use `shaderFloat64` (check `VkPhysicalDeviceFeatures::shaderFloat64`) for `uv` → `dir` mapping and the noise lattice coordinates only; keep the rest float.
4. Noise lattice offset trick: compute `floor(dir * freq)` for high octaves relative to a per-chunk integer origin.

---

## 3. Terrain function

`float terrainHeight(vec3 dir)` — the single source of truth for the planet shape. Write it once in a GLSL include and mirror it (or read back, see §6) for CPU queries.

### 3.1 Building blocks

```glsl
// Any good 3D gradient/simplex noise returning [-1,1]. Seeded via offset vectors.
float noise3(vec3 p);

float fbm(vec3 p, int oct, float lac, float gain) {
    float a = 0.5, s = 0.0;
    for (int i = 0; i < oct; ++i) { s += a * noise3(p); p *= lac; a *= gain; }
    return s;
}

float ridged(vec3 p, int oct, float lac, float gain) {
    float a = 0.5, s = 0.0, w = 1.0;
    for (int i = 0; i < oct; ++i) {
        float n = 1.0 - abs(noise3(p));
        n *= n;
        n *= w;                 // previous octave weights the next one -> sharper ridges
        w = clamp(n * 2.0, 0.0, 1.0);
        s += a * n;
        p *= lac; a *= gain;
    }
    return s;
}

vec3 warp(vec3 p, float strength) {
    return p + strength * vec3(noise3(p + vec3(1.7, 9.2, 3.1)),
                               noise3(p + vec3(8.3, 2.8, 5.5)),
                               noise3(p + vec3(4.1, 6.6, 7.9)));
}
```

### 3.2 Planet recipe

```glsl
uniform TerrainParams {             // exposed to Python for live tweaking
    float continentFreq, continentWarp, seaLevelBias;
    float mountainFreq,  mountainHeight, mountainMaskFreq;
    float hillFreq,      hillHeight;
    float oceanDepth;
    vec3  seedOffset;
} T;

float terrainHeight(vec3 dir) {
    vec3 p = dir + T.seedOffset;

    // 1. Continents: low freq, domain-warped
    float c = fbm(warp(p * T.continentFreq, T.continentWarp), 5, 2.0, 0.5) + T.seaLevelBias;
    float land = smoothstep(-0.05, 0.05, c);                 // 0 = ocean, 1 = land

    // 2. Mountains: ridged, masked to appear in bands
    float mMask = smoothstep(0.1, 0.4, fbm(p * T.mountainMaskFreq, 3, 2.0, 0.5));
    float m = ridged(p * T.mountainFreq, 8, 2.1, 0.5) * mMask;

    // 3. Hills / detail
    float hills = fbm(p * T.hillFreq, 6, 2.0, 0.5);

    float hLand  = c * 800.0 + m * T.mountainHeight + hills * T.hillHeight;
    float hOcean = -T.oceanDepth * smoothstep(0.0, -0.3, c);
    return mix(hOcean, hLand, land);
}
```

Starting values for `R = 100 km`: continentFreq 1.5, mountainFreq 6, mountainHeight 3000 m, hillFreq 40, hillHeight 150 m, oceanDepth 1500 m. Exaggerated heights look better on small planets.

### 3.3 Nicer erosion look (later)

Derivative-damped fBm (Inigo Quilez): accumulate the noise gradient across octaves and damp each octave by `1 / (1 + dot(grad, grad))`. Steep areas get less high-frequency detail, which produces smooth valleys and sharp ridges. Requires a noise function that returns value + analytic derivative (also gives you normals for free).

### 3.4 Coloring / biomes (fragment shader)

Inputs: height `h`, slope `1 - dot(normal, dir)`, latitude `abs(dir.y)`, small noise.

- `h < 0` → seabed (not visible under opaque ocean, fine)
- `h < 30 m` → sand
- slope > threshold → rock
- latitude / height high → snow (`snowLine = mix(2500, 200, latitude^2)`)
- otherwise → grass / forest / desert by a low-freq "moisture" noise

Flat colors + good lighting + atmosphere look surprisingly good. Triplanar detail textures later.

### 3.5 Ocean

Second sphere at radius `R`, rendered as its own coarse quadtree (or reuse the terrain quadtree with height clamped to 0). Shader: depth-based color (water depth = terrain height under it, pass as attribute), Fresnel, sun specular. Transparent pass after terrain.

---

## 4. Atmosphere

### 4.1 Parameters

```cpp
struct AtmosphereParams {
    float planetRadius;          // R
    float atmosphereRadius;      // R + ~0.1*R for small planets (Earth: R + 100 km)
    glm::vec3 rayleighScatter;   // Earth: (5.802, 13.558, 33.1) * 1e-6 per meter
    float rayleighScaleHeight;   // Earth: 8000 m  -> scale with planet, e.g. 0.08 * atmoThickness
    float mieScatter;            // Earth: 3.996e-6
    float mieAbsorb;             // Earth: 4.4e-6
    float mieScaleHeight;        // Earth: 1200 m
    float mieG;                  // 0.76–0.8
    glm::vec3 sunDirection;      // planet space
    glm::vec3 sunIlluminance;    // e.g. (20,20,20) before exposure
};
```

For a 100 km planet with a 10 km thick atmosphere, scale the coefficients up (×5–10) so the sky still looks dense enough. Treat it as an art parameter.

### 4.2 MVP: fullscreen single-scattering raymarch

Post-process pass after opaque terrain/ocean. Inputs: depth buffer, inverse projection, rotation-only view, camera position **relative to planet center** (computed in double on CPU, cast to float — values up to ~1e6 m are fine in float for this purpose).

```glsl
vec2 raySphere(vec3 ro, vec3 rd, float r) {       // returns (tNear, tFar), tFar < 0 = miss
    float b = dot(ro, rd);
    float c = dot(ro, ro) - r*r;
    float h = b*b - c;
    if (h < 0.0) return vec2(-1.0);
    h = sqrt(h);
    return vec2(-b - h, -b + h);
}

float densityR(float h) { return exp(-h / A.rayleighScaleHeight); }
float densityM(float h) { return exp(-h / A.mieScaleHeight); }

vec3 opticalDepthToSun(vec3 p) {                  // cheap: 6–8 samples, or a transmittance LUT later
    vec2 hit = raySphere(p, A.sunDir, A.atmosphereRadius);
    float stepLen = hit.y / 8.0;
    vec3 od = vec3(0.0);
    for (int i = 0; i < 8; ++i) {
        vec3 s = p + A.sunDir * (stepLen * (float(i) + 0.5));
        float h = length(s) - A.planetRadius;
        od += (A.rayleighScatter * densityR(h) + (A.mieScatter + A.mieAbsorb) * densityM(h)) * stepLen;
    }
    return od;
}

vec3 atmosphere(vec3 ro, vec3 rd, float sceneDist, vec3 sceneColor) {
    vec2 a = raySphere(ro, rd, A.atmosphereRadius);
    if (a.y < 0.0) return sceneColor;
    float t0 = max(a.x, 0.0);
    float t1 = min(a.y, sceneDist);
    if (t1 <= t0) return sceneColor;

    const int STEPS = 24;
    float dt = (t1 - t0) / float(STEPS);
    vec3 odView = vec3(0.0), inR = vec3(0.0), inM = vec3(0.0);

    for (int i = 0; i < STEPS; ++i) {
        vec3 p = ro + rd * (t0 + dt * (float(i) + 0.5));
        float h = length(p) - A.planetRadius;
        float dR = densityR(h) * dt, dM = densityM(h) * dt;
        odView += A.rayleighScatter * dR + (A.mieScatter + A.mieAbsorb) * dM;

        // Skip if the sun is blocked by the planet
        if (raySphere(p, A.sunDir, A.planetRadius).x > 0.0) continue;

        vec3 T = exp(-(odView + opticalDepthToSun(p)));
        inR += T * dR;
        inM += T * dM;
    }

    float mu = dot(rd, A.sunDir);
    float phaseR = 3.0 / (16.0 * PI) * (1.0 + mu*mu);
    float g = A.mieG;
    float phaseM = 3.0 / (8.0 * PI) * ((1.0 - g*g) * (1.0 + mu*mu)) /
                   ((2.0 + g*g) * pow(1.0 + g*g - 2.0*g*mu, 1.5));

    vec3 inscatter = A.sunIlluminance * (inR * A.rayleighScatter * phaseR + inM * A.mieScatter * phaseM);
    vec3 transmittance = exp(-odView);
    return sceneColor * transmittance + inscatter;
}
```

Scene distance from reverse-Z depth: `depth == 0` → sky (use `1e30`). Otherwise view-space z = `zNear / depth`, distance along ray = `z / dot(rd, camForward)`.

Also apply the sun transmittance to terrain lighting: the sun color hitting the ground = `sunIlluminance * exp(-opticalDepthToSun(groundPoint))`. That gives red sunsets on the terrain for free.

Performance: render the atmosphere at half resolution and upsample if needed; 24 view steps × 8 sun steps is fine on desktop GPUs at 1080p.

### 4.3 Upgrade: Hillaire 2020

Replace the inner loops with LUTs:
1. **Transmittance LUT** (256×64): indexed by (height, sun zenith cos). Replaces `opticalDepthToSun`.
2. **Multiple scattering LUT** (32×32): the approximation from the paper; makes the sky brighter and more natural, especially at twilight.
3. **Sky-view LUT** (~200×100, recomputed each frame): sky color by (azimuth, elevation) for the current camera height. Sky pixels just sample it.
4. **Aerial perspective volume** (32×32×32 froxels): inscatter + transmittance for geometry.

Above the atmosphere, fall back to direct raymarching (the sky-view parameterization is meant for cameras inside the atmosphere). The pl-sky repository (Vulkan, readable) and the FlightGear shaders are good references.

---

## 5. Flight model

### 5.1 State

```cpp
struct ShipState {
    glm::dvec3 posW;        // planet center = origin of world space for now
    glm::dvec3 velW;
    glm::dquat orient;      // body -> world
    glm::dvec3 angVelBody;  // rad/s in body frame
};

struct ShipParams {
    double mass       = 20'000;      // kg
    glm::dvec3 inertia = {4e5, 5e5, 3e5};
    double maxThrustMain = 6e5;      // N (forward)
    double maxThrustRCS  = 1.5e5;    // N (lateral / vertical / reverse)
    double maxTorque     = 2e5;      // N·m
    double dragArea      = 12.0;     // Cd * A, m²
    double liftCoeff     = 0.6;      // simple lift factor
};
```

### 5.2 Forces

```cpp
double surfaceGravity = 9.81 * 0.5;     // art choice for a small planet; GM = g * R²
glm::dvec3 gravity(const glm::dvec3& p) {
    double r = glm::length(p);
    return -p / r * (surfaceGravity * R * R / (r * r));
}

double airDensity(double altitude) {     // SAME shape as the atmosphere renderer
    return rho0 * std::exp(-altitude / rayleighScaleHeight);
}
```

Per step (fixed dt, e.g. 1/120 s, in double):

```cpp
glm::dvec3 up       = glm::normalize(s.posW);
double     altitude = glm::length(s.posW) - R;
double     rho      = airDensity(altitude);

glm::dvec3 airVel   = s.velW;                         // no wind, no planet rotation for MVP
double     speed    = glm::length(airVel);
glm::dvec3 drag     = speed > 1e-3 ? -0.5 * rho * speed * P.dragArea * airVel : glm::dvec3(0);

// Simple lift: pushes along ship's up axis, proportional to forward airspeed²
glm::dvec3 shipFwd = s.orient * glm::dvec3(0, 0, -1);
glm::dvec3 shipUp  = s.orient * glm::dvec3(0, 1, 0);
double fwdSpeed = glm::dot(airVel, shipFwd);
glm::dvec3 lift = shipUp * (0.5 * rho * fwdSpeed * std::abs(fwdSpeed) * P.liftCoeff);

glm::dvec3 thrustW = s.orient * thrustBody;           // from input + flight assist
glm::dvec3 acc = gravity(s.posW) + (thrustW + drag + lift) / P.mass;

// Semi-implicit Euler is fine; use RK4 later if orbits drift
s.velW += acc * dt;
s.posW += s.velW * dt;
```

Rotation: integrate angular velocity from torque (`angAcc = torque / inertia`), update quaternion with `q = normalize(q + 0.5 * dt * quat(0, ω_world) * q)`.

### 5.3 Flight assist (makes it feel good)

- **Rotation assist:** desired angular velocity from stick input (`maxRate * stick`). PD controller on `(desired - current)` → torque, clamped to `maxTorque`.
- **Translation assist (toggle):** desired velocity in body frame = throttle × max speed setting. P-controller on velocity error → thrust per axis, clamped per thruster. In atmosphere, add gravity compensation (hover assist): `thrust += -gravity * mass` projected onto available thrust axes.
- Assist off = pure Newtonian. Toggle key like Elite's FA-off.

### 5.4 Reference frames & HUD

- **Altitude:** `length(posW) - R - terrainHeight(dir)` for AGL, `length(posW) - R` for sea level.
- **Horizon / pitch ladder:** relative to local `up = normalize(posW)`.
- **Prograde marker:** project `velW` direction into screen space.
- **Orbit info (later):** compute orbital elements from `posW`, `velW`, `GM` (apoapsis/periapsis) and show them in space.
- Blend HUD mode by altitude: atmospheric HUD below `atmosphereRadius`, orbital HUD above.

### 5.5 Reentry / speed effects

`heat = speed³ × rho × k`. Drive: hull glow shader on the cockpit frame, screen-edge plasma overlay, camera shake, sound. Cheap, very satisfying.

---

## 6. Collision

MVP: point/sphere collision against the terrain directly under the ship.

Option A — **CPU mirror of `terrainHeight`.** Port the noise to C++ (same algorithm, same constants, same seed). Risk: tiny float differences between CPU and GPU, or different noise implementations, cause the ship to hover or sink. Use doubles on CPU; accept a small error margin.

Option B — **GPU height query.** A small compute shader that evaluates `terrainHeight` for a list of directions (ship position + a few probe points ahead along the velocity). Read back asynchronously with 1–2 frames of latency; for collisions at flight speeds, probe ahead by `speed × latency`. Guarantees the same function as rendering.

Recommendation: B for correctness, A if you want physics completely independent of the renderer. Either way:

```cpp
double ground = R + terrainHeightAt(normalize(s.posW));
double r = glm::length(s.posW);
if (r < ground + shipRadius) {
    glm::dvec3 n = glm::normalize(s.posW);          // approximate; use terrain normal later
    s.posW = n * (ground + shipRadius);
    double vn = glm::dot(s.velW, n);
    if (vn < 0) s.velW -= n * vn * (1.0 + restitution);
    s.velW *= friction;                              // crude landing friction
}
```

Note: the rendered mesh at low LOD can be above or below the true function. Only the finest LOD matches the function closely. Near the ground the quadtree is at max level anyway.

---

## 7. Cockpit rendering

- Cockpit mesh lives in **camera space**. Its model matrix is identity relative to the camera (or a fixed head offset). No world position → no jitter, ever.
- Render order: terrain + ocean → atmosphere post → cockpit (with its own depth clear, or a separate depth range) → HUD.
  - Rendering the cockpit after the atmosphere avoids applying aerial perspective to it.
  - If you want the cockpit to occlude terrain early (perf), render it first into depth with the same reverse-Z projection; zNear = 0.05 m works.
- Cockpit lighting: use the sun direction + sun transmittance at the ship position, plus a cheap ambient from the sky-view color above the ship. Interior shadows from the canopy frame help a lot (a simple shadow map of the cockpit alone).
- HUD: draw in screen space or as an emissive quad on the canopy glass (collimated look: project HUD symbols at infinity).

---

## 8. Python bindings integration

Expose these for live tweaking (Dear ImGui panel optional, Python REPL is often faster for this):

```python
planet = engine.planet
planet.terrain.mountain_height = 4500
planet.terrain.continent_freq = 1.2
planet.terrain.seed = 1337
planet.regenerate()                 # invalidates chunk cache

atm = engine.atmosphere
atm.rayleigh_scale_height = 1200
atm.mie_g = 0.8
atm.sun_direction = (0.3, 0.1, -0.9)

ship = engine.ship
ship.teleport(altitude=20_000, lat=10, lon=40, speed=800)   # quick test setups

engine.debug.wireframe = True
engine.debug.color_by_lod = True
engine.debug.freeze_lod = True     # fly around a frozen quadtree to inspect cracks
```

Useful debug features:
- Color chunks by LOD level.
- Freeze LOD selection.
- Show chunk bounding spheres / horizon culling result.
- Chunk generation stats: pending queue size, generated per frame, GPU memory in use.
- Teleport presets: orbit, 10 km altitude, low pass over mountains, sunset terminator.

---

## 9. Milestones

1. **Precision sandbox.** Unit sphere of radius `R`, free camera with double position, camera-relative rendering, reverse-Z infinite projection. Fly from 5 m to 5,000 km away without jitter or z-fighting.
2. **Cube-sphere.** Six faces, spherify, flat-shaded, fixed subdivision. Verify winding and seams.
3. **Quadtree LOD (CPU-generated or flat).** Split/merge with hysteresis, skirts, debug LOD coloring, frustum culling.
4. **GPU chunk generation.** Compute shader, async queue with per-frame budget, chunk cache with LRU, shared index buffer.
5. **Terrain function.** Continents + ridged mountains + hills, biome coloring, Python-tweakable params.
6. **Atmosphere MVP.** Raymarch post-process with depth buffer, sun transmittance on terrain lighting, ocean sphere.
7. **Ship.** Flight model with assist, cockpit mesh in camera space, basic HUD (speed, altitude, horizon, prograde).
8. **Collision.** Height query + simple response; land on a mountain.
9. **Polish loop (pick freely):** horizon culling, CDLOD morphing, Hillaire LUTs, derivative fBm, reentry effects, clouds, a moon (second planet instance + multiple gravity sources), orbit display.

---

## 10. Pitfalls

- **Large numbers sneaking into float:** any matrix with a world translation, or `cameraPos` uploaded as an absolute value. Grep for it.
- **Quadtree flicker:** missing hysteresis, or split distance measured to node center instead of bounds.
- **Holes while flying fast:** children rendered before they're all ready. Keep parents until all 4 children are ready.
- **Generation stalls:** unbounded chunk requests per frame. Budget them and sort by distance.
- **Normal seams between chunks:** normals computed only from interior vertices. Use the border ring or analytic derivatives.
- **Atmosphere banding at the horizon:** too few steps along very long rays. Use non-uniform step distribution (denser near the camera) or more steps when the ray is near tangent.
- **Ship hovering or sinking after landing:** CPU/GPU noise mismatch, or colliding against a coarse LOD mesh instead of the function.
- **Orbit decay with no atmosphere:** integration error. Use smaller dt or RK4/velocity Verlet for the ship.
- **Gimbal issues in the HUD:** build the local horizon frame from `up` and the ship's forward vector; handle the case forward ≈ up.

---

## 11. References

Planet geometry & LOD
- Steven Wittens — *Making Worlds 1: Of Spheres and Cubes* — https://acko.net/blog/making-worlds-1-of-spheres-and-cubes/
- *Procedural Planet Generation based on derivative fBm* (SBGames 2018) — https://www.sbgames.org/sbgames2018/files/papers/ComputacaoShort/188242.pdf
- *Procedural Planetary Multi-resolution Terrain Generation for Games* (arXiv 1803.04612) — https://arxiv.org/pdf/1803.04612v1.pdf
- Filip Strugar — *CDLOD: Continuous Distance-Dependent Level of Detail for Rendering Heightmaps* — https://github.com/fstrugar/CDLOD
- gamedev.net — *Procedural Planet – GPU normal map artifacts* (deep-LOD precision discussion) — https://gamedev.net/forums/topic/660629-procedural-planet-gpu-normal-map-artifacts/5184311/
- gamedev.net — *OpenGL Procedural Planet Generation – Quadtrees and Geomipmapping* — https://gamedev.net/forums/topic/637956-opengl-procedural-planet-generation-quadtrees-and-geomipmapping/
- vterrain.org LOD overview — http://vterrain.org/LOD/Implementations/

Noise
- Inigo Quilez — *Domain warping* — https://iquilezles.org/articles/warp/
- Inigo Quilez — *Value noise derivatives / more noise* — https://iquilezles.org/articles/morenoise/

Precision & depth
- NVIDIA — *Depth Precision Visualized* (reverse-Z) — https://developer.nvidia.com/content/depth-precision-visualized
- Outerra — *Maximizing depth buffer range and precision* — https://outerra.blogspot.com/2012/11/maximizing-depth-buffer-range-and.html
- Khronos forum — *Logarithmic depth in Vulkan?* — https://community.khronos.org/t/logarithmic-depth-in-vulkan/7507

Atmosphere
- Sébastien Hillaire — *A Scalable and Production Ready Sky and Atmosphere Rendering Technique* (EGSR 2020) — https://diglib.eg.org/handle/10.1111/cgf14050
- Hillaire — SIGGRAPH 2020 course slides — https://blog.selfshadow.com/publications/s2020-shading-course/hillaire/s2020_pbs_hillaire_slides.pdf
- pl-sky (Vulkan reference implementation of Hillaire 2020) — https://github.com/hoffstadt/pl-sky
- Eric Bruneton — *Precomputed Atmospheric Scattering* (alternative 4D-LUT approach) — https://github.com/ebruneton/precomputed_atmospheric_scattering
- Sebastian Lague — *Coding Adventure: Atmosphere* (YouTube) — good intuition for the MVP raymarch

Example projects
- VulkanLearn (Vulkan planet-scale render lab) — https://github.com/enjiushi/VulkanLearn
- PlanetTechJS (GPU quadtree planets) — https://unpkg.com/planettech@0.0.8-alpha.0.1.6/DOC.md
- Solar-Wanderer (floating origin + log depth + raymarched atmospheres, WebGL) — https://github.com/hyqzz/Solar-Wanderer
- GitHub topic: planet-renderer — https://github.com/topics/planet-renderer

---

## 12. Work packages (Shoonyakasha)

The milestones from §9, cut to fit this engine. Each package ends with a check you can see in the running example.

### 12.1 Decisions

- **Doubles live in Python.** Camera and ship state are Python floats (64-bit). The engine camera stays at the origin and only rotates. Every chunk and object is uploaded as `float(posW - camPosW)`. The engine itself does not get a floating origin.
- **The planet is an example**, at `examples/python/simulations/planet/`, with its own pipeline JSON. The default pipeline's depth-dependent passes (GTAO, TAA, fog) stay on standard Z and are not touched.
- **Only one engine change is planned:** a reverse-Z projection with an infinite far plane as a camera option. Other engine changes come up only if a package shows they are needed.

### 12.2 Packages

**WP1: Precision sandbox**
- Engine: add an infinite reverse-Z projection option to `CameraComponent`, reachable from Python, with a unit test of the depth mapping (near → 1, infinity → 0).
- Example: an own pipeline (`greater_or_equal`, depth clear 0), a double-precision free-fly camera in Python, and a sphere of R = 100 km drawn camera-relative.
- Done: fly from 5 m to 5,000 km away with no jitter and no z-fighting.

**WP2: Cube-sphere**
- Six face frames and `spherify` in Python (double), in `cube_sphere.py`.
- Fixed subdivision: each face 8 × 8 chunks at level 3, each chunk's vertices relative to its own centre and its entity transform set camera-relative every frame.
- Done: correct winding on all faces and no gaps at face seams (wireframe check, and `tests/python/test_planet_cube_sphere.py`).

**WP3: Quadtree LOD (CPU meshes)**
- Chunk meshes built with numpy.
- Split and merge with hysteresis; a parent stays visible until all four children are ready; a per-frame generation budget sorted by distance.
- Skirts and frustum culling. Skirts also cover the seams between neighbouring chunks: each places a shared edge vertex through its own float32 offset, up to about 1.4 mm (0.3 px) apart 3 m above a seam. Draws are culled by the engine per entity; split decisions ignore the frustum, and the build order puts chunks in view first.
- Debug: colour by LOD level, freeze LOD.
- Measured with split factor 1.25 and max level 12: 474 chunks drawn at 5 m altitude, 354 at 1 km; a chunk build (numpy plus `create_mesh`) takes about 1.1 ms, so frames that build 8 take about 10 ms of Python.
- Done: low flight over the sphere with no holes and no flicker.

**WP4: GPU chunk generation**
- Job queue: one `compute_dispatch` of 16 workgroups, one per job; the jobs (face, level, x, y, slot, skirt depth) and their count are custom values, and workgroups past the count return at once.
- `spherify` and the face frames in GLSL, matching `cube_sphere.py`, in 64-bit floats (`shaderFloat64`, enabled by the device where supported): positions relative to the chunk's centre in 32-bit floats would carry about 1 cm of error at R = 100 km.
- A compute pass writes chunk vertices into a pool of 2048 fixed-size slots, managed by a free list in Python.
- One shared index buffer and `draw_indexed_indirect` with a count; each chunk's camera-relative origin and colour go into an SSBO every frame, indexed by `firstInstance`.
- `--verify` reads the pool back: every chunk matches `cube_sphere.chunk_mesh` to within one ULP of the chunk's largest coordinate (15 µm at levels 8 and finer), directions and face coordinates exactly.
- Measured: Python per frame 0.9 ms median, 1.7 ms p95; frame time 1.2 ms median; the drawn chunks are not frustum culled.
- Done: same picture as WP3, generated on the GPU, and pool usage shown as a stat.

**WP5: Terrain function**
- `terrainHeight(dir)` (continents, ridged mountains, hills) in a shared GLSL include, `terrain.glsl`, with 3D gradient noise over integer-hashed lattice points.
- Colour by height, slope and latitude.
- Parameters as custom uniforms; `regenerate()` from Python.
- Per-chunk minH/maxH come back through a per-frame readback (with each job's stamp), to tighten the quadtree's bounding spheres; until then a node uses its parent's range.
- Normals from the terrain at four points around each vertex along tangents that depend on its direction only, so chunks sharing a vertex agree.
- Height probes (an extra workgroup of `BuildChunks`, read back every frame, tagged with a terrain version): the ground under the camera (keeps it above the ground, height above ground in the HUD) and the site's natural height.
- A flat pad around the site (150 m, easing out by 600 m) at the site's natural height; the quadtree starts once that height is known.
- Done: changing a parameter from Python rebuilds the planet.

**WP6: Atmosphere MVP**
- A fullscreen single-scattering pass that reads depth (distance rebuilt from reverse-Z).
- Sun transmittance applied to terrain lighting.
- The scene draws into an HDR image; the atmosphere pass applies exposure and ACES tone mapping.
- Parameters for R = 100 km: top at R + 20 km, scale heights 2.5 km (Rayleigh) and 400 m (Mie), Earth's coefficients × 3.2 (Earth's vertical optical depth). Tried × 8: the sky turns teal, since blue is then extinguished along the path. Sunsets stay pale at × 3.2, because the low path to the sun is short on a small planet; the density can be raised at run time.
- Surfaces are Lambert (albedo / π); exposure 0.4.
- Measured: frame time about 2.4 ms on the ground at day, 5.5–6 ms with a low sun or from orbit.
- Done: a blue sky from the ground, a thin atmosphere shell from orbit, red light at the terminator.

**WP7: Ocean**
- A water sphere at radius R with colour by depth, Fresnel and sun highlight, drawn as a transparent pass after the terrain.
- Geometry: the terrain's chunks drawn again from the pool with each vertex moved to sea level (position − direction × height), only those whose height range reaches below 0, through a second indirect draw list; the same LOD and skirts.
- Water depth from the sea floor's height per vertex (shallow water shows the coarse chunks' facets from far away); premultiplied blending over the sea floor; the ocean writes depth, so the atmosphere fogs the surface.
- The camera stays above max(ground, sea level).
- Done: coastlines visible from low altitude and from orbit.

**WP8: Flight model**
- Python, double precision, a fixed 1/120 s step with an accumulator inside `set_on_update`.
- Inverse-square gravity; drag and lift use the same density curve as the atmosphere pass.
- Rotation and translation assist with an on/off toggle; keyboard and mouse mapping. A gamepad needs gamepad axes and buttons in the Python bindings first; they are not there.
- `flight.py` is engine-independent and unit-tested; the free camera stays as a separate mode (Tab). Time warp up to 16×; the ship rests on the ground through the height probe until WP10.
- Measured: a scripted ascent (60° burn to a 40 km apoapsis, coast, prograde burn) reaches a 30 × 74 km orbit 189 s after lift-off; `--orbit-check` flies one full orbit at 40 km (1,486 s) in the example and stays within 39,989.8–40,000 m (the 10 m loss is drag from the thin air there).
- Done: climb to orbit, and the orbit holds for several minutes without drift.

**WP9: Cockpit and HUD**
- A cockpit mesh in camera space, drawn after the atmosphere pass.
- A Canvas HUD with speed, altitude above ground and above sea level, horizon and prograde marker.
- As built: the cockpit is generated geometry (`cockpit.py`), drawn into the HDR image before the atmosphere (which adds nothing visible at a metre). The horizon, pitch ladder, prograde marker and nose cross are a fullscreen GLSL pass that tests each pixel's view direction (collimated; canvas elements cannot rotate with the roll). The numbers are a screen canvas; two dashboard displays are world canvases drawn by their own pass from `emissiveMap`, on their own render layer.
- Added: a settings menu (F1) on a screen canvas for the simulation's settings and the planet's generation parameters (applied with a button).
- Done: the full flight from ground to orbit in cockpit view.

**WP10: Collision**
- A GPU height query (compute plus readback) with probe points ahead along the velocity to cover the readback delay.
- Simple contact response.
- Starting point: `BuildChunks` already evaluates the terrain at 4 probe directions per frame (read back per frame, tagged with the terrain version), and `Ship.rest_on` keeps the ship above the one probe under it. What is missing:
  - probes ahead along the velocity (as far as the ship travels in the readback delay, about 3 frames, times the time warp), so a fast ship does not sink into a slope it flies into;
  - the ground's normal from probes around the contact point, for the contact response (now straight up from the planet's centre);
  - a contact response along that normal, with friction, and a touchdown speed limit above which the ship is crashed (shown on the HUD, reset with a preset);
  - the ship's shape as a few contact points (gear, nose, wing tips) instead of one, so it can sit on a slope.
- As built: 32 probes. Each result carries the stamp of the frame that asked for it, which gives the latency (about 3 frames) and the terrain version. 25 of them are a 5 × 5 ground patch (`ground.py`) in the tangent plane, centred ahead by the latency times the frame time and the warp, reaching that far again beyond the ship's 8 m reach plus 4 m; it is interpolated bilinearly, with the sea as a floor. `Ship.step` takes the patch's query and solves 3 gear and 5 hull points with sequential impulses (normal and Coulomb friction, 8 iterations, speculative for points reaching the surface within the step). Crash limits are 10 m/s on the gear and 3 m/s on the hull. On its gear with idle controls the ship rests (no thrust, no torque) until a control moves; otherwise assist would hold it hovering just above a slope.
- Translation assist limits its descent over the ground patch to √(3² + 2 · 1.5 · h) m/s (braking at 1.5 m/s² to 3 m/s at the ground), so holding Q lands.
- Measured: `--landing-check` comes down from 500 m holding full Q onto the highest of 64 spots around the site (2,478 m, an 11.6° slope) and touches down at 4.5 m/s after 24 s; the gear rests 2 to 4 cm above the terrain probed at its own directions, the bilinear patch's error on 6 m cells. A 400 m/s dive at 16× warp crashes with the centre 3.26 m above the ground; with no lookahead it ended at 0.32 m, the hull 2 m into the mountain.
- Done: land on a mountain without sinking or hovering.

**WP11 and onward: Polish (any order)**
- Horizon culling, CDLOD morphing, Hillaire LUTs, derivative fBm, reentry effects, clouds, a moon, orbit display.
- Gathered while building WP1–WP9:
  - Culling the terrain's chunks against the view and the horizon before the indirect draw (WP4 draws every chunk the tree selects; the engine's per-entity culling no longer applies).
    - As built: in Python before the draw commands are written: the view cone that holds the screen's diagonal (with the framebuffer's aspect), and a horizon test against a sphere at the terrain's floor, bounded from the terrain function's parameters. Draws 194 of 564 chunks at preset 1, 131 of 387 at preset 2, 79 of 300 at preset 7, 68 of 111 in orbit; from 1,000 km and beyond the six roots stay drawn. Every capture below its text lines is pixel-identical to before.
  - Detail below the terrain's finest noise octave (about 80 m): detail noise in the shaders, or more octaves where the chunks are fine enough.
    - As built: derivative ("eroded") fBm for the hills and a detail layer from 80 m to 10 m wavelengths, 15 m high in the mountains' bands and 35 % of that outside them, in `terrain.glsl`, so the chunks, the probes and the colours share it. The `detail` vec4 (wavelength, height, octaves, erosion) is in `TerrainParams` and the menu. The ground patch grew to 11 × 11 (128 probes) to follow the detail: the gear rests within 1.6 cm of the terrain on a 17° slope.
  - Derivative fBm (from the first line above): as built with the detail; the ridged mountains are unchanged.
  - Water depth per pixel from the depth buffer instead of per vertex (shallow water shows the coarse chunks' facets from far away); rougher water further away, so the sun's glint spreads naturally.
  - Redder sunsets without a teal sky (multiple scattering, or the Hillaire LUTs above).
    - As built: transmittance and multiple scattering tables (Hillaire 2020) from two compute passes every frame, and an ozone layer; the teal and olive came from the missing multiple scattering and ozone, and are gone. The sun at the horizon stays yellow-white: through this atmosphere (R/H = 40) the air mass along the horizon is about √(πR/2H) ≈ 8, against about 38 on Earth, so the reddening is weak. Earth-like reds would need the sunlight's path computed as if the planet were larger, an artistic choice not made yet.
  - Gamepad axes and buttons in the Python bindings, then a gamepad mapping for the ship.
  - A systematic search for cracks between levels in steep terrain (only stills were inspected).
    - As built: `--crack-check` measures every gap to a coarser neighbour against its skirt at six views. With skirts of 5 % of the edge, the default terrain was covered at up to 0.79 of a skirt, and mountains twice as high were not (an 11 m gap against a 7.7 m skirt, 864 m from the camera). Skirts now reach down by the larger of that and the chunk's height range, which bounds the gap; the largest share is 0.24.
  - The terrain's lighting: the sun shadowed by mountains, and the cockpit's interior shadowed by its frame.
