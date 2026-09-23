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

#include "Vulkan/FrameGraph/FrameGraph.h"
#include "Vulkan/FrameGraph/FrameGraphJson.h"

#include <nlohmann/json.hpp>

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
