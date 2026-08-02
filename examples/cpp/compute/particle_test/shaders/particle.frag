#version 450

//
// GPU Particle Fragment Shader — color based on life and velocity
// 色即是空  空即是色
// Color is emptiness, emptiness is color
//

layout(location = 0) in float inLife;
layout(location = 1) in float inSpeed;

layout(location = 0) out vec4 outColor;

void main() {
    // Circular point shape
    vec2 coord = gl_PointCoord * 2.0 - 1.0;
    float dist = dot(coord, coord);
    if (dist > 1.0) discard;

    // Color gradient based on speed: blue (slow) -> cyan -> white (fast)
    float speedNorm = clamp(inSpeed / 10.0, 0.0, 1.0);
    vec3 slowColor = vec3(0.2, 0.4, 1.0);
    vec3 fastColor = vec3(1.0, 0.8, 0.6);
    vec3 color = mix(slowColor, fastColor, speedNorm);

    // Alpha based on life and distance from center
    float alpha = clamp(inLife, 0.0, 1.0) * (1.0 - dist * 0.5);

    outColor = vec4(color * alpha, alpha);
}
