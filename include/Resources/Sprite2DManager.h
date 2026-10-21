//
// Sprite2DManager.h - Shared GPU resources for 2D sprites/UI
//
// Owns the single shared unit-quad mesh used by every sprite/UI entity,
// and a path-keyed texture cache so loading the same sprite sheet twice
// is free. Mirrors the caching pattern already used by GltfSceneLoader's
// texture loader (src/Resources/GltfSceneLoader.cpp).
//

#pragma once

#include "ECS/RenderComponents.h"
#include "GPU/GPUTypes.h"
#include <string>
#include <unordered_map>
#include <vector>

namespace Shoonyakasha {

class VulkanDevice;

class Sprite2DManager {
public:
    explicit Sprite2DManager(VulkanDevice& device);
    ~Sprite2DManager();

    Sprite2DManager(const Sprite2DManager&) = delete;
    Sprite2DManager& operator=(const Sprite2DManager&) = delete;

    // Shared unit quad: XY in [-0.5, 0.5], UV in [0, 1]. Every sprite/UI
    // entity's MeshComponent is a copy of this (buffer handles are not
    // owned per-entity - this manager owns the single GPU allocation).
    const MeshComponent& getQuadMesh();

    // Load (or fetch from cache) a texture for use as a sprite's
    // "spriteTexture" material slot.
    GPUTexture loadTexture(const std::string& path, bool srgb = true);

    // A texture from texels in memory (GPUResourceFactory::
    // createTextureFromTexels). The manager frees it with the rest unless
    // releaseTexture hands it on first. Returns an error message, empty when
    // `out` holds the texture.
    std::string createTexture(const void* data, size_t size, uint32_t width, uint32_t height, VkFormat format,
                              GPUTexture& out);

    // Free a texture createTexture made, once no frame in flight samples it.
    void releaseTexture(const GPUTexture& texture);

private:
    VulkanDevice& m_device;
    bool m_quadCreated = false;
    MeshComponent m_quadMesh;
    std::unordered_map<std::string, GPUTexture> m_textureCache;
    std::vector<GPUTexture> m_createdTextures;   // createTexture's, not yet released

    void createQuadMesh();
};

} // namespace Shoonyakasha
