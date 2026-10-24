#version 450
//
// The head-up display's symbols, added over the finished image. Every pixel
// is tested by the direction it looks in, so the symbols sit at infinity
// (collimated) and stay on what they mark as the ship rolls:
//   - the horizon, square to the planet's up;
//   - a pitch ladder: rungs every 10 degrees above and below it, beside the
//     nose's heading and within 35 degrees of the nose's pitch, dashed below
//     the horizon;
//   - the prograde marker, a ring where the ship is moving (when it moves);
//   - a cross at the nose.
// Distances are angles, measured in pixels by how fast the direction
// changes across the screen.
//

#define CAMERA_SET 0
#include "camera.glsl"

layout(set = 1, binding = 0) uniform HudParams {
    vec4 up;          // xyz: the planet's up at the ship
    vec4 prograde;    // xyz: unit direction of the velocity; w: 1 to show the marker
    vec4 colour;      // rgb: the symbols' colour
} hud;

layout(location = 0) in vec2 fragTexCoord;
layout(location = 0) out vec4 outColor;

const float DEGREE = 0.01745329252;

// 1 on a line `distance` pixels away, fading over a pixel.
float line(float distance, float halfWidth) {
    return 1.0 - smoothstep(halfWidth - 0.5, halfWidth + 0.5, distance);
}

void main() {
    vec2 ndc = fragTexCoord * 2.0 - 1.0;
    vec4 near = camera.invProj * vec4(ndc, 1.0, 1.0);
    mat3 toWorld = mat3(camera.invView);
    vec3 d = normalize(toWorld * (near.xyz / near.w));
    float pixel = max(length(fwidth(d)), 1e-6);    // radians across one pixel

    vec3 up = normalize(hud.up.xyz);
    vec3 nose = normalize(toWorld * vec3(0.0, 0.0, -1.0));
    vec3 noseRight = normalize(toWorld * vec3(1.0, 0.0, 0.0));
    vec3 noseUp = normalize(toWorld * vec3(0.0, 1.0, 0.0));

    // Heading frame: the nose's direction along the horizon.
    vec3 level = nose - up * dot(nose, up);
    if (length(level) < 1e-3) level = noseUp - up * dot(noseUp, up);
    level = normalize(level);
    vec3 levelRight = cross(level, up);

    float elevation = asin(clamp(dot(d, up), -1.0, 1.0));
    vec3 along = d - up * dot(d, up);
    float azimuth = abs(atan(dot(along, levelRight), dot(along, level))) / DEGREE;
    float noseElevation = asin(clamp(dot(nose, up), -1.0, 1.0));

    float ink = 0.0;
    // The horizon, all the way across.
    ink = max(ink, line(abs(elevation) / pixel, 1.0));
    // The ladder's rungs.
    for (int k = -9; k <= 9; ++k) {
        if (k == 0) continue;
        float rung = float(k) * 10.0 * DEGREE;
        if (abs(rung - noseElevation) > 35.0 * DEGREE) continue;
        bool beside = azimuth > 2.5 && azimuth < 9.0;
        bool dash = k > 0 || fract(azimuth * 0.8) < 0.55;
        if (beside && dash) ink = max(ink, line(abs(elevation - rung) / pixel, 0.75));
    }
    // The prograde marker: a ring 9 pixels across the middle, and a dot.
    if (hud.prograde.w > 0.5) {
        float off = acos(clamp(dot(d, normalize(hud.prograde.xyz)), -1.0, 1.0)) / pixel;
        ink = max(ink, line(abs(off - 9.0), 0.9));
        ink = max(ink, line(off, 1.5));
    }
    // The cross at the nose, with a gap in the middle.
    float noseOff = acos(clamp(dot(d, nose), -1.0, 1.0)) / pixel;
    if (noseOff > 4.0 && noseOff < 16.0) {
        ink = max(ink, line(abs(dot(d, noseRight)) / pixel, 0.75));
        ink = max(ink, line(abs(dot(d, noseUp)) / pixel, 0.75));
    }

    outColor = vec4(hud.colour.rgb * ink, 1.0);
}
