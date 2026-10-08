#version 450
// One level of the lightImage chain from the one above it, blurred as it is
// halved: a 3 x 3 tent of bilinear taps a source texel apart, so each level
// is the image as a lobe twice as wide sees it. Rectangle lights read the
// level that matches their lobe's footprint (area_lights.glsl).

layout(set = 0, binding = 0) uniform sampler2D source;

layout(location = 0) in vec2 fragTexCoord;
layout(location = 0) out vec4 outColor;

void main() {
    vec2 texel = 1.0 / vec2(textureSize(source, 0));
    vec3 sum = vec3(0.0);
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            float weight = float((2 - abs(x)) * (2 - abs(y)));
            sum += weight * texture(source, fragTexCoord + vec2(x, y) * texel).rgb;
        }
    }
    outColor = vec4(sum / 16.0, 1.0);
}
