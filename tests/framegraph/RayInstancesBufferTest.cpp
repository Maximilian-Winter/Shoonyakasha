//
// RayInstancesBufferTest.cpp - what ray-query shaders read about the surface a ray hits
//
// A storage_buffer layout whose "source" is {"type": "scene", "data":
// "rayInstances"} holds one PackedRayInstance per instance of the
// acceleration structure, and a combined_image_sampler binding with "source":
// "sceneTextures" holds the textures those rows name. Covers the JSON, its
// errors, the field check, and the packing of one row.
//
// Tier 1: no GPU context.
//

#include <gtest/gtest.h>

#include "ECS/RenderComponents.h"
#include "Vulkan/FrameGraph/FrameGraph.h"
#include "Vulkan/FrameGraph/FrameGraphJson.h"
#include "Vulkan/RayTracingScene.h"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstring>

using namespace Shoonyakasha;
using namespace Shoonyakasha::FrameGraph;
using nlohmann::json;

namespace {

json field(const char* name, const char* type) {
    return {{"name", name}, {"type", type}};
}

json instanceFields() {
    return json::array({field("geometry", "uvec4"), field("vertexLayout", "uvec4"), field("baseColor", "vec4"),
                        field("emissive", "vec4"), field("surface", "vec4"), field("textures", "uvec4")});
}

json instancesLayout(json fields = instanceFields()) {
    return {{"usage", "storage_buffer"}, {"packing", "std430"}, {"elementCount", 4096},
            {"fields", std::move(fields)}, {"source", {{"type", "scene"}, {"data", "rayInstances"}}}};
}

json graph(json layout, json bindings = json::array()) {
    return {{"version", 1}, {"name", "ray_instances_test"},
            {"resources", {{{"name", "swapchain"}, {"kind", "image"}, {"imported", true}}}},
            {"bufferLayouts", {{"RayInstances", std::move(layout)}}},
            {"descriptorSetLayouts", {{"sceneHits", {{"bindings", std::move(bindings)}}}}},
            {"passes", json::array()}};
}

std::string compileError(const json& layout) {
    FrameGraphBuilder builder;
    loadGraphFromJson(builder, graph(layout));
    FrameGraphCompiler compiler;
    std::unordered_map<std::string, FrameGraph::CompiledBufferLayout> compiled;
    compiler.compileBufferLayouts(builder.getBufferLayouts(), compiled);
    return sceneBuffersError(builder.getBufferLayouts(), compiled);
}

json texturesBinding(const char* type = "combined_image_sampler", const char* source = "sceneTextures") {
    return {{"binding", 1}, {"type", type}, {"count", 4096}, {"name", "sceneTextures"},
            {"stages", {"compute"}}, {"source", source}};
}

GpuBufferRef fakeBuffer(uintptr_t handle) {
    auto buffer = std::make_shared<GPUBuffer>();
    buffer->buffer = reinterpret_cast<VkBuffer>(handle);
    buffer->size = 64;
    return buffer;
}

} // namespace

TEST(RayInstancesBuffer, SourceIsParsed) {
    FrameGraphBuilder builder;
    loadGraphFromJson(builder, graph(instancesLayout()));
    const auto& layouts = builder.getBufferLayouts();
    ASSERT_EQ(layouts.size(), 1u);
    EXPECT_EQ(layouts[0].initConfig.type, "scene");
    EXPECT_EQ(layouts[0].initConfig.sceneData, "rayInstances");
}

TEST(RayInstancesBuffer, FieldsMustBeThePackedRow) {
    EXPECT_EQ(compileError(instancesLayout()), "");

    json floats = instanceFields();
    floats[0]["type"] = "vec4";   // addresses are uints
    EXPECT_NE(compileError(instancesLayout(floats)), "");

    json fewer = instanceFields();
    fewer.erase(fewer.size() - 1);
    EXPECT_NE(compileError(instancesLayout(fewer)), "");

    json lights = json::array();
    for (const char* name : {"positionType", "colorIntensity", "directionRange", "attenuation", "source", "shape"}) {
        lights.push_back(field(name, "vec4"));
    }
    EXPECT_NE(compileError(instancesLayout(lights)), "");
}

TEST(RayInstancesBuffer, PackedRowIsSixVec4s) {
    EXPECT_EQ(sizeof(PackedRayInstance), 6 * sizeof(glm::vec4));
    EXPECT_EQ(offsetof(PackedRayInstance, geometry), 0u);
    EXPECT_EQ(offsetof(PackedRayInstance, layout), 16u);
    EXPECT_EQ(offsetof(PackedRayInstance, baseColor), 32u);
    EXPECT_EQ(offsetof(PackedRayInstance, emissive), 48u);
    EXPECT_EQ(offsetof(PackedRayInstance, surface), 64u);
    EXPECT_EQ(offsetof(PackedRayInstance, textures), 80u);
}

TEST(RayInstancesBuffer, RowHoldsTheMeshAndMaterial) {
    MaterialComponentV5 material;
    material.setParam("baseColorFactor", glm::vec4(0.5f, 0.25f, 1.0f, 1.0f));
    material.setParam("emissiveFactor", glm::vec4(2.0f, 3.0f, 4.0f, 1.0f));
    material.setParam("metallicFactor", 0.75f);
    material.setParam("roughnessFactor", 0.125f);
    material.alphaCutoff = 0.25f;
    material.doubleSided = true;

    MeshComponent mesh;
    mesh.vertexBuffer = fakeBuffer(0x10);
    mesh.indexBuffer = fakeBuffer(0x20);
    mesh.vertexCount = 3;
    mesh.indexCount = 3;
    mesh.indexType = IndexType::UInt16;
    mesh.vertexStride = 44;

    const VkDeviceAddress vertices = 0x0000000123456780ull, indices = 0x00000009ABCDEF00ull;
    const glm::uvec4 slots(7, PackedRayInstance::kNoTexture, 2, PackedRayInstance::kNoTexture);
    const PackedRayInstance row = RayTracingScene::packInstance(material, 42u, mesh, vertices, indices, slots);

    EXPECT_EQ(row.geometry, glm::uvec4(0x23456780u, 0x1u, 0xABCDEF00u, 0x9u));
    EXPECT_EQ(row.layout, glm::uvec4(44u, 0u, 32u | (24u << 8) | (12u << 16), PackedRayInstance::kDoubleSided));
    EXPECT_EQ(row.baseColor, glm::vec4(0.5f, 0.25f, 1.0f, 1.0f));
    EXPECT_EQ(row.emissive, glm::vec4(2.0f, 3.0f, 4.0f, 0.0f));
    EXPECT_EQ(row.surface.x, 0.75f);
    EXPECT_EQ(row.surface.y, 0.125f);
    EXPECT_EQ(row.surface.z, 0.25f);
    uint32_t entity;
    std::memcpy(&entity, &row.surface.w, sizeof(entity));
    EXPECT_EQ(entity, 42u);
    EXPECT_EQ(row.textures, slots);

    mesh.indexBuffer.reset();   // not indexed
    material.doubleSided = false;
    const PackedRayInstance plain = RayTracingScene::packInstance(material, 1u, mesh, vertices, 0, slots);
    EXPECT_EQ(plain.layout.y, PackedRayInstance::kIndexNone);
    EXPECT_EQ(plain.layout.w, 0u);
    EXPECT_EQ(plain.geometry.z, 0u);
    EXPECT_EQ(plain.geometry.w, 0u);
}

TEST(RayInstancesBuffer, MissingFactorsReadAsGltfDefaults) {
    MeshComponent mesh;
    mesh.vertexStride = 44;
    const PackedRayInstance row = RayTracingScene::packInstance(MaterialComponentV5{}, 0u, mesh, 0, 0,
                                                                glm::uvec4(PackedRayInstance::kNoTexture));
    EXPECT_EQ(row.baseColor, glm::vec4(1.0f));
    EXPECT_EQ(row.emissive, glm::vec4(0.0f));
    EXPECT_EQ(row.surface.x, 0.0f);   // metallic
    EXPECT_EQ(row.surface.y, 0.5f);   // roughness, as the glTF loader defaults it
}

TEST(SceneTexturesBinding, SourceIsParsed) {
    FrameGraphBuilder builder;
    loadGraphFromJson(builder, graph(instancesLayout(), json::array({texturesBinding()})));
    const auto& layouts = builder.getDescriptorSetLayouts();
    ASSERT_EQ(layouts.size(), 1u);
    ASSERT_EQ(layouts[0].bindings.size(), 1u);
    EXPECT_EQ(layouts[0].bindings[0].source, "sceneTextures");
    EXPECT_EQ(layouts[0].bindings[0].count, 4096u);
}

TEST(SceneTexturesBinding, ErrorsAreCaughtAtLoad) {
    for (const json& bad : {texturesBinding("storage_image"), texturesBinding("combined_image_sampler", "lights")}) {
        FrameGraphBuilder builder;
        EXPECT_THROW(loadGraphFromJson(builder, graph(instancesLayout(), json::array({bad}))), std::runtime_error)
            << bad.dump();
    }
}
