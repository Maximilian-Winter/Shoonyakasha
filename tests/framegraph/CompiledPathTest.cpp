//
// CompiledPathTest.cpp - compiled dot-paths resolve exactly as their strings do
//
// Push constants are filled every draw through paths compiled once
// (DotPathResolver::compile). Every shape the compiler recognises, and some it
// leaves to string resolution, must give the same value either way, on
// entities with and without the components the path reads.
//

#include <gtest/gtest.h>
#include "FrameGraph/DotPathResolver.h"
#include "ECS/Core.h"
#include "ECS/RenderComponents.h"
#include "ECS/SkeletonComponents.h"

#include <cstring>
#include <vector>

using namespace Shoonyakasha;
using namespace Shoonyakasha::ECS;

namespace {

bool sameValue(const ResolvedValue& a, const ResolvedValue& b) {
    if (a.value.index() != b.value.index()) return false;
    return std::visit([&](const auto& x) {
        using T = std::decay_t<decltype(x)>;
        const auto& y = std::get<T>(b.value);
        if constexpr (std::is_same_v<T, std::monostate>) return true;
        else if constexpr (std::is_same_v<T, GPUTexture>) return x.view == y.view && x.exists == y.exists;
        else return x == y;
    }, a.value);
}

const std::vector<std::string> kPaths = {
    // Compiled
    "entity.transform.worldMatrix", "entity.transform.previousWorldMatrix", "entity.transform.localMatrix",
    "entity.transform.position", "entity.transform.rotation", "entity.transform.scale",
    "entity.material.params.baseColorFactor", "entity.material.params.metallicFactor",
    "entity.material.params.missing",
    "entity.material.textures.normalMap", "entity.material.textures.normalMap.exists",
    "entity.material.textures.missing.exists", "entity.material.textures.missing",
    "entity.material.alphaCutoff", "entity.material.alphaMode", "entity.material.doubleSided",
    "entity.mesh.vertexCount", "entity.mesh.indexCount",
    "entity.skeleton.hasSkeleton", "entity.skeleton.jointCount",
    "pass.repeatIndex", "pass.repeatCount", "pass.extent", "pass.texelSize", "pass.unknown",
    "const.0.5", "const.1.0.2.0.3.0", "const.oops",
    // Left to the string
    "scene.camera.view", "scene.time.elapsed", "scene.custom.tint",
    "entity.transform.worldMatrix.extra", "entity.material.params", "entity.material.textures.a.b",
    "entity.unknown.thing", "gPosition", "",
};

class CompiledPathFixture : public testing::Test {
protected:
    void SetUp() override {
        full = registry.create();
        auto& t = registry.emplace<TransformComponent>(full);
        t.position = {1, 2, 3};
        t.scale = {2, 2, 2};
        t.worldMatrix = glm::mat4(2.0f);
        t.previousWorldMatrix = glm::mat4(3.0f);
        t.localMatrix = glm::mat4(4.0f);
        auto& m = registry.emplace<MaterialComponentV5>(full);
        m.setParam("baseColorFactor", glm::vec4(1, 0, 0, 1));
        m.setParam("metallicFactor", 0.5f);
        m.alphaCutoff = 0.25f;
        m.doubleSided = true;
        GPUTexture normal;
        normal.exists = true;
        m.textures["normalMap"] = normal;
        auto& mesh = registry.emplace<MeshComponent>(full);
        mesh.vertexCount = 24;
        mesh.indexCount = 36;

        bare = registry.create();
        destroyed = registry.create();
        registry.destroy(destroyed);

        scene.pass.repeatIndex = 3;
        scene.pass.repeatCount = 8;
        scene.pass.extent = glm::vec2(1024.0f, 512.0f);
        scene.timeElapsed = 7.0f;
    }

    entt::registry registry;
    SceneContext scene;
    DotPathResolver resolver;
    entt::entity full{}, bare{}, destroyed{};
};

} // namespace

TEST_F(CompiledPathFixture, ResolvesLikeTheString) {
    for (entt::entity e : {full, bare, destroyed, entt::entity{entt::null}}) {
        for (const auto& path : kPaths) {
            const auto compiled = resolver.compile(path);
            EXPECT_TRUE(sameValue(resolver.resolve(compiled, path, scene, e, registry),
                                  resolver.resolve(path, scene, e, registry)))
                << path << " on entity " << static_cast<uint32_t>(e);
        }
    }
}

TEST_F(CompiledPathFixture, PerDrawPathsAreCompiled) {
    for (const char* path : {"entity.transform.worldMatrix", "entity.material.params.baseColorFactor",
                             "entity.material.textures.normalMap.exists", "entity.material.alphaCutoff",
                             "pass.repeatIndex", "const.0.5"}) {
        EXPECT_NE(resolver.compile(path).op, Shoonyakasha::CompiledPath::Op::Generic) << path;
    }
    for (const char* path : {"scene.camera.view", "entity.transform.worldMatrix.extra", "gPosition"}) {
        EXPECT_EQ(resolver.compile(path).op, Shoonyakasha::CompiledPath::Op::Generic) << path;
    }
}

TEST_F(CompiledPathFixture, FilledBuffersMatchStringResolution) {
    CompiledBufferLayout layout;
    uint32_t offset = 0;
    for (const char* source : {"entity.transform.worldMatrix", "entity.material.params.baseColorFactor",
                               "entity.material.alphaCutoff", "pass.repeatIndex",
                               "entity.material.textures.normalMap.exists"}) {
        BufferField field;
        field.source = source;
        field.offset = offset;
        field.size = std::string(source) == "entity.transform.worldMatrix" ? 64u : 16u;
        offset += field.size;
        layout.fields.push_back(field);
    }
    layout.totalSize = offset;

    CompiledBufferLayout compiled = layout;
    for (auto& field : compiled.fields) field.compiled = resolver.compile(field.source);

    BufferLayoutResolver fill(resolver);
    std::vector<uint8_t> byString(offset, 0xAB), byCompiled(offset, 0xCD);
    fill.fillEntityBuffer(byString.data(), layout, scene, full, registry);
    fill.fillEntityBuffer(byCompiled.data(), compiled, scene, full, registry);
    EXPECT_EQ(byString, byCompiled);
}
