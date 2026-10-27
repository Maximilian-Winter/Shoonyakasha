#version 450
// A point shaded as a sphere: a disc, lit from above and behind the
// camera, with a rim, brightening and whitening as the nucleon's kinetic
// energy rises. Its depth is the sphere's surface.

layout(location = 0) in vec3 vColor;
layout(location = 1) in float vGlow;
layout(location = 2) in vec3 vCenterView;

layout(std140, set = 0, binding = 1) uniform View {
    mat4 view;
    mat4 proj;
    vec2 screen;
    uint count;
    float radius;
    float glowEnergy;
} view;

layout(location = 0) out vec4 outColor;

void main() {
    vec2 d = gl_PointCoord * 2.0 - 1.0;
    d.y = -d.y;
    float r2 = dot(d, d);
    if (r2 > 1.0) discard;
    vec3 normal = vec3(d, sqrt(1.0 - r2));

    vec3 light = normalize(vec3(0.4, 0.7, 0.6));
    float diffuse = max(dot(normal, light), 0.0);
    float rim = pow(1.0 - normal.z, 2.0);
    vec3 base = vColor * (0.18 + 0.82 * diffuse) + vColor * rim * 0.35;
    vec3 hot = mix(vColor, vec3(1.0, 0.95, 0.85), 0.6) * (1.5 + 2.0 * vGlow);
    outColor = vec4(mix(base, hot, vGlow * 0.85), 1.0);

    vec4 surface = view.proj * vec4(vCenterView + vec3(0.0, 0.0, normal.z * view.radius), 1.0);
    gl_FragDepth = surface.z / surface.w;
}
