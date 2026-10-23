//
// InstanceBatching.h - Grouping an entity pass's draws into instanced draws
//
// A pass with "instancing" (PassExecution::instancing) draws entities that
// share a mesh and material as one instanced draw. Each instance's matrices
// reach the vertex shader through a per-instance vertex buffer at
// kInstanceBinding: one InstanceRow per instance, read as eight vec4
// attributes from kInstanceFirstLocation (two mat4s, locations 8 to 15).
//
// Pure functions, no device: FrameGraphRenderer uses them, and the unit
// tests call them directly.
//

#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace Shoonyakasha {

struct MaterialComponentV5;
struct MeshComponent;

/// One instance's data, as the vertex shader reads it.
struct InstanceRow {
    glm::mat4 model{1.0f};
    glm::mat4 previousModel{1.0f};   // last frame's, for motion vectors
};
static_assert(sizeof(InstanceRow) == 128, "InstanceRow is two mat4s");

constexpr uint32_t kInstanceBinding = 1;
constexpr uint32_t kInstanceFirstLocation = 8;

/// A run of `count` draws in the order batchDraws returns, from `first`.
struct InstanceBatch {
    uint32_t first = 0;
    uint32_t count = 0;
};

/// Groups draws by batch key: keys[i] is draw i's key, and draws with the
/// same nonzero key may share a batch; 0 draws alone. With keepOrder, only
/// neighbouring draws with equal keys share one; otherwise each draw joins
/// the batch of the first draw with its key, and batches keep the order of
/// their first draws. Writes the draws' indices, batch by batch, to `order`.
std::vector<InstanceBatch> batchDraws(const std::vector<uint64_t>& keys, bool keepOrder,
                                      std::vector<uint32_t>& order);

/// A hash of everything a material binds: its parameters, its textures'
/// views and samplers, alpha mode and cutoff, and sidedness. Equal for equal
/// materials whatever order their parameters were set in.
uint64_t materialHash(const MaterialComponentV5& material);

/// The batch key of a draw of `mesh` with `material`: a hash of its vertex
/// and index buffers, counts, stride and index type, and materialHash.
/// Never 0.
uint64_t batchKey(const MeshComponent& mesh, const MaterialComponentV5& material);

} // namespace Shoonyakasha
