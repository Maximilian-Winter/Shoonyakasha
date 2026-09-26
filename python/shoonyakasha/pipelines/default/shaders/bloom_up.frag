#version 450
// One step up the bloom chain: a 3x3 tent filter of the smaller mip, added
// onto this one by the pass's blend state. Mip 0 ends up holding the sum of
// every level.

#include "common.glsl"

layout(set = 0, binding = 0) uniform sampler2D source;
layout(set = 1, binding = 0) uniform Settings { DEFAULT_SETTINGS_BLOCK } settings;

layout(location = 0) in vec2 fragTexCoord;
layout(location = 0) out vec4 outColor;

void main() {
    vec2 t = settings.bloomRadius / vec2(textureSize(source, 0));
    vec2 uv = fragTexCoord;
    vec3 c = textureLod(source, uv, 0.0).rgb * 4.0;
    c += (textureLod(source, uv + vec2(-t.x, 0), 0.0).rgb + textureLod(source, uv + vec2(t.x, 0), 0.0).rgb +
          textureLod(source, uv + vec2(0, -t.y), 0.0).rgb + textureLod(source, uv + vec2(0, t.y), 0.0).rgb) * 2.0;
    c += textureLod(source, uv - t, 0.0).rgb + textureLod(source, uv + t, 0.0).rgb +
         textureLod(source, uv + vec2(-t.x, t.y), 0.0).rgb + textureLod(source, uv + vec2(t.x, -t.y), 0.0).rgb;
    outColor = vec4(c / 16.0, 1.0);
}
