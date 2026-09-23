//
// ShaderInterfaceValidator.h - checks a SPIR-V module against its pass's JSON
//
// A pipeline JSON declares every uniform block, storage block and push
// constant layout, and each shader declares the same blocks again in GLSL.
// This compares the two: descriptor sets, bindings and descriptor types, and
// for each buffer block the offset, base type, vector size, matrix stride and
// array stride of every member. Member names are not compared.
//
// A shader may declare fewer members than the JSON layout (a prefix, or any
// subset at matching offsets). Blocks whose last member is a runtime array,
// and arrays of structs, are not compared member by member, because a JSON
// layout describes neither.
//

#pragma once

#include "Vulkan/FrameGraph/FrameGraph.h"

#include <vulkan/vulkan.h>

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace Shoonyakasha {
namespace FrameGraph {

/// What a pass declares at one (set, binding).
struct ExpectedDescriptor {
    VkDescriptorType type = VK_DESCRIPTOR_TYPE_MAX_ENUM;
    std::string setName;                            // key in "descriptorSetLayouts"
    std::string bindingName;                        // the binding's "name"
    const CompiledBufferLayout* layout = nullptr;   // layout named by "autoBindBuffer", if any
};

/// Everything a pass declares that a shader stage can see.
struct ShaderInterfaceExpectation {
    uint32_t setCount = 0;                          // entries in the pass's "descriptorSets"
    std::map<std::pair<uint32_t, uint32_t>, ExpectedDescriptor> descriptors;  // (set, binding)
    uint32_t pushConstantSize = 0;                  // largest offset + size of the pass's ranges
    const CompiledBufferLayout* pushConstantLayout = nullptr;  // perDraw layout, if any
};

/// Compares the interface `code` declares with `expected`. Returns one
/// message per mismatch, or an empty vector when they agree. A module that
/// SPIRV-Reflect cannot parse yields a single message saying so.
std::vector<std::string> validateShaderInterface(const void* code, size_t sizeBytes,
                                                 const ShaderInterfaceExpectation& expected);

} // namespace FrameGraph
} // namespace Shoonyakasha
