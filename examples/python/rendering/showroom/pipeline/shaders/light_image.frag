#version 450
// The top of the lightImage chain: the application's image for lights
// ("externalImage": "lightImage", white until it sets one), resampled to
// the chain's size. Each texel averages a 4 x 4 grid of bilinear taps over
// its footprint, so an image larger than the chain does not alias.

layout(set = 0, binding = 0) uniform sampler2D source;

layout(location = 0) in vec2 fragTexCoord;
layout(location = 0) out vec4 outColor;

void main() {
    vec2 footprint = fwidth(fragTexCoord);
    vec3 sum = vec3(0.0);
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            sum += texture(source, fragTexCoord + (vec2(x, y) - 1.5) * 0.25 * footprint).rgb;
        }
    }
    outColor = vec4(sum / 16.0, 1.0);
}
