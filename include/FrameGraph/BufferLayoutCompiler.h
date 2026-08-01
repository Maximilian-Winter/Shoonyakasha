//
// BufferLayoutCompiler.h — JSON front-end for buffer layouts.
//
// Parses a JSON buffer layout definition into a CompiledBufferLayout that
// BufferLayoutResolver can fill.
//
// This class owns no layout arithmetic. The std140 / std430 / scalar rules live
// in FrameGraph/BufferFieldTypes.h and are shared with
// FrameGraphCompiler::compileBufferLayouts. It previously carried its own
// third implementation, which used a size-bucket alignment rule that matched
// neither std140 nor scalar, applied mat3's 48-byte occupancy only under
// std140, and gave std430 no distinct behaviour at all.
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
#include "FrameGraph/BufferFieldTypes.h"
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <unordered_map>
#include <stdexcept>

namespace Shoonyakasha {

// ============================================================================
// BufferLayoutCompiler
// ============================================================================

class BufferLayoutCompiler {
public:
    using FieldType   = FrameGraph::BufferFieldType;
    using PackingRule = FrameGraph::BufferPackingRule;

    // ─── Compile a single layout ────────────────────────────────

    CompiledBufferLayout compile(const std::string& name, const nlohmann::json& layoutJson) const;

    // ─── Compile all layouts from JSON ──────────────────────────

    std::unordered_map<std::string, CompiledBufferLayout> compileAll(const nlohmann::json& bufferLayoutsJson) const;

    // ─── Type Utilities (thin forwards to the shared packer) ────

    /// Throws on an unknown type string rather than defaulting to float.
    static FieldType parseType(const std::string& s) { return FrameGraph::parseFieldType(s); }

    /// Unpadded size. mat3 is 36 here and only here — under std140 and std430 it
    /// occupies 48. Use occupiedSize() for what a field actually owns.
    static uint32_t nativeSize(FieldType t) { return FrameGraph::nativeSize(t); }

    static uint32_t baseAlignment(FieldType t, PackingRule p) { return FrameGraph::baseAlignment(t, p); }
    static uint32_t occupiedSize(FieldType t, PackingRule p)  { return FrameGraph::occupiedSize(t, p); }
    static uint32_t columnStride(FieldType t, PackingRule p)  { return FrameGraph::columnStride(t, p); }
    static uint32_t arrayStride(FieldType t, PackingRule p)   { return FrameGraph::arrayStride(t, p); }

    /// Throws on an unknown packing string. It used to silently return Scalar,
    /// which disagreed with the JSON front-end in FrameGraphJson.cpp defaulting
    /// the same missing key to std140.
    static PackingRule parsePackingRule(const std::string& s) { return FrameGraph::parsePackingRule(s); }

    /// Map a shader type onto what ResolvedValue can produce. Returns false for
    /// the types with no CPU-side representation (double, bool, ivecN, uvecN,
    /// mat2); those fields are packed correctly but left zeroed.
    static bool toResolverType(FieldType t, MaterialParam::Type& out);
};

// ============================================================================
// Implementation (inline for header-only convenience)
// ============================================================================

inline bool BufferLayoutCompiler::toResolverType(FieldType t, MaterialParam::Type& out) {
    switch (t) {
        case FieldType::Float: out = MaterialParam::Type::Float; return true;
        case FieldType::Int:   out = MaterialParam::Type::Int;   return true;
        case FieldType::UInt:  out = MaterialParam::Type::UInt;  return true;
        case FieldType::Vec2:  out = MaterialParam::Type::Vec2;  return true;
        case FieldType::Vec3:  out = MaterialParam::Type::Vec3;  return true;
        case FieldType::Vec4:  out = MaterialParam::Type::Vec4;  return true;
        case FieldType::Mat3:  out = MaterialParam::Type::Mat3;  return true;
        case FieldType::Mat4:  out = MaterialParam::Type::Mat4;  return true;
        default:               out = MaterialParam::Type::Float; return false;
    }
}

inline CompiledBufferLayout BufferLayoutCompiler::compile(const std::string& name,
                                                          const nlohmann::json& layoutJson) const {
    CompiledBufferLayout layout;
    layout.name = name;

    // Missing "packing" defaults to std140 — the over-aligned safe choice, and
    // the same default FrameGraphJson.cpp uses. This class used to default to
    // scalar, so the two front-ends disagreed on identical JSON.
    const PackingRule packing = parsePackingRule(layoutJson.value("packing", std::string{"std140"}));

    if (!layoutJson.contains("fields") || !layoutJson["fields"].is_array()) {
        throw std::runtime_error("Buffer layout '" + name + "' must have a 'fields' array");
    }

    uint32_t cursor = 0;
    uint32_t maxMemberAlignment = 1;

    for (const auto& fieldJson : layoutJson["fields"]) {
        BufferField field;
        field.name   = fieldJson.value("name", "");
        field.source = fieldJson.value("source", "");

        const FieldType type = parseType(fieldJson.value("type", std::string{"float"}));
        field.resolvable = toResolverType(type, field.type);
        field.arrayCount = fieldJson.value("arrayCount", 1u);

        const uint32_t explicitOffset = fieldJson.value("offset", 0u);
        const bool hasExplicitOffset  = fieldJson.contains("offset");

        const auto packed = FrameGraph::packField(
            type, packing, field.arrayCount,
            hasExplicitOffset ? &explicitOffset : nullptr,
            cursor);

        field.offset       = packed.offset;
        field.size         = packed.size;
        field.columnStride = packed.columnStride;
        field.arrayStride  = packed.arrayStride;

        maxMemberAlignment = std::max(maxMemberAlignment, baseAlignment(type, packing));

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

    layout.totalSize = FrameGraph::blockSize(cursor, packing, maxMemberAlignment);
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
