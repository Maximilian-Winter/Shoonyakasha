#version 450
//
// Both views' fragment shader: Lambert against one fixed light, the base
// colour times the vertex colour.
//

// The layout must match MaterialPushConstants in pipeline.json field for
// field; the engine packs it from that declaration.
layout(push_constant) uniform PushConstants {
    mat4 model;
    vec4 baseColorFactor;
    float metallicFactor;
    float roughnessFactor;
    float hasNormalMap;
    float hasMetalRoughMap;
    float alphaCutoff;
} push;

layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragWorldNormal;
layout(location = 2) in vec2 fragTexCoord;
layout(location = 3) in vec3 fragColor;

layout(location = 0) out vec4 outColor;

void main() {
    vec3 normal = normalize(fragWorldNormal);
    vec3 toLight = normalize(vec3(-0.4, 1.0, 0.6));
    float diffuse = max(dot(normal, toLight), 0.0);

    vec3 albedo = push.baseColorFactor.rgb * fragColor;
    vec3 lit = albedo * (0.2 + 0.85 * diffuse);

    // The swapchain is sRGB: writing to it encodes the linear colour.
    outColor = vec4(lit, push.baseColorFactor.a);
}
