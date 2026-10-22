//
// MeshTangents.cpp - MikkTSpace tangents for triangle meshes
//

#include "Resources/MeshTangents.h"

#include <mikktspace.h>

#include <cstring>
#include <unordered_map>

namespace Shoonyakasha {

namespace {

struct Input {
    const std::vector<glm::vec3>& positions;
    const std::vector<glm::vec3>& normals;
    const std::vector<glm::vec2>& uvs;
    const std::vector<uint32_t>& indices;
    std::vector<glm::vec4> cornerTangents;   // three per triangle

    uint32_t vertexOf(int face, int corner) const {
        const size_t c = static_cast<size_t>(face) * 3 + static_cast<size_t>(corner);
        return indices.empty() ? static_cast<uint32_t>(c) : indices[c];
    }
};

Input& inputOf(const SMikkTSpaceContext* context) {
    return *static_cast<Input*>(context->m_pUserData);
}

int faceCount(const SMikkTSpaceContext* context) {
    const Input& in = inputOf(context);
    return static_cast<int>((in.indices.empty() ? in.positions.size() : in.indices.size()) / 3);
}

int cornersOfFace(const SMikkTSpaceContext*, const int) { return 3; }

void position(const SMikkTSpaceContext* context, float out[], const int face, const int corner) {
    const glm::vec3& p = inputOf(context).positions[inputOf(context).vertexOf(face, corner)];
    out[0] = p.x; out[1] = p.y; out[2] = p.z;
}

void normal(const SMikkTSpaceContext* context, float out[], const int face, const int corner) {
    const glm::vec3& n = inputOf(context).normals[inputOf(context).vertexOf(face, corner)];
    out[0] = n.x; out[1] = n.y; out[2] = n.z;
}

// MikkTSpace's bitangent follows +v; glTF's points up the image, toward -v.
void texCoord(const SMikkTSpaceContext* context, float out[], const int face, const int corner) {
    const glm::vec2& uv = inputOf(context).uvs[inputOf(context).vertexOf(face, corner)];
    out[0] = uv.x; out[1] = 1.0f - uv.y;
}

void setTangent(const SMikkTSpaceContext* context, const float tangent[], const float sign, const int face,
                const int corner) {
    inputOf(context).cornerTangents[static_cast<size_t>(face) * 3 + static_cast<size_t>(corner)] =
        glm::vec4(tangent[0], tangent[1], tangent[2], sign < 0.0f ? -1.0f : 1.0f);
}

bool sameBits(const glm::vec4& a, const glm::vec4& b) { return std::memcmp(&a, &b, sizeof(glm::vec4)) == 0; }

} // namespace

TangentMesh generateTangents(const std::vector<glm::vec3>& positions, const std::vector<glm::vec3>& normals,
                             const std::vector<glm::vec2>& uvs, const std::vector<uint32_t>& indices) {
    Input in{positions, normals, uvs, indices, {}};
    const size_t corners = (indices.empty() ? positions.size() : indices.size()) / 3 * 3;
    in.cornerTangents.assign(corners, glm::vec4(0.0f));

    SMikkTSpaceInterface callbacks{};
    callbacks.m_getNumFaces = faceCount;
    callbacks.m_getNumVerticesOfFace = cornersOfFace;
    callbacks.m_getPosition = position;
    callbacks.m_getNormal = normal;
    callbacks.m_getTexCoord = texCoord;
    callbacks.m_setTSpaceBasic = setTangent;
    SMikkTSpaceContext context{};
    context.m_pInterface = &callbacks;
    context.m_pUserData = &in;
    if (corners > 0) genTangSpaceDefault(&context);

    TangentMesh out;
    if (indices.empty()) {
        out.source.resize(corners);
        for (size_t i = 0; i < corners; ++i) out.source[i] = static_cast<uint32_t>(i);
        out.tangents = std::move(in.cornerTangents);
        return out;
    }

    // One output vertex per distinct (input vertex, tangent) pair.
    std::unordered_map<uint32_t, std::vector<uint32_t>> splits;   // input vertex -> its output vertices
    out.indices.reserve(corners);
    for (size_t c = 0; c < corners; ++c) {
        const uint32_t vertex = indices[c];
        const glm::vec4& tangent = in.cornerTangents[c];
        auto& outputs = splits[vertex];
        uint32_t found = UINT32_MAX;
        for (uint32_t o : outputs) {
            if (sameBits(out.tangents[o], tangent)) { found = o; break; }
        }
        if (found == UINT32_MAX) {
            found = static_cast<uint32_t>(out.source.size());
            out.source.push_back(vertex);
            out.tangents.push_back(tangent);
            outputs.push_back(found);
        }
        out.indices.push_back(found);
    }
    return out;
}

} // namespace Shoonyakasha
