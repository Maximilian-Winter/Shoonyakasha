#version 450
// Bloom, exposure and tonemapping to the swapchain: ACES filmic, Khronos
// PBR Neutral or AgX, by settings.toneMapper (0, 1, 2). The
// swapchain is sRGB, so the hardware encodes on write; the dither breaks up
// banding in dark gradients.
//
// When the scene renders larger than the window (the engine's render scale),
// each window pixel tone maps the scene pixels it covers and averages them:
// averaging after tone mapping keeps a bright highlight from swallowing the
// edge beside it.

#include "common.glsl"
#include "sk/tonemap.glsl"

layout(set = 0, binding = 0) uniform sampler2D hdrColor;       // depth_of_field.frag's output
layout(set = 0, binding = 1) uniform Settings { DEFAULT_SETTINGS_BLOCK } settings;
layout(set = 0, binding = 2) uniform sampler2D bloom;          // mip 0 of the chain: every level summed
layout(set = 0, binding = 3) uniform sampler2D exposureImage;  // 1x1, from exposure.comp
layout(set = 0, binding = 4) uniform sampler2D accumulated;    // capture mode's average, accumulate.comp

layout(location = 0) in vec2 fragTexCoord;
layout(location = 0) out vec4 outColor;

const float BLOOM_LEVELS = 6.0;   // mips of bloomChain in pipeline.json

vec3 sceneAt(vec2 uv) {
    return settings.accumulate != 0u ? textureLod(accumulated, uv, 0.0).rgb
                                     : textureLod(hdrColor, uv, 0.0).rgb;
}

// Exposure, vignette and tone mapping of one scene sample.
vec3 develop(vec3 hdr, vec3 bloomColor, float exposure) {
    hdr = mix(hdr, bloomColor, clamp(settings.bloomIntensity, 0.0, 1.0));
    hdr *= exposure;
    // Vignette: corners darkened by `vignette` at most, smoothly from the centre.
    if (settings.vignette > 0.0) {
        vec2 d = (fragTexCoord - 0.5) * vec2(1.0, 0.75);
        hdr *= 1.0 - settings.vignette * smoothstep(0.1, 0.65, length(d) * 1.6);
    }
    return settings.toneMapper == 1u ? PBRNeutral(hdr)
         : settings.toneMapper == 2u ? AgX(hdr)
                                     : ACESFilm(hdr);
}

void main() {
    // Scene pixels per window pixel, along each axis
    vec2 sceneSize = vec2(textureSize(hdrColor, 0));
    vec2 windowTexel = vec2(abs(dFdx(fragTexCoord.x)), abs(dFdy(fragTexCoord.y)));
    vec2 ratio = sceneSize * windowTexel;
    // Samples per axis: one per scene pixel covered, up to 4
    ivec2 taps = ivec2(clamp(ceil(ratio - 0.01), vec2(1.0), vec2(4.0)));

    if (settings.debugView != 0u) {
        outColor = vec4(clamp(sceneAt(fragTexCoord), 0.0, 1.0), 1.0);
        return;
    }

    vec3 bloomColor = textureLod(bloom, fragTexCoord, 0.0).rgb / BLOOM_LEVELS;
    float exposure = settings.exposure;
    if (settings.autoExposure != 0u) {
        float adapted = texelFetch(exposureImage, ivec2(0), 0).r;
        if (adapted > 0.0 && !isinf(adapted)) exposure *= adapted;
    }

    vec3 mapped = vec3(0.0);
    for (int y = 0; y < taps.y; ++y) {
        for (int x = 0; x < taps.x; ++x) {
            // The centres of taps.x by taps.y equal parts of the window pixel
            vec2 offset = (vec2(x, y) + 0.5) / vec2(taps) - 0.5;
            mapped += develop(sceneAt(fragTexCoord + offset * windowTexel), bloomColor, exposure);
        }
    }
    mapped /= float(taps.x * taps.y);

    float dither = (interleavedGradientNoise(gl_FragCoord.xy) - 0.5) / 255.0;
    // Film grain: a little noise, new each frame and weaker in the
    // highlights, which also keeps video encoders from banding gradients.
    if (settings.grain > 0.0) {
        float n = interleavedGradientNoise(gl_FragCoord.xy + 5.588238 * float(settings.frame % 64u)) - 0.5;
        float lum = dot(mapped, vec3(0.2126, 0.7152, 0.0722));
        mapped += n * settings.grain * (1.0 - 0.7 * lum);
    }
    outColor = vec4(max(mapped + dither, 0.0), 1.0);
}
