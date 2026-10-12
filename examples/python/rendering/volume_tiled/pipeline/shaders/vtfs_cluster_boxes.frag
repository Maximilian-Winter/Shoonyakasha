#version 450
// VTFS debug overlay: each active cluster's box in the cluster's colour; see
// vtfs_cluster_boxes.vert.

layout(location = 0) in vec3 fragColor;
layout(location = 0) out vec4 outColor;

void main() {
    outColor = vec4(fragColor * 0.25, 1.0);
}
