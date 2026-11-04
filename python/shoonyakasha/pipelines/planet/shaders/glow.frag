#version 450
//
// Glowing quads (the gravity gun's beam, motes and halos), added to the
// tone-mapped image after the atmosphere. Each quad's texture coordinates
// run 0 to 1 across it; its light is emissiveFactor.rgb times the vertex
// colour, falling off from the quad's middle:
// - emissiveFactor.a < 0.5, a disc: brightest at the centre, fading to
//   nothing at the rim;
// - emissiveFactor.a >= 0.5, a streak along u: fading across v from its
//   centre line, and to nothing over STREAK_END of each end.
// The light is LDR: the swapchain clamps the sum at white.
//

layout(push_constant) uniform PushConstants {
    mat4 model;
    vec4 baseColorFactor;
    float gridStrength;
    float padding0;
    float padding1;
    float padding2;
    vec4 emissiveFactor;
} push;

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec3 fragColor;
layout(location = 2) in vec2 fragTexCoord;
layout(location = 3) in vec3 fragRelativePos;

layout(location = 0) out vec4 outColor;

const float STREAK_END = 0.08;

void main() {
    vec2 p = fragTexCoord * 2.0 - 1.0;
    float shape;
    if (push.emissiveFactor.a < 0.5) {
        float r = clamp(1.0 - length(p), 0.0, 1.0);
        shape = 0.6 * r * r + 0.4 * pow(r, 8.0);       // a soft rim, a hot core
    } else {
        float across = clamp(1.0 - abs(p.y), 0.0, 1.0);
        float ends = smoothstep(0.0, STREAK_END, fragTexCoord.x) * smoothstep(0.0, STREAK_END, 1.0 - fragTexCoord.x);
        shape = (0.5 * across * across + 0.5 * pow(across, 10.0)) * ends;
    }
    // The blend adds the colour and writes this alpha in place of the image's: 1 keeps it opaque.
    outColor = vec4(push.emissiveFactor.rgb * fragColor * shape, 1.0);
}
