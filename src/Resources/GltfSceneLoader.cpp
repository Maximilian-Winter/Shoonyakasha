//
// GltfSceneLoader.cpp - Load glTF files as ECS components
//
// 黃帝司中  調和而統御
// The Yellow Emperor governs the center — unifying meshes, materials, and entities
//

#include "Resources/GltfSceneLoader.h"
#include "Vulkan/VulkanDevice.h"
#include "ECS/Core.h"

#include "ECS/SkeletonComponents.h"
#include "ECS/SkeletalAnimationSystem.h"

#include <cgltf.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/type_ptr.hpp>

// stb_image implementation in src/ThirdParty/stb_impl.cpp
#include <stb_image.h>

#include <iostream>
#include <fstream>
#include <algorithm>
#include <cmath>

namespace Shoonyakasha {

// ═══════════════════════════════════════════════════════════════
// Standard Vertex Structure
// ═══════════════════════════════════════════════════════════════

struct StandardVertex {
    glm::vec3 pos;       // location 0
    glm::vec3 color;     // location 1
    glm::vec2 texCoord;  // location 2
    glm::vec3 normal;    // location 3
};

// Skinned vertex — includes bone joint indices and weights
// Matches the "skinned" vertex format in JSON:
//   position(vec3), normal(vec3), texCoord(vec2), jointIndices(uvec4), jointWeights(vec4)
struct SkinnedVertex {
    glm::vec3 pos;           // location 0
    glm::vec3 normal;        // location 1
    glm::vec2 texCoord;      // location 2
    glm::uvec4 joints;       // location 3 (bone indices)
    glm::vec4 weights;       // location 4 (bone weights)
};
// Stride: 12 + 12 + 8 + 16 + 16 = 64 bytes

// ═══════════════════════════════════════════════════════════════
// Material Push Constants Layout
// ═══════════════════════════════════════════════════════════════

struct MaterialPushConstantsData {
    glm::mat4 model;              // 64 bytes
    glm::vec4 baseColorFactor;    // 16 bytes
    float metallicFactor;         // 4 bytes
    float roughnessFactor;        // 4 bytes
    float hasNormalMap;           // 4 bytes
    float hasMetalRoughMap;       // 4 bytes
    float alphaCutoff;            // 4 bytes
    float padding[3];             // 12 bytes padding to 112 total
};
// Total: 112 bytes (within 128 byte push constant limit)

// ═══════════════════════════════════════════════════════════════
// Construction / Destruction
// ═══════════════════════════════════════════════════════════════

GltfSceneLoader::GltfSceneLoader(VulkanDevice& device)
    : m_device(device)
{
}

GltfSceneLoader::~GltfSceneLoader() = default;

// ═══════════════════════════════════════════════════════════════
// Main Load Function
// ═══════════════════════════════════════════════════════════════

GltfLoadResult GltfSceneLoader::load(
    const std::filesystem::path& path,
    std::shared_ptr<ECS::Scene> scene,
    const GltfLoadOptions& options)
{
    GltfLoadResult result;
    m_textureCache.clear();
    m_basePath = path.parent_path();
    m_options = options;

    if (m_options.namePrefix.empty()) {
        m_options.namePrefix = path.stem().string();
    }

    // Parse the glTF file
    cgltf_options cgltfOptions = {};
    cgltf_data* data = nullptr;

    cgltf_result parseResult = cgltf_parse_file(&cgltfOptions, path.string().c_str(), &data);
    if (parseResult != cgltf_result_success) {
        result.error = "Failed to parse glTF file: " + path.string();
        return result;
    }

    // Load buffer data
    parseResult = cgltf_load_buffers(&cgltfOptions, data, path.string().c_str());
    if (parseResult != cgltf_result_success) {
        cgltf_free(data);
        result.error = "Failed to load glTF buffers: " + path.string();
        return result;
    }

    // Validate
    parseResult = cgltf_validate(data);
    if (parseResult != cgltf_result_success) {
        cgltf_free(data);
        result.error = "glTF validation failed: " + path.string();
        return result;
    }

    std::cout << "[GltfSceneLoader] Loading: " << path.string() << std::endl;
    std::cout << "  Meshes: " << data->meshes_count << std::endl;
    std::cout << "  Materials: " << data->materials_count << std::endl;
    std::cout << "  Textures: " << data->textures_count << std::endl;
    std::cout << "  Nodes: " << data->nodes_count << std::endl;

    // Load skins (skeletons) if requested
    if (m_options.loadSkins) {
        m_skinCache.clear();
        for (cgltf_size i = 0; i < data->skins_count; ++i) {
            auto skeleton = loadSkin(data, &data->skins[i]);
            if (skeleton) {
                m_skinCache[&data->skins[i]] = skeleton;
                result.skeletons.push_back(skeleton);
            }
        }
    }

    // Load animations if requested and we have skeletons
    if (m_options.loadAnimations && !result.skeletons.empty()) {
        for (const auto& skeleton : result.skeletons) {
            auto clips = loadAnimations(data, *skeleton);
            for (auto& clip : clips) {
                result.animationClips.push_back(std::move(clip));
            }
        }
    }

    // Process scene nodes
    if (data->scene) {
        for (cgltf_size i = 0; i < data->scene->nodes_count; ++i) {
            processNode(data, data->scene->nodes[i], glm::mat4(1.0f), result);
        }
    } else if (data->scenes_count > 0) {
        for (cgltf_size i = 0; i < data->scenes[0].nodes_count; ++i) {
            processNode(data, data->scenes[0].nodes[i], glm::mat4(1.0f), result);
        }
    } else {
        // No scenes, process root nodes
        for (cgltf_size i = 0; i < data->nodes_count; ++i) {
            if (data->nodes[i].parent == nullptr) {
                processNode(data, &data->nodes[i], glm::mat4(1.0f), result);
            }
        }
    }

    // Create ECS entities if requested
    if (options.createEntities && scene) {
        for (size_t i = 0; i < result.primitives.size(); ++i) {
            const auto& primitive = result.primitives[i];
            entt::entity entity;

            // Check if this primitive came from a skinned node
            // (indicated by vertexStride matching SkinnedVertex size)
            if (primitive.vertexStride == sizeof(SkinnedVertex) &&
                !result.skeletons.empty()) {
                // Use the first skeleton for now (TODO: per-primitive skin mapping)
                entity = createSkinnedEntity(scene, primitive,
                    result.skeletons[0], result.animationClips,
                    primitive.worldTransform);
            } else {
                entity = createEntity(scene, primitive);
            }

            result.entities.push_back(entity);
        }
    }

    cgltf_free(data);

    // Collect statistics
    for (const auto& prim : result.primitives) {
        result.totalVertices += prim.vertexCount;
        result.totalIndices += prim.indexCount;
    }
    result.totalMaterials = result.primitives.size();
    result.totalTextures = m_textureCache.size();

    result.success = true;

    size_t primCount = result.primitives.size();
    std::cout << "[GltfSceneLoader] Loaded: " << primCount << " primitives, "
              << result.totalVertices << " vertices, "
              << result.totalIndices << " indices, "
              << result.totalTextures << " textures" << std::endl;

    return result;
}

// ═══════════════════════════════════════════════════════════════
// Node Processing
// ═══════════════════════════════════════════════════════════════

void GltfSceneLoader::processNode(
    cgltf_data* data,
    const cgltf_node* node,
    const glm::mat4& parentTransform,
    GltfLoadResult& result)
{
    // Get this node's world transform
    cgltf_float worldMatrix[16];
    cgltf_node_transform_world(node, worldMatrix);
    glm::mat4 worldTransform = glm::make_mat4(worldMatrix);

    // Process mesh if present
    if (node->mesh) {
        const cgltf_mesh* mesh = node->mesh;
        std::string meshBaseName = node->name ? node->name :
                                   (mesh->name ? mesh->name : "mesh");

        // Check if this node has a skin (skinned mesh)
        bool isSkinned = (node->skin != nullptr && m_options.loadSkins);

        for (cgltf_size primIdx = 0; primIdx < mesh->primitives_count; ++primIdx) {
            const cgltf_primitive& primitive = mesh->primitives[primIdx];

            // Skip non-triangle primitives
            if (primitive.type != cgltf_primitive_type_triangles) {
                continue;
            }

            // Generate unique name for this primitive
            std::string primName = m_options.namePrefix + "_" + meshBaseName;
            if (mesh->primitives_count > 1) {
                primName += "_prim" + std::to_string(primIdx);
            }

            if (isSkinned) {
                // Build skinned primitive (with joints/weights, no transform baking)
                GltfPrimitive loadedPrimitive = processPrimitive(data, primitive, glm::mat4(1.0f), primName);

                // Replace vertex buffer with skinned version
                // (processPrimitive built a StandardVertex buffer; we need SkinnedVertex)
                uint32_t vertexCount = 0;
                uint32_t vertexStride = 0;
                GPUBuffer skinnedVB = buildSkinnedVertexBuffer(data, primitive, vertexCount, vertexStride);
                if (skinnedVB.isValid()) {
                    // Destroy the StandardVertex buffer that processPrimitive created
                    GPUResourceFactory::destroyBuffer(m_device.getAllocator().getHandle(), loadedPrimitive.vertexBuffer);
                    loadedPrimitive.vertexBuffer = skinnedVB;
                    loadedPrimitive.vertexCount = vertexCount;
                    loadedPrimitive.vertexStride = vertexStride;
                }
                loadedPrimitive.worldTransform = worldTransform;  // Store but don't bake

                result.primitives.push_back(std::move(loadedPrimitive));
            } else {
                GltfPrimitive loadedPrimitive = processPrimitive(data, primitive, worldTransform, primName);
                result.primitives.push_back(std::move(loadedPrimitive));
            }
        }
    }

    // Process children recursively
    for (cgltf_size i = 0; i < node->children_count; ++i) {
        processNode(data, node->children[i], worldTransform, result);
    }
}

AlphaMode GltfSceneLoader::extractAlphaMode(const cgltf_material* material) {
    if (!material) return AlphaMode::Opaque;

    switch (material->alpha_mode) {
        case cgltf_alpha_mode_mask:
            return AlphaMode::Mask;
        case cgltf_alpha_mode_blend:
            return AlphaMode::Blend;
        case cgltf_alpha_mode_opaque:
        default:
            return AlphaMode::Opaque;
    }
}

std::string GltfSceneLoader::resolveTexturePath(const std::string& uri) {
    std::filesystem::path texPath(uri);

    if (texPath.is_relative()) {
        texPath = m_basePath / texPath;
    }

    return texPath.string();
}

// ═══════════════════════════════════════════════════════════════
// Primitive Processing
// 虚空之道 — The Way of Emptiness
// ═══════════════════════════════════════════════════════════════

GltfPrimitive GltfSceneLoader::processPrimitive(
    cgltf_data* data,
    const cgltf_primitive& primitive,
    const glm::mat4& worldTransform,
    const std::string& primitiveName)
{
    GltfPrimitive result;
    result.name = primitiveName;
    result.worldTransform = worldTransform;

    // Build vertex buffer
    result.vertexBuffer = buildVertexBuffer(data, primitive, worldTransform,
                                            result.vertexCount, result.vertexStride);

    // Build index buffer
    result.indexBuffer = buildIndexBuffer(data, primitive,
                                          result.indexCount, result.indexType);

    // Process material
    if (primitive.material && m_options.loadMaterials) {
        const cgltf_material* material = primitive.material;

        // Extract material properties
        if (material->has_pbr_metallic_roughness) {
            const auto& pbr = material->pbr_metallic_roughness;
            result.baseColorFactor = glm::vec4(
                pbr.base_color_factor[0],
                pbr.base_color_factor[1],
                pbr.base_color_factor[2],
                pbr.base_color_factor[3]
            );
            result.metallicFactor = pbr.metallic_factor;
            result.roughnessFactor = pbr.roughness_factor;

            // Load textures
            if (m_options.loadTextures) {
                if (pbr.base_color_texture.texture) {
                    result.albedoMap = loadTexture(data, pbr.base_color_texture, true);
                }
                if (pbr.metallic_roughness_texture.texture) {
                    result.metallicRoughnessMap = loadTexture(data, pbr.metallic_roughness_texture, false);
                }
            }
        }

        if (material->normal_texture.texture && m_options.loadTextures) {
            result.normalMap = loadTexture(data, material->normal_texture, false);
        }

        if (material->occlusion_texture.texture && m_options.loadTextures) {
            result.aoMap = loadTexture(data, material->occlusion_texture, false);
        }

        if (material->emissive_texture.texture && m_options.loadTextures) {
            result.emissiveMap = loadTexture(data, material->emissive_texture, true);
        }

        result.emissiveFactor = glm::vec3(
            material->emissive_factor[0],
            material->emissive_factor[1],
            material->emissive_factor[2]
        );

        result.alphaMode = extractAlphaMode(material);
        result.alphaCutoff = material->alpha_cutoff;
        result.doubleSided = material->double_sided;
    }

    return result;
}

GPUBuffer GltfSceneLoader::buildVertexBuffer(
    cgltf_data* data,
    const cgltf_primitive& primitive,
    const glm::mat4& worldTransform,
    uint32_t& outVertexCount,
    uint32_t& outVertexStride)
{
    // Find attribute accessors
    const cgltf_accessor* posAccessor = nullptr;
    const cgltf_accessor* normalAccessor = nullptr;
    const cgltf_accessor* texCoordAccessor = nullptr;
    const cgltf_accessor* colorAccessor = nullptr;

    for (cgltf_size i = 0; i < primitive.attributes_count; ++i) {
        const cgltf_attribute& attr = primitive.attributes[i];
        switch (attr.type) {
            case cgltf_attribute_type_position:  posAccessor = attr.data; break;
            case cgltf_attribute_type_normal:    normalAccessor = attr.data; break;
            case cgltf_attribute_type_texcoord:  if (attr.index == 0) texCoordAccessor = attr.data; break;
            case cgltf_attribute_type_color:     if (attr.index == 0) colorAccessor = attr.data; break;
            default: break;
        }
    }

    if (!posAccessor) {
        outVertexCount = 0;
        outVertexStride = 0;
        return GPUBuffer{};
    }

    size_t vertexCount = posAccessor->count;
    glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(worldTransform)));

    // Build interleaved vertex data
    std::vector<StandardVertex> vertices(vertexCount);

    for (size_t i = 0; i < vertexCount; ++i) {
        StandardVertex& v = vertices[i];

        // Position
        cgltf_float pos[3] = {0, 0, 0};
        cgltf_accessor_read_float(posAccessor, i, pos, 3);
        glm::vec4 worldPos = worldTransform * glm::vec4(pos[0], pos[1], pos[2], 1.0f);
        v.pos = glm::vec3(worldPos);

        // Color
        if (colorAccessor) {
            cgltf_float col[4] = {1, 1, 1, 1};
            cgltf_accessor_read_float(colorAccessor, i, col, cgltf_num_components(colorAccessor->type));
            v.color = glm::vec3(col[0], col[1], col[2]);
        } else {
            v.color = glm::vec3(1.0f);
        }

        // TexCoord
        if (texCoordAccessor) {
            cgltf_float uv[2] = {0, 0};
            cgltf_accessor_read_float(texCoordAccessor, i, uv, 2);
            v.texCoord = glm::vec2(uv[0], uv[1]);
        } else {
            v.texCoord = glm::vec2(0.0f);
        }

        // Normal
        if (normalAccessor) {
            cgltf_float norm[3] = {0, 1, 0};
            cgltf_accessor_read_float(normalAccessor, i, norm, 3);
            v.normal = glm::normalize(normalMatrix * glm::vec3(norm[0], norm[1], norm[2]));
        } else {
            v.normal = glm::vec3(0.0f, 1.0f, 0.0f);
        }
    }

    outVertexCount = static_cast<uint32_t>(vertexCount);
    outVertexStride = sizeof(StandardVertex);

    // Create GPU buffer
    VkDeviceSize bufferSize = vertices.size() * sizeof(StandardVertex);
    GPUBuffer buffer = GPUResourceFactory::createBuffer(
        m_device.getAllocator().getHandle(),
        bufferSize,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY
    );

    // Upload data
    GPUResourceFactory::uploadBuffer(
        m_device.getAllocator().getHandle(),
        m_device.getLogicalDevice(),
        m_device.getGraphicsQueue(),
        m_device.getCommandPool(),
        buffer,
        vertices.data(),
        bufferSize
    );

    return buffer;
}

GPUBuffer GltfSceneLoader::buildIndexBuffer(
    cgltf_data* data,
    const cgltf_primitive& primitive,
    uint32_t& outIndexCount,
    Shoonyakasha::IndexType& outIndexType)
{
    if (!primitive.indices) {
        // Non-indexed geometry - return empty buffer
        outIndexCount = 0;
        outIndexType = Shoonyakasha::IndexType::UInt32;
        return GPUBuffer{};
    }

    const cgltf_accessor* indexAccessor = primitive.indices;
    size_t indexCount = indexAccessor->count;
    bool use16Bit = (indexCount <= 65535);

    outIndexCount = static_cast<uint32_t>(indexCount);
    outIndexType = use16Bit ? Shoonyakasha::IndexType::UInt16 : Shoonyakasha::IndexType::UInt32;

    VkDeviceSize bufferSize;
    GPUBuffer buffer;

    if (use16Bit) {
        std::vector<uint16_t> indices(indexCount);
        for (size_t i = 0; i < indexCount; ++i) {
            indices[i] = static_cast<uint16_t>(cgltf_accessor_read_index(indexAccessor, i));
        }

        bufferSize = indices.size() * sizeof(uint16_t);
        buffer = GPUResourceFactory::createBuffer(
            m_device.getAllocator().getHandle(),
            bufferSize,
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VMA_MEMORY_USAGE_GPU_ONLY
        );

        GPUResourceFactory::uploadBuffer(
            m_device.getAllocator().getHandle(),
            m_device.getLogicalDevice(),
            m_device.getGraphicsQueue(),
            m_device.getCommandPool(),
            buffer,
            indices.data(),
            bufferSize
        );
    } else {
        std::vector<uint32_t> indices(indexCount);
        for (size_t i = 0; i < indexCount; ++i) {
            indices[i] = static_cast<uint32_t>(cgltf_accessor_read_index(indexAccessor, i));
        }

        bufferSize = indices.size() * sizeof(uint32_t);
        buffer = GPUResourceFactory::createBuffer(
            m_device.getAllocator().getHandle(),
            bufferSize,
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VMA_MEMORY_USAGE_GPU_ONLY
        );

        GPUResourceFactory::uploadBuffer(
            m_device.getAllocator().getHandle(),
            m_device.getLogicalDevice(),
            m_device.getGraphicsQueue(),
            m_device.getCommandPool(),
            buffer,
            indices.data(),
            bufferSize
        );
    }

    return buffer;
}

GPUTexture GltfSceneLoader::loadTexture(
    cgltf_data* data,
    const cgltf_texture_view& textureView,
    bool srgb)
{
    if (!textureView.texture || !textureView.texture->image) {
        return GPUTexture{};
    }

    const cgltf_image* image = textureView.texture->image;
    std::string cacheKey;
    bool isEmbedded = false;

    if (image->uri) {
        cacheKey = resolveTexturePath(image->uri);
    } else if (image->buffer_view) {
        // Embedded texture — data lives in the glTF/glb buffer
        isEmbedded = true;
        // Use buffer_view offset as a stable cache key
        cacheKey = "embedded_bv_" + std::to_string(
            reinterpret_cast<uintptr_t>(image->buffer_view));
    }

    if (cacheKey.empty()) {
        return GPUTexture{};
    }

    // Check cache first to avoid loading same texture multiple times
    std::string fullCacheKey = cacheKey + (srgb ? "_srgb" : "_linear");
    auto cacheIt = m_textureCache.find(fullCacheKey);
    if (cacheIt != m_textureCache.end()) {
        return cacheIt->second;  // Return cached texture
    }

    // Load image with stb_image
    int width, height, channels;
    stbi_uc* pixels = nullptr;

    if (isEmbedded) {
        // 蘊中取像 — Extract the image from within the vessel
        const cgltf_buffer_view* bv = image->buffer_view;
        const uint8_t* bufData = static_cast<const uint8_t*>(bv->buffer->data) + bv->offset;
        pixels = stbi_load_from_memory(bufData, static_cast<int>(bv->size),
                                       &width, &height, &channels, STBI_rgb_alpha);
        if (!pixels) {
            std::cerr << "[GltfSceneLoader] Failed to decode embedded texture"
                      << (image->name ? std::string(" '") + image->name + "'" : "")
                      << " (mime: " << (image->mime_type ? image->mime_type : "unknown") << ")"
                      << std::endl;
            return GPUTexture{};
        }
        std::cout << "[GltfSceneLoader] Loaded embedded texture"
                  << (image->name ? std::string(" '") + image->name + "'" : "")
                  << " (" << width << "x" << height << ")" << std::endl;
    } else {
        pixels = stbi_load(cacheKey.c_str(), &width, &height, &channels, STBI_rgb_alpha);
        if (!pixels) {
            std::cerr << "[GltfSceneLoader] Failed to load texture: " << cacheKey << std::endl;
            return GPUTexture{};
        }
    }

    // Warn about large textures (resize not implemented yet)
    uint32_t maxSize = m_options.maxTextureSize;
    if (maxSize > 0 && (static_cast<uint32_t>(width) > maxSize || static_cast<uint32_t>(height) > maxSize)) {
        std::cout << "[GltfSceneLoader] Warning - texture " << cacheKey
                  << " (" << width << "x" << height << ") exceeds maxTextureSize " << maxSize
                  << " (resize not yet implemented)" << std::endl;
    }

    VkFormat format = srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
    VkDeviceSize imageSize = width * height * 4;

    // Create texture using GPUResourceFactory
    GPUTexture texture = GPUResourceFactory::createTexture2DWithData(
        m_device.getAllocator().getHandle(),
        m_device.getLogicalDevice(),
        m_device.getGraphicsQueue(),
        m_device.getCommandPool(),
        static_cast<uint32_t>(width),
        static_cast<uint32_t>(height),
        format,
        pixels,
        imageSize,
        m_options.generateMipmaps
    );

    stbi_image_free(pixels);

    // Create sampler for this texture
    texture.sampler = GPUResourceFactory::createSampler(
        m_device.getLogicalDevice(),
        VK_FILTER_LINEAR, VK_FILTER_LINEAR,
        VK_SAMPLER_ADDRESS_MODE_REPEAT,
        16.0f,  // max anisotropy
        VK_SAMPLER_MIPMAP_MODE_LINEAR,
        static_cast<float>(texture.mipLevels)
    );

    texture.exists = true;

    // Cache the texture for reuse
    m_textureCache[fullCacheKey] = texture;

    return texture;
}

entt::entity GltfSceneLoader::createEntity(
    std::shared_ptr<ECS::Scene> scene,
    const GltfPrimitive& primitive)
{
    auto entity = scene->createEntity(primitive.name)
        .withTransform(glm::vec3(0.0f))  // Transform is baked into vertices
        .build();

    // Add MeshComponent
    auto& mesh = scene->addComponent<MeshComponent>(entity);
    mesh.vertexBuffer = primitive.vertexBuffer;
    mesh.indexBuffer = primitive.indexBuffer;
    mesh.vertexCount = primitive.vertexCount;
    mesh.indexCount = primitive.indexCount;
    mesh.vertexStride = primitive.vertexStride;
    mesh.indexType = primitive.indexType;

    // Add MaterialComponentV5
    auto& material = scene->addComponent<MaterialComponentV5>(entity);
    material.setParam("baseColorFactor", primitive.baseColorFactor);
    material.setParam("metallicFactor", primitive.metallicFactor);
    material.setParam("roughnessFactor", primitive.roughnessFactor);
    material.setParam("emissiveFactor", glm::vec4(primitive.emissiveFactor, 1.0f));
    material.alphaMode = primitive.alphaMode;
    material.alphaCutoff = primitive.alphaCutoff;
    material.doubleSided = primitive.doubleSided;

    // Add textures
    if (primitive.albedoMap.isValid()) {
        material.textures["albedoMap"] = primitive.albedoMap;
    }
    if (primitive.normalMap.isValid()) {
        material.textures["normalMap"] = primitive.normalMap;
    }
    if (primitive.metallicRoughnessMap.isValid()) {
        material.textures["metallicRoughnessMap"] = primitive.metallicRoughnessMap;
    }
    if (primitive.aoMap.isValid()) {
        material.textures["aoMap"] = primitive.aoMap;
    }
    if (primitive.emissiveMap.isValid()) {
        material.textures["emissiveMap"] = primitive.emissiveMap;
    }

    // Add RenderableTagComponent
    auto& tag = scene->addComponent<RenderableTagComponent>(entity);
    tag.visible = true;
    tag.castShadows = true;
    tag.receiveShadows = true;

    return entity;
}

// ═══════════════════════════════════════════════════════════════
// Skin Loading
// 骨之器 — The vessel of bones
// ═══════════════════════════════════════════════════════════════

const cgltf_skin* GltfSceneLoader::getNodeSkin(const cgltf_node* node) const {
    return node ? node->skin : nullptr;
}

std::unordered_map<const cgltf_node*, int> GltfSceneLoader::buildNodeToJointMap(
    const cgltf_skin* skin) const
{
    std::unordered_map<const cgltf_node*, int> map;
    if (!skin) return map;
    for (cgltf_size i = 0; i < skin->joints_count; ++i) {
        map[skin->joints[i]] = static_cast<int>(i);
    }
    return map;
}

std::shared_ptr<Shoonyakasha::Skeleton> GltfSceneLoader::loadSkin(
    cgltf_data* data,
    const cgltf_skin* skin)
{
    if (!skin || skin->joints_count == 0) return nullptr;

    auto skeleton = std::make_shared<Shoonyakasha::Skeleton>();
    skeleton->name = skin->name ? skin->name : "skeleton";

    auto nodeToJoint = buildNodeToJointMap(skin);

    // Read inverse bind matrices
    std::vector<glm::mat4> inverseBindMatrices(skin->joints_count, glm::mat4(1.0f));
    if (skin->inverse_bind_matrices) {
        const cgltf_accessor* ibmAccessor = skin->inverse_bind_matrices;
        for (cgltf_size i = 0; i < ibmAccessor->count && i < skin->joints_count; ++i) {
            cgltf_float mat[16];
            cgltf_accessor_read_float(ibmAccessor, i, mat, 16);
            inverseBindMatrices[i] = glm::make_mat4(mat);
        }
    }

    // Build joints
    skeleton->joints.resize(skin->joints_count);
    for (cgltf_size i = 0; i < skin->joints_count; ++i) {
        const cgltf_node* jointNode = skin->joints[i];
        Shoonyakasha::Joint& joint = skeleton->joints[i];

        joint.name = jointNode->name ? jointNode->name : ("joint_" + std::to_string(i));
        joint.inverseBindMatrix = inverseBindMatrices[i];

        // Get default local transform from node
        if (jointNode->has_translation) {
            joint.defaultTranslation = glm::vec3(
                jointNode->translation[0],
                jointNode->translation[1],
                jointNode->translation[2]
            );
        }
        if (jointNode->has_rotation) {
            // cgltf stores quaternion as [x, y, z, w]
            joint.defaultRotation = glm::quat(
                jointNode->rotation[3],  // w
                jointNode->rotation[0],  // x
                jointNode->rotation[1],  // y
                jointNode->rotation[2]   // z
            );
        }
        if (jointNode->has_scale) {
            joint.defaultScale = glm::vec3(
                jointNode->scale[0],
                jointNode->scale[1],
                jointNode->scale[2]
            );
        }

        // Find parent index
        joint.parentIndex = -1;
        if (jointNode->parent) {
            auto parentIt = nodeToJoint.find(jointNode->parent);
            if (parentIt != nodeToJoint.end()) {
                joint.parentIndex = parentIt->second;
            }
        }
    }

    // Find root joint
    for (int i = 0; i < static_cast<int>(skeleton->joints.size()); ++i) {
        if (skeleton->joints[i].parentIndex == -1) {
            skeleton->rootJointIndex = i;
            break;
        }
    }

    skeleton->buildNameLookup();

    std::cout << "[GltfSceneLoader] Loaded skin '" << skeleton->name
              << "' with " << skeleton->jointCount() << " joints" << std::endl;

    return skeleton;
}

// ═══════════════════════════════════════════════════════════════
// Animation Loading
// 動之器 — The vessel of motion
// ═══════════════════════════════════════════════════════════════

std::vector<std::shared_ptr<Shoonyakasha::AnimationClip>> GltfSceneLoader::loadAnimations(
    cgltf_data* data,
    const Shoonyakasha::Skeleton& skeleton)
{
    std::vector<std::shared_ptr<Shoonyakasha::AnimationClip>> clips;

    for (cgltf_size animIdx = 0; animIdx < data->animations_count; ++animIdx) {
        const cgltf_animation& anim = data->animations[animIdx];

        auto clip = std::make_shared<Shoonyakasha::AnimationClip>();
        clip->name = anim.name ? anim.name : ("animation_" + std::to_string(animIdx));
        clip->duration = 0.0f;

        for (cgltf_size chanIdx = 0; chanIdx < anim.channels_count; ++chanIdx) {
            const cgltf_animation_channel& channel = anim.channels[chanIdx];
            const cgltf_animation_sampler& sampler = *channel.sampler;

            if (!channel.target_node) continue;

            // Find joint index for target node
            int jointIdx = skeleton.findJoint(
                channel.target_node->name ? channel.target_node->name : ""
            );
            if (jointIdx < 0) continue;  // Target not in skeleton

            Shoonyakasha::AnimationChannel animChannel;
            animChannel.targetJointIndex = jointIdx;

            // Map target path
            switch (channel.target_path) {
                case cgltf_animation_path_type_translation:
                    animChannel.property = Shoonyakasha::AnimationChannel::Property::Translation;
                    break;
                case cgltf_animation_path_type_rotation:
                    animChannel.property = Shoonyakasha::AnimationChannel::Property::Rotation;
                    break;
                case cgltf_animation_path_type_scale:
                    animChannel.property = Shoonyakasha::AnimationChannel::Property::Scale;
                    break;
                default:
                    continue;  // Skip weights/unknown
            }

            // Map interpolation
            switch (sampler.interpolation) {
                case cgltf_interpolation_type_step:
                    animChannel.interpolation = Shoonyakasha::AnimationInterpolation::Step;
                    break;
                case cgltf_interpolation_type_linear:
                    animChannel.interpolation = Shoonyakasha::AnimationInterpolation::Linear;
                    break;
                case cgltf_interpolation_type_cubic_spline:
                    animChannel.interpolation = Shoonyakasha::AnimationInterpolation::CubicSpline;
                    break;
            }

            // Read timestamps
            const cgltf_accessor* inputAccessor = sampler.input;
            animChannel.timestamps.resize(inputAccessor->count);
            for (cgltf_size t = 0; t < inputAccessor->count; ++t) {
                cgltf_float val;
                cgltf_accessor_read_float(inputAccessor, t, &val, 1);
                animChannel.timestamps[t] = val;
                clip->duration = std::max(clip->duration, val);
            }

            // Read values
            const cgltf_accessor* outputAccessor = sampler.output;
            uint32_t numComponents = (animChannel.property == Shoonyakasha::AnimationChannel::Property::Rotation) ? 4 : 3;
            animChannel.values.resize(outputAccessor->count);
            for (cgltf_size v = 0; v < outputAccessor->count; ++v) {
                cgltf_float components[4] = {0, 0, 0, 0};
                cgltf_accessor_read_float(outputAccessor, v, components, numComponents);
                animChannel.values[v] = glm::vec4(components[0], components[1], components[2],
                                                    numComponents == 4 ? components[3] : 0.0f);
            }

            clip->channels.push_back(std::move(animChannel));
        }

        if (!clip->channels.empty()) {
            std::cout << "[GltfSceneLoader] Loaded animation '" << clip->name
                      << "' (" << clip->duration << "s, "
                      << clip->channels.size() << " channels)" << std::endl;
            clips.push_back(std::move(clip));
        }
    }

    return clips;
}

// ═══════════════════════════════════════════════════════════════
// Skinned Vertex Buffer Building
// ═══════════════════════════════════════════════════════════════

GPUBuffer GltfSceneLoader::buildSkinnedVertexBuffer(
    cgltf_data* data,
    const cgltf_primitive& primitive,
    uint32_t& outVertexCount,
    uint32_t& outVertexStride)
{
    // Find attribute accessors
    const cgltf_accessor* posAccessor = nullptr;
    const cgltf_accessor* normalAccessor = nullptr;
    const cgltf_accessor* texCoordAccessor = nullptr;
    const cgltf_accessor* jointsAccessor = nullptr;
    const cgltf_accessor* weightsAccessor = nullptr;

    for (cgltf_size i = 0; i < primitive.attributes_count; ++i) {
        const cgltf_attribute& attr = primitive.attributes[i];
        switch (attr.type) {
            case cgltf_attribute_type_position:  posAccessor = attr.data; break;
            case cgltf_attribute_type_normal:    normalAccessor = attr.data; break;
            case cgltf_attribute_type_texcoord:  if (attr.index == 0) texCoordAccessor = attr.data; break;
            case cgltf_attribute_type_joints:    if (attr.index == 0) jointsAccessor = attr.data; break;
            case cgltf_attribute_type_weights:   if (attr.index == 0) weightsAccessor = attr.data; break;
            default: break;
        }
    }

    if (!posAccessor) {
        outVertexCount = 0;
        outVertexStride = 0;
        return GPUBuffer{};
    }

    size_t vertexCount = posAccessor->count;

    // Build interleaved skinned vertex data (NO transform baking for skinned meshes)
    std::vector<SkinnedVertex> vertices(vertexCount);

    for (size_t i = 0; i < vertexCount; ++i) {
        SkinnedVertex& v = vertices[i];

        // Position (not transformed — skinning will handle this)
        cgltf_float pos[3] = {0, 0, 0};
        cgltf_accessor_read_float(posAccessor, i, pos, 3);
        v.pos = glm::vec3(pos[0], pos[1], pos[2]);

        // Normal
        if (normalAccessor) {
            cgltf_float norm[3] = {0, 1, 0};
            cgltf_accessor_read_float(normalAccessor, i, norm, 3);
            v.normal = glm::normalize(glm::vec3(norm[0], norm[1], norm[2]));
        } else {
            v.normal = glm::vec3(0.0f, 1.0f, 0.0f);
        }

        // TexCoord
        if (texCoordAccessor) {
            cgltf_float uv[2] = {0, 0};
            cgltf_accessor_read_float(texCoordAccessor, i, uv, 2);
            v.texCoord = glm::vec2(uv[0], uv[1]);
        } else {
            v.texCoord = glm::vec2(0.0f);
        }

        // Joint indices
        if (jointsAccessor) {
            cgltf_uint joints[4] = {0, 0, 0, 0};
            cgltf_accessor_read_uint(jointsAccessor, i, joints, 4);
            v.joints = glm::uvec4(joints[0], joints[1], joints[2], joints[3]);
        } else {
            v.joints = glm::uvec4(0);
        }

        // Joint weights
        if (weightsAccessor) {
            cgltf_float weights[4] = {1, 0, 0, 0};
            cgltf_accessor_read_float(weightsAccessor, i, weights, 4);
            // Normalize weights
            float sum = weights[0] + weights[1] + weights[2] + weights[3];
            if (sum > 0.0f) {
                v.weights = glm::vec4(weights[0], weights[1], weights[2], weights[3]) / sum;
            } else {
                v.weights = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
            }
        } else {
            v.weights = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
        }
    }

    outVertexCount = static_cast<uint32_t>(vertexCount);
    outVertexStride = sizeof(SkinnedVertex);

    // Create GPU buffer
    VkDeviceSize bufferSize = vertices.size() * sizeof(SkinnedVertex);
    GPUBuffer buffer = GPUResourceFactory::createBuffer(
        m_device.getAllocator().getHandle(),
        bufferSize,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY
    );

    // Upload data
    GPUResourceFactory::uploadBuffer(
        m_device.getAllocator().getHandle(),
        m_device.getLogicalDevice(),
        m_device.getGraphicsQueue(),
        m_device.getCommandPool(),
        buffer,
        vertices.data(),
        bufferSize
    );

    return buffer;
}

// ═══════════════════════════════════════════════════════════════
// Skinned Entity Creation
// ═══════════════════════════════════════════════════════════════

entt::entity GltfSceneLoader::createSkinnedEntity(
    std::shared_ptr<ECS::Scene> scene,
    const GltfPrimitive& primitive,
    std::shared_ptr<Shoonyakasha::Skeleton> skeleton,
    const std::vector<std::shared_ptr<Shoonyakasha::AnimationClip>>& clips,
    const glm::mat4& worldTransform)
{
    // Create entity with the node's world transform (NOT baked into vertices)
    // Decompose the world transform matrix into TRS for TransformComponent
    // 形之解 — Decomposing the form
    glm::vec3 pos = glm::vec3(worldTransform[3]);

    // Extract scale from column vector lengths
    glm::vec3 scl;
    scl.x = glm::length(glm::vec3(worldTransform[0]));
    scl.y = glm::length(glm::vec3(worldTransform[1]));
    scl.z = glm::length(glm::vec3(worldTransform[2]));

    // Extract rotation matrix by normalizing columns (removing scale)
    glm::mat3 rotMatrix(1.0f);
    if (scl.x > 0.0f) rotMatrix[0] = glm::vec3(worldTransform[0]) / scl.x;
    if (scl.y > 0.0f) rotMatrix[1] = glm::vec3(worldTransform[1]) / scl.y;
    if (scl.z > 0.0f) rotMatrix[2] = glm::vec3(worldTransform[2]) / scl.z;

    // Extract euler angles (Y→X→Z order, matching TransformComponent::getLocalMatrix)
    float pitch = std::asin(std::clamp(-rotMatrix[2][1], -1.0f, 1.0f));  // X rotation
    float yaw   = std::atan2(rotMatrix[2][0], rotMatrix[2][2]);           // Y rotation
    float roll  = std::atan2(rotMatrix[0][1], rotMatrix[1][1]);           // Z rotation
    glm::vec3 rot(pitch, yaw, roll);

    auto entity = scene->createEntity(primitive.name)
        .withTransform(pos)
        .build();

    // Apply rotation and scale (withTransform only sets position)
    auto& transform = scene->getRegistry().get<ECS::TransformComponent>(entity);
    transform.rotation = rot;
    transform.scale = scl;
    transform.isDirty = true;

    // Add MeshComponent (with skinned vertex buffer)
    auto& mesh = scene->addComponent<MeshComponent>(entity);
    mesh.vertexBuffer = primitive.vertexBuffer;
    mesh.indexBuffer = primitive.indexBuffer;
    mesh.vertexCount = primitive.vertexCount;
    mesh.indexCount = primitive.indexCount;
    mesh.vertexStride = primitive.vertexStride;
    mesh.indexType = primitive.indexType;

    // Add MaterialComponentV5
    auto& material = scene->addComponent<MaterialComponentV5>(entity);
    material.setParam("baseColorFactor", primitive.baseColorFactor);
    material.setParam("metallicFactor", primitive.metallicFactor);
    material.setParam("roughnessFactor", primitive.roughnessFactor);
    material.setParam("emissiveFactor", glm::vec4(primitive.emissiveFactor, 1.0f));
    material.alphaMode = primitive.alphaMode;
    material.alphaCutoff = primitive.alphaCutoff;
    material.doubleSided = primitive.doubleSided;

    // Add textures
    if (primitive.albedoMap.isValid()) material.textures["albedoMap"] = primitive.albedoMap;
    if (primitive.normalMap.isValid()) material.textures["normalMap"] = primitive.normalMap;
    if (primitive.metallicRoughnessMap.isValid()) material.textures["metallicRoughnessMap"] = primitive.metallicRoughnessMap;
    if (primitive.aoMap.isValid()) material.textures["aoMap"] = primitive.aoMap;
    if (primitive.emissiveMap.isValid()) material.textures["emissiveMap"] = primitive.emissiveMap;

    // Add SkeletonComponent
    auto& skelComp = scene->addComponent<SkeletonComponent>(entity);
    skelComp.skeleton = skeleton;
    skelComp.allocate();

    // Add AnimationPlaybackComponent
    if (!clips.empty()) {
        auto& playbackComp = scene->addComponent<AnimationPlaybackComponent>(entity);
        playbackComp.clips = clips;
        playbackComp.allocate(skeleton->jointCount());
        // Auto-play first clip
        playbackComp.play(0);
    }

    // Add RenderableTagComponent
    auto& tag = scene->addComponent<RenderableTagComponent>(entity);
    tag.visible = true;
    tag.castShadows = true;
    tag.receiveShadows = true;

    return entity;
}

} // namespace Shoonyakasha
