//
// MorphComponents.h - Morph targets (glTF blend shapes)
//
// A mesh with morph targets draws its vertices blended from them by weight:
// position = base + sum over targets of weight * position delta, the same
// for normals and tangents, which are then normalised again. The CPU blends
// them (blendMorphTargets) into a vertex buffer of the entity's own, one per
// frame in flight, which the draws bind in place of the mesh's
// (RenderGraph::updateMorphedMeshes). The motion vectors leave out what the
// weights move.
//

#pragma once

#include "GPU/GPUTypes.h"
#include "GPU/GpuDeleteQueue.h"

#include <glm/glm.hpp>
#include <vulkan/vulkan.h>

#include <cstdint>
#include <memory>
#include <vector>

namespace Shoonyakasha {

// ============================================================================
// MorphTargetSet - A mesh's morph targets, shared by the entities drawing it
// ============================================================================

struct MorphTargetSet {
    uint32_t vertexCount = 0;
    uint32_t targetCount = 0;
    // The mesh's vertex layout: bytes per vertex, and where in a vertex the
    // vec3 position, vec3 normal and vec4 tangent start.
    uint32_t vertexStride = 0;
    uint32_t positionOffset = 0;
    uint32_t normalOffset = 0;
    uint32_t tangentOffset = 0;
    // The vertices with every weight 0, vertexCount * vertexStride bytes.
    std::vector<uint8_t> baseVertices;
    // Target t's delta for vertex v at t * vertexCount + v. The normal and
    // tangent deltas are empty when no target moves them.
    std::vector<glm::vec3> positionDeltas;
    std::vector<glm::vec3> normalDeltas;
    std::vector<glm::vec3> tangentDeltas;
    // The mesh's weights when nothing sets them (glTF mesh.weights).
    std::vector<float> defaultWeights;
};

/// Writes set.vertexCount vertices of set's layout to `out`: the base
/// vertices with each target added at its weight. Weights past the set's
/// targets are ignored and missing ones count as 0.
void blendMorphTargets(const MorphTargetSet& set, const std::vector<float>& weights, uint8_t* out);

// ============================================================================
// MorphComponent - Attached to every entity whose mesh has morph targets
// ============================================================================

struct MorphComponent {
    std::shared_ptr<const MorphTargetSet> targets;
    std::vector<float> weights;   // one per target
    int sourceNode = -1;          // the glTF node it was made from, which weight channels name
    uint64_t version = 1;         // grows each time the weights change

    // One blended vertex buffer per frame in flight, and the weight version
    // each holds (0: not blended yet).
    std::vector<GpuBufferRef> frames;
    std::vector<uint64_t> frameVersions;

    /// Sets the weights, one per target (extra ones dropped, missing ones 0).
    void setWeights(std::vector<float> w) {
        w.resize(targets ? targets->targetCount : w.size(), 0.0f);
        if (w != weights) {
            weights = std::move(w);
            ++version;
        }
    }

    /// The blended vertices for frame `frameIndex`; VK_NULL_HANDLE before
    /// they exist.
    VkBuffer bufferFor(uint32_t frameIndex) const {
        if (frames.empty()) return VK_NULL_HANDLE;
        const GpuBufferRef& ref = frames[frameIndex % frames.size()];
        return ref && ref->isValid() ? ref->buffer : VK_NULL_HANDLE;
    }
};

} // namespace Shoonyakasha
