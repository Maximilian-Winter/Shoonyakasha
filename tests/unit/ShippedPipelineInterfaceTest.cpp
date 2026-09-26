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
#include "Vulkan/FrameGraph/FrameGraph.h"
#include "Vulkan/FrameGraph/FrameGraphJson.h"

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace Shoonyakasha::FrameGraph;
namespace fs = std::filesystem;

namespace {

/// Every JSON file under examples/ that declares render passes.
std::vector<fs::path> pipelineFiles() {
    std::vector<fs::path> files;
    const fs::path root = fs::path(SHOONYAKASHA_SOURCE_DIR) / "examples";
    for (const auto& entry : fs::recursive_directory_iterator(root)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".json") continue;
        std::ifstream in(entry.path());
        const auto json = nlohmann::json::parse(in, nullptr, /*allow_exceptions=*/false);
        if (json.is_object() && json.contains("passes")) files.push_back(entry.path());
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
