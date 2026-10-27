//
// PipelineDocumentTest.cpp - pipelines split across files (loadPipelineDocument)
//
// Tier 1: files only, no GPU.
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
using nlohmann::json;

namespace {

struct TempDir {
    fs::path path;
    TempDir() {
        path = fs::temp_directory_path() /
               ("shoonyakasha_pipeline_doc_test_" + std::to_string(reinterpret_cast<uintptr_t>(this)));
        fs::create_directories(path);
    }
    ~TempDir() {
        std::error_code error;
        fs::remove_all(path, error);
    }
    void write(const std::string& name, const json& value) const {
        const fs::path file = path / name;
        fs::create_directories(file.parent_path());
        std::ofstream(file) << value.dump(2);
    }
    void touch(const std::string& name) const {
        const fs::path file = path / name;
        fs::create_directories(file.parent_path());
        std::ofstream(file) << "x";
    }
};

json pass(const std::string& name, const std::string& shader = "") {
    json p = {{"name", name}, {"type", "compute"}};
    if (!shader.empty()) p["pipeline"] = {{"computeShader", shader}};
    return p;
}

std::vector<std::string> passNames(const json& document) {
    std::vector<std::string> out;
    for (const auto& p : document["passes"]) out.push_back(p["name"]);
    return out;
}

std::string errorOf(const std::string& path) {
    try {
        loadPipelineDocument(path);
    } catch (const std::runtime_error& e) {
        return e.what();
    }
    return {};
}

} // namespace

TEST(PipelineDocument, SingleFileIsUnchanged) {
    TempDir dir;
    const json single = {{"name", "P"}, {"resources", json::array({{{"name", "a"}, {"kind", "image"}}})},
                         {"passes", json::array({pass("x")})}, {"samplers", {{"s", {{"filter", "linear"}}}}}};
    dir.write("pipeline.json", single);
    EXPECT_EQ(loadPipelineDocument((dir.path / "pipeline.json").string()), single);
    EXPECT_EQ(loadPipelineDocument(dir.path.string()), single);   // a directory loads its pipeline.json
}

TEST(PipelineDocument, IncludesMergeInOrderThenOwnKeys) {
    TempDir dir;
    dir.write("pipeline.json", {{"name", "P"},
                                {"include", {"resources.json", "passes/*.json"}},
                                {"passes", json::array({pass("last")})}});
    dir.write("resources.json", {{"resources", json::array({{{"name", "a"}, {"kind", "image"}}})},
                                 {"bufferLayouts", {{"L1", json::object()}}}});
    dir.write("passes/20_b.json", {{"passes", json::array({pass("b1"), pass("b2")})},
                                   {"bufferLayouts", {{"L2", json::object()}}}});
    dir.write("passes/10_a.json", {{"passes", json::array({pass("a1")})}});
    dir.touch("passes/notes.txt");   // not matched by *.json

    std::vector<std::string> files;
    const json document = loadPipelineDocument(dir.path.string(), &files);
    EXPECT_EQ(passNames(document), (std::vector<std::string>{"a1", "b1", "b2", "last"}));
    EXPECT_EQ(document["bufferLayouts"].size(), 2u);
    EXPECT_EQ(document["name"], "P");
    EXPECT_FALSE(document.contains("include"));
    ASSERT_EQ(files.size(), 4u);
    EXPECT_NE(files[0].find("pipeline.json"), std::string::npos);
    EXPECT_NE(files[2].find("10_a.json"), std::string::npos);
    EXPECT_NE(files[3].find("20_b.json"), std::string::npos);
}

TEST(PipelineDocument, ShaderPathsResolveBesideTheirOwnFile) {
    TempDir dir;
    dir.touch("shaders/root.comp.spv");
    dir.touch("passes/local.comp.spv");
    dir.write("pipeline.json", {{"include", {"passes/a.json"}}, {"passes", json::array({pass("r", "shaders/root.comp.spv")})}});
    dir.write("passes/a.json", {{"passes", json::array({pass("up", "../shaders/root.comp.spv"),
                                                        pass("here", "local.comp.spv"),
                                                        pass("missing", "nowhere.comp.spv")})}});
    const json document = loadPipelineDocument(dir.path.string());
    const auto shader = [&](size_t i) { return document["passes"][i]["pipeline"]["computeShader"].get<std::string>(); };
    // Included passes come first, then the including file's own.
    const fs::path root = fs::absolute(dir.path).lexically_normal();
    EXPECT_EQ(fs::path(shader(0)), (root / "shaders/root.comp.spv").lexically_normal());   // "../" from passes/
    EXPECT_EQ(fs::path(shader(1)), (root / "passes/local.comp.spv").lexically_normal());
    EXPECT_EQ(shader(2), "nowhere.comp.spv");   // not beside its file: left for the working directory
    EXPECT_EQ(fs::path(shader(3)), (root / "shaders/root.comp.spv").lexically_normal());
}

TEST(PipelineDocument, NamesDefinedTwiceNameBothFiles) {
    TempDir dir;
    dir.write("pipeline.json", {{"include", {"a.json", "b.json"}}});
    dir.write("a.json", {{"descriptorSetLayouts", {{"camera", json::object()}}}});
    dir.write("b.json", {{"descriptorSetLayouts", {{"camera", json::object()}}}});
    const std::string error = errorOf(dir.path.string());
    EXPECT_NE(error.find("descriptorSetLayouts 'camera'"), std::string::npos) << error;
    EXPECT_NE(error.find("a.json"), std::string::npos) << error;
    EXPECT_NE(error.find("b.json"), std::string::npos) << error;

    dir.write("b.json", {{"passes", json::array({pass("x")})}});
    dir.write("a.json", {{"passes", json::array({pass("x")})}});
    EXPECT_NE(errorOf(dir.path.string()).find("passes 'x'"), std::string::npos);

    dir.write("a.json", {{"ui", {{"depth", "d1"}}}});
    dir.write("b.json", {{"ui", {{"depth", "d2"}}}});
    EXPECT_NE(errorOf(dir.path.string()).find("'ui'"), std::string::npos);

    dir.write("b.json", {{"ui", {{"depth", "d1"}}}});   // the same value twice is fine
    EXPECT_EQ(errorOf(dir.path.string()), "");
}

TEST(PipelineDocument, BadIncludes) {
    TempDir dir;
    dir.write("pipeline.json", {{"include", {"missing.json"}}});
    EXPECT_NE(errorOf(dir.path.string()).find("names no file"), std::string::npos);

    dir.write("pipeline.json", {{"include", {"none/*.json"}}});
    EXPECT_NE(errorOf(dir.path.string()).find("names no file"), std::string::npos);

    dir.write("pipeline.json", {{"include", {"*/x.json"}}});
    EXPECT_NE(errorOf(dir.path.string()).find("last part"), std::string::npos);

    dir.write("pipeline.json", {{"include", {"a.json"}}});
    dir.write("a.json", {{"include", {"pipeline.json"}}});
    EXPECT_NE(errorOf(dir.path.string()).find("includes itself"), std::string::npos);

    dir.write("pipeline.json", {{"include", "a.json"}});
    EXPECT_NE(errorOf(dir.path.string()).find("not a list"), std::string::npos);

    std::ofstream(dir.path / "pipeline.json") << "{ \"passes\": [ }";
    const std::string parse = errorOf(dir.path.string());
    EXPECT_NE(parse.find("pipeline.json"), std::string::npos) << parse;
}

TEST(PipelineDocument, LoadGraphFromADirectory) {
    TempDir dir;
    dir.write("pipeline.json", {{"include", {"passes.json"}},
                                {"resources", json::array({{{"name", "buf"}, {"kind", "buffer"},
                                                            {"buffer", {{"size", 64}}}}})}});
    dir.write("passes.json", {{"passes", json::array({{{"name", "fill"}, {"type", "compute"}, {"hasSideEffects", true},
                                                       {"outputs", json::array({{{"resource", "buf"},
                                                                                 {"usage", "shader_read_write"}}})}}})}});
    FrameGraphBuilder builder;
    loadGraphFromFile(builder, dir.path.string());
    EXPECT_EQ(builder.getPassDeclarations().size(), 1u);
}
