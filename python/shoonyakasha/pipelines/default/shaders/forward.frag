#version 450
// Forward shading of blended materials (glTF alphaMode BLEND), over the
// lit opaque scene. Shadows come from the cascades directly: the shadow mask
// only covers the opaque surfaces behind.

#include "common.glsl"
#include "sk/pbr.glsl"
#include "material_draw.glsl"
#include "surface.glsl"

layout(set = 0, binding = 0) uniform Camera { DEFAULT_CAMERA_BLOCK } camera;
layout(set = 2, binding = 0) uniform Lights { DEFAULT_LIGHTS_BLOCK };
layout(set = 3, binding = 0) uniform samplerCube irradianceMap;
layout(set = 3, binding = 1) uniform samplerCube prefilterMap;
layout(set = 3, binding = 2) uniform sampler2D brdfLUT;
layout(set = 4, binding = 0) uniform sampler2DArrayShadow shadowMap;
layout(set = 5, binding = 0) uniform Settings { DEFAULT_SETTINGS_BLOCK } settings;
layout(set = 5, binding = 1) uniform Cascades { DEFAULT_CASCADES_BLOCK } cascades;

#include "csm.glsl"
#include "lights.glsl"

layout(location = 0) out vec4 outColor;

void main() {
    Surface s = evaluateSurface(surfaceBaseColor());
    vec3 V = normalize(camera.position.xyz - fragWorldPos);
    vec3 F0 = baseReflectivity(s.baseColor.rgb, s.metallic);

    float viewDepth = -(camera.view * vec4(fragWorldPos, 1.0)).z;
    vec3 L = -normalize(cascades.sunDirection.xyz);
    int cascade;
    float sunLit = sunVisibility(fragWorldPos, s.N, L, viewDepth,
                                        interleavedGradientNoise(gl_FragCoord.xy), cascade);
    int sunIndex = cascades.sunEnabled != 0u ? cascades.sunLightIndex : -1;

    vec3 color = directLight(fragWorldPos, s.N, V, s.baseColor.rgb, s.metallic, s.roughness, F0,
                             sunIndex, sunLit)
               + ambientLight(irradianceMap, prefilterMap, brdfLUT, s.N, V, s.baseColor.rgb,
                              s.metallic, s.roughness, F0)
                 * settings.iblIntensity * s.occlusion * mix(settings.shadowAmbient, 1.0, sunLit)
               + s.emissive;
    outColor = vec4(color, s.baseColor.a);
}
