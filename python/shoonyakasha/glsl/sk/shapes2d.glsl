//
// sk/shapes2d.glsl - 2D signed distances, antialiased coverage, compositing
//
//     #include "sk/shapes2d.glsl"
//
// Distances are negative inside a shape. fill() and stroke() turn a distance
// into coverage antialiased over one pixel with fwidth(), so they need
// derivatives: call them from a fragment shader in uniform control flow.
//

#ifndef SK_SHAPES2D_GLSL
#define SK_SHAPES2D_GLSL

const float SK_TAU = 6.28318531;

// Coverage of the region d < 0, antialiased over one pixel.
float fill(float d) {
    return clamp(0.5 - d / max(fwidth(d), 1e-5), 0.0, 1.0);
}

// Coverage of a line of half-width w along d = 0.
float stroke(float d, float w) {
    return fill(abs(d) - w);
}

// Premultiplied "over": paints colour at the given coverage on top of acc.
void over(inout vec4 acc, vec3 color, float alpha) {
    acc = vec4(color * alpha, alpha) + acc * (1.0 - alpha);
}

float sdBox(vec2 p, vec2 b) {
    vec2 d = abs(p) - b;
    return length(max(d, 0.0)) + min(max(d.x, d.y), 0.0);
}

// Equilateral triangle pointing up (+y), centred on its centroid; r is half the side.
float sdTriangle(vec2 p, float r) {
    const float k = sqrt(3.0);
    p.x = abs(p.x) - r;
    p.y = p.y + r / k;
    if (p.x + k * p.y > 0.0) p = vec2(p.x - k * p.y, -k * p.x - p.y) / 2.0;
    p.x -= clamp(p.x, -2.0 * r, 0.0);
    return -length(p) * sign(p.y);
}

// Vertical vesica: intersection of two circles of radius r whose centres are d apart from the axis.
float sdVesica(vec2 p, float r, float d) {
    p = abs(p);
    float b = sqrt(r * r - d * d);
    return ((p.y - b) * d > p.x * b) ? length(p - vec2(0.0, b)) : length(p - vec2(-d, 0.0)) - r;
}

// Pointed petal lying along x, centred on the origin.
float sdPetal(vec2 p, float halfLength, float halfWidth) {
    float r = 0.5 * (halfLength * halfLength / halfWidth + halfWidth);
    return sdVesica(p.yx, r, r - halfWidth);
}

// Approximate ellipse distance, exact on the axes.
float sdEllipse(vec2 p, vec2 radii) {
    return (length(p / radii) - 1.0) * min(radii.x, radii.y);
}

// Folds the plane into one of n equal angular sectors, centred on +x.
vec2 foldPolar(vec2 p, float n) {
    float sector = SK_TAU / n;
    float a = mod(atan(p.y, p.x) + 0.5 * sector, sector) - 0.5 * sector;
    return length(p) * vec2(cos(a), sin(a));
}

vec2 rotate(vec2 p, float a) {
    float c = cos(a);
    float s = sin(a);
    return vec2(c * p.x - s * p.y, s * p.x + c * p.y);
}

#endif
