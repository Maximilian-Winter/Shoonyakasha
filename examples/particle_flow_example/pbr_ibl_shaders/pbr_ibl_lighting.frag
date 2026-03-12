#version 450
//
// PBR IBL Lighting Fragment Shader
//
// 朱雀司變  熾熱而速達
// The Vermilion Bird governs transformation — physically-based enlightenment
//
// Implements split-sum approximation for real-time IBL:
//   Diffuse: irradiance map sampled by normal
//   Specular: pre-filtered environment map + BRDF LUT
//

const float PI = 3.14159265359;

// GBuffer textures (set 0)
layout(set = 0, binding = 0) uniform sampler2D gPosition;
layout(set = 0, binding = 1) uniform sampler2D gNormal;
layout(set = 0, binding = 2) uniform sampler2D gAlbedo;
layout(set = 0, binding = 3) uniform sampler2D gMetallicRoughness;

// IBL textures (set 1)
layout(set = 1, binding = 0) uniform samplerCube irradianceMap;
layout(set = 1, binding = 1) uniform samplerCube prefilterMap;
layout(set = 1, binding = 2) uniform sampler2D brdfLUT;

// Camera UBO (set 2) — dot-path filled from scene.camera.*
layout(set = 2, binding = 0) uniform CameraUBO {
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
    vec4 lightsPositionType[MAX_LIGHTS];     // xyz=position, w=type (0=dir,1=point,2=spot)
    vec4 lightsColorIntensity[MAX_LIGHTS];   // xyz=color, w=intensity
    vec4 lightsDirectionRange[MAX_LIGHTS];   // xyz=direction, w=range
    vec4 lightsAttenuation[MAX_LIGHTS];      // x=constant, y=linear, z=quadratic, w=cos(outerCone)
};

// Input from fullscreen triangle
layout(location = 0) in vec2 fragTexCoord;

// Output HDR color
layout(location = 0) out vec4 outColor;

// ═══════════════════════════════════════════════════════════════
// PBR Helper Functions
// ═══════════════════════════════════════════════════════════════

// Fresnel-Schlick approximation
vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// Fresnel-Schlick with roughness for IBL
vec3 fresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness) {
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

void main() {
    // Sample GBuffer
    vec3 worldPos = texture(gPosition, fragTexCoord).rgb;
    vec3 N = texture(gNormal, fragTexCoord).rgb * 2.0 - 1.0;  // Unpack from [0,1] to [-1,1]
    vec4 albedoAlpha = texture(gAlbedo, fragTexCoord);
    vec2 metalRough = texture(gMetallicRoughness, fragTexCoord).rg;

    vec3 albedo = albedoAlpha.rgb;
    float metallic = metalRough.r;
    float roughness = metalRough.g;

    // Skip pixels with no geometry (check if position is near zero)
    if (length(worldPos) < 0.001 && albedoAlpha.a < 0.01) {
        outColor = vec4(0.05, 0.05, 0.08, 1.0);  // Sky color fallback
        return;
    }

    // Normalize normal (should already be normalized, but safety first)
    N = normalize(N);

    // DEBUG: Check for NaN/invalid normals - output CYAN if detected
    if (any(isnan(N)) || any(isinf(N)) || length(N) < 0.001) {
        outColor = vec4(0.0, 1.0, 1.0, 1.0);  // CYAN = bad normal
        return;
    }

    // View direction
    vec3 V = normalize(camera.position.xyz - worldPos);

    // DEBUG: Check for NaN/invalid view direction - output YELLOW if detected
    if (any(isnan(V)) || any(isinf(V)) || length(V) < 0.001) {
        outColor = vec4(1.0, 1.0, 0.0, 1.0);  // YELLOW = bad view direction
        return;
    }

    // Reflection vector
    vec3 R = reflect(-V, N);

    // F0 (base reflectivity)
    // Dielectrics: 0.04 (typical for non-metals)
    // Metals: use albedo color
    vec3 F0 = vec3(0.04);
    F0 = mix(F0, albedo, metallic);

    // ═══════════════════════════════════════════════════════════════
    // IBL Diffuse
    // ═══════════════════════════════════════════════════════════════

    // Sample irradiance map (pre-convolved diffuse environment)
    vec3 irradiance = texture(irradianceMap, N).rgb;

    // DEBUG: Check for invalid irradiance - output GREEN if detected
    if (any(isnan(irradiance)) || any(isinf(irradiance))) {
        outColor = vec4(0.0, 1.0, 0.0, 1.0);  // GREEN = bad irradiance sample
        return;
    }

    // Fresnel for diffuse (affects how much light is absorbed vs reflected)
    vec3 kS = fresnelSchlickRoughness(max(dot(N, V), 0.0), F0, roughness);
    vec3 kD = 1.0 - kS;
    kD *= 1.0 - metallic;  // Metals have no diffuse

    vec3 diffuse = irradiance * albedo;

    // ═══════════════════════════════════════════════════════════════
    // IBL Specular
    // ═══════════════════════════════════════════════════════════════

    // Determine mip level from roughness
    // Typically prefilter map has 5 mip levels for roughness 0..1
    const float MAX_REFLECTION_LOD = 4.0;
    float mipLevel = roughness * MAX_REFLECTION_LOD;

    // Sample pre-filtered environment map
    vec3 prefilteredColor = textureLod(prefilterMap, R, mipLevel).rgb;

    // Sample BRDF integration LUT
    // x = scale, y = bias for specular
    vec2 brdf = texture(brdfLUT, vec2(max(dot(N, V), 0.0), roughness)).rg;

    vec3 specular = prefilteredColor * (kS * brdf.x + brdf.y);

    // ═══════════════════════════════════════════════════════════════
    // Combine and Output
    // ═══════════════════════════════════════════════════════════════

    vec3 ambient = (kD * diffuse + specular);

    // Add a subtle ambient occlusion factor (could sample AO texture here)
    float ao = 1.0;

    vec3 color = ambient * ao;

    // Dynamic lights from ECS via dot-path UBO
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
            // Directional light
            L = normalize(-lightDirection);
        } else {
            // Point or spot light
            vec3 toLight = lightPos - worldPos;
            float dist = length(toLight);
            L = toLight / max(dist, 0.001);
            attenFactor = 1.0 / (atten.x + atten.y * dist + atten.z * dist * dist);
            if (lightRange > 0.0) {
                attenFactor *= clamp(1.0 - dist / lightRange, 0.0, 1.0);
            }
            if (lightType > 1.5) {
                // Spot light cone falloff
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
            vec3 directDiffuse = (1.0 - F) * (1.0 - metallic) * albedo / PI;

            color += (directDiffuse + directSpecular) * lColor * lIntensity * NdotL * attenFactor;
        }
    }

    // DEBUG: Check for NaN/invalid final color - output RED if detected
    if (any(isnan(color)) || any(isinf(color))) {
        outColor = vec4(1.0, 0.0, 0.0, 1.0);  // RED = NaN in final color
        return;
    }

    // Output HDR color
    outColor = vec4(color, 1.0);
}
