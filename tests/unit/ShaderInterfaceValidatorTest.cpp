//
// ShaderInterfaceValidatorTest.cpp - SPIR-V interfaces against JSON layouts
//
// Tier 1: no GPU. The fixtures under tests/shaders/ are compiled to SPIR-V by
// tests/CMakeLists.txt; their block layouts are spelled out in each fixture.
//

#include <gtest/gtest.h>

#include "FrameGraph/ShaderInterfaceValidator.h"
#include "Vulkan/FrameGraph/FrameGraph.h"
#include "Vulkan/FrameGraph/FrameGraphJson.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <string>
#include <vector>

using namespace Shoonyakasha::FrameGraph;

namespace {

const std::string kShaderDir = SHOONYAKASHA_TEST_SHADER_DIR;

std::vector<char> readSpirv(const std::string& name) {
    std::ifstream file(kShaderDir + "/" + name, std::ios::binary | std::ios::ate);
    EXPECT_TRUE(file) << "missing fixture " << name << " (built by tests/CMakeLists.txt)";
    std::vector<char> code(static_cast<size_t>(file.tellg()));
    file.seekg(0);
    file.read(code.data(), static_cast<std::streamsize>(code.size()));
    return code;
}

BufferFieldDesc field(const char* name, BufferFieldType type, uint32_t arrayCount = 1) {
    BufferFieldDesc f;
    f.name = name;
    f.type = type;
    f.arrayCount = arrayCount;
    return f;
}

std::unordered_map<std::string, CompiledBufferLayout> compileLayouts(std::vector<BufferLayoutDesc> descs) {
    FrameGraphCompiler compiler;
    std::unordered_map<std::string, CompiledBufferLayout> out;
    compiler.compileBufferLayouts(descs, out);
    return out;
}

BufferLayoutDesc cameraDesc(std::vector<BufferFieldDesc> fields) {
    BufferLayoutDesc desc;
    desc.name = "CameraUBO";
    desc.usage = BufferUsageType::UniformBuffer;
    desc.packing = BufferPackingRule::Std140;
    desc.fields = std::move(fields);
    return desc;
}

BufferLayoutDesc pushDesc(std::vector<BufferFieldDesc> fields) {
    BufferLayoutDesc desc;
    desc.name = "Push";
    desc.usage = BufferUsageType::PushConstant;
    desc.packing = BufferPackingRule::Std430;
    desc.fields = std::move(fields);
    return desc;
}

/// The fields interface.frag declares, in the same order and types.
std::vector<BufferFieldDesc> matchingCameraFields() {
    return {
        field("view",     BufferFieldType::Mat4),
        field("position", BufferFieldType::Vec4),
        field("count",    BufferFieldType::UInt),
        field("pad",      BufferFieldType::Float),
        field("lights",   BufferFieldType::Vec4, 4),
    };
}

std::vector<BufferFieldDesc> matchingPushFields() {
    return {field("model", BufferFieldType::Mat4), field("tint", BufferFieldType::Vec4)};
}

/// A pass declaring what interface.frag uses: CameraUBO at set 0, a sampler
/// at set 1, and 80 bytes of push constants.
ShaderInterfaceExpectation interfaceExpectation(
    const std::unordered_map<std::string, CompiledBufferLayout>& layouts) {
    ShaderInterfaceExpectation e;
    e.setCount = 2;
    e.descriptors[{0, 0}] = {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, "cameraSet", "CameraUBO", &layouts.at("CameraUBO")};
    e.descriptors[{1, 0}] = {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, "materialSet", "albedo", nullptr};
    e.pushConstantSize = 80;
    e.pushConstantLayout = &layouts.at("Push");
    return e;
}

std::vector<std::string> validate(const std::string& shader, const ShaderInterfaceExpectation& e) {
    const auto code = readSpirv(shader);
    return validateShaderInterface(code.data(), code.size(), e);
}

std::string joined(const std::vector<std::string>& messages) {
    std::string all;
    for (const auto& m : messages) all += m + "\n";
    return all;
}

bool anyContains(const std::vector<std::string>& messages, const std::string& needle) {
    for (const auto& m : messages) {
        if (m.find(needle) != std::string::npos) return true;
    }
    return false;
}

} // namespace

TEST(ShaderInterfaceValidator, MatchingInterfaceHasNoErrors) {
    const auto layouts = compileLayouts({cameraDesc(matchingCameraFields()), pushDesc(matchingPushFields())});
    const auto errors = validate("interface.frag.spv", interfaceExpectation(layouts));
    EXPECT_TRUE(errors.empty()) << joined(errors);
}

TEST(ShaderInterfaceValidator, ExtraTrailingJsonFieldsAreAccepted) {
    // The shader may read a prefix of the JSON block.
    auto fields = matchingCameraFields();
    fields.push_back(field("unusedByShader", BufferFieldType::Vec4));
    const auto layouts = compileLayouts({cameraDesc(fields), pushDesc(matchingPushFields())});
    const auto errors = validate("interface.frag.spv", interfaceExpectation(layouts));
    EXPECT_TRUE(errors.empty()) << joined(errors);
}

TEST(ShaderInterfaceValidator, ReportsTypeMismatch) {
    auto fields = matchingCameraFields();
    fields[2] = field("count", BufferFieldType::Float);   // shader says uint
    const auto layouts = compileLayouts({cameraDesc(fields), pushDesc(matchingPushFields())});
    const auto errors = validate("interface.frag.spv", interfaceExpectation(layouts));
    ASSERT_EQ(errors.size(), 1u) << joined(errors);
    EXPECT_NE(errors[0].find("camera.count"), std::string::npos) << errors[0];
    EXPECT_NE(errors[0].find("uint"), std::string::npos) << errors[0];
    EXPECT_NE(errors[0].find("float"), std::string::npos) << errors[0];
}

TEST(ShaderInterfaceValidator, ReportsShiftedOffsets) {
    // An extra vec4 in the JSON pushes everything after `view` 16 bytes later.
    auto fields = matchingCameraFields();
    fields.insert(fields.begin() + 1, field("inserted", BufferFieldType::Vec4));
    const auto layouts = compileLayouts({cameraDesc(fields), pushDesc(matchingPushFields())});
    const auto errors = validate("interface.frag.spv", interfaceExpectation(layouts));
    EXPECT_FALSE(errors.empty());
    EXPECT_TRUE(anyContains(errors, "camera.count")) << joined(errors);
}

TEST(ShaderInterfaceValidator, ReportsArrayLengthMismatch) {
    auto fields = matchingCameraFields();
    fields[4] = field("lights", BufferFieldType::Vec4, 3);
    const auto layouts = compileLayouts({cameraDesc(fields), pushDesc(matchingPushFields())});
    const auto errors = validate("interface.frag.spv", interfaceExpectation(layouts));
    ASSERT_EQ(errors.size(), 1u) << joined(errors);
    EXPECT_NE(errors[0].find("4 element(s)"), std::string::npos) << errors[0];
}

TEST(ShaderInterfaceValidator, ReportsDescriptorTypeMismatch) {
    const auto layouts = compileLayouts({cameraDesc(matchingCameraFields()), pushDesc(matchingPushFields())});
    auto e = interfaceExpectation(layouts);
    e.descriptors[{1, 0}].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    const auto errors = validate("interface.frag.spv", e);
    ASSERT_EQ(errors.size(), 1u) << joined(errors);
    EXPECT_NE(errors[0].find("combined_image_sampler"), std::string::npos) << errors[0];
    EXPECT_NE(errors[0].find("storage_image"), std::string::npos) << errors[0];
}

TEST(ShaderInterfaceValidator, ReportsMissingBinding) {
    const auto layouts = compileLayouts({cameraDesc(matchingCameraFields()), pushDesc(matchingPushFields())});
    auto e = interfaceExpectation(layouts);
    e.descriptors.erase({1, 0});
    const auto errors = validate("interface.frag.spv", e);
    ASSERT_EQ(errors.size(), 1u) << joined(errors);
    EXPECT_NE(errors[0].find("no binding 0 in set 1"), std::string::npos) << errors[0];
}

TEST(ShaderInterfaceValidator, ReportsTooFewDescriptorSets) {
    const auto layouts = compileLayouts({cameraDesc(matchingCameraFields()), pushDesc(matchingPushFields())});
    auto e = interfaceExpectation(layouts);
    e.setCount = 1;
    const auto errors = validate("interface.frag.spv", e);
    ASSERT_EQ(errors.size(), 1u) << joined(errors);
    EXPECT_NE(errors[0].find("uses set 1"), std::string::npos) << errors[0];
}

TEST(ShaderInterfaceValidator, ReportsPushConstantsLargerThanDeclared) {
    const auto layouts = compileLayouts({cameraDesc(matchingCameraFields()), pushDesc(matchingPushFields())});
    auto e = interfaceExpectation(layouts);
    e.pushConstantSize = 64;
    const auto errors = validate("interface.frag.spv", e);
    ASSERT_EQ(errors.size(), 1u) << joined(errors);
    EXPECT_NE(errors[0].find("ends at byte 80"), std::string::npos) << errors[0];
}

TEST(ShaderInterfaceValidator, ReportsPushConstantFieldMismatch) {
    const auto layouts = compileLayouts({cameraDesc(matchingCameraFields()),
                                         pushDesc({field("model", BufferFieldType::Mat4),
                                                   field("tint", BufferFieldType::Vec3)})});
    const auto errors = validate("interface.frag.spv", interfaceExpectation(layouts));
    ASSERT_EQ(errors.size(), 1u) << joined(errors);
    EXPECT_NE(errors[0].find("push.tint"), std::string::npos) << errors[0];
}

TEST(ShaderInterfaceValidator, SkipsRuntimeArrayBlocks) {
    // The JSON describes one particle; the shader declares an unsized array.
    BufferLayoutDesc desc;
    desc.name = "Particles";
    desc.usage = BufferUsageType::StorageBuffer;
    desc.packing = BufferPackingRule::Std430;
    desc.fields = {field("position", BufferFieldType::Vec4), field("velocity", BufferFieldType::Vec4)};
    const auto layouts = compileLayouts({desc});

    ShaderInterfaceExpectation e;
    e.setCount = 1;
    e.descriptors[{0, 0}] = {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, "particleSet", "Particles", &layouts.at("Particles")};
    const auto errors = validate("runtime_array.comp.spv", e);
    EXPECT_TRUE(errors.empty()) << joined(errors);
}

TEST(ShaderInterfaceValidator, SharedLibraryIncludesCompile) {
    // uses_library.frag #includes sk/pbr.glsl and sk/tonemap.glsl; the build
    // only produces it when glslc gets the library's include directory.
    const auto errors = validate("uses_library.frag.spv", ShaderInterfaceExpectation{});
    EXPECT_TRUE(errors.empty()) << joined(errors);
}

TEST(ShaderInterfaceValidator, UnparseableModuleIsOneError) {
    const std::vector<char> garbage(64, '\x7f');
    const auto errors = validateShaderInterface(garbage.data(), garbage.size(), ShaderInterfaceExpectation{});
    ASSERT_EQ(errors.size(), 1u);
    EXPECT_NE(errors[0].find("could not parse"), std::string::npos) << errors[0];
}

// The compiler stage builds the expectation from the pass's JSON: descriptor
// set order gives set indices, autoBindBuffer names the layout, and the
// entity data binding's perDraw layout describes the push constants.
TEST(ShaderInterfaceValidator, CompilerStageReadsThePassJson) {
    auto graph = [](const std::string& countType) {
        return nlohmann::json{
            {"version", 3},
            {"name", "interface_test"},
            {"bufferLayouts", {
                {"CameraUBO", {{"usage", "uniform_buffer"}, {"packing", "std140"}, {"fields", {
                    {{"name", "view"}, {"type", "mat4"}, {"source", "const.0"}},
                    {{"name", "position"}, {"type", "vec4"}, {"source", "const.0"}},
                    {{"name", "count"}, {"type", countType}, {"source", "const.0"}},
                    {{"name", "pad"}, {"type", "float"}, {"source", "const.0"}},
                    {{"name", "lights"}, {"type", "vec4"}, {"arrayCount", 4}, {"source", "const.0"}}
                }}}},
                {"Push", {{"usage", "push_constant"}, {"packing", "scalar"},
                          {"binding", {{"offset", 0}, {"stages", {"vertex", "fragment"}}}},
                          {"fields", {
                    {{"name", "model"}, {"type", "mat4"}, {"source", "entity.transform.worldMatrix"}},
                    {{"name", "tint"}, {"type", "vec4"}, {"source", "const.0"}}
                }}}}
            }},
            {"entityDataBindings", {{"drawable", {{"perDraw", {{"layoutRef", "Push"}}}}}}},
            {"descriptorSetLayouts", {
                {"cameraSet", {{"bindings", {
                    {{"binding", 0}, {"type", "uniform_buffer"}, {"stages", {"fragment"}},
                     {"name", "CameraUBO"}, {"autoBindBuffer", "CameraUBO"}}
                }}}},
                {"materialSet", {{"bindings", {
                    {{"binding", 0}, {"type", "combined_image_sampler"}, {"stages", {"fragment"}}, {"name", "albedo"}}
                }}}}
            }},
            {"resources", {{{"name", "swapchain"}, {"kind", "image"}, {"imported", true}}}},
            {"passes", {{
                {"name", "DrawPass"},
                {"type", "graphics"},
                {"execution", {{"type", "opaque_geometry"}, {"entityDataBinding", "drawable"}}},
                {"outputs", {{{"resource", "swapchain"}, {"usage", "color_write"}}}},
                {"pipeline", {{"fragmentShader", kShaderDir + "/interface.frag.spv"}}},
                {"descriptorSets", {"cameraSet", "materialSet"}},
                {"pushConstants", {{{"stages", {"vertex", "fragment"}}, {"size", 80}, {"offset", 0}}}}
            }}}
        };
    };

    auto run = [](const nlohmann::json& json, std::string& error) {
        FrameGraphBuilder builder;
        loadGraphFromJson(builder, json);
        FrameGraphCompiler compiler;
        std::unordered_map<std::string, CompiledBufferLayout> layouts;
        compiler.compileBufferLayouts(builder.getBufferLayouts(), layouts);
        return compiler.validateShaderInterfaces(builder, layouts, error);
    };

    std::string error;
    EXPECT_TRUE(run(graph("uint"), error)) << error;

    error.clear();
    EXPECT_FALSE(run(graph("float"), error));
    EXPECT_NE(error.find("pass 'DrawPass'"), std::string::npos) << error;
    EXPECT_NE(error.find("camera.count"), std::string::npos) << error;
}
