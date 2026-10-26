#version 450
//
// The reentry glow, added over the finished image in ship mode: light from
// the air heated ahead of the ship (flight.reentry_glow gives its strength,
// hud.reentry.x). Each pixel is tested by the direction it looks in: a
// small white-pink core straight along the direction of travel, and an
// orange sheath that grows from about 18 degrees off it towards the sides
// and fades behind, mostly clear but for thin streaks that flow back from
// the front, all flickering with time. Pixels nearer than
// COCKPIT_DISTANCE (the cockpit's frame and dashboard) get none.
//

#define CAMERA_SET 0
#include "camera.glsl"
#include "terrain.glsl"

layout(set = 1, binding = 0) uniform HudParams {
    vec4 up;
    vec4 prograde;    // xyz: unit direction of the velocity
    vec4 colour;
    vec4 reentry;     // x: the glow's strength, 0 to 1
} hud;

layout(set = 2, binding = 1) uniform sampler2D sceneDepth;

layout(location = 0) in vec2 fragTexCoord;
layout(location = 0) out vec4 outColor;

const float COCKPIT_DISTANCE = 20.0;    // metres
const vec3 HEART = vec3(1.0, 0.85, 0.75);
const vec3 EDGE = vec3(1.0, 0.42, 0.12);

void main() {
    float strength = hud.reentry.x;
    vec2 ndc = fragTexCoord * 2.0 - 1.0;
    float depth = texture(sceneDepth, fragTexCoord).r;
    if (strength <= 0.0) {
        outColor = vec4(0.0);
        return;
    }
    if (depth > 0.0) {
        vec4 viewPos = camera.invProj * vec4(ndc, depth, 1.0);
        if (length(viewPos.xyz / viewPos.w) < COCKPIT_DISTANCE) {
            outColor = vec4(0.0);
            return;
        }
    }
    vec4 near = camera.invProj * vec4(ndc, 1.0, 1.0);
    vec3 d = normalize(mat3(camera.invView) * (near.xyz / near.w));
    vec3 forward = normalize(hud.prograde.xyz);

    float along = dot(d, forward);                       // 1 straight ahead
    vec3 side = d - forward * along;
    vec3 a = normalize(abs(forward.y) < 0.9 ? cross(forward, vec3(0, 1, 0)) : cross(forward, vec3(1, 0, 0)));
    vec3 b = cross(forward, a);
    float around = atan(dot(side, b), dot(side, a));     // angle about the direction of travel

    // A small hot core straight ahead, and a streaked sheath that grows
    // towards the sides of the view and fades behind.
    float front = pow(max(along, 0.0), 40.0);
    float sheath = smoothstep(0.95, 0.7, along) * smoothstep(-0.3, 0.3, along);
    float t = camera.time;
    // Thin streaks: the noise raised to a power, so most of the sheath is clear.
    float n = 0.5 + 0.5 * noise3(vec3(cos(around) * 9.0, sin(around) * 9.0, along * 5.0 + t * 5.0));
    float streaks = 0.15 + 0.85 * pow(n, 4.0) * 4.0;
    float flicker = 0.85 + 0.15 * noise3(vec3(t * 7.0, 0.5, 0.5));
    vec3 colour = mix(EDGE, HEART, front);
    outColor = vec4(colour * (front * 0.6 + sheath * streaks * 0.3) * flicker * strength, 0.0);
}
