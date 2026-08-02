#version 450
//
// Sponza Particle Fragment Shader — Warm Embers
//
// 色如焰光 — Color like firelight
// Each fragment a tiny flame dancing in the void-space
//
// Outputs HDR color for additive blending into litColorHDR
//

layout(location = 0) in float inLife;
layout(location = 1) in float inSpeed;

layout(location = 0) out vec4 outColor;

void main() {
    // Circular point sprite shape
    vec2 coord = gl_PointCoord * 2.0 - 1.0;
    float dist2 = dot(coord, coord);
    if (dist2 > 1.0) discard;

    // Warm ember color palette
    // Slow/dying = deep ember red, fast/alive = bright golden-white
    float speedNorm = clamp(inSpeed / 3.0, 0.0, 1.0);

    vec3 slowColor  = vec3(0.8, 0.15, 0.02);   // Deep ember red
    vec3 midColor   = vec3(1.0, 0.45, 0.08);    // Warm orange
    vec3 fastColor  = vec3(1.0, 0.85, 0.5);     // Hot golden-white

    vec3 color = mix(slowColor, midColor, smoothstep(0.0, 0.5, speedNorm));
    color = mix(color, fastColor, smoothstep(0.5, 1.0, speedNorm));

    // Core glow: brighter at center
    float coreBrightness = 1.0 - dist2 * 0.6;
    color *= coreBrightness;

    // Alpha: life-based fadeout + radial edge softness
    float lifeFade = clamp(inLife, 0.0, 1.0);
    float edgeFade = 1.0 - smoothstep(0.4, 1.0, dist2);
    float alpha = lifeFade * edgeFade;

    // HDR intensity — fast particles glow brighter
    float intensity = 0.6 + speedNorm * 1.0;

    outColor = vec4(color * intensity * alpha, alpha);
}
