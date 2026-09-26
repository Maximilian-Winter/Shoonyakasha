//
// ShaderInterfaceValidator.cpp - SPIR-V reflection against JSON layouts
//

#include "FrameGraph/ShaderInterfaceValidator.h"
#include "Vulkan/FrameGraph/FrameGraphJson.h"

#include <spirv-reflect/spirv_reflect.h>

#include <sstream>

namespace Shoonyakasha {
namespace FrameGraph {

namespace {

/// A block member reduced to what its bytes mean: one scalar, vector or
/// matrix, possibly arrayed.
struct Leaf {
    std::string name;
    uint32_t offset = 0;
    char base = '?';            // 'f' float, 'd' double, 'i' int, 'u' uint, 'b' bool
    uint32_t components = 1;    // vector size; rows for a matrix
    uint32_t columns = 1;       // 1 unless a matrix
    uint32_t matrixStride = 0;
    uint32_t arrayCount = 1;    // 1 = not an array, 0 = runtime array
    uint32_t arrayStride = 0;
};

/// Type of a JSON field in the same terms as a Leaf.
struct FieldShape {
    char base;
    uint32_t components;
    uint32_t columns;
};

FieldShape shapeOf(BufferFieldType type) {
    switch (type) {
        case BufferFieldType::Float:  return {'f', 1, 1};
        case BufferFieldType::Double: return {'d', 1, 1};
        case BufferFieldType::Int:    return {'i', 1, 1};
        case BufferFieldType::UInt:   return {'u', 1, 1};
        case BufferFieldType::Bool:   return {'b', 1, 1};
        case BufferFieldType::Vec2:   return {'f', 2, 1};
        case BufferFieldType::Vec3:   return {'f', 3, 1};
        case BufferFieldType::Vec4:   return {'f', 4, 1};
        case BufferFieldType::IVec2:  return {'i', 2, 1};
        case BufferFieldType::IVec3:  return {'i', 3, 1};
        case BufferFieldType::IVec4:  return {'i', 4, 1};
        case BufferFieldType::UVec2:  return {'u', 2, 1};
        case BufferFieldType::UVec3:  return {'u', 3, 1};
        case BufferFieldType::UVec4:  return {'u', 4, 1};
        case BufferFieldType::Mat2:   return {'f', 2, 2};
        case BufferFieldType::Mat3:   return {'f', 3, 3};
        case BufferFieldType::Mat4:   return {'f', 4, 4};
    }
    return {'?', 0, 0};
}

/// GLSL spelling, for messages.
std::string glslName(char base, uint32_t components, uint32_t columns) {
    if (columns > 1) {
        return components == columns ? "mat" + std::to_string(columns)
                                     : "mat" + std::to_string(columns) + "x" + std::to_string(components);
    }
    const char* scalar = base == 'f' ? "float" : base == 'd' ? "double" : base == 'i' ? "int"
                       : base == 'u' ? "uint" : base == 'b' ? "bool" : "?";
    if (components == 1) return scalar;
    const char* prefix = base == 'f' ? "vec" : base == 'd' ? "dvec" : base == 'i' ? "ivec"
                       : base == 'u' ? "uvec" : base == 'b' ? "bvec" : "?vec";
    return prefix + std::to_string(components);
}

/// A bool in a Vulkan block is stored as a 32-bit uint, so JSON "bool" also
/// accepts a shader-side uint.
bool sameBase(char json, char shader) {
    return json == shader || (json == 'b' && shader == 'u');
}

/// Appends the leaves of `v` to `out`. Returns false when `v` contains
/// something a JSON layout cannot describe (an array of structs or a runtime
/// array), in which case the caller skips the whole block.
bool flatten(const SpvReflectBlockVariable& v, const std::string& prefix, std::vector<Leaf>& out) {
    const std::string name = prefix.empty() ? std::string(v.name ? v.name : "?")
                                            : prefix + "." + (v.name ? v.name : "?");
    const uint32_t flags = v.type_description ? v.type_description->type_flags : 0;

    uint32_t arrayCount = 1;
    for (uint32_t d = 0; d < v.array.dims_count; ++d) {
        if (v.array.dims[d] == SPV_REFLECT_ARRAY_DIM_RUNTIME) return false;
        arrayCount *= v.array.dims[d];
    }

    if (flags & SPV_REFLECT_TYPE_FLAG_STRUCT) {
        if (v.array.dims_count > 0) return false;
        for (uint32_t m = 0; m < v.member_count; ++m) {
            if (!flatten(v.members[m], name, out)) return false;
        }
        return true;
    }

    Leaf leaf;
    leaf.name = name;
    leaf.offset = v.absolute_offset;
    if (flags & SPV_REFLECT_TYPE_FLAG_FLOAT) {
        leaf.base = v.numeric.scalar.width == 64 ? 'd' : 'f';
    } else if (flags & SPV_REFLECT_TYPE_FLAG_INT) {
        leaf.base = v.numeric.scalar.signedness ? 'i' : 'u';
    } else if (flags & SPV_REFLECT_TYPE_FLAG_BOOL) {
        leaf.base = 'b';
    }
    if (flags & SPV_REFLECT_TYPE_FLAG_MATRIX) {
        leaf.components = v.numeric.matrix.row_count;
        leaf.columns = v.numeric.matrix.column_count;
        leaf.matrixStride = v.numeric.matrix.stride;
        // SPIRV-Reflect reports no matrix stride for an array of matrices.
        // An element of such an array is exactly its columns, so the column
        // stride is the array stride divided by the column count.
        if (leaf.matrixStride == 0 && v.array.dims_count > 0 && leaf.columns > 0) {
            leaf.matrixStride = v.array.stride / leaf.columns;
        }
    } else if (flags & SPV_REFLECT_TYPE_FLAG_VECTOR) {
        leaf.components = v.numeric.vector.component_count;
    }
    leaf.arrayCount = arrayCount;
    leaf.arrayStride = v.array.dims_count > 0 ? v.array.stride : 0;
    out.push_back(leaf);
    return true;
}

/// Compares every member of `block` with `layout`, appending to `errors`.
void compareBlock(const SpvReflectBlockVariable& block, const CompiledBufferLayout& layout,
                  const std::string& where, std::vector<std::string>& errors) {
    std::vector<Leaf> leaves;
    for (uint32_t m = 0; m < block.member_count; ++m) {
        if (!flatten(block.members[m], block.name ? block.name : "", leaves)) return;
    }

    std::map<uint32_t, const BufferFieldDesc*> fieldsByOffset;
    for (const auto& field : layout.fields) fieldsByOffset[field.offset] = &field;

    for (const auto& leaf : leaves) {
        const std::string member = where + ": member '" + leaf.name + "' (" +
                                   glslName(leaf.base, leaf.components, leaf.columns) +
                                   (leaf.arrayCount > 1 ? "[" + std::to_string(leaf.arrayCount) + "]" : "") +
                                   " at offset " + std::to_string(leaf.offset) + ")";

        const auto it = fieldsByOffset.find(leaf.offset);
        if (it == fieldsByOffset.end()) {
            errors.push_back(member + " has no field of layout '" + layout.name +
                             "' starting at that offset");
            continue;
        }
        const BufferFieldDesc& field = *it->second;
        const FieldShape shape = shapeOf(field.type);
        const std::string jsonField = "JSON field '" + field.name + "'";

        if (!sameBase(shape.base, leaf.base) || shape.components != leaf.components ||
            shape.columns != leaf.columns) {
            errors.push_back(member + " does not match " + jsonField + ", which is " +
                             glslName(shape.base, shape.components, shape.columns));
            continue;
        }
        if (leaf.columns > 1 && field.columnStride != 0 && field.columnStride != leaf.matrixStride) {
            errors.push_back(member + " has matrix stride " + std::to_string(leaf.matrixStride) +
                             ", " + jsonField + " has " + std::to_string(field.columnStride));
        }
        if (leaf.arrayCount != field.arrayCount) {
            errors.push_back(member + " has " + std::to_string(leaf.arrayCount) + " element(s), " +
                             jsonField + " has " + std::to_string(field.arrayCount));
        } else if (leaf.arrayCount > 1 && leaf.arrayStride != field.arrayStride) {
            errors.push_back(member + " has array stride " + std::to_string(leaf.arrayStride) +
                             ", " + jsonField + " has " + std::to_string(field.arrayStride));
        }
    }
}

std::string describeBinding(const SpvReflectDescriptorBinding& b) {
    std::ostringstream s;
    s << "set " << b.set << " binding " << b.binding;
    if (b.name && *b.name) s << " ('" << b.name << "')";
    return s.str();
}

} // namespace

std::vector<std::string> validateShaderInterface(const void* code, size_t sizeBytes,
                                                 const ShaderInterfaceExpectation& expected) {
    std::vector<std::string> errors;

    SpvReflectShaderModule module{};
    if (spvReflectCreateShaderModule(sizeBytes, code, &module) != SPV_REFLECT_RESULT_SUCCESS) {
        errors.push_back("SPIRV-Reflect could not parse the module");
        return errors;
    }

    uint32_t bindingCount = 0;
    spvReflectEnumerateDescriptorBindings(&module, &bindingCount, nullptr);
    std::vector<SpvReflectDescriptorBinding*> bindings(bindingCount);
    spvReflectEnumerateDescriptorBindings(&module, &bindingCount, bindings.data());

    for (const auto* b : bindings) {
        const std::string where = describeBinding(*b);
        if (b->set >= expected.setCount) {
            errors.push_back(where + ": the shader uses set " + std::to_string(b->set) +
                             ", but the pass declares " + std::to_string(expected.setCount) +
                             " descriptor set(s)");
            continue;
        }
        const auto it = expected.descriptors.find({b->set, b->binding});
        if (it == expected.descriptors.end()) {
            errors.push_back(where + ": the pass declares no binding " + std::to_string(b->binding) +
                             " in set " + std::to_string(b->set));
            continue;
        }
        const ExpectedDescriptor& want = it->second;
        const auto shaderType = static_cast<VkDescriptorType>(b->descriptor_type);
        if (shaderType != want.type) {
            errors.push_back(where + ": the shader declares " + JsonUtils::descriptorTypeToString(shaderType) +
                             ", but binding '" + want.bindingName + "' of '" + want.setName + "' is " +
                             JsonUtils::descriptorTypeToString(want.type));
            continue;
        }
        const bool isBuffer = shaderType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER ||
                              shaderType == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        if (isBuffer && want.layout) {
            compareBlock(b->block, *want.layout, where, errors);
        }
    }

    uint32_t pushCount = 0;
    spvReflectEnumeratePushConstantBlocks(&module, &pushCount, nullptr);
    std::vector<SpvReflectBlockVariable*> pushBlocks(pushCount);
    spvReflectEnumeratePushConstantBlocks(&module, &pushCount, pushBlocks.data());

    for (const auto* block : pushBlocks) {
        const uint32_t end = block->offset + block->size;
        if (end > expected.pushConstantSize) {
            errors.push_back("push constants: the shader's block ends at byte " + std::to_string(end) +
                             ", but the pass declares " + std::to_string(expected.pushConstantSize) +
                             " byte(s) of push constants");
            continue;
        }
        if (expected.pushConstantLayout) {
            compareBlock(*block, *expected.pushConstantLayout, "push constants", errors);
        }
    }

    spvReflectDestroyShaderModule(&module);
    return errors;
}

} // namespace FrameGraph
} // namespace Shoonyakasha
