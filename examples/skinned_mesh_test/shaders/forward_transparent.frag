#version 450
//
// Forward Transparency Fragment Shader — Full PBR + IBL Inline
//
// 透明之美 — The beauty of transparency
// Light passes through, blending realities into one
//
// This shader performs complete PBR + IBL lighting in a single forward pass
// for alpha-blended surfaces. It outputs HDR color with alpha for blending.
//

const float PI = 3.14159265359;

// Material textures (set 1)
layout(set = 1, binding = 0) uniform sampler2D albedoMap;
layout(set = 1, binding = 1) uniform sampler2D normalMap;
layout(set = 1, binding = 2) uniform sampler2D metallicRoughnessMap;

// IBL textures (set 2)
layout(set = 2, binding = 0) uniform samplerCube irradianceMap;
layout(set = 2, binding = 1) uniform samplerCube prefilterMap;
layout(set = 2, binding = 2) uniform sampler2D brdfLUT;

// Camera UBO (set 0) — dot-path filled from scene.camera.*
layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 proj;
    mat4 invView;
    mat4 invProj;
    vec4 position;   // xyz = camera world position, w = padding
    vec4 params;     // x = near, y = far, z = fov (radians), w = aspect
} camera;

// Lights UBO (set 3) — dot-path filled from scene.lights[i].*
#define MAX_LIGHTS 16
layout(set = 3, binding = 0) uniform LightsUBO {
    uint lightCount;
    float _pad1, _pad2, _pad3;
    vec4 lightsPositionType[MAX_LIGHTS];
    vec4 lightsColorIntensity[MAX_LIGHTS];
    vec4 lightsDirectionRange[MAX_LIGHTS];
    vec4 lightsAttenuation[MAX_LIGHTS];
};

// Material factors push constant (104 bytes total)
layout(push_constant) uniform MaterialFactors {
    mat4 model;            // offset 0
    vec4 baseColorFactor;  // offset 64
    float metallicFactor;  // offset 80
    float roughnessFactor; // offset 84
    float hasNormalMap;    // offset 88
    float hasMetalRoughMap; // offset 92
    float alphaCutoff;     // offset 96
    float padding1;        // offset 100
} material;

// Input from vertex shader
layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragWorldNormal;
layout(location = 2) in vec2 fragTexCoord;
layout(location = 3) in vec3 fragColor;

// Output HDR color with alpha for blending
layout(location = 0) out vec4 outColor;

// ═══════════════════════════════════════════════════════════════
// PBR Helper Functions (same as ibl_lighting.frag)
// ═══════════════════════════════════════════════════════════════

// Fresnel-Schlick approximation
vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// Fresnel-Schlick with roughness for IBL
vec3 fresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness) {
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// Compute TBN matrix for normal mapping (robust version)
mat3 computeTBN(vec3 worldNormal, vec3 worldPos, vec2 uv) {
    vec3 Q1 = dFdx(worldPos);
    vec3 Q2 = dFdy(worldPos);
    vec2 st1 = dFdx(uv);
    vec2 st2 = dFdy(uv);

    vec3 N = normalize(worldNormal);

    float det = st1.s * st2.t - st2.s * st1.t;
    if (abs(det) < 0.0001) {
        vec3 T = abs(N.y) < 0.999 ? normalize(cross(N, vec3(0.0, 1.0, 0.0))) : normalize(cross(N, vec3(1.0, 0.0, 0.0)));
        vec3 B = normalize(cross(N, T));
        return mat3(T, B, N);
    }

    float invDet = 1.0 / det;
    vec3 T = (Q1 * st2.t - Q2 * st1.t) * invDet;
    vec3 B = (Q2 * st1.s - Q1 * st2.s) * invDet;

    T = normalize(T - N * dot(N, T));
    B = cross(N, T);

    return mat3(T, B, N);
}

void main() {
    // Sample albedo texture
    vec4 albedoSample = texture(albedoMap, fragTexCoord);

    // Apply base color factor and vertex color
    vec4 albedo;
    albedo.rgb = albedoSample.rgb * material.baseColorFactor.rgb * fragColor;
    albedo.a = albedoSample.a * material.baseColorFactor.a;

    // Discard nearly invisible fragments (optimization)
    if (albedo.a < 0.001) {
        discard;
    }

    // Normal calculation
    vec3 N = normalize(fragWorldNormal);

    // Apply normal map if available
    if (material.hasNormalMap > 0.5) {
        vec3 tangentNormal = texture(normalMap, fragTexCoord).rgb * 2.0 - 1.0;
        mat3 TBN = computeTBN(fragWorldNormal, fragWorldPos, fragTexCoord);
        vec3 mappedNormal = TBN * tangentNormal;
        if (dot(mappedNormal, mappedNormal) > 0.001) {
            N = normalize(mappedNormal);
        }
    }

    // Two-sided lighting
    if (!gl_FrontFacing) {
        N = -N;
    }

    // Sample metallic/roughness or use factors
    float metallic = material.metallicFactor;
    float roughness = material.roughnessFactor;

    if (material.hasMetalRoughMap > 0.5) {
        vec4 mr = texture(metallicRoughnessMap, fragTexCoord);
        metallic = mr.b * material.metallicFactor;
        roughness = mr.g * material.roughnessFactor;
    }

    roughness = clamp(roughness, 0.04, 1.0);

    // View direction
    vec3 V = normalize(camera.position.xyz - fragWorldPos);

    // Reflection vector
    vec3 R = reflect(-V, N);

    // F0 (base reflectivity)
    vec3 F0 = vec3(0.04);
    F0 = mix(F0, albedo.rgb, metallic);

    // ═══════════════════════════════════════════════════════════════
    // IBL Diffuse
    // ═══════════════════════════════════════════════════════════════

    vec3 irradiance = texture(irradianceMap, N).rgb;

    vec3 kS = fresnelSchlickRoughness(max(dot(N, V), 0.0), F0, roughness);
    vec3 kD = 1.0 - kS;
    kD *= 1.0 - metallic;

    vec3 diffuse = irradiance * albedo.rgb;

    // ═══════════════════════════════════════════════════════════════
    // IBL Specular
    // ═══════════════════════════════════════════════════════════════

    const float MAX_REFLECTION_LOD = 4.0;
    float mipLevel = roughness * MAX_REFLECTION_LOD;

    vec3 prefilteredColor = textureLod(prefilterMap, R, mipLevel).rgb;
    vec2 brdf = texture(brdfLUT, vec2(max(dot(N, V), 0.0), roughness)).rg;

    vec3 specular = prefilteredColor * (kS * brdf.x + brdf.y);

    // ═══════════════════════════════════════════════════════════════
    // Direct Lighting — dynamic lights from ECS via dot-path UBO
    // ═══════════════════════════════════════════════════════════════

    vec3 ambient = kD * diffuse + specular;
    vec3 color = ambient;

    float NdotV_direct = max(dot(N, V), 0.0);
    for (uint idx = 0u; idx < min(lightCount, uint(MAX_LIGHTS)); idx++) {
        vec3 lightPos = lightsPositionType[idx].xyz;
        float lightType = lightsPositionType[idx].w;
        vec3 lColor = lightsColorIntensity[idx].xyz;
        float lIntensity = lightsColorIntensity[idx].w;
        vec3 lightDirection = lightsDirectionRange[idx].xyz;
        float lightRange = lightsDirectionRange[idx].w;
        vec4 atten = lightsAttenuation[idx];

        vec3 L;
        float attenFactor = 1.0;

        if (lightType < 0.5) {
            L = normalize(-lightDirection);
        } else {
            vec3 toLight = lightPos - fragWorldPos;
            float dist = length(toLight);
            L = toLight / max(dist, 0.001);
            attenFactor = 1.0 / (atten.x + atten.y * dist + atten.z * dist * dist);
            if (lightRange > 0.0) {
                attenFactor *= clamp(1.0 - dist / lightRange, 0.0, 1.0);
            }
            if (lightType > 1.5) {
                float theta = dot(L, normalize(-lightDirection));
                float cosOuter = atten.w;
                attenFactor *= clamp((theta - cosOuter) / max(1.0 - cosOuter, 0.001), 0.0, 1.0);
            }
        }

        float NdotL = max(dot(N, L), 0.0);
        if (NdotL > 0.0 && attenFactor > 0.001) {
            vec3 H = normalize(V + L);
            float NdotH = max(dot(N, H), 0.0);
            float VdotH = max(dot(V, H), 0.0);

            vec3 F = fresnelSchlick(VdotH, F0);
            float alpha = roughness * roughness;
            float alpha2 = alpha * alpha;
            float denom = NdotH * NdotH * (alpha2 - 1.0) + 1.0;
            float D = alpha2 / (PI * denom * denom);

            float k = (roughness + 1.0) * (roughness + 1.0) / 8.0;
            float G = (NdotV_direct / (NdotV_direct * (1.0 - k) + k))
                    * (NdotL / (NdotL * (1.0 - k) + k));

            vec3 directSpecular = (D * G * F) / max(4.0 * NdotV_direct * NdotL, 0.001);
            vec3 directDiffuse = (1.0 - F) * (1.0 - metallic) * albedo.rgb / PI;

            color += (directDiffuse + directSpecular) * lColor * lIntensity * NdotL * attenFactor;
        }
    }

    // Output HDR color with alpha
    // Note: alpha is preserved for blending with the background
    outColor = vec4(color, albedo.a);
}
