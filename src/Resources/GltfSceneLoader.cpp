//
// GltfSceneLoader.cpp - Load glTF files as ECS components
//
// 黃帝司中  調和而統御
// The Yellow Emperor governs the center — unifying meshes, materials, and entities
//

#include "Resources/GltfSceneLoader.h"
#include "Vulkan/VulkanDevice.h"
#include "ECS/Core.h"

#include "ECS/TransformMath.h"
#include "ECS/SkeletonComponents.h"
#include "ECS/SkeletalAnimationSystem.h"
#include "Resources/MeshTangents.h"
#include "Resources/TextureProcessing.h"
#include "Vulkan/VertexTypes.h"

#include <cgltf.h>
#include <algorithm>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>

// stb_image implementation in src/ThirdParty/stb_impl.cpp
#include <stb_image.h>

#include <iostream>
#include <fstream>
#include <algorithm>
#include <cmath>
#include <limits>

namespace Shoonyakasha {

// ═══════════════════════════════════════════════════════════════
// Standard Vertex Structure
// ═══════════════════════════════════════════════════════════════

// The same layout as Vertex (VertexTypes.h).
struct StandardVertex {
    glm::vec3 pos;       // location 0
    glm::vec3 color;     // location 1
    glm::vec2 texCoord;  // location 2
    glm::vec3 normal;    // location 3
    glm::vec4 tangent;   // location 4: xyz along +u, w = ±1; 0 without uvs
};
static_assert(sizeof(StandardVertex) == sizeof(Vertex), "StandardVertex must match Vertex");

// Skinned vertex — includes bone joint indices and weights
// Matches the "skinned" vertex format in JSON:
//   position(vec3), normal(vec3), texCoord(vec2), jointIndices(uvec4), jointWeights(vec4), tangent(vec4)
struct SkinnedVertex {
    glm::vec3 pos;           // location 0
    glm::vec3 normal;        // location 1
    glm::vec2 texCoord;      // location 2
    glm::uvec4 joints;       // location 3 (bone indices)
    glm::vec4 weights;       // location 4 (bone weights)
    glm::vec4 tangent;       // location 5: xyz along +u, w = ±1; 0 without uvs
};
// Stride: 12 + 12 + 8 + 16 + 16 + 16 = 80 bytes

// ═══════════════════════════════════════════════════════════════
// Primitive Geometry
// ═══════════════════════════════════════════════════════════════

// A primitive's vertex attributes as arrays, one entry per vertex. `source`
// is the glTF vertex each one was read from: generated tangents split the
// vertices whose triangles disagree on the tangent.
struct PrimitiveGeometry {
    std::vector<glm::vec3> positions;
    std::vector<glm::vec3> normals;
    std::vector<glm::vec2> uvs;
    std::vector<glm::vec3> colors;
    std::vector<glm::vec4> tangents;
    std::vector<uint32_t> indices;   // empty: every three vertices are a triangle
    std::vector<uint32_t> source;
};

namespace {

// MikkTSpace tangents for the geometry's triangles; the arrays are rebuilt
// over the split vertices.
void generatePrimitiveTangents(PrimitiveGeometry& g) {
    TangentMesh t = generateTangents(g.positions, g.normals, g.uvs, g.indices);
    auto expand = [&t](auto& values) {
        std::remove_reference_t<decltype(values)> out;
        out.reserve(t.source.size());
        for (uint32_t s : t.source) out.push_back(values[s]);
        values = std::move(out);
    };
    expand(g.positions);
    expand(g.normals);
    expand(g.uvs);
    expand(g.colors);
    expand(g.source);
    g.tangents = std::move(t.tangents);
    if (!g.indices.empty()) g.indices = std::move(t.indices);
}

// A glTF primitive's attributes, with positions, normals and tangents under
// `transform`. Tangents come from the file's TANGENT attribute, or from
// MikkTSpace for triangles with uvs; otherwise they are 0.
PrimitiveGeometry readPrimitiveGeometry(const cgltf_primitive& primitive, const glm::mat4& transform) {
    const cgltf_accessor* posAccessor = nullptr;
    const cgltf_accessor* normalAccessor = nullptr;
    const cgltf_accessor* texCoordAccessor = nullptr;
    const cgltf_accessor* colorAccessor = nullptr;
    const cgltf_accessor* tangentAccessor = nullptr;
    for (cgltf_size i = 0; i < primitive.attributes_count; ++i) {
        const cgltf_attribute& attr = primitive.attributes[i];
        switch (attr.type) {
            case cgltf_attribute_type_position:  posAccessor = attr.data; break;
            case cgltf_attribute_type_normal:    normalAccessor = attr.data; break;
            case cgltf_attribute_type_tangent:   tangentAccessor = attr.data; break;
            case cgltf_attribute_type_texcoord:  if (attr.index == 0) texCoordAccessor = attr.data; break;
            case cgltf_attribute_type_color:     if (attr.index == 0) colorAccessor = attr.data; break;
            default: break;
        }
    }

    PrimitiveGeometry g;
    if (!posAccessor) return g;

    const size_t n = posAccessor->count;
    const glm::mat3 linear(transform);
    const glm::mat3 normalMatrix = glm::transpose(glm::inverse(linear));
    // A mirroring transform flips the bitangent relative to cross(normal, tangent).
    const float handedness = glm::determinant(linear) < 0.0f ? -1.0f : 1.0f;
    g.positions.resize(n);
    g.normals.resize(n);
    g.uvs.resize(n);
    g.colors.resize(n);
    g.tangents.assign(n, glm::vec4(0.0f));
    g.source.resize(n);
    for (size_t i = 0; i < n; ++i) {
        cgltf_float pos[3] = {0, 0, 0};
        cgltf_accessor_read_float(posAccessor, i, pos, 3);
        g.positions[i] = glm::vec3(transform * glm::vec4(pos[0], pos[1], pos[2], 1.0f));

        if (colorAccessor) {
            cgltf_float col[4] = {1, 1, 1, 1};
            cgltf_accessor_read_float(colorAccessor, i, col, cgltf_num_components(colorAccessor->type));
            g.colors[i] = glm::vec3(col[0], col[1], col[2]);
        } else {
            g.colors[i] = glm::vec3(1.0f);
        }

        if (texCoordAccessor) {
            cgltf_float uv[2] = {0, 0};
            cgltf_accessor_read_float(texCoordAccessor, i, uv, 2);
            g.uvs[i] = glm::vec2(uv[0], uv[1]);
        } else {
            g.uvs[i] = glm::vec2(0.0f);
        }

        if (normalAccessor) {
            cgltf_float norm[3] = {0, 1, 0};
            cgltf_accessor_read_float(normalAccessor, i, norm, 3);
            g.normals[i] = glm::normalize(normalMatrix * glm::vec3(norm[0], norm[1], norm[2]));
        } else {
            g.normals[i] = glm::vec3(0.0f, 1.0f, 0.0f);
        }

        if (tangentAccessor) {
            cgltf_float tan[4] = {1, 0, 0, 1};
            cgltf_accessor_read_float(tangentAccessor, i, tan, 4);
            const glm::vec3 t = linear * glm::vec3(tan[0], tan[1], tan[2]);
            const float length = glm::length(t);
            g.tangents[i] = glm::vec4(length > 0.0f ? t / length : glm::vec3(0.0f),
                                      (tan[3] < 0.0f ? -1.0f : 1.0f) * handedness);
        }
        g.source[i] = static_cast<uint32_t>(i);
    }

    if (primitive.indices) {
        g.indices.resize(primitive.indices->count);
        for (size_t i = 0; i < g.indices.size(); ++i) {
            g.indices[i] = static_cast<uint32_t>(cgltf_accessor_read_index(primitive.indices, i));
        }
    }

    if (!tangentAccessor && texCoordAccessor && primitive.type == cgltf_primitive_type_triangles) {
        generatePrimitiveTangents(g);
    }
    return g;
}

// A primitive's morph targets for the vertices of `g` (each read at its
// source vertex), under `transform`, with the mesh's default weights; null
// without targets. The caller fills in the vertex layout and base vertices.
std::shared_ptr<MorphTargetSet> readMorphTargets(const cgltf_primitive& primitive, const cgltf_mesh& mesh,
                                                 const PrimitiveGeometry& g, const glm::mat4& transform) {
    if (primitive.targets_count == 0 || g.positions.empty()) return nullptr;
    auto set = std::make_shared<MorphTargetSet>();
    const size_t n = g.positions.size();
    set->vertexCount = static_cast<uint32_t>(n);
    set->targetCount = static_cast<uint32_t>(primitive.targets_count);
    const glm::mat3 linear(transform);
    const glm::mat3 normalMatrix = glm::transpose(glm::inverse(linear));
    set->positionDeltas.assign(n * set->targetCount, glm::vec3(0.0f));

    for (cgltf_size t = 0; t < primitive.targets_count; ++t) {
        const cgltf_morph_target& target = primitive.targets[t];
        for (cgltf_size a = 0; a < target.attributes_count; ++a) {
            const cgltf_attribute& attribute = target.attributes[a];
            std::vector<glm::vec3>* deltas = nullptr;
            glm::mat3 matrix = linear;
            switch (attribute.type) {
                case cgltf_attribute_type_position: deltas = &set->positionDeltas; break;
                case cgltf_attribute_type_normal:   deltas = &set->normalDeltas; matrix = normalMatrix; break;
                case cgltf_attribute_type_tangent:  deltas = &set->tangentDeltas; break;
                default: break;
            }
            if (!deltas || !attribute.data) continue;
            deltas->resize(n * set->targetCount, glm::vec3(0.0f));
            for (size_t v = 0; v < n; ++v) {
                cgltf_float d[3] = {0, 0, 0};
                cgltf_accessor_read_float(attribute.data, g.source[v], d, 3);
                (*deltas)[t * n + v] = matrix * glm::vec3(d[0], d[1], d[2]);
            }
        }
    }

    set->defaultWeights.assign(set->targetCount, 0.0f);
    for (cgltf_size t = 0; t < std::min<cgltf_size>(mesh.weights_count, set->targetCount); ++t) {
        set->defaultWeights[t] = mesh.weights[t];
    }
    return set;
}

} // namespace

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

GltfSceneLoader::~GltfSceneLoader() {
    destroyTextureCache();
}

void GltfSceneLoader::releaseCachedGeometry() {
    m_primitiveCache.clear();
}

void GltfSceneLoader::destroyTextureCache() {
    for (auto& [key, texture] : m_textureCache) {
        GPUResourceFactory::destroyTexture(
            m_device.getAllocator().getHandle(),
            m_device.getLogicalDevice(),
            texture);
    }
    m_textureCache.clear();
    for (auto& [key, sampler] : m_samplerCache) {
        vkDestroySampler(m_device.getLogicalDevice(), sampler, nullptr);
    }
    m_samplerCache.clear();
}

VkSampler GltfSceneLoader::gltfSampler(const cgltf_sampler* sampler, uint32_t mipLevels) {
    if (!sampler) return VK_NULL_HANDLE;
    auto wrap = [](cgltf_wrap_mode mode) {
        switch (mode) {
            case cgltf_wrap_mode_clamp_to_edge:   return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            case cgltf_wrap_mode_mirrored_repeat: return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
            default:                              return VK_SAMPLER_ADDRESS_MODE_REPEAT;
        }
    };
    const VkSamplerAddressMode wrapU = wrap(sampler->wrap_s), wrapV = wrap(sampler->wrap_t);
    const VkFilter mag = sampler->mag_filter == cgltf_filter_type_nearest ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
    VkFilter min = VK_FILTER_LINEAR;
    VkSamplerMipmapMode mipmap = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    bool mipmapped = true;
    switch (sampler->min_filter) {
        case cgltf_filter_type_nearest:                min = VK_FILTER_NEAREST; mipmapped = false; break;
        case cgltf_filter_type_linear:                 mipmapped = false; break;
        case cgltf_filter_type_nearest_mipmap_nearest: min = VK_FILTER_NEAREST; mipmap = VK_SAMPLER_MIPMAP_MODE_NEAREST; break;
        case cgltf_filter_type_linear_mipmap_nearest:  mipmap = VK_SAMPLER_MIPMAP_MODE_NEAREST; break;
        case cgltf_filter_type_nearest_mipmap_linear:  min = VK_FILTER_NEAREST; break;
        default: break;
    }
    if (mag == VK_FILTER_LINEAR && min == VK_FILTER_LINEAR && mipmapped && mipmap == VK_SAMPLER_MIPMAP_MODE_LINEAR &&
        wrapU == VK_SAMPLER_ADDRESS_MODE_REPEAT && wrapV == VK_SAMPLER_ADDRESS_MODE_REPEAT) {
        return VK_NULL_HANDLE;
    }

    const float maxLod = mipmapped ? static_cast<float>(mipLevels) : 0.0f;
    const std::string key = std::to_string(mag) + "_" + std::to_string(min) + "_" + std::to_string(mipmap) + "_" +
                            std::to_string(wrapU) + "_" + std::to_string(wrapV) + "_" + std::to_string(maxLod);
    if (auto it = m_samplerCache.find(key); it != m_samplerCache.end()) return it->second;

    VkSamplerCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    info.magFilter = mag;
    info.minFilter = min;
    info.mipmapMode = mipmap;
    info.addressModeU = wrapU;
    info.addressModeV = wrapV;
    info.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    // Anisotropic filtering where both filters are linear.
    info.anisotropyEnable = (mag == VK_FILTER_LINEAR && min == VK_FILTER_LINEAR) ? VK_TRUE : VK_FALSE;
    info.maxAnisotropy = info.anisotropyEnable ? 16.0f : 1.0f;
    info.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    info.compareOp = VK_COMPARE_OP_ALWAYS;
    info.minLod = 0.0f;
    info.maxLod = maxLod;
    VkSampler made = VK_NULL_HANDLE;
    if (vkCreateSampler(m_device.getLogicalDevice(), &info, nullptr, &made) != VK_SUCCESS) {
        std::cerr << "[GltfSceneLoader] Failed to create a texture sampler; using the default" << std::endl;
        return VK_NULL_HANDLE;
    }
    m_samplerCache[key] = made;
    return made;
}

// ═══════════════════════════════════════════════════════════════
// Main Load Function
// ═══════════════════════════════════════════════════════════════

GltfLoadResult GltfSceneLoader::load(
    const std::filesystem::path& path,
    std::shared_ptr<ECS::Scene> scene,
    const GltfLoadOptions& options)
{
    GltfLoadResult result;
    // The texture cache spans loads: entities created by an earlier load still
    // hold its views and samplers. Only the per-load key set is reset here.
    m_loadTextureKeys.clear();
    m_basePath = path.parent_path();
    m_currentFile = path;
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

    // Load animations if requested: each skeleton's joint channels, and the
    // morph weight channels once, with the first skeleton's or on their own.
    if (m_options.loadAnimations) {
        for (size_t s = 0; s < std::max<size_t>(result.skeletons.size(), 1); ++s) {
            const Skeleton* skeleton = s < result.skeletons.size() ? result.skeletons[s].get() : nullptr;
            for (auto& clip : loadAnimations(data, skeleton, s == 0)) {
                result.animationClips.push_back(std::move(clip));
            }
        }
    }

    // Walk the scene graph. Entities are created during the walk now, not in a
    // second pass — the pass could only see a flat primitive list, so the parent
    // links were gone by the time it ran, and it had to guess at skinning by
    // comparing vertexStride against sizeof(SkinnedVertex).
    if (data->scene) {
        for (cgltf_size i = 0; i < data->scene->nodes_count; ++i) {
            processNode(data, data->scene->nodes[i], entt::null, scene, result);
        }
    } else if (data->scenes_count > 0) {
        for (cgltf_size i = 0; i < data->scenes[0].nodes_count; ++i) {
            processNode(data, data->scenes[0].nodes[i], entt::null, scene, result);
        }
    } else {
        // No scenes, process root nodes
        for (cgltf_size i = 0; i < data->nodes_count; ++i) {
            if (data->nodes[i].parent == nullptr) {
                processNode(data, &data->nodes[i], entt::null, scene, result);
            }
        }
    }

    cgltf_free(data);

    // Collect statistics
    for (const auto& prim : result.primitives) {
        result.totalVertices += prim.vertexCount;
        result.totalIndices += prim.indexCount;
    }
    result.totalMaterials = result.primitives.size();
    result.totalTextures = m_loadTextureKeys.size();

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

void GltfSceneLoader::applyNodeTransform(ECS::TransformComponent& transform, const cgltf_node* node) {
    if (node->has_matrix) {
        decomposeTRS(glm::make_mat4(node->matrix),
                     transform.position, transform.rotation, transform.scale);
    } else {
        // Preferred path: glTF gave us translation/rotation/scale directly, so
        // there is nothing to decompose and nothing to lose.
        if (node->has_translation) {
            transform.position = glm::vec3(node->translation[0], node->translation[1], node->translation[2]);
        }
        if (node->has_scale) {
            transform.scale = glm::vec3(node->scale[0], node->scale[1], node->scale[2]);
        }
        if (node->has_rotation) {
            // cgltf stores the quaternion xyzw; glm::quat takes w first.
            const glm::quat q(node->rotation[3], node->rotation[0],
                              node->rotation[1], node->rotation[2]);
            transform.rotation = eulerYXZFromRotation(glm::mat3_cast(glm::normalize(q)));
        }
    }
    transform.isDirty = true;
}

GltfPrimitive GltfSceneLoader::buildPrimitive(
    cgltf_data* data,
    const cgltf_node* node,
    size_t primitiveIndex,
    bool skinned,
    const std::string& name,
    const glm::mat4& transform)
{
    const cgltf_primitive& primitive = node->mesh->primitives[primitiveIndex];
    // Skinned meshes are not baked: the skin places them in skeleton space.
    return processPrimitive(data, *node->mesh, primitive, skinned ? glm::mat4(1.0f) : transform, name, skinned);
}

const GltfPrimitive* GltfSceneLoader::getOrBuildPrimitive(
    cgltf_data* data,
    const cgltf_node* node,
    size_t primitiveIndex,
    bool skinned,
    const std::string& name)
{
    // Keyed by file path, not by pointer: cgltf_free() runs at the end of load()
    // and a later parse can reuse the same addresses.
    const std::string key = m_currentFile.string() + "#"
        + std::to_string(cgltf_mesh_index(data, node->mesh)) + "/"
        + std::to_string(primitiveIndex)
        + (skinned ? "_skinned" : "");

    auto it = m_primitiveCache.find(key);
    if (it != m_primitiveCache.end()) {
        return &it->second;
    }

    GltfPrimitive built = buildPrimitive(data, node, primitiveIndex, skinned, name, glm::mat4(1.0f));
    if (!built.vertexBuffer) {
        return nullptr;
    }
    return &m_primitiveCache.emplace(key, std::move(built)).first->second;
}

// ═══════════════════════════════════════════════════════════════
// Node Processing
// ═══════════════════════════════════════════════════════════════

void GltfSceneLoader::processNode(
    cgltf_data* data,
    const cgltf_node* node,
    entt::entity parentEntity,
    std::shared_ptr<ECS::Scene> scene,
    GltfLoadResult& result)
{
    const bool makeEntities = m_options.createEntities && scene != nullptr;
    const bool flatten = m_options.flattenHierarchy;

    cgltf_float worldMatrix[16];
    cgltf_node_transform_world(node, worldMatrix);
    const glm::mat4 worldTransform = glm::make_mat4(worldMatrix);

    // The entity that carries this node's transform. In flatten mode there is no
    // scene graph to carry — the transform is in the vertices — so geometry
    // entities are created unparented, exactly as before.
    entt::entity nodeEntity = entt::null;
    if (makeEntities && !flatten) {
        const std::string nodeName = m_options.namePrefix + "_"
                                   + (node->name ? node->name : "node");
        nodeEntity = scene->createEntity(nodeName)
            .withTransform(glm::vec3(0.0f))
            .withParent(parentEntity)
            .build();

        applyNodeTransform(
            scene->getRegistry().get<ECS::TransformComponent>(nodeEntity), node);

        result.nodeEntities.push_back(nodeEntity);
        if (parentEntity == entt::null) {
            result.rootEntities.push_back(nodeEntity);
        }
    }

    if (node->mesh) {
        const cgltf_mesh* mesh = node->mesh;
        const std::string meshBaseName = node->name ? node->name
                                       : (mesh->name ? mesh->name : "mesh");
        const bool isSkinned = (node->skin != nullptr && m_options.loadSkins);

        for (cgltf_size primIdx = 0; primIdx < mesh->primitives_count; ++primIdx) {
            if (mesh->primitives[primIdx].type != cgltf_primitive_type_triangles) {
                continue;
            }

            std::string primName = m_options.namePrefix + "_" + meshBaseName;
            if (mesh->primitives_count > 1) {
                primName += "_prim" + std::to_string(primIdx);
            }

            GltfPrimitive instance;
            if (flatten) {
                // Baked vertices differ per node, so there is nothing to share.
                instance = buildPrimitive(data, node, primIdx, isSkinned, primName, worldTransform);
            } else {
                const GltfPrimitive* shared =
                    getOrBuildPrimitive(data, node, primIdx, isSkinned, primName);
                if (!shared) {
                    continue;
                }
                instance = *shared;       // shares the buffers, copies the material
                instance.name = primName;
            }
            instance.worldTransform = worldTransform;

            if (makeEntities) {
                const entt::entity parent = flatten ? entt::null : nodeEntity;
                const entt::entity entity =
                    (isSkinned && !result.skeletons.empty())
                        ? createSkinnedEntity(scene, instance, result.skeletons[0],
                                              result.animationClips, parent)
                        : createEntity(scene, instance, parent);
                if (instance.morphTargets) {
                    addMorphTargets(scene, entity, instance, data, node, result.animationClips);
                }
                result.entities.push_back(entity);
            }

            result.primitives.push_back(std::move(instance));
        }
    }

    for (cgltf_size i = 0; i < node->children_count; ++i) {
        processNode(data, node->children[i], nodeEntity, scene, result);
    }
}

void GltfSceneLoader::addMorphTargets(
    const std::shared_ptr<ECS::Scene>& scene,
    entt::entity entity,
    const GltfPrimitive& primitive,
    cgltf_data* data,
    const cgltf_node* node,
    const std::vector<std::shared_ptr<AnimationClip>>& clips)
{
    auto& registry = scene->getRegistry();
    auto& morph = registry.emplace_or_replace<MorphComponent>(entity);
    morph.targets = primitive.morphTargets;
    morph.sourceNode = static_cast<int>(cgltf_node_index(data, node));
    std::vector<float> weights = primitive.morphTargets->defaultWeights;
    for (cgltf_size t = 0; t < std::min<cgltf_size>(node->weights_count, weights.size()); ++t) {
        weights[t] = node->weights[t];
    }
    morph.setWeights(std::move(weights));

    // Skinned entities already carry the clips.
    if (!clips.empty() && !registry.all_of<AnimationPlaybackComponent>(entity)) {
        registry.emplace<AnimationPlaybackComponent>(entity).clips = clips;
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
    const cgltf_mesh& mesh,
    const cgltf_primitive& primitive,
    const glm::mat4& worldTransform,
    const std::string& primitiveName,
    bool skinned)
{
    GltfPrimitive result;
    result.name = primitiveName;
    result.worldTransform = worldTransform;

    const PrimitiveGeometry geometry = readPrimitiveGeometry(primitive, worldTransform);
    std::shared_ptr<MorphTargetSet> morph = readMorphTargets(primitive, mesh, geometry, worldTransform);
    std::vector<uint8_t>* keep = morph ? &morph->baseVertices : nullptr;
    result.vertexBuffer = m_device.getDeleteQueue().adopt(
        skinned ? buildSkinnedVertexBuffer(primitive, geometry, result.vertexCount, result.vertexStride, keep)
                : buildVertexBuffer(geometry, result.vertexCount, result.vertexStride, keep));
    if (morph) {
        morph->vertexStride = result.vertexStride;
        morph->positionOffset = 0;
        morph->normalOffset = skinned ? offsetof(SkinnedVertex, normal) : offsetof(StandardVertex, normal);
        morph->tangentOffset = skinned ? offsetof(SkinnedVertex, tangent) : offsetof(StandardVertex, tangent);
        result.morphTargets = std::move(morph);
    }
    // Morphed meshes move outside their base bounds, so they are never culled.
    result.hasBounds = result.vertexCount > 0 && !result.morphTargets;
    if (result.hasBounds) {
        result.boundsMin = glm::vec3(std::numeric_limits<float>::max());
        result.boundsMax = glm::vec3(std::numeric_limits<float>::lowest());
        for (const glm::vec3& p : geometry.positions) {
            result.boundsMin = glm::min(result.boundsMin, p);
            result.boundsMax = glm::max(result.boundsMax, p);
        }
    }

    result.indexBuffer = m_device.getDeleteQueue().adopt(
        buildIndexBuffer(geometry.indices, result.indexCount, result.indexType));

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
            result.normalMap = loadTexture(data, material->normal_texture, false, true);
        }
        result.normalScale = material->normal_texture.scale;

        if (material->occlusion_texture.texture && m_options.loadTextures) {
            result.aoMap = loadTexture(data, material->occlusion_texture, false);
        }
        result.occlusionStrength = material->occlusion_texture.scale;

        if (material->emissive_texture.texture && m_options.loadTextures) {
            result.emissiveMap = loadTexture(data, material->emissive_texture, true);
        }

        result.emissiveFactor = glm::vec3(
            material->emissive_factor[0],
            material->emissive_factor[1],
            material->emissive_factor[2]
        );
        if (material->has_emissive_strength) {
            result.emissiveFactor *= material->emissive_strength.emissive_strength;
        }

        if (material->has_clearcoat) {
            result.hasClearcoat = true;
            result.clearcoatFactor = material->clearcoat.clearcoat_factor;
            result.clearcoatRoughnessFactor = material->clearcoat.clearcoat_roughness_factor;
            if (m_options.loadTextures) {
                if (material->clearcoat.clearcoat_texture.texture) {
                    result.clearcoatMap = loadTexture(data, material->clearcoat.clearcoat_texture, false);
                }
                if (material->clearcoat.clearcoat_roughness_texture.texture) {
                    result.clearcoatRoughnessMap =
                        loadTexture(data, material->clearcoat.clearcoat_roughness_texture, false);
                }
            }
        }

        if (material->has_transmission) result.transmissionFactor = material->transmission.transmission_factor;
        if (material->has_ior) result.ior = material->ior.ior;

        if (material->has_iridescence) {
            const auto& film = material->iridescence;
            result.hasIridescence = true;
            result.iridescenceFactor = film.iridescence_factor;
            result.iridescenceIor = film.iridescence_ior;
            result.iridescenceThicknessMinimum = film.iridescence_thickness_min;
            result.iridescenceThicknessMaximum = film.iridescence_thickness_max;
            if (m_options.loadTextures) {
                if (film.iridescence_texture.texture) {
                    result.iridescenceMap = loadTexture(data, film.iridescence_texture, false);
                }
                if (film.iridescence_thickness_texture.texture) {
                    result.iridescenceThicknessMap = loadTexture(data, film.iridescence_thickness_texture, false);
                }
            }
        }

        result.alphaMode = extractAlphaMode(material);
        result.alphaCutoff = material->alpha_cutoff;
        result.doubleSided = material->double_sided;
    }

    return result;
}

GPUBuffer GltfSceneLoader::buildVertexBuffer(
    const PrimitiveGeometry& geometry,
    uint32_t& outVertexCount,
    uint32_t& outVertexStride,
    std::vector<uint8_t>* keepVertices)
{
    const size_t vertexCount = geometry.positions.size();
    outVertexCount = static_cast<uint32_t>(vertexCount);
    outVertexStride = vertexCount > 0 ? sizeof(StandardVertex) : 0;
    if (vertexCount == 0) return GPUBuffer{};

    std::vector<StandardVertex> vertices(vertexCount);
    for (size_t i = 0; i < vertexCount; ++i) {
        vertices[i] = {geometry.positions[i], geometry.colors[i], geometry.uvs[i], geometry.normals[i],
                       geometry.tangents[i]};
    }

    VkDeviceSize bufferSize = vertices.size() * sizeof(StandardVertex);
    if (keepVertices) {
        const auto* bytes = reinterpret_cast<const uint8_t*>(vertices.data());
        keepVertices->assign(bytes, bytes + bufferSize);
    }
    GPUBuffer buffer = GPUResourceFactory::createBuffer(
        m_device.getAllocator().getHandle(),
        bufferSize,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | rayTracingInputUsage(),
        VMA_MEMORY_USAGE_GPU_ONLY
    );
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

std::string GltfSceneLoader::buildMeshPrimitive(const MeshArrays& arrays, GltfPrimitive& out) {
    const size_t n = arrays.positions.size();
    if (n == 0) return "the mesh has no positions";
    if (n > std::numeric_limits<uint32_t>::max()) return "the mesh has too many positions";
    for (const auto& [count, what] : {std::pair{arrays.normals.size(), "normals"}, {arrays.uvs.size(), "uvs"},
                                      {arrays.colors.size(), "colors"}, {arrays.tangents.size(), "tangents"}}) {
        if (count != 0 && count != n) {
            return std::string("there are ") + std::to_string(count) + " " + what + " for " + std::to_string(n) +
                   " positions";
        }
    }
    const bool indexed = !arrays.indices.empty();
    const size_t corners = indexed ? arrays.indices.size() : n;
    if (corners % 3 != 0) {
        return std::string(indexed ? "the index count" : "without indices, the position count") +
               " is not a multiple of 3";
    }
    for (uint32_t i : arrays.indices) {
        if (i >= n) return "index " + std::to_string(i) + " is past the " + std::to_string(n) + " positions";
    }

    PrimitiveGeometry g;
    g.positions = arrays.positions;
    g.indices = arrays.indices;
    g.uvs = arrays.uvs.empty() ? std::vector<glm::vec2>(n, glm::vec2(0.0f)) : arrays.uvs;
    g.colors = arrays.colors.empty() ? std::vector<glm::vec3>(n, glm::vec3(1.0f)) : arrays.colors;
    g.source.resize(n);
    for (size_t i = 0; i < n; ++i) g.source[i] = static_cast<uint32_t>(i);

    // Area-weighted vertex normals when none are given.
    g.normals = arrays.normals;
    if (g.normals.empty()) {
        g.normals.assign(n, glm::vec3(0.0f));
        for (size_t t = 0; t < corners; t += 3) {
            const uint32_t a = indexed ? arrays.indices[t] : static_cast<uint32_t>(t);
            const uint32_t b = indexed ? arrays.indices[t + 1] : static_cast<uint32_t>(t + 1);
            const uint32_t c = indexed ? arrays.indices[t + 2] : static_cast<uint32_t>(t + 2);
            const glm::vec3 face = glm::cross(arrays.positions[b] - arrays.positions[a],
                                              arrays.positions[c] - arrays.positions[a]);
            g.normals[a] += face;
            g.normals[b] += face;
            g.normals[c] += face;
        }
    }
    for (glm::vec3& normal : g.normals) {
        const float length = glm::length(normal);
        normal = length > 0.0f ? normal / length : glm::vec3(0.0f, 1.0f, 0.0f);
    }

    if (!arrays.tangents.empty()) {
        g.tangents = arrays.tangents;
    } else if (!arrays.uvs.empty()) {
        generatePrimitiveTangents(g);
    } else {
        g.tangents.assign(n, glm::vec4(0.0f));
    }

    const size_t count = g.positions.size();
    std::vector<StandardVertex> vertices(count);
    glm::vec3 lo(std::numeric_limits<float>::max()), hi(std::numeric_limits<float>::lowest());
    for (size_t i = 0; i < count; ++i) {
        vertices[i] = {g.positions[i], g.colors[i], g.uvs[i], g.normals[i], g.tangents[i]};
        lo = glm::min(lo, g.positions[i]);
        hi = glm::max(hi, g.positions[i]);
    }

    auto upload = [&](const void* data, VkDeviceSize size, VkBufferUsageFlags usage) {
        GPUBuffer buffer = GPUResourceFactory::createBuffer(
            m_device.getAllocator().getHandle(), size,
            usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT | rayTracingInputUsage(), VMA_MEMORY_USAGE_GPU_ONLY);
        GPUResourceFactory::uploadBuffer(m_device.getAllocator().getHandle(), m_device.getLogicalDevice(),
                                         m_device.getGraphicsQueue(), m_device.getCommandPool(), buffer, data, size);
        return m_device.getDeleteQueue().adopt(buffer);
    };
    out.vertexBuffer = upload(vertices.data(), vertices.size() * sizeof(StandardVertex),
                              VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    out.vertexCount = static_cast<uint32_t>(count);
    out.vertexStride = sizeof(StandardVertex);
    out.indexBuffer = indexed ? upload(g.indices.data(), g.indices.size() * sizeof(uint32_t),
                                       VK_BUFFER_USAGE_INDEX_BUFFER_BIT)
                              : GpuBufferRef{};
    out.indexCount = indexed ? static_cast<uint32_t>(g.indices.size()) : 0;
    out.indexType = IndexType::UInt32;
    out.boundsMin = lo;
    out.boundsMax = hi;
    out.hasBounds = true;
    return {};
}

entt::entity GltfSceneLoader::createMeshEntity(std::shared_ptr<ECS::Scene> scene, const std::string& name,
                                               const MeshArrays& arrays, std::string& error) {
    GltfPrimitive primitive;
    primitive.name = name;
    error = buildMeshPrimitive(arrays, primitive);
    if (!error.empty()) return entt::null;
    return createEntity(std::move(scene), primitive, entt::null);
}

std::string GltfSceneLoader::setMeshGeometry(entt::registry& registry, entt::entity entity,
                                             const MeshArrays& arrays) {
    auto* mesh = registry.valid(entity) ? registry.try_get<MeshComponent>(entity) : nullptr;
    if (!mesh) return "the entity has no mesh";
    GltfPrimitive primitive;
    const std::string error = buildMeshPrimitive(arrays, primitive);
    if (!error.empty()) return error;
    mesh->vertexBuffer = primitive.vertexBuffer;
    mesh->indexBuffer = primitive.indexBuffer;
    mesh->vertexCount = primitive.vertexCount;
    mesh->indexCount = primitive.indexCount;
    mesh->vertexStride = primitive.vertexStride;
    mesh->indexType = primitive.indexType;
    mesh->boundsMin = primitive.boundsMin;
    mesh->boundsMax = primitive.boundsMax;
    mesh->hasBounds = true;
    mesh->rayTracingInput = m_device.hasRayQuery();
    return {};
}

VkBufferUsageFlags GltfSceneLoader::rayTracingInputUsage() const {
    // Static meshes are built into acceleration structures for ray queries,
    // which read their vertices and indices by device address.
    return m_device.hasRayQuery()
        ? VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
          VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR
        : 0;
}

GPUBuffer GltfSceneLoader::buildIndexBuffer(
    const std::vector<uint32_t>& wide,
    uint32_t& outIndexCount,
    Shoonyakasha::IndexType& outIndexType)
{
    if (wide.empty()) {
        // Non-indexed geometry - return empty buffer
        outIndexCount = 0;
        outIndexType = Shoonyakasha::IndexType::UInt32;
        return GPUBuffer{};
    }

    // Narrow only if the largest value fits. The width follows the maximum
    // index value, not the index count: indices address the primitive's
    // vertex attributes, so a mesh with 100k vertices and 60k indices holds
    // values above 65535.
    const size_t indexCount = wide.size();
    const uint32_t maxIndex = *std::max_element(wide.begin(), wide.end());

    // 0xFFFF is reserved as the primitive-restart marker for 16-bit indices, so it
    // is not usable as a vertex index.
    bool use16Bit = (indexCount > 0) && (maxIndex < 0xFFFFu);

    outIndexCount = static_cast<uint32_t>(indexCount);
    outIndexType = use16Bit ? Shoonyakasha::IndexType::UInt16 : Shoonyakasha::IndexType::UInt32;

    VkDeviceSize bufferSize;
    GPUBuffer buffer;

    if (use16Bit) {
        std::vector<uint16_t> indices(indexCount);
        for (size_t i = 0; i < indexCount; ++i) {
            indices[i] = static_cast<uint16_t>(wide[i]);
        }

        bufferSize = indices.size() * sizeof(uint16_t);
        buffer = GPUResourceFactory::createBuffer(
            m_device.getAllocator().getHandle(),
            bufferSize,
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | rayTracingInputUsage(),
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
        const std::vector<uint32_t>& indices = wide;

        bufferSize = indices.size() * sizeof(uint32_t);
        buffer = GPUResourceFactory::createBuffer(
            m_device.getAllocator().getHandle(),
            bufferSize,
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | rayTracingInputUsage(),
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
    cgltf_data* /*data*/,
    const cgltf_texture_view& textureView,
    bool srgb,
    bool normalMap)
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
        // Embedded texture — data lives in the glTF/glb buffer.
        isEmbedded = true;
        // Keyed by source file plus the view's offset and size. The old key was
        // the buffer_view *pointer*, which is only valid until cgltf_free() at the
        // end of this load; with a cache that spans loads, a later allocation
        // landing on the same address would return an unrelated texture.
        cacheKey = m_currentFile.string() + "_bv_"
                 + std::to_string(image->buffer_view->offset) + "_"
                 + std::to_string(image->buffer_view->size);
    }

    if (cacheKey.empty()) {
        return GPUTexture{};
    }

    // Check cache first to avoid loading same texture multiple times. The
    // key names everything that changes the texture made from the file.
    const bool compress = m_options.compressTextures && m_device.getEnabledFeatures().textureCompressionBC;
    const uint32_t maxSize = m_options.maxTextureSize > 0 ? static_cast<uint32_t>(m_options.maxTextureSize) : 0;
    std::string fullCacheKey = cacheKey + (srgb ? "_srgb" : normalMap ? "_normal" : "_linear") +
                               (maxSize ? "_max" + std::to_string(maxSize) : "") + (compress ? "_bc" : "");
    m_loadTextureKeys.insert(fullCacheKey);
    // The cached texture, with the glTF sampler's own VkSampler when it is
    // not the default.
    auto sampled = [&](Shoonyakasha::GPUTexture texture) {
        if (VkSampler sampler = gltfSampler(textureView.texture->sampler, texture.mipLevels)) {
            texture.sampler = sampler;
        }
        return texture;
    };
    auto cacheIt = m_textureCache.find(fullCacheKey);
    if (cacheIt != m_textureCache.end()) {
        return sampled(cacheIt->second);
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

    RgbaImage decoded;
    decoded.width = static_cast<uint32_t>(width);
    decoded.height = static_cast<uint32_t>(height);
    decoded.texels.assign(pixels, pixels + static_cast<size_t>(width) * height * 4);
    stbi_image_free(pixels);
    decoded = fitImage(std::move(decoded), maxSize, srgb);

    GPUTexture texture;
    if (compress) {
        // Mips from the CPU: a block-compressed decoded cannot be blitted.
        BlockFormat block = BlockFormat::BC1;
        VkFormat format = srgb ? VK_FORMAT_BC1_RGB_SRGB_BLOCK : VK_FORMAT_BC1_RGB_UNORM_BLOCK;
        if (normalMap) {
            block = BlockFormat::BC5;
            format = VK_FORMAT_BC5_UNORM_BLOCK;
        } else if (hasTransparency(decoded)) {
            block = BlockFormat::BC3;
            format = srgb ? VK_FORMAT_BC3_SRGB_BLOCK : VK_FORMAT_BC3_UNORM_BLOCK;
        }
        const uint32_t baseWidth = decoded.width, baseHeight = decoded.height;
        std::vector<RgbaImage> mips;
        if (m_options.generateMipmaps) {
            mips = mipChain(std::move(decoded), srgb);
        } else {
            mips.push_back(std::move(decoded));
        }
        std::vector<std::vector<uint8_t>> levels;
        for (const auto& mip : mips) levels.push_back(compressBlocks(mip, block));
        texture = GPUResourceFactory::createTexture2DWithLevels(
            m_device.getAllocator().getHandle(), m_device.getLogicalDevice(), m_device.getGraphicsQueue(),
            m_device.getCommandPool(), baseWidth, baseHeight, format, levels);
    } else {
        texture = GPUResourceFactory::createTexture2DWithData(
            m_device.getAllocator().getHandle(),
            m_device.getLogicalDevice(),
            m_device.getGraphicsQueue(),
            m_device.getCommandPool(),
            decoded.width,
            decoded.height,
            srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM,
            decoded.texels.data(),
            decoded.texels.size(),
            m_options.generateMipmaps
        );
    }

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

    return sampled(texture);
}

entt::entity GltfSceneLoader::createEntity(
    std::shared_ptr<ECS::Scene> scene,
    const GltfPrimitive& primitive,
    entt::entity parentEntity)
{
    // Identity either way. Parented, the node entity above carries the transform
    // and the vertices are in mesh space; unparented (flattenHierarchy), the
    // transform is already baked into the vertices.
    auto builder = scene->createEntity(primitive.name);
    builder.withTransform(glm::vec3(0.0f));
    if (parentEntity != entt::null) {
        builder.withParent(parentEntity);
    }
    auto entity = builder.build();

    // Add MeshComponent
    auto& mesh = scene->addComponent<MeshComponent>(entity);
    mesh.vertexBuffer = primitive.vertexBuffer;
    mesh.indexBuffer = primitive.indexBuffer;
    mesh.vertexCount = primitive.vertexCount;
    mesh.indexCount = primitive.indexCount;
    mesh.vertexStride = primitive.vertexStride;
    mesh.indexType = primitive.indexType;
    mesh.boundsMin = primitive.boundsMin;
    mesh.boundsMax = primitive.boundsMax;
    mesh.hasBounds = primitive.hasBounds;
    // Standard-vertex meshes are made with acceleration structure input usage
    // on a device with ray queries (rayTracingInputUsage); skinned ones are not.
    mesh.rayTracingInput = m_device.hasRayQuery() && mesh.vertexStride == sizeof(StandardVertex);

    // Add MaterialComponentV5
    auto& material = scene->addComponent<MaterialComponentV5>(entity);
    material.setParam("baseColorFactor", primitive.baseColorFactor);
    material.setParam("metallicFactor", primitive.metallicFactor);
    material.setParam("roughnessFactor", primitive.roughnessFactor);
    material.setParam("emissiveFactor", glm::vec4(primitive.emissiveFactor, 1.0f));
    material.setParam("normalScale", primitive.normalScale);
    material.setParam("occlusionStrength", primitive.occlusionStrength);
    if (primitive.hasClearcoat) {
        material.setParam("clearcoatFactor", primitive.clearcoatFactor);
        material.setParam("clearcoatRoughnessFactor", primitive.clearcoatRoughnessFactor);
    }
    material.setParam("transmissionFactor", primitive.transmissionFactor);
    material.setParam("ior", primitive.ior);
    if (primitive.hasIridescence) {
        material.setParam("iridescenceFactor", primitive.iridescenceFactor);
        material.setParam("iridescenceIor", primitive.iridescenceIor);
        material.setParam("iridescenceThicknessMinimum", primitive.iridescenceThicknessMinimum);
        material.setParam("iridescenceThicknessMaximum", primitive.iridescenceThicknessMaximum);
    }
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
    if (primitive.clearcoatMap.isValid()) {
        material.textures["clearcoatMap"] = primitive.clearcoatMap;
    }
    if (primitive.clearcoatRoughnessMap.isValid()) {
        material.textures["clearcoatRoughnessMap"] = primitive.clearcoatRoughnessMap;
    }
    if (primitive.iridescenceMap.isValid()) {
        material.textures["iridescenceMap"] = primitive.iridescenceMap;
    }
    if (primitive.iridescenceThicknessMap.isValid()) {
        material.textures["iridescenceThicknessMap"] = primitive.iridescenceThicknessMap;
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
    cgltf_data* /*data*/,
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
    const Shoonyakasha::Skeleton* skeleton,
    bool withWeights)
{
    std::vector<std::shared_ptr<Shoonyakasha::AnimationClip>> clips;

    auto interpolationOf = [](const cgltf_animation_sampler& sampler) {
        switch (sampler.interpolation) {
            case cgltf_interpolation_type_step:         return Shoonyakasha::AnimationInterpolation::Step;
            case cgltf_interpolation_type_cubic_spline: return Shoonyakasha::AnimationInterpolation::CubicSpline;
            default:                                    return Shoonyakasha::AnimationInterpolation::Linear;
        }
    };

    for (cgltf_size animIdx = 0; animIdx < data->animations_count; ++animIdx) {
        const cgltf_animation& anim = data->animations[animIdx];

        auto clip = std::make_shared<Shoonyakasha::AnimationClip>();
        clip->name = anim.name ? anim.name : ("animation_" + std::to_string(animIdx));
        clip->duration = 0.0f;

        for (cgltf_size chanIdx = 0; chanIdx < anim.channels_count; ++chanIdx) {
            const cgltf_animation_channel& channel = anim.channels[chanIdx];
            const cgltf_animation_sampler& sampler = *channel.sampler;

            if (!channel.target_node) continue;

            // Morph target weights of a mesh node.
            if (channel.target_path == cgltf_animation_path_type_weights) {
                const cgltf_mesh* mesh = channel.target_node->mesh;
                if (!withWeights || !mesh || mesh->primitives_count == 0) continue;
                Shoonyakasha::MorphWeightChannel weights;
                weights.targetNode = static_cast<int>(cgltf_node_index(data, channel.target_node));
                weights.interpolation = interpolationOf(sampler);
                weights.weightCount = static_cast<uint32_t>(mesh->primitives[0].targets_count);
                weights.timestamps.resize(sampler.input->count);
                for (cgltf_size t = 0; t < sampler.input->count; ++t) {
                    cgltf_accessor_read_float(sampler.input, t, &weights.timestamps[t], 1);
                    clip->duration = std::max(clip->duration, weights.timestamps[t]);
                }
                weights.values.resize(sampler.output->count);
                for (cgltf_size v = 0; v < sampler.output->count; ++v) {
                    cgltf_accessor_read_float(sampler.output, v, &weights.values[v], 1);
                }
                if (weights.weightCount > 0) clip->weightChannels.push_back(std::move(weights));
                continue;
            }
            if (!skeleton) continue;

            // Find joint index for target node
            int jointIdx = skeleton->findJoint(
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

            animChannel.interpolation = interpolationOf(sampler);

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

        if (!clip->channels.empty() || !clip->weightChannels.empty()) {
            std::cout << "[GltfSceneLoader] Loaded animation '" << clip->name
                      << "' (" << clip->duration << "s, "
                      << clip->channels.size() << " channels, "
                      << clip->weightChannels.size() << " weight channels)" << std::endl;
            clips.push_back(std::move(clip));
        }
    }

    return clips;
}

// ═══════════════════════════════════════════════════════════════
// Skinned Vertex Buffer Building
// ═══════════════════════════════════════════════════════════════

GPUBuffer GltfSceneLoader::buildSkinnedVertexBuffer(
    const cgltf_primitive& primitive,
    const PrimitiveGeometry& geometry,
    uint32_t& outVertexCount,
    uint32_t& outVertexStride,
    std::vector<uint8_t>* keepVertices)
{
    const cgltf_accessor* jointsAccessor = nullptr;
    const cgltf_accessor* weightsAccessor = nullptr;
    for (cgltf_size i = 0; i < primitive.attributes_count; ++i) {
        const cgltf_attribute& attr = primitive.attributes[i];
        switch (attr.type) {
            case cgltf_attribute_type_joints:    if (attr.index == 0) jointsAccessor = attr.data; break;
            case cgltf_attribute_type_weights:   if (attr.index == 0) weightsAccessor = attr.data; break;
            default: break;
        }
    }

    const size_t vertexCount = geometry.positions.size();
    outVertexCount = static_cast<uint32_t>(vertexCount);
    outVertexStride = vertexCount > 0 ? sizeof(SkinnedVertex) : 0;
    if (vertexCount == 0) return GPUBuffer{};

    // Mesh-space vertices: the skin places them.
    std::vector<SkinnedVertex> vertices(vertexCount);
    for (size_t i = 0; i < vertexCount; ++i) {
        SkinnedVertex& v = vertices[i];
        const cgltf_size gltfVertex = geometry.source[i];
        v.pos = geometry.positions[i];
        v.normal = geometry.normals[i];
        v.texCoord = geometry.uvs[i];
        v.tangent = geometry.tangents[i];

        // Joint indices
        if (jointsAccessor) {
            cgltf_uint joints[4] = {0, 0, 0, 0};
            cgltf_accessor_read_uint(jointsAccessor, gltfVertex, joints, 4);
            v.joints = glm::uvec4(joints[0], joints[1], joints[2], joints[3]);
        } else {
            v.joints = glm::uvec4(0);
        }

        // Joint weights, normalised
        v.weights = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
        if (weightsAccessor) {
            cgltf_float weights[4] = {1, 0, 0, 0};
            cgltf_accessor_read_float(weightsAccessor, gltfVertex, weights, 4);
            const float sum = weights[0] + weights[1] + weights[2] + weights[3];
            if (sum > 0.0f) v.weights = glm::vec4(weights[0], weights[1], weights[2], weights[3]) / sum;
        }
    }

    // On a device with ray queries, RayTracingScene's skinning shader reads
    // the vertices by device address.
    VkDeviceSize bufferSize = vertices.size() * sizeof(SkinnedVertex);
    if (keepVertices) {
        const auto* bytes = reinterpret_cast<const uint8_t*>(vertices.data());
        keepVertices->assign(bytes, bytes + bufferSize);
    }
    GPUBuffer buffer = GPUResourceFactory::createBuffer(
        m_device.getAllocator().getHandle(),
        bufferSize,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
            (m_device.hasRayQuery() ? VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT : 0),
        VMA_MEMORY_USAGE_GPU_ONLY
    );
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
    entt::entity parentEntity)
{
    // Skinned vertices are not baked, so the node transform is carried by the
    // parent node entity and this entity keeps an identity transform.
    auto builder = scene->createEntity(primitive.name);
    builder.withTransform(glm::vec3(0.0f));
    if (parentEntity != entt::null) {
        builder.withParent(parentEntity);
    }
    auto entity = builder.build();

    if (parentEntity == entt::null) {
        // flattenHierarchy: no node entity exists, so decompose the world matrix
        // here. Lossy if an ancestor's non-uniform scale composed with a rotation —
        // one of the reasons the hierarchy path is the default.
        auto& transform = scene->getRegistry().get<ECS::TransformComponent>(entity);
        decomposeTRS(primitive.worldTransform,
                     transform.position, transform.rotation, transform.scale);
        transform.isDirty = true;
    }

    // Add MeshComponent (with skinned vertex buffer)
    auto& mesh = scene->addComponent<MeshComponent>(entity);
    mesh.vertexBuffer = primitive.vertexBuffer;
    mesh.indexBuffer = primitive.indexBuffer;
    mesh.vertexCount = primitive.vertexCount;
    mesh.indexCount = primitive.indexCount;
    mesh.vertexStride = primitive.vertexStride;
    mesh.indexType = primitive.indexType;
    mesh.boundsMin = primitive.boundsMin;
    mesh.boundsMax = primitive.boundsMax;
    mesh.hasBounds = primitive.hasBounds;
    // Standard-vertex meshes are made with acceleration structure input usage
    // on a device with ray queries (rayTracingInputUsage); skinned ones are not.
    mesh.rayTracingInput = m_device.hasRayQuery() && mesh.vertexStride == sizeof(StandardVertex);

    // Add MaterialComponentV5
    auto& material = scene->addComponent<MaterialComponentV5>(entity);
    material.setParam("baseColorFactor", primitive.baseColorFactor);
    material.setParam("metallicFactor", primitive.metallicFactor);
    material.setParam("roughnessFactor", primitive.roughnessFactor);
    material.setParam("emissiveFactor", glm::vec4(primitive.emissiveFactor, 1.0f));
    material.setParam("normalScale", primitive.normalScale);
    material.setParam("occlusionStrength", primitive.occlusionStrength);
    if (primitive.hasClearcoat) {
        material.setParam("clearcoatFactor", primitive.clearcoatFactor);
        material.setParam("clearcoatRoughnessFactor", primitive.clearcoatRoughnessFactor);
    }
    material.alphaMode = primitive.alphaMode;
    material.alphaCutoff = primitive.alphaCutoff;
    material.doubleSided = primitive.doubleSided;

    // Add textures
    if (primitive.albedoMap.isValid()) material.textures["albedoMap"] = primitive.albedoMap;
    if (primitive.normalMap.isValid()) material.textures["normalMap"] = primitive.normalMap;
    if (primitive.metallicRoughnessMap.isValid()) material.textures["metallicRoughnessMap"] = primitive.metallicRoughnessMap;
    if (primitive.aoMap.isValid()) material.textures["aoMap"] = primitive.aoMap;
    if (primitive.emissiveMap.isValid()) material.textures["emissiveMap"] = primitive.emissiveMap;
    if (primitive.clearcoatMap.isValid()) material.textures["clearcoatMap"] = primitive.clearcoatMap;
    if (primitive.clearcoatRoughnessMap.isValid())
        material.textures["clearcoatRoughnessMap"] = primitive.clearcoatRoughnessMap;

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
