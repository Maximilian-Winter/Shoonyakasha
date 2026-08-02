#version 450
//
// Starter fragment shader.
//
// Lambert against one hard-coded light, so the scaffold shows shaded geometry
// without needing an environment map. Swap in the PBR + IBL shaders from
// examples/ when you want the real lighting model.
//

// Material parameters arrive in the push constant block. The layout must match
// MaterialPushConstants in pipeline.json field for field — the engine packs it
// from that declaration, not from this file.
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
    float ambient = 0.15;

    vec3 albedo = push.baseColorFactor.rgb * fragColor;
    vec3 lit = albedo * (ambient + diffuse * 0.9);

    // The swapchain is UNORM, so encode for display here. A pipeline with a
    // separate tonemap pass would do this there instead.
    outColor = vec4(pow(lit, vec3(1.0 / 2.2)), push.baseColorFactor.a);
}
