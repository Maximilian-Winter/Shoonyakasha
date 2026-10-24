#version 450
//
// The orbit path's fragment shader: one colour, written after tone mapping,
// fading in between NEAR_FADE and FAR_FADE metres from the camera so the
// path does not start in the viewer's face when the camera is the ship.
//

layout(location = 0) in float fragDistance;

layout(location = 0) out vec4 outColor;

const vec3 COLOUR = vec3(0.35, 0.85, 1.0);
const float NEAR_FADE = 200.0;
const float FAR_FADE = 2000.0;

void main() {
    outColor = vec4(COLOUR, 0.85 * smoothstep(NEAR_FADE, FAR_FADE, fragDistance));
}
