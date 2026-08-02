#version 450
//
// Particle Fragment Shader — Kaleidoscopic luminous orbs
//
// Spatially coherent HSV color palette — nearby particles
// share hues so additive overlap creates vivid pools of color
//

layout(location = 0) in float inLife;
layout(location = 1) in float inSpeed;
layout(location = 2) flat in int inParticleID;
layout(location = 3) in float inElapsedTime;
layout(location = 4) in vec3 inWorldPos;

layout(location = 0) out vec4 outColor;

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

    // Spatially coherent hue — nearby particles share colours
    float spatialHue = (inWorldPos.x + inWorldPos.z) * 0.04
                     + inWorldPos.y * 0.06;
    float timeDrift = inElapsedTime * 0.1;
    float tinyVariation = float(inParticleID) * 0.0003;
    float hue = fract(spatialHue + timeDrift + tinyVariation);

    float saturation = 0.9 + speedNorm * 0.1;
    float value = 0.85 + speedNorm * 0.15;

    vec3 color = hsv2rgb(hue, saturation, value);

    // Gaussian-like soft glow
    float coreGlow = exp(-dist2 * 3.5);
    float haloGlow = exp(-dist2 * 0.7);
    float glow = coreGlow * 0.65 + haloGlow * 0.35;

    // Life fade
    float lifeFade = smoothstep(0.0, 0.5, lifeNorm);

    // HDR intensity
    float intensity = (1.0 + speedNorm * 2.0) * glow * lifeFade;
    float alpha = haloGlow * lifeFade;

    outColor = vec4(color * intensity, alpha);
}
