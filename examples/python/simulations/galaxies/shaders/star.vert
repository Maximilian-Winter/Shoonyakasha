#version 450
// Every particle as a point of `pointSize` pixels. A star's colour and
// brightness come from its age, `time` minus its birth time: young stars
// blue and bright, old ones yellow-orange and dimmer. A star formed during
// the simulation also glows pink for its first HII_MYR, drawn three times as
// large, the glow fading as it ages. Gas is drawn red, twice as large and
// dimmer. With colorMode 1 every particle takes its galaxy's colour instead.
// Particles past `count` are put outside the view.

layout(std430, binding = 0) readonly buffer Particles {
    vec4 data[];   // element 2i: position, kind + 4 * galaxy; element 2i + 1: velocity, birth time
} particles;

layout(std140, binding = 1) uniform View {
    mat4 view;
    mat4 proj;
    vec2 screen;
    uint count;
    float pointSize;
    float brightness;
    uint colorMode;
    float time;
} view;

layout(location = 0) out vec3 color;

const uint GAS = 2u;
const uint YOUNG = 3u;
const float TIME_MYR = 4.714;   // one time unit in Myr (model.py)
const float HII_MYR = 15.0;
const vec3 HII_COLOR = vec3(1.00, 0.18, 0.45) * 6.0;
const vec3 GAS_COLOR = vec3(1.00, 0.12, 0.25);
const float GAS_WEIGHT = 0.25;
const vec3 GALAXY_COLORS[4] = vec3[4](
    vec3(1.00, 0.62, 0.30), vec3(0.35, 0.62, 1.00), vec3(0.50, 1.00, 0.55), vec3(1.00, 0.45, 0.85));

// Colour times brightness of a star `ageMyr` old, between points at
// 10 Myr, 300 Myr, 3 Gyr and 10 Gyr (log10 ages 1, 2.5, 3.5, 4).
vec3 starColor(float ageMyr) {
    float x = log(max(ageMyr, 1.0)) / log(10.0);
    vec3 c0 = vec3(0.20, 0.38, 1.00) * 3.0;
    vec3 c1 = vec3(0.35, 0.55, 1.00) * 1.3;
    vec3 c2 = vec3(1.00, 0.70, 0.40) * 0.9;
    vec3 c3 = vec3(1.00, 0.45, 0.15) * 0.8;
    if (x < 2.5) return mix(c0, c1, clamp((x - 1.0) / 1.5, 0.0, 1.0));
    if (x < 3.5) return mix(c1, c2, x - 2.5);
    return mix(c2, c3, clamp((x - 3.5) / 0.5, 0.0, 1.0));
}

void main() {
    uint i = uint(gl_VertexIndex);
    if (i >= view.count) {
        gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
        gl_PointSize = 1.0;
        color = vec3(0.0);
        return;
    }
    vec4 pk = particles.data[2u * i];
    float birth = particles.data[2u * i + 1u].w;
    uint code = uint(pk.w + 0.5);
    uint kind = code & 3u;
    uint galaxy = min(code >> 2u, 3u);
    gl_Position = view.proj * view.view * vec4(pk.xyz, 1.0);
    float age = (view.time - birth) * TIME_MYR;
    float glow = kind == YOUNG ? max(1.0 - age / HII_MYR, 0.0) : 0.0;
    float size = kind == GAS ? 2.0 : glow > 0.0 ? 3.0 : 1.0;
    gl_PointSize = view.pointSize * size;
    vec3 base = kind == GAS ? GAS_COLOR * GAS_WEIGHT : starColor(age) + HII_COLOR * glow * size * size;
    if (view.colorMode == 1u) base = GALAXY_COLORS[galaxy];
    color = base * view.brightness / (size * size);
}
