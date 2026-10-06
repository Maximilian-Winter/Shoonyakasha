// surface.glsl - a glTF metallic-roughness material evaluated at a fragment.
//
// The including fragment shader declares the material textures at set 1 and
// includes material_draw.glsl, and takes the gbuffer.vert outputs as inputs.

#ifndef DEFAULT_SURFACE_GLSL
#define DEFAULT_SURFACE_GLSL

layout(set = 1, binding = 0) uniform sampler2D albedoMap;
layout(set = 1, binding = 1) uniform sampler2D normalMap;
layout(set = 1, binding = 2) uniform sampler2D metallicRoughnessMap;
layout(set = 1, binding = 3) uniform sampler2D aoMap;
layout(set = 1, binding = 4) uniform sampler2D emissiveMap;

layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragWorldNormal;
layout(location = 2) in vec2 fragTexCoord;
layout(location = 3) in vec3 fragColor;

struct Surface {
    vec4 baseColor;   // rgb albedo, a coverage
    vec3 N;           // world normal, facing the viewer
    float metallic;
    float roughness;
    float occlusion;
    vec3 emissive;
};

// Tangent frame from screen-space derivatives: meshes carry no tangents.
mat3 derivativeTBN(vec3 N, vec3 p, vec2 uv) {
    vec3 dp1 = dFdx(p), dp2 = dFdy(p);
    vec2 duv1 = dFdx(uv), duv2 = dFdy(uv);
    vec3 dp2perp = cross(dp2, N);
    vec3 dp1perp = cross(N, dp1);
    vec3 T = dp2perp * duv1.x + dp1perp * duv2.x;
    vec3 B = dp2perp * duv1.y + dp1perp * duv2.y;
    float invMax = inversesqrt(max(max(dot(T, T), dot(B, B)), 1e-20));
    return mat3(T * invMax, B * invMax, N);
}

vec4 surfaceBaseColor() {
    return texture(albedoMap, fragTexCoord) * draw.baseColorFactor * vec4(fragColor, 1.0);
}

Surface evaluateSurface(vec4 baseColor) {
    Surface s;
    s.baseColor = baseColor;

    vec3 N = normalize(fragWorldNormal);
    // Double-sided materials, and single-sided ones seen from behind, shade
    // the side facing the camera.
    if (!gl_FrontFacing) N = -N;
    if (draw.hasNormalMap > 0.5) {
        vec3 t = texture(normalMap, fragTexCoord).xyz * 2.0 - 1.0;
        vec3 mapped = derivativeTBN(N, fragWorldPos, fragTexCoord) * t;
        if (dot(mapped, mapped) > 1e-6) N = normalize(mapped);
    }
    s.N = N;

    s.metallic = draw.metallicFactor;
    s.roughness = draw.roughnessFactor;
    if (draw.hasMetalRoughMap > 0.5) {
        vec4 mr = texture(metallicRoughnessMap, fragTexCoord);   // glTF: g roughness, b metallic
        s.metallic *= mr.b;
        s.roughness *= mr.g;
    }
    s.metallic = clamp(s.metallic, 0.0, 1.0);
    s.roughness = clamp(s.roughness, 0.045, 1.0);

    s.occlusion = texture(aoMap, fragTexCoord).r;   // no texture: the fallback is 1
    s.emissive = texture(emissiveMap, fragTexCoord).rgb * draw.emissiveFactor.rgb;
    return s;
}

#endif
