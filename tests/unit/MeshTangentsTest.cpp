//
// MeshTangentsTest.cpp - MikkTSpace tangents and the vertex layout that carries them
//

#include <gtest/gtest.h>

#include "Resources/MeshTangents.h"
#include "Vulkan/VertexTypes.h"

#include <cstddef>

using namespace Shoonyakasha;

namespace {

// A unit quad in the xy plane facing +z, two triangles, counter-clockwise.
// `u` gives each corner's u from its x; v grows down the image (glTF).
struct Quad {
    std::vector<glm::vec3> positions{{-0.5f, -0.5f, 0.0f}, {0.5f, -0.5f, 0.0f}, {0.5f, 0.5f, 0.0f}, {-0.5f, 0.5f, 0.0f}};
    std::vector<glm::vec3> normals = std::vector<glm::vec3>(4, glm::vec3(0.0f, 0.0f, 1.0f));
    std::vector<glm::vec2> uvs;
    std::vector<uint32_t> indices{0, 1, 2, 0, 2, 3};

    explicit Quad(bool mirrored) {
        for (const auto& p : positions) uvs.push_back({mirrored ? 0.5f - p.x : p.x + 0.5f, 0.5f - p.y});
    }
};

void expectNear(const glm::vec4& actual, const glm::vec4& expected) {
    EXPECT_NEAR(actual.x, expected.x, 1e-5f);
    EXPECT_NEAR(actual.y, expected.y, 1e-5f);
    EXPECT_NEAR(actual.z, expected.z, 1e-5f);
    EXPECT_EQ(actual.w, expected.w);
}

} // namespace

TEST(MeshTangents, TangentAlongUAndBitangentUpTheImage) {
    const Quad quad(false);
    const TangentMesh mesh = generateTangents(quad.positions, quad.normals, quad.uvs, quad.indices);

    ASSERT_EQ(mesh.tangents.size(), 4u);   // one tangent everywhere: no vertex is split
    EXPECT_EQ(mesh.indices.size(), 6u);
    for (const auto& t : mesh.tangents) {
        expectNear(t, {1.0f, 0.0f, 0.0f, 1.0f});
        // bitangent = w * cross(normal, tangent) points up, toward -v.
        const glm::vec3 bitangent = t.w * glm::cross(glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(t));
        EXPECT_NEAR(bitangent.y, 1.0f, 1e-5f);
    }
}

TEST(MeshTangents, MirroredUvsFlipTheHandedness) {
    const Quad quad(true);
    const TangentMesh mesh = generateTangents(quad.positions, quad.normals, quad.uvs, quad.indices);

    ASSERT_EQ(mesh.tangents.size(), 4u);
    for (const auto& t : mesh.tangents) {
        expectNear(t, {-1.0f, 0.0f, 0.0f, -1.0f});
        const glm::vec3 bitangent = t.w * glm::cross(glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(t));
        EXPECT_NEAR(bitangent.y, 1.0f, 1e-5f);
    }
}

TEST(MeshTangents, VerticesOnAMirrorSeamAreSplit) {
    // Two quads side by side sharing the middle edge; the right one's u runs
    // back toward the seam, as on a mirrored model.
    //   0 - 1 - 2     top row, y = 1
    //   3 - 4 - 5     bottom row, y = 0
    const std::vector<glm::vec3> positions{{-1, 1, 0}, {0, 1, 0}, {1, 1, 0}, {-1, 0, 0}, {0, 0, 0}, {1, 0, 0}};
    const std::vector<glm::vec3> normals(6, glm::vec3(0.0f, 0.0f, 1.0f));
    const std::vector<glm::vec2> uvs{{0, 0}, {1, 0}, {0, 0}, {0, 1}, {1, 1}, {0, 1}};
    const std::vector<uint32_t> indices{3, 4, 1, 3, 1, 0, 4, 5, 2, 4, 2, 1};

    const TangentMesh mesh = generateTangents(positions, normals, uvs, indices);

    ASSERT_EQ(mesh.source.size(), 8u);   // vertices 1 and 4 are split in two
    ASSERT_EQ(mesh.indices.size(), indices.size());
    for (size_t c = 0; c < indices.size(); ++c) {
        const uint32_t vertex = mesh.indices[c];
        EXPECT_EQ(mesh.source[vertex], indices[c]);
        const float expectedX = c < 6 ? 1.0f : -1.0f;   // left quad, then right quad
        expectNear(mesh.tangents[vertex], {expectedX, 0.0f, 0.0f, expectedX});
    }
}

TEST(MeshTangents, WithoutIndicesEveryVertexStays) {
    const Quad quad(false);
    std::vector<glm::vec3> positions, normals;
    std::vector<glm::vec2> uvs;
    for (uint32_t i : quad.indices) {
        positions.push_back(quad.positions[i]);
        normals.push_back(quad.normals[i]);
        uvs.push_back(quad.uvs[i]);
    }

    const TangentMesh mesh = generateTangents(positions, normals, uvs, {});

    EXPECT_TRUE(mesh.indices.empty());
    ASSERT_EQ(mesh.source.size(), 6u);
    for (uint32_t i = 0; i < 6; ++i) {
        EXPECT_EQ(mesh.source[i], i);
        expectNear(mesh.tangents[i], {1.0f, 0.0f, 0.0f, 1.0f});
    }
}

TEST(MeshTangents, VerticesNoTriangleUsesAreLeftOut) {
    Quad quad(false);
    quad.positions.push_back({5.0f, 5.0f, 0.0f});
    quad.normals.push_back({0.0f, 0.0f, 1.0f});
    quad.uvs.push_back({0.0f, 0.0f});

    const TangentMesh mesh = generateTangents(quad.positions, quad.normals, quad.uvs, quad.indices);

    EXPECT_EQ(mesh.source.size(), 4u);
    for (uint32_t s : mesh.source) EXPECT_LT(s, 4u);
}

TEST(VertexLayout, TangentFollowsTheNormal) {
    EXPECT_EQ(sizeof(Vertex), 60u);
    EXPECT_EQ(offsetof(Vertex, tangent), 44u);
    const auto attributes = Vertex::getAttributeDescriptions();
    ASSERT_EQ(attributes.size(), 5u);
    EXPECT_EQ(attributes[4].location, 4u);
    EXPECT_EQ(attributes[4].format, VK_FORMAT_R32G32B32A32_SFLOAT);
    EXPECT_EQ(attributes[4].offset, 44u);
}
