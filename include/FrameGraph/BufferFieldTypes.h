//
// BufferFieldTypes.h — shader field types, packing rules, and the one
// implementation of the std140 / std430 / scalar layout arithmetic.
//
// These offsets are the contract between three parties that never see each
// other: the JSON that declares a buffer, the GLSL that reads it, and
// DotPathResolver, which writes into it. They were previously computed in two
// places that disagreed — FrameGraphCompiler.cpp handled std140 and dropped
// std430, Scalar and PushConstant into a branch that applied no alignment at
// all, while BufferLayoutCompiler.h used a third, size-bucket rule. This header
// is the single source.
//
// Deliberately dependency-free (no Vulkan, no glm, no entt) so both front-ends
// can include it cheaply.
//
// Reference: OpenGL 4.6 §7.6.2.2, adopted by Vulkan as the Standard Uniform /
// Storage Buffer Layouts.
//

#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <stdexcept>
#include <algorithm>

namespace Shoonyakasha {
namespace FrameGraph {

// ═══════════════════════════════════════════════════════════════
// Types
// ═══════════════════════════════════════════════════════════════

/// Shader-side field type. This is what determines alignment, so the packer
/// speaks this rather than the narrower MaterialParam::Type that bounds what
/// the CPU dot-path system can actually produce.
enum class BufferFieldType {
    Float, Double, Int, UInt, Bool,
    Vec2, Vec3, Vec4,
    IVec2, IVec3, IVec4,
    UVec2, UVec3, UVec4,
    Mat2, Mat3, Mat4
};

/// JSON: "std140", "std430", "scalar", "push_constant"
enum class BufferPackingRule {
    Std140,         // UBO standard layout
    Std430,         // SSBO standard layout
    Scalar,         // VK_EXT_scalar_block_layout
    PushConstant    // Push constants: std430 (plain `layout(push_constant)` in
                    // Vulkan GLSL is std430; every shipped shader uses that form)
};

// ═══════════════════════════════════════════════════════════════
// Parsing — throws rather than guessing
// ═══════════════════════════════════════════════════════════════

inline BufferFieldType parseFieldType(std::string_view s) {
    if (s == "float")  return BufferFieldType::Float;
    if (s == "double") return BufferFieldType::Double;
    if (s == "int")    return BufferFieldType::Int;
    if (s == "uint")   return BufferFieldType::UInt;
    if (s == "bool")   return BufferFieldType::Bool;
    if (s == "vec2")   return BufferFieldType::Vec2;
    if (s == "vec3")   return BufferFieldType::Vec3;
    if (s == "vec4")   return BufferFieldType::Vec4;
    if (s == "ivec2")  return BufferFieldType::IVec2;
    if (s == "ivec3")  return BufferFieldType::IVec3;
    if (s == "ivec4")  return BufferFieldType::IVec4;
    if (s == "uvec2")  return BufferFieldType::UVec2;
    if (s == "uvec3")  return BufferFieldType::UVec3;
    if (s == "uvec4")  return BufferFieldType::UVec4;
    if (s == "mat2")   return BufferFieldType::Mat2;
    if (s == "mat3")   return BufferFieldType::Mat3;
    if (s == "mat4")   return BufferFieldType::Mat4;

    // Silently defaulting an unrecognised type to float produced a buffer whose
    // offsets disagreed with the shader from that field onward.
    throw std::runtime_error("Unknown buffer field type: '" + std::string(s) + "'");
}

inline BufferPackingRule parsePackingRule(std::string_view s) {
    if (s == "std140")        return BufferPackingRule::Std140;
    if (s == "std430")        return BufferPackingRule::Std430;
    if (s == "scalar")        return BufferPackingRule::Scalar;
    if (s == "push_constant") return BufferPackingRule::PushConstant;

    throw std::runtime_error("Unknown buffer packing rule: '" + std::string(s) + "'");
}

inline const char* toString(BufferPackingRule rule) {
    switch (rule) {
        case BufferPackingRule::Std140:       return "std140";
        case BufferPackingRule::Std430:       return "std430";
        case BufferPackingRule::Scalar:       return "scalar";
        case BufferPackingRule::PushConstant: return "push_constant";
    }
    return "unknown";
}

// ═══════════════════════════════════════════════════════════════
// Packing primitives
// ═══════════════════════════════════════════════════════════════

namespace detail {

/// Push constants follow std430.
constexpr BufferPackingRule effective(BufferPackingRule rule) {
    return rule == BufferPackingRule::PushConstant ? BufferPackingRule::Std430 : rule;
}

constexpr uint32_t roundUp(uint32_t value, uint32_t multiple) {
    return multiple <= 1 ? value : ((value + multiple - 1) / multiple) * multiple;
}

/// Columns for a matrix type, 0 for everything else.
constexpr uint32_t columnCount(BufferFieldType t) {
    switch (t) {
        case BufferFieldType::Mat2: return 2;
        case BufferFieldType::Mat3: return 3;
        case BufferFieldType::Mat4: return 4;
        default:                    return 0;
    }
}

/// The vector type one column of a matrix has.
constexpr BufferFieldType columnType(BufferFieldType t) {
    switch (t) {
        case BufferFieldType::Mat2: return BufferFieldType::Vec2;
        case BufferFieldType::Mat3: return BufferFieldType::Vec3;
        case BufferFieldType::Mat4: return BufferFieldType::Vec4;
        default:                    return t;
    }
}

} // namespace detail

/// Size with no padding at all — what the type occupies in a C struct.
/// mat3 is 36 here and only here; under std140 and std430 it occupies 48.
constexpr uint32_t nativeSize(BufferFieldType t) {
    switch (t) {
        case BufferFieldType::Float:
        case BufferFieldType::Int:
        case BufferFieldType::UInt:
        case BufferFieldType::Bool:   return 4;
        case BufferFieldType::Double: return 8;
        case BufferFieldType::Vec2:
        case BufferFieldType::IVec2:
        case BufferFieldType::UVec2:  return 8;
        case BufferFieldType::Vec3:
        case BufferFieldType::IVec3:
        case BufferFieldType::UVec3:  return 12;
        case BufferFieldType::Vec4:
        case BufferFieldType::IVec4:
        case BufferFieldType::UVec4:  return 16;
        case BufferFieldType::Mat2:   return 16;   // 2 x vec2
        case BufferFieldType::Mat3:   return 36;   // 3 x vec3
        case BufferFieldType::Mat4:   return 64;   // 4 x vec4
    }
    return 4;
}

/// Base alignment.
///
/// The two rules everyone gets wrong: vec3 aligns to 16 in std430 exactly as in
/// std140 — std430 does not make vec3 tightly packable — and mat3 is three vec3
/// columns at 16-byte stride in both. std430's relaxations are about array and
/// struct stride, and about mat2.
constexpr uint32_t baseAlignment(BufferFieldType t, BufferPackingRule rule) {
    const BufferPackingRule r = detail::effective(rule);

    if (r == BufferPackingRule::Scalar) {
        // Scalar layout aligns to the component type, never to the vector.
        return t == BufferFieldType::Double ? 8u : 4u;
    }

    if (detail::columnCount(t) > 0) {
        const uint32_t colAlign = baseAlignment(detail::columnType(t), rule);
        // std140 rounds a matrix's (and array's) alignment up to vec4.
        return r == BufferPackingRule::Std140 ? std::max(colAlign, 16u) : colAlign;
    }

    switch (t) {
        case BufferFieldType::Float:
        case BufferFieldType::Int:
        case BufferFieldType::UInt:
        case BufferFieldType::Bool:   return 4;
        case BufferFieldType::Double: return 8;
        case BufferFieldType::Vec2:
        case BufferFieldType::IVec2:
        case BufferFieldType::UVec2:  return 8;
        case BufferFieldType::Vec3:
        case BufferFieldType::IVec3:
        case BufferFieldType::UVec3:
        case BufferFieldType::Vec4:
        case BufferFieldType::IVec4:
        case BufferFieldType::UVec4:  return 16;
        default:                      return 4;
    }
}

/// Byte distance between consecutive matrix columns; 0 for non-matrices.
/// This is what the resolver needs in order to write a glm matrix correctly:
/// glm stores columns contiguously, the GPU may not.
constexpr uint32_t columnStride(BufferFieldType t, BufferPackingRule rule) {
    const uint32_t cols = detail::columnCount(t);
    if (cols == 0) return 0;

    const BufferPackingRule r = detail::effective(rule);
    const BufferFieldType col = detail::columnType(t);

    if (r == BufferPackingRule::Scalar) {
        return nativeSize(col);                       // contiguous
    }

    const uint32_t stride = std::max(baseAlignment(col, rule), nativeSize(col));

    // std140 treats a matrix as an array of column vectors, and rounds array
    // element stride up to vec4. This only bites mat2, whose vec2 columns would
    // otherwise sit 8 bytes apart: mat2 is 2x16 = 32 bytes in std140 and
    // 2x8 = 16 in std430. mat3 and mat4 already have 16-byte columns.
    return r == BufferPackingRule::Std140 ? detail::roundUp(stride, 16u) : stride;
}

/// Bytes one element owns from its own offset, including interior padding but
/// excluding trailing padding to the next field. vec3 is 12 (its tail padding is
/// created by the *next* field's alignment); mat3 under std140/std430 is 48
/// (that padding is interior and genuinely occupied).
constexpr uint32_t occupiedSize(BufferFieldType t, BufferPackingRule rule) {
    const uint32_t cols = detail::columnCount(t);
    return cols > 0 ? cols * columnStride(t, rule) : nativeSize(t);
}

/// Distance between consecutive array elements.
/// This is the single largest practical difference between std140 and std430:
/// float x[4] is stride 16 / total 64 under std140, stride 4 / total 16 under
/// std430.
constexpr uint32_t arrayStride(BufferFieldType t, BufferPackingRule rule) {
    const BufferPackingRule r = detail::effective(rule);
    const uint32_t occupied = occupiedSize(t, rule);

    switch (r) {
        case BufferPackingRule::Std140:
            return detail::roundUp(std::max(occupied, baseAlignment(t, rule)), 16u);
        case BufferPackingRule::Std430:
            return detail::roundUp(occupied, baseAlignment(t, rule));
        default: // Scalar
            return occupied;
    }
}

// ═══════════════════════════════════════════════════════════════
// Field placement
// ═══════════════════════════════════════════════════════════════

/// Everything the packer decides about one field.
struct PackedField {
    uint32_t offset       = 0;
    uint32_t size         = 0;  // occupied size of one element
    uint32_t columnStride = 0;  // 0 for non-matrix types
    uint32_t arrayStride  = 0;  // 0 when arrayCount <= 1
};

/// Walks the cursor over one field. `cursor` is updated in place.
///
/// `explicitOffset` is honoured when present. It must be a real optional rather
/// than a sentinel: an explicitly declared offset of 0 used to be
/// indistinguishable from "not set".
inline PackedField packField(BufferFieldType type,
                             BufferPackingRule rule,
                             uint32_t arrayCount,
                             const uint32_t* explicitOffset,
                             uint32_t& cursor)
{
    const uint32_t count = arrayCount > 0 ? arrayCount : 1;
    const BufferPackingRule r = detail::effective(rule);

    uint32_t align = baseAlignment(type, rule);
    if (count > 1 && r == BufferPackingRule::Std140) {
        align = std::max(align, 16u);   // std140 arrays align to vec4
    }

    PackedField out;
    out.offset       = explicitOffset ? *explicitOffset : detail::roundUp(cursor, align);
    out.size         = occupiedSize(type, rule);
    out.columnStride = columnStride(type, rule);
    out.arrayStride  = count > 1 ? arrayStride(type, rule) : 0;

    // The last element of a std140 array is still padded out to the full
    // stride, so the cursor advances by stride * count, not
    // stride * (count - 1) + size.
    cursor = out.offset + (count > 1 ? out.arrayStride * count : out.size);
    return out;
}

/// Size of the whole block.
/// std140 rounds to 16; std430 and scalar round to the largest member alignment,
/// which is also the array-of-structs stride for an SSBO element.
inline uint32_t blockSize(uint32_t cursor, BufferPackingRule rule, uint32_t maxMemberAlignment) {
    if (detail::effective(rule) == BufferPackingRule::Std140) {
        return detail::roundUp(cursor, 16u);
    }
    return detail::roundUp(cursor, std::max(maxMemberAlignment, 1u));
}

} // namespace FrameGraph
} // namespace Shoonyakasha
