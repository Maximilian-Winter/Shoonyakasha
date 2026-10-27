// The View uniform block at binding 1, and the colours of stars by age and
// of galaxies, for star.vert and replay.vert.

layout(std140, binding = 1) uniform View {
    mat4 view;
    mat4 proj;
    vec2 screen;
    uint count;
    float pointSize;
    float brightness;
    uint colorMode;      // 1: every particle in its galaxy's colour
    float time;          // simulation time, for ages
    uint replayCount;    // stars in the replay's recording
    float replayAhead;   // time since the replay's frame, by which each star moves on along its velocity
} view;

const float TIME_MYR = 4.714;   // one time unit in Myr (model.py)
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
