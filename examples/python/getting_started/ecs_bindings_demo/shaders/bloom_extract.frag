#version 450
//
// Bloom Extract — Bright Pixel Threshold
//
// Extracts pixels whose luminance exceeds a threshold,
// with a soft knee to avoid harsh cutoff artefacts.
// These bright fragments shall be blurred and composited
// to create the illusion of scattered light.
//

layout(set = 0, binding = 0) uniform sampler2D hdrColor;

layout(location = 0) in vec2 fragTexCoord;
layout(location = 0) out vec4 outColor;

void main() {
    vec3 color = texture(hdrColor, fragTexCoord).rgb;

    // Perceptual luminance (Rec. 709 coefficients)
    float luminance = dot(color, vec3(0.2126, 0.7152, 0.0722));

    // Soft knee threshold — gradual transition rather than hard cutoff
    // Lower threshold catches more colourful particles for richer bloom
    float threshold = 0.5;
    float softKnee = 0.6;

    float knee = threshold * softKnee;
    float soft = luminance - threshold + knee;
    soft = clamp(soft / (2.0 * knee + 0.00001), 0.0, 1.0);
    soft = soft * soft;

    float contribution = max(soft, step(threshold, luminance));

    // Scale by how much the luminance exceeds threshold
    float brightness = max(luminance - threshold, 0.0) / max(luminance, 0.001);
    brightness = max(brightness, contribution * 0.5);

    outColor = vec4(color * brightness, 1.0);
}
