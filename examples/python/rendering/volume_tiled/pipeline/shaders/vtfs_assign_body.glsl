// vtfs_assign_body.glsl - list the lights that reach each active cluster
// (van Oosten, Algorithm 8.4). One workgroup per cluster of uniqueClusters,
// dispatched indirectly from the count vtfs_indirect_args.comp wrote, and
// numbered across x and y as it spreads them. The cluster's lights are
// gathered in shared memory,
// then one atomic add on indexCount reserves their place in the global
// lightIndexList, and lightGrid[cluster] records (offset, count).
//
// vtfs_assign_bvh.comp, built with BVH, finds them by walking the light BVH
// (Algorithm 7.2): 32 threads test the 32 children of the node on top of a
// shared stack, push the inner nodes that touch the cluster, and collect
// the lights that reach it. vtfs_assign_all.comp tests every light, the
// paper's Volume Tiled Forward Shading without the BVH.

#include "vtfs_compute.glsl"

#ifdef BVH
#define GROUP_SIZE 32u
#else
#define GROUP_SIZE 256u
#endif

layout(local_size_x = GROUP_SIZE) in;

VTFS_COUNTERS(restrict)
VTFS_UNIQUE_CLUSTERS(readonly)
VTFS_CLUSTER_AABBS(readonly)
VTFS_LIGHT_GRID(writeonly)
VTFS_LIGHT_INDEX_LIST(writeonly)
VTFS_LIGHTS_VIEW(readonly)
#ifdef BVH
VTFS_VALUES_A(readonly)
VTFS_BVH_NODES_BUFFER(readonly)
#endif

shared uint found[VTFS_MAX_PER_CLUSTER];
shared uint foundCount;
shared uint listOffset;
shared uint listCount;

#ifdef BVH
const uint STACK_SIZE = 256u;
shared uint stack[STACK_SIZE];      // level << 24 | node index within the level
shared uint stackSize;
shared uint current;
shared uint stackOverflow;
#endif

void addLight(uint light) {
    uint n = atomicAdd(foundCount, 1u);
    if (n < VTFS_MAX_PER_CLUSTER) found[n] = light;
}

void main() {
    uint t = gl_LocalInvocationID.x;
    uint count = lightCount();
    uint activeClusters = counters.activeClusters;
#ifdef BVH
    uint levels = bvhLevels(count);
#endif

    uint groups = gl_NumWorkGroups.x * gl_NumWorkGroups.y;
    for (uint slot = gl_WorkGroupID.x + gl_WorkGroupID.y * gl_NumWorkGroups.x; slot < activeClusters;
         slot += groups) {
        uint cluster = uniqueClusters[slot];
        AABB box = clusterAABBs[cluster];
        if (t == 0u) {
            foundCount = 0u;
#ifdef BVH
            stack[0] = 0u;              // the root, untested: its box holds every light
            stackSize = count > 0u ? 1u : 0u;
            stackOverflow = 0u;
#endif
        }
        barrier();

#ifdef BVH
        while (true) {
            uint size = stackSize;
            barrier();
            if (size == 0u) break;
            if (t == 0u) {
                current = stack[size - 1u];
                stackSize = size - 1u;
            }
            barrier();
            uint level = current >> 24;
            uint child = (current & 0xFFFFFFu) * 32u + t;
            if (level == levels - 1u) {
                // Children of the lowest level are lights, in sorted order.
                if (child < count) {
                    uint light = valuesA[child];
                    vec4 l = lightsView[light];
                    if (sphereTouchesAABB(l.xyz, l.w, box)) addLight(light);
                }
            } else if (child < bvhLevelNodes(level + 1u, levels, count)) {
                if (aabbTouchesAABB(bvhNodes[bvhFirstNode(level + 1u) + child], box)) {
                    uint p = atomicAdd(stackSize, 1u);
                    if (p < STACK_SIZE) stack[p] = ((level + 1u) << 24) | child;
                    else stackOverflow = 1u;
                }
            }
            barrier();
            if (t == 0u) stackSize = min(stackSize, STACK_SIZE);
            barrier();
        }
#else
        for (uint i = t; i < count; i += GROUP_SIZE) {
            vec4 l = lightsView[i];
            if (sphereTouchesAABB(l.xyz, l.w, box)) addLight(i);
        }
        barrier();
#endif

        if (t == 0u) {
            uint n = foundCount;
            uint overflow = 0u;
            if (n > VTFS_MAX_PER_CLUSTER) {
                n = VTFS_MAX_PER_CLUSTER;
                overflow |= VTFS_OVERFLOW_CLUSTER;
            }
#ifdef BVH
            if (stackOverflow != 0u) overflow |= VTFS_OVERFLOW_CLUSTER;
#endif
            uint offset = n > 0u ? atomicAdd(counters.indexCount, n) : 0u;
            if (offset >= VTFS_MAX_INDICES) {
                n = 0u;
                overflow |= VTFS_OVERFLOW_INDICES;
            } else if (offset + n > VTFS_MAX_INDICES) {
                n = VTFS_MAX_INDICES - offset;
                overflow |= VTFS_OVERFLOW_INDICES;
            }
            if (overflow != 0u) atomicOr(counters.overflow, overflow);
            listOffset = offset;
            listCount = n;
            lightGrid[cluster] = uvec2(offset, n);
        }
        barrier();
        for (uint k = t; k < listCount; k += GROUP_SIZE) {
            lightIndexList[listOffset + k] = found[k];
        }
        barrier();
    }
}
