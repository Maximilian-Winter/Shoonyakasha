//
// MeshLods.h - generated levels of detail for triangle meshes
//

#pragma once

#include "ECS/RenderComponents.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace Shoonyakasha {

/// Coarser levels of detail of a triangle list, each simplified from the
/// full mesh (meshoptimizer) to about half the triangles of the level before,
/// appended to `indices` after the full mesh's. Vertices are shared and the
/// mesh's open borders kept in place. Levels stop after `maxLevels`, below
/// 32 triangles, or when a level would keep more than three quarters of the
/// triangles of the one before. With `normals` (one per vertex), how far
/// the normals across a simplified triangle stray from the full mesh's also
/// counts in its error (meshopt_simplifyWithAttributes, weight 0.5 against
/// positions scaled to the mesh's size). Each level's error is in the
/// positions' units. Meshes under 64 triangles get none.
std::vector<MeshLod> appendMeshLods(const std::vector<glm::vec3>& positions, std::vector<uint32_t>& indices,
                                    uint32_t maxLevels = 4, const std::vector<glm::vec3>& normals = {});

} // namespace Shoonyakasha
