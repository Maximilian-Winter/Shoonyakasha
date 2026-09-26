//
// ViewCullingTest.cpp - frustum culling math and the execution "view" key
//
// Tier 1: no GPU.
//

#include <gtest/gtest.h>
#include "FrameGraph/ViewCulling.h"
#include "FrameGraph/FrameGraphRenderer.h"
#include "Vulkan/FrameGraph/FrameGraphJson.h"
#include "Vulkan/FrameGraph/FrameGraph.h"

#include <glm/gtc/matrix_transform.hpp>
#include <nlohmann/json.hpp>

#include <stdexcept>

using namespace Shoonyakasha;
using nlohmann::json;

namespace {

/// A camera at the origin looking down -Z, 90 degree field of view, 1..100.
glm::mat4 cameraViewProj() {
    return glm::perspective(glm::radians(90.0f), 1.0f, 1.0f, 100.0f) *
           glm::lookAtRH(glm::vec3(0.0f), glm::vec3(0, 0, -1), glm::vec3(0, 1, 0));
}

bool inside(const Frustum& f, glm::vec3 centre, float half = 0.5f) {
    return boundsInFrustum(f, centre - glm::vec3(half), centre + glm::vec3(half));
}

} // namespace

TEST(ViewCulling, CameraFrustumKeepsWhatIsInView) {
    const Frustum f = frustumFromViewProj(cameraViewProj(), ClipDepth::MinusOneToOne);
    EXPECT_EQ(f.count, 6u);
    EXPECT_TRUE(inside(f, {0, 0, -10}));
    EXPECT_TRUE(inside(f, {9, 0, -10}));           // near the right edge (x = z at 45 degrees)
    EXPECT_TRUE(inside(f, {10.3f, 0, -10}));       // straddling the edge
    EXPECT_FALSE(inside(f, {12, 0, -10}));         // right of the view
    EXPECT_FALSE(inside(f, {0, 12, -10}));         // above it
    EXPECT_FALSE(inside(f, {0, 0, 10}));           // behind the camera
    EXPECT_FALSE(inside(f, {0, 0, -120}));         // beyond the far plane
    EXPECT_FALSE(inside(f, {0, 0, -0.2f}, 0.1f));  // between the eye and the near plane
}

TEST(ViewCulling, WithoutTheNearPlaneCastersTowardsTheLightPass) {
    // A light-space ortho box, 0..1 depth: z from -10 to -30 in light view.
    const glm::mat4 vp = glm::orthoRH_ZO(-5.0f, 5.0f, -5.0f, 5.0f, 10.0f, 30.0f);
    const Frustum clamped = frustumFromViewProj(vp, ClipDepth::ZeroToOne, false);
    const Frustum full = frustumFromViewProj(vp, ClipDepth::ZeroToOne, true);
    EXPECT_EQ(clamped.count, 5u);

    EXPECT_TRUE(inside(full, {0, 0, -20}));
    EXPECT_TRUE(inside(clamped, {0, 0, -20}));
    EXPECT_FALSE(inside(full, {0, 0, -2}));      // between the light and the near plane
    EXPECT_TRUE(inside(clamped, {0, 0, -2}));    // still casts with depth clamping
    EXPECT_FALSE(inside(clamped, {0, 0, -40}));  // beyond the far plane
    EXPECT_FALSE(inside(clamped, {8, 0, -20}));  // outside the sides
}

TEST(ViewCulling, TransformedBoundsCoverTheRotatedBox) {
    const glm::mat4 world = glm::translate(glm::mat4(1.0f), glm::vec3(10, 0, 0)) *
                            glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(0, 1, 0));
    glm::vec3 min, max;
    transformBounds(world, {0, 0, 0}, {2, 1, 1}, min, max);
    // Rotating +90 degrees about Y maps x -> -z and z -> x.
    EXPECT_NEAR(min.x, 10.0f, 1e-5f);
    EXPECT_NEAR(max.x, 11.0f, 1e-5f);
    EXPECT_NEAR(min.y, 0.0f, 1e-5f);
    EXPECT_NEAR(max.y, 1.0f, 1e-5f);
    EXPECT_NEAR(min.z, -2.0f, 1e-5f);
    EXPECT_NEAR(max.z, 0.0f, 1e-5f);
}

TEST(ViewCulling, EntitiesWithoutBoundsOrOutsideAnActiveViewOnly) {
    FrameGraphRenderer::ViewCull view;
    view.cull = true;
    view.frustum = frustumFromViewProj(cameraViewProj(), ClipDepth::MinusOneToOne);

    MeshComponent mesh;
    const glm::mat4 behind = glm::translate(glm::mat4(1.0f), glm::vec3(0, 0, 10));
    EXPECT_TRUE(FrameGraphRenderer::isVisible(view, mesh, behind));  // no bounds: never culled

    mesh.hasBounds = true;
    mesh.boundsMin = glm::vec3(-0.5f);
    mesh.boundsMax = glm::vec3(0.5f);
    EXPECT_FALSE(FrameGraphRenderer::isVisible(view, mesh, behind));
    EXPECT_TRUE(FrameGraphRenderer::isVisible(view, mesh, glm::translate(glm::mat4(1.0f), glm::vec3(0, 0, -10))));

    view.cull = false;
    EXPECT_TRUE(FrameGraphRenderer::isVisible(view, mesh, behind));
}

// ── execution "view" ────────────────────────────────────────────

namespace {

json graphWithExecution(json execution, json extra = json::object()) {
    json pass = {
        {"name", "Casters"}, {"type", "graphics"},
        {"execution", std::move(execution)},
        {"outputs", json::array({{{"resource", "shadow"}, {"usage", "depth_write"}}})}
    };
    pass.update(extra);
    return {
        {"version", 1},
        {"resources", json::array({{{"name", "shadow"}, {"kind", "image"},
                                    {"image", {{"format", "D32_SFLOAT"}, {"arrayLayers", 4}}}}})},
        {"passes", json::array({pass})}
    };
}

FrameGraph::FrameGraphBuilder load(const json& j) {
    FrameGraph::FrameGraphBuilder b;
    FrameGraph::loadGraphFromJson(b, j);
    return b;
}

} // namespace

TEST(ExecutionView, DefaultsAndNamedViewsParse) {
    using FrameGraph::CullView;
    EXPECT_EQ(load(graphWithExecution({{"type", "shadow_casters"}})).getPassDeclarations()[0].execution.view,
              CullView::Default);
    EXPECT_EQ(load(graphWithExecution({{"type", "opaque_geometry"}, {"view", "none"}}))
                  .getPassDeclarations()[0].execution.view, CullView::None);
    EXPECT_EQ(load(graphWithExecution({{"type", "shadow_casters"}, {"view", "camera"}}))
                  .getPassDeclarations()[0].execution.view, CullView::Camera);

    const auto b = load(graphWithExecution({{"type", "shadow_casters"}, {"view", "shadows.sun.cascades[3]"}}));
    EXPECT_EQ(b.getPassDeclarations()[0].execution.view, CullView::SunCascade);
    EXPECT_EQ(b.getPassDeclarations()[0].execution.viewIndex, 3u);
}

TEST(ExecutionView, RepeatedCascadePassesEachGetTheirCascade) {
    const auto b = load(graphWithExecution(
        {{"type", "shadow_casters"}, {"view", "shadows.sun.cascades[{cascade}]"}},
        {{"repeat", {{"count", 4}, {"index", "cascade"}}}}));
    const auto& passes = b.getPassDeclarations();
    ASSERT_EQ(passes.size(), 4u);
    for (uint32_t i = 0; i < 4; ++i) {
        EXPECT_EQ(passes[i].execution.view, FrameGraph::CullView::SunCascade);
        EXPECT_EQ(passes[i].execution.viewIndex, i);
    }
}

TEST(ExecutionView, BadViewsFailAtLoad) {
    for (const json& execution : {
             json{{"type", "shadow_casters"}, {"view", "shadows.sun.cascades[4]"}},
             json{{"type", "shadow_casters"}, {"view", "shadows.sun.cascades[x]"}},
             json{{"type", "shadow_casters"}, {"view", "sky"}},
             json{{"type", "fullscreen"}, {"view", "camera"}}}) {
        FrameGraph::FrameGraphBuilder b;
        EXPECT_THROW(FrameGraph::loadGraphFromJson(b, graphWithExecution(execution)), std::runtime_error)
            << execution.dump();
    }
}
