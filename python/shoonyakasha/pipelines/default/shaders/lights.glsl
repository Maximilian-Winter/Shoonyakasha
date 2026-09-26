// lights.glsl - direct light from scene.lights[i], shared by the lighting
// and forward passes. The including shader declares the Lights block
// without an instance name (DEFAULT_LIGHTS_BLOCK) and includes sk/pbr.glsl.

#ifndef DEFAULT_LIGHTS_GLSL
#define DEFAULT_LIGHTS_GLSL

// Light from every scene light. `sunIndex` is the light shadowed by
// `sunVisibility`; -1 when none is.
vec3 directLight(vec3 worldPos, vec3 N, vec3 V, vec3 albedo, float metallic, float roughness,
                 vec3 F0, int sunIndex, float sunVisibility) {
    vec3 total = vec3(0.0);
    for (uint i = 0u; i < min(lightCount, uint(MAX_LIGHTS)); ++i) {
        float type = lightsPositionType[i].w;
        vec3 color = lightsColorIntensity[i].rgb * lightsColorIntensity[i].w;
        vec3 direction = lightsDirectionRange[i].xyz;
        float range = lightsDirectionRange[i].w;
        vec4 attenuation = lightsAttenuation[i];

        vec3 L;
        float falloff = 1.0;
        if (type < 0.5) {                       // directional
            L = -normalize(direction);
            if (int(i) == sunIndex) falloff = sunVisibility;
        } else {                                // point or spot
            vec3 toLight = lightsPositionType[i].xyz - worldPos;
            float dist = length(toLight);
            L = toLight / max(dist, 1e-4);
            falloff = 1.0 / max(attenuation.x + attenuation.y * dist + attenuation.z * dist * dist, 1e-4);
            if (range > 0.0) {
                // Smooth window to zero at the range.
                float r = clamp(1.0 - pow(dist / range, 4.0), 0.0, 1.0);
                falloff *= r * r;
            }
            if (type > 1.5) {
                float cosOuter = attenuation.w;
                float theta = dot(L, -normalize(direction));
                falloff *= smoothstep(cosOuter, mix(cosOuter, 1.0, 0.1), theta);
            }
        }

        float NdotL = dot(N, L);
        if (NdotL > 0.0 && falloff > 1e-4) {
            total += cookTorrance(N, V, L, albedo, metallic, roughness, F0) * color * NdotL * falloff;
        }
    }
    return total;
}

// Image-based light, diffuse irradiance and split-sum specular apart, for
// occluding them differently.
void ambientLightParts(samplerCube irradianceMap, samplerCube prefilterMap, sampler2D brdfLUT,
                       vec3 N, vec3 V, vec3 albedo, float metallic, float roughness, vec3 F0,
                       out vec3 diffuse, out vec3 specular) {
    float NdotV = max(dot(N, V), 1e-4);
    vec3 kS = fresnelSchlickRoughness(NdotV, F0, roughness);
    vec3 kD = (1.0 - kS) * (1.0 - metallic);
    diffuse = kD * texture(irradianceMap, N).rgb * albedo;

    float maxLod = float(textureQueryLevels(prefilterMap) - 1);
    vec3 R = reflect(-V, N);
    vec3 prefiltered = textureLod(prefilterMap, R, roughness * maxLod).rgb;
    vec2 brdf = texture(brdfLUT, vec2(NdotV, roughness)).rg;
    specular = prefiltered * (F0 * brdf.x + brdf.y);
}

vec3 ambientLight(samplerCube irradianceMap, samplerCube prefilterMap, sampler2D brdfLUT,
                  vec3 N, vec3 V, vec3 albedo, float metallic, float roughness, vec3 F0) {
    vec3 diffuse, specular;
    ambientLightParts(irradianceMap, prefilterMap, brdfLUT, N, V, albedo, metallic, roughness, F0,
                      diffuse, specular);
    return diffuse + specular;
}

#endif
