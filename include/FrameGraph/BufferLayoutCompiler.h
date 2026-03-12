//
// BufferLayoutCompiler.h - Compiles JSON buffer layouts to executable form
//
// Takes a JSON buffer layout definition and produces a CompiledBufferLayout
// that can be used with BufferLayoutResolver to fill GPU buffers.
//
// Example JSON input:
//   {
//     "CameraUBO": {
//       "usage": "uniform_buffer",
//       "packing": "std140",
//       "fields": [
//         { "name": "view", "type": "mat4", "source": "scene.camera.view" },
//         { "name": "projection", "type": "mat4", "source": "scene.camera.projection" }
//       ]
//     }
//   }
//

#pragma once

#include "FrameGraph/DotPathResolver.h"
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <unordered_map>
#include <stdexcept>

namespace Shoonyakasha {

// ============================================================================
// Packing Rules
// ============================================================================

enum class PackingRule {
    Std140,     // OpenGL std140 - UBOs
    Std430,     // OpenGL std430 - SSBOs
    Scalar      // VK_EXT_scalar_block_layout - tightly packed
};

// ============================================================================
// BufferLayoutCompiler
// ============================================================================

class BufferLayoutCompiler {
public:
    // ─── Compile a single layout ────────────────────────────────

    CompiledBufferLayout compile(const std::string& name, const nlohmann::json& layoutJson) const;

    // ─── Compile all layouts from JSON ──────────────────────────

    std::unordered_map<std::string, CompiledBufferLayout> compileAll(const nlohmann::json& bufferLayoutsJson) const;

    // ─── Type Utilities ─────────────────────────────────────────

    static MaterialParam::Type parseType(const std::string& typeStr);
    static uint32_t getTypeSize(MaterialParam::Type type);
    static uint32_t getTypeAlignment(MaterialParam::Type type, PackingRule packing);
    static PackingRule parsePackingRule(const std::string& packingStr);

private:
    uint32_t computeOffset(uint32_t currentOffset, MaterialParam::Type type, PackingRule packing) const;
};

// ============================================================================
// Implementation (inline for header-only convenience)
// ============================================================================

inline MaterialParam::Type BufferLayoutCompiler::parseType(const std::string& typeStr) {
    if (typeStr == "float") return MaterialParam::Type::Float;
    if (typeStr == "vec2") return MaterialParam::Type::Vec2;
    if (typeStr == "vec3") return MaterialParam::Type::Vec3;
    if (typeStr == "vec4") return MaterialParam::Type::Vec4;
    if (typeStr == "mat3") return MaterialParam::Type::Mat3;
    if (typeStr == "mat4") return MaterialParam::Type::Mat4;
    if (typeStr == "int") return MaterialParam::Type::Int;
    if (typeStr == "uint") return MaterialParam::Type::UInt;

    throw std::runtime_error("Unknown buffer layout type: " + typeStr);
}

inline uint32_t BufferLayoutCompiler::getTypeSize(MaterialParam::Type type) {
    switch (type) {
        case MaterialParam::Type::Float: return 4;
        case MaterialParam::Type::Vec2:  return 8;
        case MaterialParam::Type::Vec3:  return 12;
        case MaterialParam::Type::Vec4:  return 16;
        case MaterialParam::Type::Mat3:  return 36;  // 3x3 floats
        case MaterialParam::Type::Mat4:  return 64;  // 4x4 floats
        case MaterialParam::Type::Int:   return 4;
        case MaterialParam::Type::UInt:  return 4;
        default: return 4;
    }
}

inline uint32_t BufferLayoutCompiler::getTypeAlignment(MaterialParam::Type type, PackingRule packing) {
    if (packing == PackingRule::Scalar) {
        // Scalar packing: everything aligned to its natural size
        return getTypeSize(type) <= 4 ? 4 : (getTypeSize(type) <= 8 ? 8 : 16);
    }

    // std140/std430 alignment rules
    switch (type) {
        case MaterialParam::Type::Float:
        case MaterialParam::Type::Int:
        case MaterialParam::Type::UInt:
            return 4;

        case MaterialParam::Type::Vec2:
            return 8;

        case MaterialParam::Type::Vec3:
        case MaterialParam::Type::Vec4:
            return 16;  // vec3 is aligned to 16 bytes in std140!

        case MaterialParam::Type::Mat3:
            // mat3 is stored as 3 vec4s in std140 (each column padded to vec4)
            return 16;

        case MaterialParam::Type::Mat4:
            return 16;

        default:
            return 16;
    }
}

inline PackingRule BufferLayoutCompiler::parsePackingRule(const std::string& packingStr) {
    if (packingStr == "std140") return PackingRule::Std140;
    if (packingStr == "std430") return PackingRule::Std430;
    if (packingStr == "scalar") return PackingRule::Scalar;

    // Default to scalar for push constants (most common use case)
    return PackingRule::Scalar;
}

inline uint32_t BufferLayoutCompiler::computeOffset(uint32_t currentOffset, MaterialParam::Type type, PackingRule packing) const {
    uint32_t alignment = getTypeAlignment(type, packing);
    // Align up to the required alignment
    return (currentOffset + alignment - 1) & ~(alignment - 1);
}

inline CompiledBufferLayout BufferLayoutCompiler::compile(const std::string& name, const nlohmann::json& layoutJson) const {
    CompiledBufferLayout layout;
    layout.name = name;

    // Parse packing rule
    std::string packingStr = layoutJson.value("packing", "scalar");
    PackingRule packing = parsePackingRule(packingStr);

    // Parse fields
    uint32_t currentOffset = 0;

    if (!layoutJson.contains("fields") || !layoutJson["fields"].is_array()) {
        throw std::runtime_error("Buffer layout '" + name + "' must have a 'fields' array");
    }

    for (const auto& fieldJson : layoutJson["fields"]) {
        BufferField field;
        field.name = fieldJson.value("name", "");
        field.source = fieldJson.value("source", "");

        std::string typeStr = fieldJson.value("type", "float");
        field.type = parseType(typeStr);
        field.size = getTypeSize(field.type);

        // Compute aligned offset
        field.offset = computeOffset(currentOffset, field.type, packing);

        // Handle mat3 specially in std140 (stored as 3 vec4s)
        uint32_t effectiveSize = field.size;
        if (field.type == MaterialParam::Type::Mat3 && packing == PackingRule::Std140) {
            effectiveSize = 48;  // 3 vec4s = 3 * 16 bytes
        }

        currentOffset = field.offset + effectiveSize;

        // Classify sources
        if (DotPathResolver::isScenePath(field.source)) {
            layout.hasSceneSources = true;
        } else if (DotPathResolver::isEntityPath(field.source)) {
            layout.hasEntitySources = true;
        } else if (DotPathResolver::isConstPath(field.source)) {
            layout.hasConstSources = true;
        }

        layout.fields.push_back(std::move(field));
    }

    // Final size (aligned to largest alignment for UBO compatibility)
    uint32_t finalAlignment = (packing == PackingRule::Std140) ? 16 : 4;
    layout.totalSize = (currentOffset + finalAlignment - 1) & ~(finalAlignment - 1);

    return layout;
}

inline std::unordered_map<std::string, CompiledBufferLayout>
BufferLayoutCompiler::compileAll(const nlohmann::json& bufferLayoutsJson) const {
    std::unordered_map<std::string, CompiledBufferLayout> layouts;

    for (auto it = bufferLayoutsJson.begin(); it != bufferLayoutsJson.end(); ++it) {
        layouts[it.key()] = compile(it.key(), it.value());
    }

    return layouts;
}

} // namespace Shoonyakasha
