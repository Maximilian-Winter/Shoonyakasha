// bloom_down_body.glsl - one step down the bloom chain: a 13-tap filter (Jimenez 2014, "Next
// generation post processing in Call of Duty: Advanced Warfare"). From the
// HDR image itself (KARIS_AVERAGE) each 2x2 group is weighted by 1/(1+luma),
// so single very bright pixels do not flicker into large blobs.

layout(set = 0, binding = 0) uniform sampler2D source;

layout(location = 0) in vec2 fragTexCoord;
layout(location = 0) out vec4 outColor;

float karisWeight(vec3 c) {
    return 1.0 / (1.0 + dot(c, vec3(0.2126, 0.7152, 0.0722)));
}

void main() {
    vec2 t = 1.0 / vec2(textureSize(source, 0));
    vec2 uv = fragTexCoord;
    vec3 a = textureLod(source, uv + t * vec2(-2, -2), 0.0).rgb;
    vec3 b = textureLod(source, uv + t * vec2( 0, -2), 0.0).rgb;
    vec3 c = textureLod(source, uv + t * vec2( 2, -2), 0.0).rgb;
    vec3 d = textureLod(source, uv + t * vec2(-2,  0), 0.0).rgb;
    vec3 e = textureLod(source, uv, 0.0).rgb;
    vec3 f = textureLod(source, uv + t * vec2( 2,  0), 0.0).rgb;
    vec3 g = textureLod(source, uv + t * vec2(-2,  2), 0.0).rgb;
    vec3 h = textureLod(source, uv + t * vec2( 0,  2), 0.0).rgb;
    vec3 i = textureLod(source, uv + t * vec2( 2,  2), 0.0).rgb;
    vec3 j = textureLod(source, uv + t * vec2(-1, -1), 0.0).rgb;
    vec3 k = textureLod(source, uv + t * vec2( 1, -1), 0.0).rgb;
    vec3 l = textureLod(source, uv + t * vec2(-1,  1), 0.0).rgb;
    vec3 m = textureLod(source, uv + t * vec2( 1,  1), 0.0).rgb;

#ifdef KARIS_AVERAGE
    vec3 g0 = (a + b + d + e) * 0.25, g1 = (b + c + e + f) * 0.25;
    vec3 g2 = (d + e + g + h) * 0.25, g3 = (e + f + h + i) * 0.25;
    vec3 g4 = (j + k + l + m) * 0.25;
    float w0 = karisWeight(g0) * 0.125, w1 = karisWeight(g1) * 0.125;
    float w2 = karisWeight(g2) * 0.125, w3 = karisWeight(g3) * 0.125;
    float w4 = karisWeight(g4) * 0.5;
    vec3 color = (g0 * w0 + g1 * w1 + g2 * w2 + g3 * w3 + g4 * w4) / (w0 + w1 + w2 + w3 + w4);
#else
    vec3 color = e * 0.125 + (a + c + g + i) * 0.03125 + (b + d + f + h) * 0.0625 + (j + k + l + m) * 0.125;
#endif
    // Keep NaN and infinity from a bad pixel out of the whole chain.
    color = clamp(color, vec3(0.0), vec3(65000.0));
    if (any(isnan(color))) color = vec3(0.0);
    outColor = vec4(color, 1.0);
}
