#version 450

//
// Forward Pass Fragment Shader - Procedural Bright Scene
// 明暗相生  光從虛來
// Light and dark give birth to each other — light emerges from the void
//

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 proj;
    mat4 model;
} camera;

// Push constant for animation
layout(push_constant) uniform PushConstants {
    float time;
} pc;

void main() {
    vec2 uv = inUV;
    vec2 center = vec2(0.5);

    // Create multiple bright orbs that move around
    vec3 color = vec3(0.02, 0.02, 0.03); // Dark background

    // Orb 1 - bright white/blue
    vec2 orb1Pos = center + vec2(sin(pc.time * 0.7) * 0.3, cos(pc.time * 0.5) * 0.2);
    float dist1 = length(uv - orb1Pos);
    float orb1 = exp(-dist1 * dist1 * 50.0) * 3.0; // HDR bright!
    color += vec3(0.8, 0.9, 1.0) * orb1;

    // Orb 2 - bright orange/yellow
    vec2 orb2Pos = center + vec2(cos(pc.time * 0.6) * 0.25, sin(pc.time * 0.8) * 0.25);
    float dist2 = length(uv - orb2Pos);
    float orb2 = exp(-dist2 * dist2 * 40.0) * 2.5;
    color += vec3(1.0, 0.6, 0.2) * orb2;

    // Orb 3 - bright magenta
    vec2 orb3Pos = center + vec2(sin(pc.time * 0.9 + 2.0) * 0.35, cos(pc.time * 0.4 + 1.0) * 0.3);
    float dist3 = length(uv - orb3Pos);
    float orb3 = exp(-dist3 * dist3 * 60.0) * 2.0;
    color += vec3(1.0, 0.3, 0.8) * orb3;

    // Orb 4 - bright cyan
    vec2 orb4Pos = center + vec2(cos(pc.time * 0.5 + 3.0) * 0.2, sin(pc.time * 0.7 + 2.0) * 0.35);
    float dist4 = length(uv - orb4Pos);
    float orb4 = exp(-dist4 * dist4 * 55.0) * 2.2;
    color += vec3(0.3, 1.0, 1.0) * orb4;

    // Center glow
    float centerDist = length(uv - center);
    float centerGlow = exp(-centerDist * centerDist * 8.0) * 0.5;
    color += vec3(0.5, 0.4, 0.6) * centerGlow;

    outColor = vec4(color, 1.0);
}
