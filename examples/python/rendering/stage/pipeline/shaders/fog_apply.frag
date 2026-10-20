#version 450
// The fog over the image (pass FogApply): for each pixel, the fog between
// the eye and its surface, from fogIntegrated at the pixel's depth. The
// pass blends "one, src_alpha": the HDR colour becomes the fog's light
// plus the colour times its transmittance.

#include "common.glsl"
#include "fog.glsl"

layout(set = 0, binding = 0) uniform sampler2DArray fogIntegrated;
layout(set = 0, binding = 1) uniform sampler2D gDepth;
layout(set = 0, binding = 2) uniform Fog { FOG_BLOCK } fog;
layout(set = 1, binding = 0) uniform Camera { DEFAULT_CAMERA_BLOCK } camera;

layout(location = 0) in vec2 fragTexCoord;
layout(location = 0) out vec4 outColor;

// fogIntegrated at uv, after `t` slices (continuous): layer k holds the fog
// up to the end of slice k, so between layers the value is interpolated,
// and before the first it is none.
vec4 fogAt(vec2 uv, float t, float slices) {
    float k = clamp(t - 1.0, -1.0, slices - 1.0);
    float k0 = floor(k);
    vec4 a = k0 < 0.0 ? vec4(0.0, 0.0, 0.0, 1.0) : textureLod(fogIntegrated, vec3(uv, k0), 0.0);
    vec4 b = textureLod(fogIntegrated, vec3(uv, min(k0 + 1.0, slices - 1.0)), 0.0);
    return mix(a, b, k - k0);
}

void main() {
    if (fog.grid.w <= 0.0) {
        outColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }
    float depth = textureLod(gDepth, fragTexCoord, 0.0).r;
    float viewDepth = depth >= 1.0 ? fog.grid.y
                                   : -viewPositionFromDepth(camera.invProj, fragTexCoord, depth).z;
    float slices = float(textureSize(fogIntegrated, 0).z);
    vec4 f = fogAt(fragTexCoord, depthSlice(viewDepth, slices, fog.grid), slices);
    outColor = vec4(f.rgb, f.a);
}
