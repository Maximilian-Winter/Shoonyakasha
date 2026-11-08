#version 450
//
// The entities' fragment shader (the blocks, the ship's model): the base
// colour times the vertex colour times the material's albedo texture (white
// where it has none), with face lines where the gridStrength material
// parameter asks for them (shading.glsl), in HDR. Both sides of each face
// are drawn, a back face lit with its normal turned towards the camera.
//
// A metallic-roughness material: its metallic and roughness factors (0 and
// 0.8 where the material has none) times the metallic-roughness map's blue
// and green, the roughness at least MIN_ROUGHNESS; its normal map, with its
// normal scale, in a tangent frame from the screen-space changes of the
// position and the texture coordinates. The sun (atmosphere.glsl's
// sunlightAt, times its shadow) lights it as GGX with Smith's
// height-correlated visibility and Schlick's Fresnel, over a Lambert
// diffuse of the base colour less the metal; the sky probe (sky_probe.glsl)
// lights it from everywhere else: its harmonics' irradiance on the diffuse,
// and its level for the roughness along the reflected view times the
// split-sum table's scale and bias of F0 (brdfLut) on the specular. The
// local lights (lights.glsl) light its diffuse colour.
//

#define CAMERA_SET 0
#include "camera.glsl"
#include "terrain.glsl"
#include "atmosphere.glsl"
#include "lights.glsl"
#include "shading.glsl"
#include "shadow.glsl"
#include "clouds.glsl"
#include "sky_probe.glsl"

layout(push_constant) uniform PushConstants {
    mat4 model;
    vec4 baseColorFactor;
    float gridStrength;
    float metallicFactor;
    float roughnessFactor;
    float normalScale;
    vec4 emissiveFactor;
    float hasNormalMap;
    float hasMetalRoughMap;
} push;

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec3 fragColor;
layout(location = 2) in vec2 fragFaceUv;
layout(location = 3) in vec3 fragRelativePos;

layout(location = 0) out vec4 outColor;

layout(set = 1, binding = 0) uniform sampler2D albedoMap;
layout(set = 1, binding = 2) uniform sampler2D normalMap;
layout(set = 1, binding = 3) uniform sampler2D metallicRoughnessMap;

const float MIN_ROUGHNESS = 0.04;

// `normal` tilted by the normal map's texel `t` (tangent space, its slopes
// times `scale`), in the frame the position's and `uv`'s screen-space
// changes give (Schuler 2006).
vec3 mappedNormal(vec3 normal, vec3 position, vec2 uv, vec3 t, float scale) {
    vec3 dp1 = dFdx(position), dp2 = dFdy(position);
    vec2 duv1 = dFdx(uv), duv2 = dFdy(uv);
    vec3 dp2perp = cross(dp2, normal), dp1perp = cross(normal, dp1);
    vec3 tangent = dp2perp * duv1.x + dp1perp * duv2.x;
    vec3 bitangent = dp2perp * duv1.y + dp1perp * duv2.y;
    float size = max(dot(tangent, tangent), dot(bitangent, bitangent));
    if (size < 1e-20) return normal;
    float inverse = inversesqrt(size);
    vec3 n = normalize(vec3(t.xy * scale, t.z));
    return normalize(tangent * inverse * n.x + bitangent * inverse * n.y + normal * n.z);
}

void main() {
    vec3 base = push.baseColorFactor.rgb * fragColor * texture(albedoMap, fragFaceUv).rgb;
    vec3 albedo = withGrid(base, push.gridStrength, fragFaceUv);
    vec3 p = camera.cameraFromCentre.xyz + fragRelativePos;
    vec3 surface = normalize(gl_FrontFacing ? fragNormal : -fragNormal);
    float sunShare = sunShadow(fragRelativePos, surface) * cloudShadow(p);
    vec3 normal = surface;
    if (push.hasNormalMap > 0.5) {
        vec3 t = texture(normalMap, fragFaceUv).xyz * 2.0 - 1.0;
        normal = mappedNormal(surface, fragRelativePos, fragFaceUv, t, push.normalScale);
    }
    float metallic = push.metallicFactor, roughness = push.roughnessFactor;
    if (push.hasMetalRoughMap > 0.5) {
        vec4 mr = texture(metallicRoughnessMap, fragFaceUv);
        roughness *= mr.g;
        metallic *= mr.b;
    }
    roughness = clamp(roughness, MIN_ROUGHNESS, 1.0);
    metallic = clamp(metallic, 0.0, 1.0);

    vec3 view = -normalize(fragRelativePos);
    float nv = max(dot(normal, view), 1e-4);
    vec3 f0 = mix(vec3(0.04), albedo, metallic);
    vec3 diffuse = albedo * (1.0 - metallic);

    // The sun.
    vec3 sun = atmosphere.sun.xyz;
    float nl = max(dot(normal, sun), 0.0);
    vec3 direct = vec3(0.0);
    if (nl > 0.0) {
        vec3 h = normalize(sun + view);
        float nh = max(dot(normal, h), 0.0), vh = max(dot(view, h), 0.0);
        float a2 = roughness * roughness * roughness * roughness;
        float d = a2 / (ATMOSPHERE_PI * pow(nh * nh * (a2 - 1.0) + 1.0, 2.0));
        float visibility = 0.5 / (nl * sqrt(nv * nv * (1.0 - a2) + a2) + nv * sqrt(nl * nl * (1.0 - a2) + a2));
        vec3 fresnel = f0 + (1.0 - f0) * pow(1.0 - vh, 5.0);
        direct = (diffuse * (1.0 - fresnel) / ATMOSPHERE_PI + d * visibility * fresnel)
               * sunlightAt(p) * nl * sunShare;
    }

    // The sky probe.
    vec2 split = texture(brdfLut, vec2(nv, roughness)).rg;
    vec3 reflected = skyReflection(reflect(-view, normal), roughness) * (f0 * split.x + split.y);
    vec3 ambient = diffuse / ATMOSPHERE_PI * skyIrradiance(normal);

    outColor = vec4(direct + ambient + reflected + localLights(diffuse, fragRelativePos, normal, false), 1.0);
}
