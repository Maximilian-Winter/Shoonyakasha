//
// RenderComponents.h - ECS Components for Rendering
//
// Design:
//   - MeshComponent: GPU buffers for geometry (vertex + index)
//   - MaterialComponentV5: Generic params + textures maps
//   - SceneEnvironment: Global environment data (IBL maps)
//
// These components are data-only. The frame graph reads them via dot-paths
// at render time.
//

#pragma once

#include "GPU/GPUTypes.h"
#include <unordered_map>
#include <string>
#include <optional>

namespace Shoonyakasha {

// ============================================================================
// MeshComponent - GPU geometry data
// ============================================================================
//
// Holds the vertex and index buffers for a renderable mesh.
// At render time, the frame graph binds these buffers automatically
// when processing "draw_entities" passes.
//

struct MeshComponent {
    GPUBuffer vertexBuffer;
    GPUBuffer indexBuffer;      // Optional - if not present, use non-indexed drawing

    IndexType indexType   = IndexType::UInt32;
    uint32_t  vertexCount = 0;
    uint32_t  indexCount  = 0;

    // Vertex stride in bytes (for binding)
    uint32_t  vertexStride = 0;

    // ─── Helpers ────────────────────────────────────────────────

    bool hasIndices() const { return indexBuffer.isValid() && indexCount > 0; }
    bool isValid() const { return vertexBuffer.isValid() && vertexCount > 0; }
};

// ============================================================================
// MaterialComponentV5 - Generic material parameters and textures
// ============================================================================
//
// Uses generic maps for material properties. The dot-path resolver reads
// values by name at render time.
//
// Named with V5 suffix for historical reasons. This is the canonical
// material component — uses generic maps for maximum flexibility.
//
// Example usage:
//   auto& mat = entity.get<MaterialComponentV5>();
//   mat.params["baseColorFactor"] = MaterialParam::from(glm::vec4(1, 0, 0, 1));
//   mat.params["metallicFactor"] = MaterialParam::from(0.5f);
//   mat.textures["albedoMap"] = loadedAlbedoTexture;
//
// At render time, the JSON declares:
//   "source": "entity.material.params.baseColorFactor"
// And the resolver fetches it from this map.
//

struct MaterialComponentV5 {
    // Scalar/vector/matrix parameters (baseColorFactor, metallicFactor, etc.)
    std::unordered_map<std::string, MaterialParam> params;

    // Texture slots (albedoMap, normalMap, metallicRoughnessMap, etc.)
    std::unordered_map<std::string, GPUTexture> textures;

    // Transparency handling
    AlphaMode alphaMode   = AlphaMode::Opaque;
    float     alphaCutoff = 0.5f;

    // Double-sided rendering
    bool doubleSided = false;

    // ─── Parameter Access ───────────────────────────────────────

    // Get a parameter by name, with type-safe extraction
    template<typename T>
    T getParam(const std::string& name, const T& defaultValue = T{}) const {
        auto it = params.find(name);
        if (it != params.end()) {
            return it->second.as<T>();
        }
        return defaultValue;
    }

    // Set a parameter by name
    template<typename T>
    void setParam(const std::string& name, const T& value) {
        params[name] = MaterialParam::from(value);
    }

    // Check if a parameter exists
    bool hasParam(const std::string& name) const {
        return params.find(name) != params.end();
    }

    // ─── Texture Access ─────────────────────────────────────────

    // Get a texture by name (returns nullptr if not found)
    const GPUTexture* getTexture(const std::string& name) const {
        auto it = textures.find(name);
        return it != textures.end() ? &it->second : nullptr;
    }

    GPUTexture* getTexture(const std::string& name) {
        auto it = textures.find(name);
        return it != textures.end() ? &it->second : nullptr;
    }

    // Check if a texture exists AND is a real texture (not a fallback)
    bool hasTexture(const std::string& name) const {
        auto it = textures.find(name);
        return it != textures.end() && it->second.exists;
    }

    // ─── Sorting Helpers ────────────────────────────────────────

    bool isOpaque() const { return alphaMode == AlphaMode::Opaque; }
    bool isMasked() const { return alphaMode == AlphaMode::Mask; }
    bool isTransparent() const { return alphaMode == AlphaMode::Blend; }

    // Returns true for entities that should be rendered in the opaque pass
    // (both fully opaque and alpha-masked materials)
    bool isOpaqueOrMasked() const { return alphaMode != AlphaMode::Blend; }
};

// ============================================================================
// SceneEnvironment - Global environment data (not per-entity)
// ============================================================================
//
// Holds IBL (Image-Based Lighting) textures and other scene-wide data.
// This is NOT an ECS component - it's a singleton or scene-level object.
//
// The dot-path resolver accesses it via "scene.environment.*" paths.
//

struct SceneEnvironment {
    // IBL textures for PBR rendering
    GPUTexture irradianceMap;   // Diffuse IBL (low-res convolved cubemap)
    GPUTexture prefilterMap;    // Specular IBL (mip-mapped roughness cubemap)
    GPUTexture brdfLUT;         // BRDF integration lookup texture

    // Optional: additional environment data
    GPUTexture environmentMap;  // Original HDR environment map (for skybox)

    // Shadow maps (if using shadow mapping)
    // GPUTexture shadowMap;
    // GPUBuffer lightBuffer;

    // ─── Helpers ────────────────────────────────────────────────

    bool hasIBL() const {
        return irradianceMap.isValid() &&
               prefilterMap.isValid() &&
               brdfLUT.isValid();
    }
};

// ============================================================================
// RenderableTagComponent - Marker for renderable entities
// ============================================================================
//
// A simple tag component to mark entities that should be rendered.
//
// An entity is renderable if it has:
//   - MeshComponent (geometry)
//   - MaterialComponentV5 (appearance)
//   - TransformComponent (position) - from Core.h
//   - RenderableTagComponent (marker)
//
// The frame graph queries for entities with these components when
// executing "draw_entities" passes.
//

struct RenderableTagComponent {
    bool visible       = true;
    bool castShadows   = true;
    bool receiveShadows = true;

    // Layer mask for filtering (bitfield - supports 8 layers)
    uint8_t renderLayerMask = 0xFF;

    // Sort key hint (lower = rendered first within same category)
    // Can be set to material/shader hash for draw call batching
    uint32_t sortKey = 0;

    bool shouldRender() const { return visible; }
};

} // namespace Shoonyakasha
