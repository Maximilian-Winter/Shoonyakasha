//
// UiCanvasExecutionTest.cpp - the "ui_canvas" execution type
//
// A ui_canvas pass is drawn by the renderer registered for that type through
// RenderGraph::registerGeometryRenderer, which the executor only calls for
// SceneRenderer passes. It is not drawn by the entity renderer.
//

#include <gtest/gtest.h>

#include "Vulkan/FrameGraph/FrameGraphJson.h"
#include "Vulkan/FrameGraph/FrameGraph.h"

#include <nlohmann/json.hpp>

using namespace Shoonyakasha;
using namespace Shoonyakasha::FrameGraph;

TEST(UiCanvasExecution, CompilesToSceneRenderer) {
    EXPECT_EQ(executionKindOf("ui_canvas"), ExecutionKind::SceneRenderer);
}

TEST(UiCanvasExecution, IsNotAnEntityGeometryType) {
    EXPECT_FALSE(isEntityGeometryExecutionType("ui_canvas"));
}

TEST(UiCanvasExecution, OverlayPassLoadsFromJson) {
    const nlohmann::json graph = {
        {"version", 1},
        {"name", "ui_canvas_test"},
        {"resources", {
            {{"name", "swapchain"}, {"kind", "image"}, {"imported", true}}
        }},
        {"passes", {
            {
                {"name", "UIOverlay"},
                {"type", "graphics"},
                {"execution", {{"type", "ui_canvas"}}},
                {"outputs", {{{"resource", "swapchain"}, {"usage", "color_blend"}, {"present", true}}}}
            }
        }}
    };

    FrameGraphBuilder builder;
    ASSERT_NO_THROW(loadGraphFromJson(builder, graph));
    ASSERT_EQ(builder.getPassDeclarations().size(), 1u);
    EXPECT_EQ(builder.getPassDeclarations()[0].execution.type, "ui_canvas");
}
