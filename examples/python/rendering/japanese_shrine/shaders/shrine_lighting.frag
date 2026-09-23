#version 450
//
// Shrine lighting: deferred PBR shading of a glTF metallic-roughness G-buffer.
//
// Implements split-sum approximation for real-time IBL:
//   Diffuse: irradiance map sampled by normal
//   Specular: pre-filtered environment map + BRDF LUT
// plus Cook-Torrance direct lighting for the scene's lights.
//
// Pixels with no geometry show the environment itself, taken from the
// pre-filtered map at mip `skyBlur` so the sky can be softened.
//

#include "sk/pbr.glsl"

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

// Shading controls (set 4) — dot-path filled from scene.custom.shrine.*
layout(set = 4, binding = 0) uniform ShadingUBO {
    float exposure;       // read by tonemap.frag
    float iblIntensity;   // scales diffuse and specular image-based light
    float skyBlur;        // prefilter mip shown as the background
    float fogDensity;     // per world unit past fogStart; geometry fades towards the sky behind it
    float fogStart;       // distance from the camera where the fog begins
} shading;

// Sun shadow map and its projection (set 5) — written by ShadowPass
layout(set = 5, binding = 0) uniform sampler2D shadowMap;
layout(set = 5, binding = 1) uniform ShadowUBO {
    mat4 lightViewProj;  // world -> sun clip space, Vulkan depth range [0, 1]
    vec4 params;         // x = normal offset (world units), y = depth bias,
                         // z = slope bias, w = sky light kept in shadow (0..1)
} shadow;

// Fraction of sunlight reaching worldPos, 0 = fully shadowed. A 5x5 PCF
// kernel softens the edge. Points outside the shadow map are lit.
float sunVisibility(vec3 worldPos, vec3 N, vec3 L) {
    float NdotL = clamp(dot(N, L), 0.0, 1.0);
    vec3 offsetPos = worldPos + N * shadow.params.x * (1.0 - NdotL);
    vec4 clip = shadow.lightViewProj * vec4(offsetPos, 1.0);
    vec3 ndc = clip.xyz / clip.w;
    vec2 uv = ndc.xy * 0.5 + 0.5;
    if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0))) || ndc.z > 1.0) {
        return 1.0;
    }

    float bias = shadow.params.y + shadow.params.z * sqrt(1.0 - NdotL * NdotL) / max(NdotL, 0.05);
    vec2 texel = 1.0 / vec2(textureSize(shadowMap, 0));
    float lit = 0.0;
    for (int y = -2; y <= 2; y++) {
        for (int x = -2; x <= 2; x++) {
            float occluder = texture(shadowMap, uv + vec2(x, y) * texel).r;
            lit += (ndc.z - bias <= occluder) ? 1.0 : 0.0;
        }
    }
    return lit / 25.0;
}

// The environment seen along a direction, as drawn behind the scene.
vec3 skyAlong(vec3 dir) {
    return textureLod(prefilterMap, dir, shading.skyBlur).rgb * shading.iblIntensity;
}

// Input from fullscreen triangle
layout(location = 0) in vec2 fragTexCoord;

// Output HDR color
layout(location = 0) out vec4 outColor;

void main() {
    // Sample GBuffer
    vec3 worldPos = texture(gPosition, fragTexCoord).rgb;
    vec3 N = texture(gNormal, fragTexCoord).rgb * 2.0 - 1.0;  // Unpack from [0,1] to [-1,1]
    vec4 albedoAlpha = texture(gAlbedo, fragTexCoord);
    vec2 metalRough = texture(gMetallicRoughness, fragTexCoord).rg;

    vec3 albedo = albedoAlpha.rgb;
    float metallic = metalRough.r;
    float roughness = metalRough.g;

    // Pixels with no geometry show the environment along the view ray.
    if (length(worldPos) < 0.001 && albedoAlpha.a < 0.01) {
        vec4 viewPos = camera.invProj * vec4(fragTexCoord * 2.0 - 1.0, 1.0, 1.0);
        vec3 rayDir = normalize((camera.invView * vec4(viewPos.xyz / viewPos.w, 0.0)).xyz);
        outColor = vec4(skyAlong(rayDir), 1.0);
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

    vec3 F0 = baseReflectivity(albedo, metallic);

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

    // Determine mip level from roughness.
    //
    // Queried rather than hardcoded: IBLGenerator fills mip m of the prefilter
    // map with roughness m/(mipLevels-1), and mipLevels comes from the cubemap
    // size, which apps override (512 -> 10 mips, 256 -> 9). The old constant 4.0
    // assumed a 5-mip map, so a roughness-1.0 surface sampled mip 4 — convolved
    // for roughness 0.44 — and mips 5..9 were never read. Rough metals rendered
    // far too glossy.
    float maxReflectionLod = float(textureQueryLevels(prefilterMap) - 1);
    float mipLevel = roughness * maxReflectionLod;

    // Sample pre-filtered environment map
    vec3 prefilteredColor = textureLod(prefilterMap, R, mipLevel).rgb;

    // Sample BRDF integration LUT
    // x = scale, y = bias for specular
    vec2 brdf = texture(brdfLUT, vec2(max(dot(N, V), 0.0), roughness)).rg;

    vec3 specular = prefilteredColor * (kS * brdf.x + brdf.y);

    // ═══════════════════════════════════════════════════════════════
    // Combine and Output
    // ═══════════════════════════════════════════════════════════════

    vec3 ambient = (kD * diffuse + specular) * shading.iblIntensity;
    vec3 direct = vec3(0.0);
    // Shadowing of the first directional light, the sun; 1 when there is none.
    float sunLit = 1.0;
    bool sunSeen = false;

    // Dynamic lights from ECS via dot-path UBO
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
            // Directional light; the first one is the sun and casts the shadow map.
            L = normalize(-lightDirection);
            if (!sunSeen) {
                sunSeen = true;
                sunLit = sunVisibility(worldPos, N, L);
                attenFactor = sunLit;
            }
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
            direct += cookTorrance(N, V, L, albedo, metallic, roughness, F0)
                    * lColor * lIntensity * NdotL * attenFactor;
        }
    }

    // Sky light is dimmed in the sun's shadow too: whatever blocks the sun
    // usually hides part of the sky as well.
    vec3 color = ambient * mix(shadow.params.w, 1.0, sunLit) + direct;

    // Distance fog towards the sky behind the surface.
    vec3 toSurface = worldPos - camera.position.xyz;
    float fog = 1.0 - exp(-shading.fogDensity * max(length(toSurface) - shading.fogStart, 0.0));
    color = mix(color, skyAlong(normalize(toSurface)), fog);

    // DEBUG: Check for NaN/invalid final color - output RED if detected
    if (any(isnan(color)) || any(isinf(color))) {
        outColor = vec4(1.0, 0.0, 0.0, 1.0);  // RED = NaN in final color
        return;
    }

    // Output HDR color
    outColor = vec4(color, 1.0);
}
