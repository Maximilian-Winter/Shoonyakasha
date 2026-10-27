#version 450
// Six vertices per flash: a disc facing the camera at the flash's place,
// growing and fading over LIFE time units from its start. Its largest
// radius grows with the square root of its strength. Flashes not yet
// started, over, or of strength 0 are moved off screen.

struct Flash {
    vec4 placeStart;      // xyz where, w the simulation time it started
    vec4 strength;        // x its strength
};

layout(std140, set = 0, binding = 1) uniform View {
    mat4 view;
    mat4 proj;
    vec2 screen;
    uint count;
    float radius;
    float glowEnergy;
    float trailTime;
    float time;           // simulation time now
} view;
layout(std430, set = 0, binding = 3) readonly buffer Flashes { Flash f[]; } flashes;

layout(location = 0) out vec2 vCorner;     // -1 to 1 across the disc
layout(location = 1) out float vBright;

const float LIFE = 30.0;
const vec2 CORNERS[6] = vec2[](vec2(-1.0, -1.0), vec2(1.0, -1.0), vec2(-1.0, 1.0),
                               vec2(-1.0, 1.0), vec2(1.0, -1.0), vec2(1.0, 1.0));

void main() {
    uint i = uint(gl_VertexIndex) / 6u;
    vCorner = CORNERS[gl_VertexIndex % 6];
    vBright = 0.0;
    gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
    if (i >= uint(flashes.f.length())) return;
    Flash flash = flashes.f[i];
    float age = (view.time - flash.placeStart.w) / LIFE;
    if (flash.strength.x <= 0.0 || age < 0.0 || age > 1.0) return;

    float largest = 3.0 + 2.0 * sqrt(flash.strength.x);
    float size = largest * (0.25 + 0.75 * sqrt(age));
    vec4 center = view.view * vec4(flash.placeStart.xyz, 1.0);
    center.z += size;                       // in front of the nucleons it lights
    vBright = exp(-3.0 * age) * min(1.0, 0.5 + 0.25 * flash.strength.x);
    gl_Position = view.proj * (center + vec4(vCorner * size, 0.0, 0.0));
}
