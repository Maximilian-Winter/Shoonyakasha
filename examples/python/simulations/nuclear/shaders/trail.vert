#version 450
// Six vertices per nucleon: a ribbon from a marked nucleon (a free neutron)
// back along its path, from its position x to x - v * trailTime, which a
// free neutron moving in a straight line has covered in that time. The
// ribbon faces the camera and is as wide as a nucleon at each end, and at
// least 3 pixels.
// Unmarked nucleons, and those past the count, are moved off screen.

struct Nucleon {
    vec3 position;
    float type;
    vec3 velocity;
    float energy;
    vec3 force;
    uint nucleusId;
};

layout(std430, set = 0, binding = 0) readonly buffer Nucleons { Nucleon n[]; } nucleons;
layout(std140, set = 0, binding = 1) uniform View {
    mat4 view;
    mat4 proj;
    vec2 screen;
    uint count;
    float radius;
    float glowEnergy;
    float trailTime;      // time units of path a trail shows
    float time;
} view;
layout(std430, set = 0, binding = 2) readonly buffer Marks { uint mark[]; } marks;

layout(location = 0) out float vAlong;    // 0 at the nucleon, 1 at the tail
layout(location = 1) out float vAcross;   // -1 to 1 across the ribbon

const vec2 CORNERS[6] = vec2[](vec2(0.0, -1.0), vec2(0.0, 1.0), vec2(1.0, -1.0),
                               vec2(1.0, -1.0), vec2(0.0, 1.0), vec2(1.0, 1.0));

void main() {
    uint i = uint(gl_VertexIndex) / 6u;
    vec2 corner = CORNERS[gl_VertexIndex % 6];
    vAlong = corner.x;
    vAcross = corner.y;
    gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
    if (i >= view.count || marks.mark[i] == 0u) return;

    Nucleon nucleon = nucleons.n[i];
    vec4 headView = view.view * vec4(nucleon.position, 1.0);
    vec4 tailView = view.view * vec4(nucleon.position - nucleon.velocity * view.trailTime, 1.0);
    if (headView.z > -1e-2 || tailView.z > -1e-2) return;      // behind the camera
    vec4 head = view.proj * headView;
    vec4 tail = view.proj * tailView;

    vec2 halfScreen = 0.5 * view.screen;
    vec2 direction = head.xy / head.w * halfScreen - tail.xy / tail.w * halfScreen;
    direction = length(direction) > 1e-3 ? normalize(direction) : vec2(1.0, 0.0);
    vec2 normal = vec2(-direction.y, direction.x);

    vec4 end = corner.x < 0.5 ? head : tail;
    float depth = corner.x < 0.5 ? -headView.z : -tailView.z;
    float halfWidth = max(view.radius * abs(view.proj[1][1]) * halfScreen.y / depth, 1.5);   // pixels
    end.xy += normal * corner.y * halfWidth / halfScreen * end.w;
    gl_Position = end;
}
