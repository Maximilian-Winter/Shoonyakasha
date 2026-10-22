//
// MeshTangents.h - MikkTSpace tangents for triangle meshes
//

#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace Shoonyakasha {

/// A triangle mesh after tangent generation. Where the triangles around one
/// input vertex get different tangents (UV seams, mirrored UVs), the vertex
/// is split into one vertex per tangent.
struct TangentMesh {
    std::vector<uint32_t> source;     // per vertex: the input vertex it copies
    std::vector<glm::vec4> tangents;  // per vertex: xyz along +u; w = ±1, bitangent = w * cross(normal, xyz)
    std::vector<uint32_t> indices;    // three per triangle; empty when the input had none
};

/// MikkTSpace tangents for a triangle list. `positions`, `normals` and `uvs`
/// have one entry per vertex; uvs follow glTF, with v growing down the image,
/// and the bitangent points up the image. `indices` holds three per triangle;
/// empty, every three vertices are a triangle and no vertex is split.
/// Vertices that no triangle uses are left out.
TangentMesh generateTangents(const std::vector<glm::vec3>& positions, const std::vector<glm::vec3>& normals,
                             const std::vector<glm::vec2>& uvs, const std::vector<uint32_t>& indices);

} // namespace Shoonyakasha
