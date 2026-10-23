#version 450
// Weighted blended order-independent transparency, resolved (pass
// TransparentComposite): the weighted average of the transparent surfaces'
// colour (transparentAccum rgb over its a) alpha-blended over the HDR target
// by their combined coverage, 1 - transparentReveal. Nothing where no
// transparent surface was accumulated, which is everywhere without
// settings.oit.

layout(set = 0, binding = 0) uniform sampler2D transparentAccum;
layout(set = 0, binding = 1) uniform sampler2D transparentReveal;

layout(location = 0) in vec2 fragTexCoord;
layout(location = 0) out vec4 outColor;

void main() {
    ivec2 p = ivec2(gl_FragCoord.xy);
    float coverage = 1.0 - texelFetch(transparentReveal, p, 0).r;
    vec4 accum = texelFetch(transparentAccum, p, 0);
    if (coverage <= 0.0 || accum.a <= 0.0) discard;
    outColor = vec4(accum.rgb / max(accum.a, 1e-5), coverage);
}
