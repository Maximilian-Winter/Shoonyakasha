#version 450
#extension GL_GOOGLE_include_directive : require
// The replay's stars as points, as star.vert draws stars. Each star's place
// is the recorded frame's (half floats, written by sk.streams.play into
// `Replay`), moved on along its recorded velocity by `replayAhead`, so the
// stars move smoothly between frames. Its kind, galaxy and birth time come
// from `ReplayStars`. Stars past `replayCount` are put outside the view.

#include "view.glsl"

layout(std430, binding = 0) readonly buffer Replay {
    uvec4 packed[];   // half floats: x y, z 0, vx vy, vz 0
} replay;

layout(std430, binding = 2) readonly buffer ReplayStars {
    vec2 codeBirth[];  // kind + 4 * galaxy, birth time
} stars;

layout(location = 0) out vec3 color;

void main() {
    uint i = uint(gl_VertexIndex);
    if (i >= view.replayCount) {
        gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
        gl_PointSize = 1.0;
        color = vec3(0.0);
        return;
    }
    uvec4 p = replay.packed[i];
    vec3 x = vec3(unpackHalf2x16(p.x), unpackHalf2x16(p.y).x);
    vec3 v = vec3(unpackHalf2x16(p.z), unpackHalf2x16(p.w).x);
    gl_Position = view.proj * view.view * vec4(x + v * view.replayAhead, 1.0);
    gl_PointSize = view.pointSize;
    vec2 cb = stars.codeBirth[i];
    uint galaxy = min(uint(cb.x + 0.5) >> 2u, 3u);
    vec3 base = view.colorMode == 1u ? GALAXY_COLORS[galaxy] : starColor((view.time - cb.y) * TIME_MYR);
    color = base * view.brightness;
}
