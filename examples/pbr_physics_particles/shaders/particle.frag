#version 450
//
// Particle Flow — Fragment Shader
//
// Kaleidoscopic luminous orbs with time-varying HSV color palette
// Each particle carries a unique hue that drifts over time,
// modulated by speed, height, and life — a cosmic aurora of light
//
// HDR output for additive blending into litColorHDR
//

layout(location = 0) in float inLife;
layout(location = 1) in float inSpeed;
layout(location = 2) flat in int inParticleID;
layout(location = 3) in float inElapsedTime;
layout(location = 4) in vec3 inWorldPos;

layout(location = 0) out vec4 outColor;

// ═══════════════════════════════════════════════════════════════
// HSV to RGB conversion — the colour wheel made algebraic
// ═══════════════════════════════════════════════════════════════
vec3 hsv2rgb(float h, float s, float v) {
    vec3 c = vec3(h, s, v);
    vec3 rgb = clamp(abs(mod(c.x * 6.0 + vec3(0.0, 4.0, 2.0), 6.0) - 3.0) - 1.0, 0.0, 1.0);
    return c.z * mix(vec3(1.0), rgb, c.y);
}

void main() {
    vec2 coord = gl_PointCoord * 2.0 - 1.0;
    float dist2 = dot(coord, coord);
    if (dist2 > 1.0) discard;

    float speedNorm = clamp(inSpeed / 12.0, 0.0, 1.0);
    float lifeNorm = clamp(inLife / 3.0, 0.0, 1.0);

    // ── Spatially coherent hue — nearby particles share colours ──
    // Hue driven by world position so overlapping particles REINFORCE
    // each other's colour rather than averaging to white.
    // Large-scale spatial waves create rivers and pools of colour
    // that drift and flow with time.
    float spatialHue = (inWorldPos.x + inWorldPos.z) * 0.04
                     + inWorldPos.y * 0.06;
    float timeDrift = inElapsedTime * 0.1;
    float tinyVariation = float(inParticleID) * 0.0003;  // barely perceptible per-particle jitter
    float hue = fract(spatialHue + timeDrift + tinyVariation);

    // ── Saturation: always vivid — this is a colour show ──
    float saturation = 0.9 + speedNorm * 0.1;

    // ── Value/brightness: speed drives incandescence ──
    float value = 0.85 + speedNorm * 0.15;

    // Convert HSV to base RGB
    vec3 color = hsv2rgb(hue, saturation, value);

    // ── Gaussian-like soft glow (not hard edge) ──
    float coreGlow = exp(-dist2 * 3.5);     // Bright centre
    float haloGlow = exp(-dist2 * 0.7);     // Soft wide halo

    float glow = coreGlow * 0.65 + haloGlow * 0.35;

    // ── Life fade — dying particles dim gracefully ──
    float lifeFade = smoothstep(0.0, 0.5, lifeNorm);

    // ── HDR intensity — generous now that spatial coherence prevents wash-out ──
    // Nearby particles share the same hue, so additive overlap
    // creates brighter colour rather than grey.
    float intensity = (1.0 + speedNorm * 2.0) * glow * lifeFade;

    float alpha = haloGlow * lifeFade;

    outColor = vec4(color * intensity, alpha);
}
