//
// ShippedPipelineInterfaceTest.cpp - every example pipeline against its shaders
//
// Tier 1: no GPU. Loads each pipeline JSON under examples/ and runs the same
// shader interface check the compiler runs at startup, from the example's own
// directory so the relative shader paths resolve as they do at runtime.
//
// The C++ examples commit their .spv files. The Python examples compile theirs
// on first run and do not commit them, so a Python pipeline whose shaders have
// not been built yet is checked only as far as its .spv files exist.
//

#include <gtest/gtest.h>

#include "App/ApplicationBase.h"
#include "FrameGraph/DotPathResolver.h"
#include "Vulkan/FrameGraph/FrameGraph.h"
#include "Vulkan/FrameGraph/FrameGraphJson.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace Shoonyakasha::FrameGraph;
namespace fs = std::filesystem;

namespace {

/// Every pipeline under examples/: the JSON files that declare render passes
/// or include other files, less those another one includes.
std::vector<fs::path> pipelineFiles() {
    std::vector<fs::path> candidates;
    const fs::path root = fs::path(SHOONYAKASHA_SOURCE_DIR) / "examples";
    for (const auto& entry : fs::recursive_directory_iterator(root)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".json") continue;
        std::ifstream in(entry.path());
        const auto json = nlohmann::json::parse(in, nullptr, /*allow_exceptions=*/false);
        if (json.is_object() && (json.contains("passes") || json.contains("include"))) {
            candidates.push_back(entry.path());
        }
    }
    std::vector<std::string> included;
    for (const auto& file : candidates) {
        std::vector<std::string> read;
        loadPipelineDocument(file.string(), &read);
        for (size_t i = 1; i < read.size(); ++i) included.push_back(fs::path(read[i]).lexically_normal().generic_string());
    }
    std::vector<fs::path> files;
    for (const auto& file : candidates) {
        const std::string name = file.lexically_normal().generic_string();
        if (std::find(included.begin(), included.end(), name) == included.end()) files.push_back(file);
    }
    return files;
}

/// Restores the working directory when it goes out of scope.
struct WorkingDirectory {
    fs::path saved = fs::current_path();
    explicit WorkingDirectory(const fs::path& dir) { fs::current_path(dir); }
    ~WorkingDirectory() { fs::current_path(saved); }
};

} // namespace

TEST(ShippedPipelineInterfaces, EveryExampleMatchesItsShaders) {
    const auto files = pipelineFiles();
    ASSERT_FALSE(files.empty()) << "found no example pipelines under " << SHOONYAKASHA_SOURCE_DIR;

    for (const auto& file : files) {
        SCOPED_TRACE(file.string());
        WorkingDirectory cwd(file.parent_path());

        FrameGraphBuilder builder;
        loadGraphFromFile(builder, file.string());

        FrameGraphCompiler compiler;
        std::unordered_map<std::string, CompiledBufferLayout> layouts;
        compiler.compileBufferLayouts(builder.getBufferLayouts(), layouts);

        std::string error;
        EXPECT_TRUE(compiler.validateShaderInterfaces(builder, layouts, error)) << error;
    }
}

// ── The default pipeline ────────────────────────────────────────

namespace {

const fs::path& defaultPipeline() {
    static const fs::path path =
        fs::path(SHOONYAKASHA_SOURCE_DIR) / "python" / "shoonyakasha" / "pipelines" / "default" / "pipeline.json";
    return path;
}

} // namespace

TEST(DefaultPipeline, IsWhereAnEmptyPipelinePathLooks) {
    if (std::getenv("SHOONYAKASHA_DEFAULT_PIPELINE")) GTEST_SKIP() << "overridden by the environment";
    EXPECT_TRUE(fs::equivalent(Shoonyakasha::ApplicationBase::defaultPipelinePath(), defaultPipeline()));
}

TEST(DefaultPipeline, LoadsFromAnyWorkingDirectoryAndMatchesItsShaders) {
    // Shader paths resolve beside the JSON, so the working directory does not
    // matter. The .spv files are committed: a wheel has no compiler to make them.
    WorkingDirectory cwd(fs::temp_directory_path());

    FrameGraphBuilder builder;
    loadGraphFromFile(builder, defaultPipeline().string());

    for (const auto& pass : builder.getPassDeclarations()) {
        for (const auto* shader : {&pass.pipelineDesc.vertexShader, &pass.pipelineDesc.fragmentShader}) {
            if (shader->empty()) continue;
            EXPECT_TRUE(fs::path(*shader).is_absolute()) << pass.name << ": " << *shader;
            EXPECT_TRUE(fs::exists(*shader)) << pass.name << ": " << *shader;
        }
    }

    FrameGraphCompiler compiler;
    std::unordered_map<std::string, CompiledBufferLayout> layouts;
    compiler.compileBufferLayouts(builder.getBufferLayouts(), layouts);
    std::string error;
    EXPECT_TRUE(compiler.validateShaderInterfaces(builder, layouts, error)) << error;
}

TEST(DefaultPipeline, ShipsTheIBLShaders) {
    const fs::path ibl = defaultPipeline().parent_path() / "shaders" / "ibl";
    for (const char* name : {"equirect_to_cubemap.comp.spv", "irradiance_convolution.comp.spv",
                             "prefilter_convolution.comp.spv"}) {
        EXPECT_TRUE(fs::exists(ibl / name)) << name;
    }
}

TEST(DefaultPipeline, ShipsTheUIShaders) {
    const fs::path ui = defaultPipeline().parent_path() / "shaders" / "ui";
    for (const char* name : {"ui.vert.spv", "ui.frag.spv"}) {
        EXPECT_TRUE(fs::exists(ui / name)) << name;
    }
}

TEST(DefaultPipeline, RendersEveryLocalShadowSlot) {
    FrameGraphBuilder builder;
    loadGraphFromFile(builder, defaultPipeline().string());
    Shoonyakasha::SceneContext scene;
    setLocalShadowSlots(builder, scene);
    EXPECT_EQ(scene.localShadow.pipelineSpotSlots, Shoonyakasha::MAX_SPOT_SHADOWS);
    EXPECT_EQ(scene.localShadow.pipelinePointSlots, Shoonyakasha::MAX_POINT_SHADOWS);
}

TEST(ShippedPipelineInterfaces, ShowroomRendersEightSpotAndFourPointShadowSlots) {
    const fs::path showroom = fs::path(SHOONYAKASHA_SOURCE_DIR) / "examples/python/rendering/showroom/pipeline";
    WorkingDirectory cwd(showroom);
    FrameGraphBuilder builder;
    loadGraphFromFile(builder, (showroom / "pipeline.json").string());
    Shoonyakasha::SceneContext scene;
    setLocalShadowSlots(builder, scene);
    EXPECT_EQ(scene.localShadow.pipelineSpotSlots, 8u);
    EXPECT_EQ(scene.localShadow.pipelinePointSlots, 4u);
}

TEST(DefaultPipeline, DrawsTheUIOverlayLastAndPresentsFromIt) {
    FrameGraphBuilder builder;
    loadGraphFromFile(builder, defaultPipeline().string());
    const auto& passes = builder.getPassDeclarations();
    ASSERT_FALSE(passes.empty());

    const auto& overlay = passes.back();
    EXPECT_EQ(overlay.name, "UIOverlay");
    EXPECT_EQ(overlay.execution.type, "ui_canvas");
    ASSERT_EQ(overlay.outputs.size(), 1u);
    EXPECT_EQ(overlay.outputs[0].usage, ResourceUsage::ColorAttachmentBlend);
    EXPECT_TRUE(leavesPresentable(overlay.outputs[0]));

    // Only the overlay leaves the swapchain ready to present.
    for (size_t i = 0; i + 1 < passes.size(); ++i) {
        for (const auto& output : passes[i].outputs) {
            EXPECT_FALSE(leavesPresentable(output)) << passes[i].name;
        }
    }
}

TEST(DefaultPipeline, TestsWorldCanvasHitsAgainstTheLastGBufferDepth) {
    FrameGraphBuilder builder;
    loadGraphFromFile(builder, defaultPipeline().string());
    const auto& ui = builder.getUISettings();
    EXPECT_EQ(ui.depth, "gDepth");
    EXPECT_EQ(ui.afterPass, "GBufferSkinnedMasked");

    // No later pass writes the depth: the opaque scene is complete there.
    const auto depth = builder.getResource("gDepth");
    bool after = false;
    for (const auto& pass : builder.getPassDeclarations()) {
        if (after) {
            for (const auto& output : pass.outputs) {
                EXPECT_FALSE(output.handle == depth && output.usage == ResourceUsage::DepthStencilWrite)
                    << pass.name;
            }
        }
        if (pass.name == ui.afterPass) after = true;
    }
    EXPECT_TRUE(after);
}

TEST(DefaultPipeline, MaterialPassesReadLastFramesExposure) {
    FrameGraphBuilder builder;
    loadGraphFromFile(builder, defaultPipeline().string());
    const auto exposure = builder.getResource("exposure");

    // TransparentRT is left out on a device without ray queries.
    const std::vector<std::string> drawing = {"GBufferOpaque", "GBufferMasked", "GBufferSkinned",
                                              "GBufferSkinnedMasked", "Transparent", "TransparentRT"};
    bool exposureWritten = false;
    int materialPasses = 0;
    for (const auto& pass : builder.getPassDeclarations()) {
        if (pass.name == "AutoExposure") exposureWritten = true;
        if (std::find(drawing.begin(), drawing.end(), pass.name) == drawing.end()) continue;
        ++materialPasses;
        EXPECT_FALSE(exposureWritten) << pass.name << " comes after AutoExposure";
        EXPECT_NE(std::find(pass.descriptorSetRefs.begin(), pass.descriptorSetRefs.end(), "exposureReadSet"),
                  pass.descriptorSetRefs.end()) << pass.name;
        EXPECT_TRUE(std::any_of(pass.inputs.begin(), pass.inputs.end(),
                                [&](const ResourceAccess& a) { return a.handle == exposure; })) << pass.name;
    }
    EXPECT_GE(materialPasses, 5);

    const auto* draw = builder.getBufferLayout("MaterialDraw");
    ASSERT_NE(draw, nullptr);
    const auto field = std::find_if(draw->fields.begin(), draw->fields.end(),
                                    [](const BufferFieldDesc& f) { return f.name == "exposureCompensation"; });
    ASSERT_NE(field, draw->fields.end());
    EXPECT_EQ(field->source, "entity.material.params.exposureCompensation");
    ASSERT_EQ(field->defaultValue.size(), 1u);
    EXPECT_EQ(field->defaultValue[0], 0.0f);   // materials without it are not compensated
}

TEST(PipelineShaderPaths, RelativePathsOnlyResolveBesideTheJsonWhenTheFileIsThere) {
    const fs::path dir = fs::temp_directory_path() / "sk_shader_path_test";
    fs::remove_all(dir);
    fs::create_directories(dir / "shaders");
    std::ofstream(dir / "shaders" / "present.vert.spv") << "x";

    const nlohmann::json graph = {
        {"version", 1},
        {"resources", {{{"name", "swapchain"}, {"kind", "image"}, {"imported", true}}}},
        {"passes", {{{"name", "P"}, {"type", "graphics"},
                     {"outputs", {{{"resource", "swapchain"}, {"usage", "present"}}}},
                     {"pipeline", {{"vertexShader", "shaders/present.vert.spv"},
                                   {"fragmentShader", "shaders/elsewhere.frag.spv"}}}}}}
    };
    std::ofstream(dir / "graph.json") << graph.dump();

    FrameGraphBuilder builder;
    loadGraphFromFile(builder, (dir / "graph.json").string());
    const auto& desc = builder.getPassDeclarations().at(0).pipelineDesc;
    EXPECT_TRUE(fs::equivalent(desc.vertexShader, dir / "shaders" / "present.vert.spv"));
    EXPECT_EQ(desc.fragmentShader, "shaders/elsewhere.frag.spv");   // left for the working directory
    fs::remove_all(dir);
}

TEST(PipelineJson, BindingsNameTheApplicationImageTheySample) {
    const nlohmann::json graph = {
        {"version", 1},
        {"descriptorSetLayouts", {{"imageSet", {{"bindings", {{
            {"binding", 0}, {"type", "combined_image_sampler"}, {"name", "picture"},
            {"externalImage", "lightImage"}, {"autoBindSampler", "linearClamp"}}}}}}}},
        {"resources", {{{"name", "swapchain"}, {"kind", "image"}, {"imported", true}}}},
        {"passes", {{{"name", "P"}, {"type", "graphics"},
                     {"outputs", {{{"resource", "swapchain"}, {"usage", "present"}}}}}}}
    };
    FrameGraphBuilder builder;
    loadGraphFromJson(builder, graph);
    ASSERT_EQ(builder.getDescriptorSetLayouts().size(), 1u);
    const auto& binding = builder.getDescriptorSetLayouts()[0].bindings.at(0);
    EXPECT_EQ(binding.externalImage, "lightImage");
    EXPECT_EQ(binding.autoBindSampler, "linearClamp");
}

TEST(PipelineShaderPaths, BufferFilesResolveBesideTheJsonWhenTheFileIsThere) {
    const fs::path dir = fs::temp_directory_path() / "sk_buffer_path_test";
    fs::remove_all(dir);
    fs::create_directories(dir);
    std::ofstream(dir / "table.bin") << "x";

    auto layout = [](const char* path) {
        return nlohmann::json{{"usage", "storage_buffer"}, {"packing", "std430"}, {"elementCount", 1},
                              {"fields", {{{"name", "v"}, {"type", "vec4"}}}},
                              {"source", {{"type", "file"}, {"path", path}}}};
    };
    const nlohmann::json graph = {
        {"version", 1},
        {"bufferLayouts", {{"Present", layout("table.bin")}, {"Elsewhere", layout("data/elsewhere.bin")}}},
        {"resources", {{{"name", "swapchain"}, {"kind", "image"}, {"imported", true}}}},
        {"passes", {{{"name", "P"}, {"type", "graphics"},
                     {"outputs", {{{"resource", "swapchain"}, {"usage", "present"}}}}}}}
    };
    std::ofstream(dir / "graph.json") << graph.dump();

    FrameGraphBuilder builder;
    loadGraphFromFile(builder, (dir / "graph.json").string());
    for (const auto& desc : builder.getBufferLayouts()) {
        if (desc.name == "Present") {
            EXPECT_TRUE(fs::equivalent(desc.initConfig.filePath, dir / "table.bin"));
        } else {
            EXPECT_EQ(desc.initConfig.filePath, "data/elsewhere.bin");   // left for the working directory
        }
    }
    fs::remove_all(dir);
}
