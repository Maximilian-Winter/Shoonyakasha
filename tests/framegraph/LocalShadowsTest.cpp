//
// LocalShadowsTest.cpp - spot and point light shadow matrices and slots
//
// Tier 1: no GPU.
//

#include <gtest/gtest.h>
#include "FrameGraph/LocalShadows.h"
#include "FrameGraph/DotPathResolver.h"
#include "ECS/Core.h"
#include "ECS/Systems.h"
#include "Vulkan/FrameGraph/FrameGraph.h"
#include "Vulkan/FrameGraph/FrameGraphJson.h"
#include "Vulkan/FrameGraph/FrameGraphSchedule.h"

#include <glm/gtc/matrix_transform.hpp>
#include <nlohmann/json.hpp>

#include <cmath>
#include <random>
#include <stdexcept>

using namespace Shoonyakasha;

namespace {

/// The cube-map face and texel coordinates Vulkan uses for direction r
/// (specification, "Cube Map Face Selection and Transformations").
struct CubeTexel { int face; float s, t; };
CubeTexel vulkanCubeLookup(const glm::vec3& r) {
    const glm::vec3 a = glm::abs(r);
    float sc, tc, ma;
    int face;
    if (a.x >= a.y && a.x >= a.z) {
        face = r.x > 0 ? 0 : 1; ma = r.x;
        sc = r.x > 0 ? -r.z : r.z; tc = -r.y;
    } else if (a.y >= a.z) {
        face = r.y > 0 ? 2 : 3; ma = r.y;
        sc = r.x; tc = r.y > 0 ? r.z : -r.z;
    } else {
        face = r.z > 0 ? 4 : 5; ma = r.z;
        sc = r.z > 0 ? r.x : -r.x; tc = -r.y;
    }
    return {face, 0.5f * (sc / std::abs(ma) + 1.0f), 0.5f * (tc / std::abs(ma) + 1.0f)};
}

glm::vec3 project(const glm::mat4& m, const glm::vec3& p) {
    const glm::vec4 c = m * glm::vec4(p, 1.0f);
    return glm::vec3(c) / c.w;
}

} // namespace

TEST(PointShadowFaces, RenderWhereACubeLookupReads) {
    const glm::vec3 light(1.0f, 2.0f, -3.0f);
    const float nearPlane = 0.05f, farPlane = 20.0f;
    const auto faces = pointShadowFaceViewProj(light, nearPlane, farPlane);

    std::mt19937 rng(7);
    std::uniform_real_distribution<float> u(-1.0f, 1.0f);
    for (int i = 0; i < 500; ++i) {
        glm::vec3 r(u(rng), u(rng), u(rng));
        if (glm::length(r) < 0.1f) continue;
        r = glm::normalize(r) * (0.5f + 10.0f * std::abs(u(rng)));
        const auto lookup = vulkanCubeLookup(r);
        const glm::vec3 ndc = project(faces[lookup.face], light + r);
        // Rendering puts NDC (x, y) at framebuffer ((x + 1) / 2, (y + 1) / 2).
        EXPECT_NEAR((ndc.x + 1.0f) * 0.5f, lookup.s, 1e-4f) << "face " << lookup.face;
        EXPECT_NEAR((ndc.y + 1.0f) * 0.5f, lookup.t, 1e-4f) << "face " << lookup.face;
        // Depth from the major-axis distance, as the shader recomputes it.
        const float m = std::max(std::abs(r.x), std::max(std::abs(r.y), std::abs(r.z)));
        const float A = farPlane / (farPlane - nearPlane), B = -farPlane * nearPlane / (farPlane - nearPlane);
        EXPECT_NEAR(ndc.z, A + B / m, 1e-4f);
        EXPECT_GE(ndc.z, 0.0f);
        EXPECT_LE(ndc.z, 1.0f);
    }
}

TEST(PointShadowFaces, EachFaceSeesOnlyItsSideOfTheLight) {
    const auto faces = pointShadowFaceViewProj(glm::vec3(0.0f), 0.1f, 10.0f);
    const glm::vec3 axes[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    for (int f = 0; f < 6; ++f) {
        for (int g = 0; g < 6; ++g) {
            const glm::vec4 clip = faces[f] * glm::vec4(axes[g] * 3.0f, 1.0f);
            if (f == g) {
                EXPECT_GT(clip.w, 0.0f);
                EXPECT_NEAR(clip.x / clip.w, 0.0f, 1e-5f);
                EXPECT_NEAR(clip.y / clip.w, 0.0f, 1e-5f);
            } else {
                // Behind the face, or on the edge of it at best.
                EXPECT_TRUE(clip.w <= 0.0f || std::abs(clip.x) >= clip.w - 1e-5f ||
                            std::abs(clip.y) >= clip.w - 1e-5f) << f << " sees " << g;
            }
        }
    }
}

TEST(SpotShadow, CoversTheConeAlongItsAxis) {
    const glm::vec3 position(0.0f, 4.0f, 0.0f), direction(0.0f, -1.0f, 0.0f);
    const float cosOuter = std::cos(glm::radians(30.0f));
    const glm::mat4 vp = spotShadowViewProj(position, direction, cosOuter, 0.05f, 10.0f);

    const glm::vec3 onAxis = project(vp, position + direction * 3.0f);
    EXPECT_NEAR(onAxis.x, 0.0f, 1e-5f);
    EXPECT_NEAR(onAxis.y, 0.0f, 1e-5f);
    EXPECT_GT(project(vp, position + direction * 6.0f).z, onAxis.z);   // farther is deeper

    // The cone's edge is inside the map, with a little room.
    const glm::vec3 edgeDir = glm::normalize(glm::vec3(std::tan(glm::radians(30.0f)), -1.0f, 0.0f));
    const glm::vec3 edge = project(vp, position + edgeDir * 3.0f);
    EXPECT_LT(std::max(std::abs(edge.x), std::abs(edge.y)), 1.0f);
    EXPECT_GT(std::max(std::abs(edge.x), std::abs(edge.y)), 0.85f);
}

// ── Slots ───────────────────────────────────────────────────────

namespace {

LocalShadowCandidate candidate(uint32_t index, bool spot, glm::vec3 position, float intensity, float range = 8.0f) {
    LocalShadowCandidate c;
    c.lightIndex = index;
    c.spot = spot;
    c.position = position;
    c.direction = glm::vec3(0, -1, 0);
    c.intensity = intensity;
    c.range = range;
    return c;
}

} // namespace

TEST(LocalShadowSlots, BudgetsKeepTheMostImportantLights) {
    LocalShadowSettings settings;
    settings.spotCount = 2;
    settings.pointCount = 1;
    const std::vector<LocalShadowCandidate> lights = {
        candidate(0, true,  {0, 3, -5}, 1.0f),
        candidate(1, true,  {0, 3, -5}, 5.0f),
        candidate(2, true,  {0, 3, -60}, 5.0f),   // far from the camera
        candidate(3, false, {2, 3, -4}, 1.0f),
        candidate(4, false, {2, 3, -4}, 3.0f),
    };
    const auto s = assignLocalShadows(lights, glm::vec3(0.0f), glm::mat4(1.0f), true, settings);
    ASSERT_EQ(s.spotCount, 2u);
    EXPECT_EQ(s.spot[0].lightIndex, 1);
    EXPECT_EQ(s.spot[1].lightIndex, 0);
    EXPECT_EQ(s.spot[2].lightIndex, -1);
    ASSERT_EQ(s.pointCount, 1u);
    EXPECT_EQ(s.point[0].lightIndex, 4);
    EXPECT_FLOAT_EQ(s.point[0].positionFar.w, 8.0f);
    EXPECT_GT(s.spot[0].params.x, 0.0f);   // texel size at one unit
}

TEST(LocalShadowSlots, LightsThatCannotReachTheViewAreSkipped) {
    const glm::mat4 camera = glm::perspective(glm::radians(60.0f), 1.0f, 0.1f, 100.0f) *
                             glm::lookAtRH(glm::vec3(0.0f), glm::vec3(0, 0, -1), glm::vec3(0, 1, 0));
    LocalShadowSettings settings;
    const auto s = assignLocalShadows({candidate(0, false, {0, 0, 30}, 10.0f, 5.0f),     // behind, out of reach
                                       candidate(1, false, {0, 0, 3}, 1.0f, 5.0f)},      // behind, reaches in
                                      glm::vec3(0.0f), camera, true, settings);
    ASSERT_EQ(s.pointCount, 1u);
    EXPECT_EQ(s.point[0].lightIndex, 1);
}

// ── The atlas ───────────────────────────────────────────────────

namespace {

bool overlap(const glm::vec4& a, const glm::vec4& b) {
    return a.x < b.x + b.z && b.x < a.x + a.z && a.y < b.y + b.w && b.y < a.y + a.w;
}

} // namespace

TEST(ShadowAtlas, PackingFillsWithoutOverlap) {
    // 1 of 512, 3 of 256, 4 of 128 in a 1024 atlas: 0.25 + 0.1875 + 0.0625 of it.
    const std::vector<uint32_t> sizes = {128, 512, 256, 128, 256, 128, 256, 128};
    const auto corners = packAtlasTiles(sizes, 1024);
    ASSERT_EQ(corners.size(), sizes.size());
    for (size_t i = 0; i < sizes.size(); ++i) {
        const glm::vec4 a{glm::vec2(corners[i]), float(sizes[i]), float(sizes[i])};
        EXPECT_LE(a.x + a.z, 1024.0f);
        EXPECT_LE(a.y + a.w, 1024.0f);
        EXPECT_EQ(corners[i].x % sizes[i], 0u);   // aligned to its own size
        EXPECT_EQ(corners[i].y % sizes[i], 0u);
        for (size_t j = 0; j < i; ++j) {
            const glm::vec4 b{glm::vec2(corners[j]), float(sizes[j]), float(sizes[j])};
            EXPECT_FALSE(overlap(a, b)) << i << " and " << j;
        }
    }
    // Exactly full is fine; one more is not.
    EXPECT_EQ(packAtlasTiles(std::vector<uint32_t>(16, 256), 1024).size(), 16u);
    EXPECT_TRUE(packAtlasTiles(std::vector<uint32_t>(17, 256), 1024).empty());
}

TEST(ShadowAtlas, NearerLightsGetLargerTilesAndEverythingFits) {
    LocalShadowSettings settings;   // 4096 atlas, spot tiles up to 2048, faces up to 1024
    std::vector<LocalShadowCandidate> lights;
    for (uint32_t i = 0; i < 8; ++i)   // spots at 2, 6, 10 ... units, each reaching 4
        lights.push_back(candidate(i, true, {0, 0, -2.0f - 4.0f * float(i)}, 1.0f, 4.0f));
    for (uint32_t i = 0; i < 4; ++i)
        lights.push_back(candidate(8 + i, false, {3, 0, -2.0f - 6.0f * float(i)}, 1.0f, 4.0f));
    const auto s = assignLocalShadows(lights, glm::vec3(0.0f), glm::mat4(1.0f), true, settings);
    ASSERT_EQ(s.spotCount, 8u);
    ASSERT_EQ(s.pointCount, 4u);

    std::vector<glm::vec4> rects;
    for (uint32_t i = 0; i < s.spotCount; ++i) rects.push_back(s.spot[i].rect);
    for (uint32_t i = 0; i < s.pointCount; ++i)
        for (const auto& r : s.point[i].faceRect) rects.push_back(r);
    for (size_t i = 0; i < rects.size(); ++i) {
        EXPECT_GT(rects[i].z, 0.0f);
        EXPECT_EQ(rects[i].z, rects[i].w);
        EXPECT_LE(rects[i].x + rects[i].z, 1.0f);
        EXPECT_LE(rects[i].y + rects[i].w, 1.0f);
        for (size_t j = 0; j < i; ++j) EXPECT_FALSE(overlap(rects[i], rects[j])) << i << " and " << j;
    }
    // The camera stands inside the nearest spot's range: the largest tile.
    // Farther lights look smaller and get smaller ones.
    EXPECT_FLOAT_EQ(s.spot[0].rect.z * 4096.0f, 2048.0f);
    float previous = 1.0f;
    for (uint32_t i = 0; i < s.spotCount; ++i) {
        EXPECT_LE(s.spot[i].rect.z, previous);
        previous = s.spot[i].rect.z;
    }
    EXPECT_LT(s.spot[7].rect.z, s.spot[0].rect.z);
    // The texel size follows the tile: the same cone over fewer texels.
    for (uint32_t i = 1; i < s.spotCount; ++i)
        EXPECT_NEAR(s.spot[i].params.x * s.spot[i].rect.z, s.spot[0].params.x * s.spot[0].rect.z, 1e-6f);
    // A point light's faces share one size.
    for (const auto& r : s.point[0].faceRect) EXPECT_EQ(r.z, s.point[0].faceRect[0].z);
    EXPECT_FLOAT_EQ(s.point[0].depthParams.z, 2.0f / (s.point[0].faceRect[0].z * 4096.0f));
}

TEST(ShadowAtlas, TilesShrinkLeastImportantFirstToFit) {
    LocalShadowSettings settings;
    settings.atlasResolution = 2048;
    settings.spotResolution = 2048;
    std::vector<LocalShadowCandidate> lights;
    // All around the camera, so each wants the largest tile: 8 x 2048^2 cannot fit.
    for (uint32_t i = 0; i < 8; ++i) lights.push_back(candidate(i, true, {0, 0, -1}, float(8 - i), 10.0f));
    const auto s = assignLocalShadows(lights, glm::vec3(0.0f), glm::mat4(1.0f), true, settings);
    ASSERT_EQ(s.spotCount, 8u);
    float area = 0.0f;
    for (uint32_t i = 0; i < s.spotCount; ++i) area += s.spot[i].rect.z * s.spot[i].rect.w;
    EXPECT_LE(area, 1.0f + 1e-6f);
    EXPECT_EQ(s.spot[0].lightIndex, 0);                          // the brightest
    EXPECT_GE(s.spot[0].rect.z, s.spot[7].rect.z);
    EXPECT_GT(s.spot[0].rect.z, s.spot[7].rect.z);
}

TEST(ShadowAtlas, ArraysUseTheWholeLayer) {
    LocalShadowSettings settings;
    settings.atlasResolution = 0;
    const auto s = assignLocalShadows({candidate(0, true, {0, 0, -3}, 1.0f), candidate(1, false, {0, 0, -3}, 1.0f)},
                                      glm::vec3(0.0f), glm::mat4(1.0f), true, settings);
    EXPECT_EQ(s.spot[0].rect, glm::vec4(0, 0, 1, 1));
    EXPECT_EQ(s.point[0].faceRect[5], glm::vec4(0, 0, 1, 1));
    EXPECT_EQ(s.spot[1].rect, glm::vec4(0.0f));   // empty slot
}

TEST(ShadowAtlas, ViewportRectanglesInTexels) {
    const VkExtent2D extent{4096, 4096};
    const VkRect2D r = FrameGraph::FrameGraphExecutor::viewportRect(glm::vec4(0.25f, 0.5f, 0.125f, 0.125f), extent);
    EXPECT_EQ(r.offset.x, 1024);
    EXPECT_EQ(r.offset.y, 2048);
    EXPECT_EQ(r.extent.width, 512u);
    EXPECT_EQ(r.extent.height, 512u);
    EXPECT_EQ(FrameGraph::FrameGraphExecutor::viewportRect(glm::vec4(0.0f), extent).extent.width, 0u);
    const VkRect2D clamped = FrameGraph::FrameGraphExecutor::viewportRect(glm::vec4(0.75f, 0.75f, 0.5f, 0.5f), extent);
    EXPECT_EQ(clamped.extent.width, 1024u);
}

// ── In the scene context ────────────────────────────────────────

class LocalShadowScene : public testing::Test {
protected:
    void SetUp() override {
        auto camera = registry.create();
        registry.emplace<ECS::TransformComponent>(camera).position = glm::vec3(0.0f, 2.0f, 8.0f);
        auto& cam = registry.emplace<ECS::CameraComponent>(camera);
        cam.isMainCamera = true;
        cam.fov = 60.0f;
        cam.nearPlane = 0.1f;
        cam.farPlane = 200.0f;

        auto sun = registry.create();
        registry.emplace<ECS::TransformComponent>(sun);
        registry.emplace<ECS::LightComponent>(sun).type = ECS::LightComponent::Directional;

        spot = registry.create();
        registry.emplace<ECS::TransformComponent>(spot).position = glm::vec3(0.0f, 4.0f, 0.0f);
        registry.emplace<ECS::LightComponent>(spot).type = ECS::LightComponent::Spot;

        point = registry.create();
        registry.emplace<ECS::TransformComponent>(point).position = glm::vec3(2.0f, 1.0f, 0.0f);
        registry.emplace<ECS::LightComponent>(point).type = ECS::LightComponent::Point;

        transformSystem.update(registry, 0.016f);
        cameraSystem.update(registry, 0.016f);
    }

    entt::registry registry;
    entt::entity spot{}, point{};
    ECS::TransformSystem transformSystem;
    ECS::CameraSystem cameraSystem;
    SceneContext scene;
    DotPathResolver resolver;
};

TEST_F(LocalShadowScene, OnlyLightsThatCastShadowsGetSlots) {
    scene.updateFromRegistry(registry);
    EXPECT_EQ(scene.localShadow.shadows.spotCount, 0u);
    EXPECT_EQ(resolver.resolveScene("scene.shadows.spot.count", scene).as<uint32_t>(), 0u);
    EXPECT_EQ(resolver.resolveScene("scene.shadows.spot[0].lightIndex", scene).as<int32_t>(), -1);
}

TEST_F(LocalShadowScene, CastingLightsArePublished) {
    registry.get<ECS::LightComponent>(spot).castShadows = true;
    registry.get<ECS::LightComponent>(point).castShadows = true;
    scene.updateFromRegistry(registry);

    const auto& local = scene.localShadow.shadows;
    ASSERT_EQ(local.spotCount, 1u);
    ASSERT_EQ(local.pointCount, 1u);
    EXPECT_EQ(scene.lights[local.spot[0].lightIndex].positionType.w, 2.0f);    // a spot light
    EXPECT_EQ(scene.lights[local.point[0].lightIndex].positionType.w, 1.0f);   // a point light

    EXPECT_EQ(resolver.resolveScene("scene.shadows.spot.count", scene).as<uint32_t>(), 1u);
    EXPECT_EQ(resolver.resolveScene("scene.shadows.point.count", scene).as<uint32_t>(), 1u);
    EXPECT_EQ(resolver.resolveScene("scene.shadows.spot[0].lightIndex", scene).as<int32_t>(),
              local.spot[0].lightIndex);
    EXPECT_EQ(resolver.resolveScene("scene.shadows.spot[0].viewProj", scene).as<glm::mat4>(), local.spot[0].viewProj);
    EXPECT_EQ(resolver.resolveScene("scene.shadows.point[0].positionFar", scene).as<glm::vec4>(),
              local.point[0].positionFar);
    EXPECT_EQ(resolver.resolveScene("scene.shadows.point.faces[3].viewProj", scene).as<glm::mat4>(),
              local.point[0].faceViewProj[3]);
    EXPECT_EQ(resolver.resolveScene("scene.shadows.spot[0].rect", scene).as<glm::vec4>(), local.spot[0].rect);
    EXPECT_GT(local.spot[0].rect.z, 0.0f);
    EXPECT_EQ(resolver.resolveScene("scene.shadows.point.faces[3].rect", scene).as<glm::vec4>(),
              local.point[0].faceRect[3]);
    EXPECT_EQ(resolver.resolveScene("scene.shadows.spot[1].rect", scene).as<glm::vec4>(), glm::vec4(0.0f));
    EXPECT_FALSE(resolver.resolveScene("scene.shadows.spot[8].viewProj", scene).isValid());
    EXPECT_FALSE(resolver.resolveScene("scene.shadows.point.faces[24].viewProj", scene).isValid());
}

// ── Pass views ──────────────────────────────────────────────────

TEST(LocalShadowViews, ParseAndCheckTheirSlot) {
    using nlohmann::json;
    auto graph = [](const std::string& view) {
        return json{{"version", 1},
                    {"resources", json::array({{{"name", "map"}, {"kind", "image"},
                                                {"image", {{"format", "D32_SFLOAT"}}}}})},
                    {"passes", json::array({{{"name", "Casters"}, {"type", "graphics"},
                                             {"execution", {{"type", "shadow_casters"}, {"view", view}}},
                                             {"outputs", json::array({{{"resource", "map"},
                                                                       {"usage", "depth_write"}}})}}})}};
    };
    FrameGraph::FrameGraphBuilder b;
    FrameGraph::loadGraphFromJson(b, graph("shadows.spot[3]"));
    EXPECT_EQ(b.getPassDeclarations()[0].execution.view, FrameGraph::CullView::SpotShadow);
    EXPECT_EQ(b.getPassDeclarations()[0].execution.viewIndex, 3u);
    FrameGraph::loadGraphFromJson(b, graph("shadows.point.faces[23]"));
    EXPECT_EQ(b.getPassDeclarations()[0].execution.view, FrameGraph::CullView::PointShadowFace);
    EXPECT_EQ(b.getPassDeclarations()[0].execution.viewIndex, 23u);
    for (const char* bad : {"shadows.spot[8]", "shadows.point.faces[24]", "shadows.spot[a]", "shadows.point[0]"}) {
        FrameGraph::FrameGraphBuilder c;
        EXPECT_THROW(FrameGraph::loadGraphFromJson(c, graph(bad)), std::runtime_error) << bad;
    }
}

TEST(ShadowAtlas, PassViewportParses) {
    using nlohmann::json;
    auto graph = [](const json& viewport, const std::string& type) {
        json pass = {{"name", "Tile"}, {"type", type}, {"viewport", viewport},
                     {"execution", {{"type", type == "graphics" ? "shadow_casters" : "dispatch"}}}};
        if (type == "graphics") {
            pass["outputs"] = json::array({{{"resource", "atlas"}, {"usage", "depth_write"}}});
        }
        return json{{"version", 1},
                    {"resources", json::array({{{"name", "atlas"}, {"kind", "image"},
                                                {"image", {{"format", "D32_SFLOAT"}}}}})},
                    {"passes", json::array({pass})}};
    };
    FrameGraph::FrameGraphBuilder b;
    FrameGraph::loadGraphFromJson(b, graph("scene.shadows.spot[2].rect", "graphics"));
    EXPECT_EQ(b.getPassDeclarations()[0].viewport, "scene.shadows.spot[2].rect");
    FrameGraph::FrameGraphBuilder c;
    EXPECT_THROW(FrameGraph::loadGraphFromJson(c, graph(json::array({0, 0, 1, 1}), "graphics")), std::runtime_error);
    FrameGraph::FrameGraphBuilder d;
    EXPECT_THROW(FrameGraph::loadGraphFromJson(d, graph("scene.shadows.spot[2].rect", "compute")), std::runtime_error);
}

TEST(ShadowAtlas, TilesThatClearTheirRectangleKeepEarlierTiles) {
    using nlohmann::json;
    // Two tile passes clear and draw into one atlas; a third pass reads it.
    auto tile = [](const std::string& name, const std::string& rect) {
        return json{{"name", name}, {"type", "graphics"}, {"viewport", rect},
                    {"execution", {{"type", "shadow_casters"}}},
                    {"outputs", json::array({{{"resource", "atlas"}, {"usage", "depth_write"},
                                              {"clear", {{"depth", 1.0}, {"stencil", 0}}}}})}};
    };
    const json graph = {
        {"version", 1},
        {"resources", json::array({{{"name", "atlas"}, {"kind", "image"},
                                    {"image", {{"format", "D32_SFLOAT"}, {"width", 64}, {"height", 64}}}}})},
        {"passes", json::array({tile("TileA", "scene.shadows.spot[0].rect"), tile("TileB", "scene.shadows.spot[1].rect"),
                                {{"name", "Read"}, {"type", "graphics"}, {"hasSideEffects", true},
                                 {"execution", {{"type", "fullscreen"}}},
                                 {"inputs", json::array({{{"resource", "atlas"}, {"usage", "shader_read"}}})}}})}};
    FrameGraph::FrameGraphBuilder b;
    FrameGraph::loadGraphFromJson(b, graph);
    const auto scheduled = FrameGraph::describeResources(b.getResourceDeclarations());
    const auto deps = FrameGraph::passDependencies(b.getPassDeclarations(), scheduled);
    ASSERT_EQ(deps.size(), 3u);
    EXPECT_EQ(deps[1], std::vector<uint32_t>{0});   // TileB keeps TileA's tile
    EXPECT_EQ(deps[2], std::vector<uint32_t>{1});
    const auto live = FrameGraph::livePasses(b.getPassDeclarations(), b.getResourceDeclarations(), deps);
    EXPECT_EQ(live, (std::vector<uint32_t>{0, 1, 2}));
}
