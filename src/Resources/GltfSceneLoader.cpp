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
#include "Resources/MeshLods.h"
#include "Resources/TextureProcessing.h"
#include "Vulkan/VertexTypes.h"

#include <cgltf.h>
#include <meshoptimizer.h>
#include <draco/compression/decode.h>
#include <ktx.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>

// stb_image implementation in src/ThirdParty/stb_impl.cpp
#include <stb_image.h>

#include <iostream>
#include <fstream>
#include <iterator>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>

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
    glm::vec2 texCoord1; // TEXCOORD_1; 0 without it
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
    glm::vec2 texCoord1;     // TEXCOORD_1; 0 without it
};
// Stride: 12 + 12 + 8 + 16 + 16 + 16 + 8 = 88 bytes

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
    std::vector<glm::vec2> uvs1;   // TEXCOORD_1
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
    expand(g.uvs1);
    expand(g.colors);
    expand(g.source);
    g.tangents = std::move(t.tangents);
    if (!g.indices.empty()) g.indices = std::move(t.indices);
}

// A texture's uv set and transform as two rows: the transformed uv is
// (dot(rows[0].xyz, (u, v, 1)), dot(rows[1].xyz, (u, v, 1))) of the set
// rows[0].w (0 or 1). KHR_texture_transform: offset, then rotation, then scale.
std::array<glm::vec4, 2> textureUvTransform(const cgltf_texture_view& view) {
    float set = view.texcoord == 1 ? 1.0f : 0.0f;
    if (!view.has_transform) return {glm::vec4(1.0f, 0.0f, 0.0f, set), glm::vec4(0.0f, 1.0f, 0.0f, 0.0f)};
    const cgltf_texture_transform& t = view.transform;
    if (t.has_texcoord) set = t.texcoord == 1 ? 1.0f : 0.0f;
    const float c = std::cos(t.rotation), s = std::sin(t.rotation);
    return {glm::vec4(c * t.scale[0], s * t.scale[1], t.offset[0], set),
            glm::vec4(-s * t.scale[0], c * t.scale[1], t.offset[1], 0.0f)};
}

// The KHR_materials_sheen and KHR_materials_specular params and textures of
// a material with the extensions.
void setSheenAndSpecular(MaterialComponentV5& material, const GltfPrimitive& primitive) {
    if (primitive.hasSheen) {
        material.setParam("sheenColorFactor", glm::vec4(primitive.sheenColorFactor, 1.0f));
        material.setParam("sheenRoughnessFactor", primitive.sheenRoughnessFactor);
    }
    if (primitive.hasSpecular) {
        material.setParam("specularFactor", primitive.specularFactor);
        material.setParam("specularColorFactor", glm::vec4(primitive.specularColorFactor, 1.0f));
    }
    if (primitive.sheenColorMap.isValid()) material.textures["sheenColorMap"] = primitive.sheenColorMap;
    if (primitive.sheenRoughnessMap.isValid()) material.textures["sheenRoughnessMap"] = primitive.sheenRoughnessMap;
    if (primitive.specularMap.isValid()) material.textures["specularMap"] = primitive.specularMap;
    if (primitive.specularColorMap.isValid()) material.textures["specularColorMap"] = primitive.specularColorMap;
}

// A KHR_lights_punctual light as a LightComponent. The intensity is kept as
// glTF gives it (candela for point and spot lights, lux for directional
// ones), with inverse-square falloff. A light without a range reaches as far
// as its intensity times its brightest colour channel, over the distance
// squared, stays above 0.01. Cone angles are half angles, in degrees.
ECS::LightComponent lightFromGltf(const cgltf_light& source) {
    ECS::LightComponent light;
    light.color = glm::vec3(source.color[0], source.color[1], source.color[2]);
    light.intensity = source.intensity;
    switch (source.type) {
        case cgltf_light_type_directional: light.type = ECS::LightComponent::Directional; break;
        case cgltf_light_type_spot:        light.type = ECS::LightComponent::Spot; break;
        default:                           light.type = ECS::LightComponent::Point; break;
    }
    light.constant = 0.0f;
    light.linear = 0.0f;
    light.quadratic = 1.0f;
    const float brightest = std::max(light.color.r, std::max(light.color.g, light.color.b));
    light.range = source.range > 0.0f ? source.range
                                      : std::max(std::sqrt(std::max(light.intensity * brightest, 0.0f) / 0.01f), 0.01f);
    if (source.type == cgltf_light_type_spot) {
        light.innerCone = glm::degrees(source.spot_inner_cone_angle);
        light.outerCone = glm::degrees(source.spot_outer_cone_angle);
    }
    return light;
}

// Decodes the buffer views compressed with EXT_meshopt_compression or
// KHR_meshopt_compression into memory of their own (cgltf_buffer_view::data,
// which cgltf reads in place of the buffer and frees in cgltf_free).
// Returns an error message, empty when every view decoded.
std::string decodeMeshoptBuffers(cgltf_data* data) {
    for (cgltf_size i = 0; i < data->buffer_views_count; ++i) {
        cgltf_buffer_view& view = data->buffer_views[i];
        if (!view.has_meshopt_compression || view.data) continue;
        const cgltf_meshopt_compression& mc = view.meshopt_compression;
        const std::string where = "meshopt-compressed buffer view " + std::to_string(i);
        if (!mc.buffer || !mc.buffer->data || mc.offset + mc.size > mc.buffer->size) {
            return where + ": its compressed data is missing";
        }
        const auto* source = static_cast<const unsigned char*>(mc.buffer->data) + mc.offset;
        void* decoded = std::malloc(mc.count * mc.stride);
        if (!decoded) return where + ": out of memory";
        int status = -1;
        switch (mc.mode) {
            case cgltf_meshopt_compression_mode_attributes:
                status = meshopt_decodeVertexBuffer(decoded, mc.count, mc.stride, source, mc.size);
                break;
            case cgltf_meshopt_compression_mode_triangles:
                status = meshopt_decodeIndexBuffer(decoded, mc.count, mc.stride, source, mc.size);
                break;
            case cgltf_meshopt_compression_mode_indices:
                status = meshopt_decodeIndexSequence(decoded, mc.count, mc.stride, source, mc.size);
                break;
            default:
                break;
        }
        if (status != 0) {
            std::free(decoded);
            return where + ": decoding failed (" + std::to_string(status) + ")";
        }
        switch (mc.filter) {
            case cgltf_meshopt_compression_filter_octahedral:  meshopt_decodeFilterOct(decoded, mc.count, mc.stride); break;
            case cgltf_meshopt_compression_filter_quaternion:  meshopt_decodeFilterQuat(decoded, mc.count, mc.stride); break;
            case cgltf_meshopt_compression_filter_exponential: meshopt_decodeFilterExp(decoded, mc.count, mc.stride); break;
            case cgltf_meshopt_compression_filter_color:       meshopt_decodeFilterColor(decoded, mc.count, mc.stride); break;
            default: break;
        }
        view.data = decoded;
    }
    return {};
}

// A KTX2 image (KHR_texture_basisu), read with libktx. With more than one
// mip level: the levels from the first that fits maxSize (0: no limit), as
// BC7 when transcoded from Basis Universal where `bc` (the device samples BC
// formats), and RGBA8 otherwise, in
// `levels` with their `format`. With one level: that level as RGBA8 in
// `single`, for the path PNG and JPEG images take. `error` is set when the
// file cannot be read or transcoded.
struct KtxImage {
    uint32_t width = 0, height = 0;
    VkFormat format = VK_FORMAT_UNDEFINED;
    std::vector<std::vector<uint8_t>> levels;
    RgbaImage single;
    std::string error;
};

KtxImage decodeKtx2(const uint8_t* bytes, size_t size, bool srgb, bool bc, uint32_t maxSize) {
    KtxImage out;
    ktxTexture2* texture = nullptr;
    if (ktxTexture2_CreateFromMemory(bytes, size, KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &texture) != KTX_SUCCESS) {
        out.error = "not a KTX2 file libktx can read";
        return out;
    }
    const bool mipmapped = texture->numLevels > 1;
    const bool transcoded = ktxTexture2_NeedsTranscoding(texture);
    const bool toBc = mipmapped && bc && transcoded;
    if (transcoded) {
        const ktx_error_code_e status =
            ktxTexture2_TranscodeBasis(texture, toBc ? KTX_TTF_BC7_RGBA : KTX_TTF_RGBA32, 0);
        if (status != KTX_SUCCESS) {
            out.error = std::string("transcoding failed: ") + ktxErrorString(status);
            ktxTexture_Destroy(ktxTexture(texture));
            return out;
        }
    } else if (texture->vkFormat != VK_FORMAT_R8G8B8A8_UNORM && texture->vkFormat != VK_FORMAT_R8G8B8A8_SRGB) {
        out.error = "neither Basis Universal nor RGBA8 (VkFormat " + std::to_string(texture->vkFormat) + ")";
        ktxTexture_Destroy(ktxTexture(texture));
        return out;
    }
    out.format = toBc ? (srgb ? VK_FORMAT_BC7_SRGB_BLOCK : VK_FORMAT_BC7_UNORM_BLOCK)
                      : (srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM);

    auto levelData = [&](uint32_t level) {
        ktx_size_t offset = 0;
        ktxTexture_GetImageOffset(ktxTexture(texture), level, 0, 0, &offset);
        const ktx_size_t bytesInLevel = ktxTexture_GetImageSize(ktxTexture(texture), level);
        const uint8_t* start = ktxTexture_GetData(ktxTexture(texture)) + offset;
        return std::vector<uint8_t>(start, start + bytesInLevel);
    };
    if (!mipmapped) {
        out.single.width = texture->baseWidth;
        out.single.height = texture->baseHeight;
        out.single.texels = levelData(0);
        out.width = texture->baseWidth;
        out.height = texture->baseHeight;
    } else {
        uint32_t first = 0;
        while (maxSize > 0 && first + 1 < texture->numLevels &&
               std::max(texture->baseWidth >> first, texture->baseHeight >> first) > maxSize) {
            ++first;
        }
        out.width = std::max(texture->baseWidth >> first, 1u);
        out.height = std::max(texture->baseHeight >> first, 1u);
        for (uint32_t level = first; level < texture->numLevels; ++level) out.levels.push_back(levelData(level));
    }
    ktxTexture_Destroy(ktxTexture(texture));
    return out;
}

// KHR_node_visibility: whether the node, or one of its ancestors, has
// "visible": false.
bool nodeHidden(const cgltf_node* node) {
    for (; node; node = node->parent) {
        for (cgltf_size i = 0; i < node->extensions_count; ++i) {
            const cgltf_extension& extension = node->extensions[i];
            if (!extension.name || !extension.data || std::strcmp(extension.name, "KHR_node_visibility") != 0) {
                continue;
            }
            const auto json = nlohmann::json::parse(extension.data, nullptr, false);
            if (json.is_object() && json.value("visible", true) == false) return true;
        }
    }
    return false;
}

// Memory holding decoded KHR_draco_mesh_compression data: buffer views the
// decoded accessors point at, kept until the cgltf data is freed.
struct DracoStorage {
    std::vector<std::unique_ptr<cgltf_buffer_view>> views;
    std::vector<std::vector<uint8_t>> bytes;

    // A buffer view over `size` new bytes; `out` is where they are written.
    cgltf_buffer_view* add(size_t size, uint8_t*& out) {
        bytes.emplace_back(size);
        auto view = std::make_unique<cgltf_buffer_view>();
        *view = cgltf_buffer_view{};
        view->size = size;
        view->data = bytes.back().data();
        out = bytes.back().data();
        views.push_back(std::move(view));
        return views.back().get();
    }
};

// The values of draco attribute `source` for every point of `mesh`, in the
// component type T, written tightly packed to `out`.
template <typename T>
void writeDracoValues(const draco::PointAttribute& source, const draco::Mesh& mesh, int components, uint8_t* out) {
    std::vector<T> value(static_cast<size_t>(components));
    for (draco::PointIndex p(0); p < mesh.num_points(); ++p) {
        source.ConvertValue<T>(source.mapped_index(p), static_cast<int8_t>(components), value.data());
        std::memcpy(out, value.data(), sizeof(T) * components);
        out += sizeof(T) * components;
    }
}

// Decodes the primitives compressed with KHR_draco_mesh_compression: each
// attribute's accessor, and the indices, get a buffer view in `storage`
// holding the decoded values in the accessor's own component type (32-bit
// indices). Returns an error message, empty when every primitive decoded.
std::string decodeDracoPrimitives(cgltf_data* data, DracoStorage& storage) {
    for (cgltf_size m = 0; m < data->meshes_count; ++m) {
        for (cgltf_size p = 0; p < data->meshes[m].primitives_count; ++p) {
            cgltf_primitive& primitive = data->meshes[m].primitives[p];
            if (!primitive.has_draco_mesh_compression) continue;
            const std::string where = "Draco primitive " + std::to_string(p) + " of mesh " + std::to_string(m);
            const cgltf_draco_mesh_compression& draco = primitive.draco_mesh_compression;
            const uint8_t* compressed = draco.buffer_view ? cgltf_buffer_view_data(draco.buffer_view) : nullptr;
            if (!compressed) return where + ": its compressed data is missing";

            draco::DecoderBuffer buffer;
            buffer.Init(reinterpret_cast<const char*>(compressed), draco.buffer_view->size);
            draco::Decoder decoder;
            auto decoded = decoder.DecodeMeshFromBuffer(&buffer);
            if (!decoded.ok()) return where + ": " + decoded.status().error_msg_string();
            const std::unique_ptr<draco::Mesh> mesh = std::move(decoded).value();
            const size_t points = mesh->num_points();

            for (cgltf_size a = 0; a < primitive.attributes_count; ++a) {
                cgltf_accessor* accessor = primitive.attributes[a].data;
                if (!accessor || accessor->buffer_view) continue;   // not compressed, or already decoded
                const cgltf_attribute* entry = nullptr;
                for (cgltf_size d = 0; d < draco.attributes_count; ++d) {
                    if (std::strcmp(draco.attributes[d].name, primitive.attributes[a].name) == 0) entry = &draco.attributes[d];
                }
                if (!entry) continue;
                // cgltf resolves the draco attribute id as an accessor index.
                const auto id = static_cast<uint32_t>(cgltf_accessor_index(data, entry->data));
                const draco::PointAttribute* source = mesh->GetAttributeByUniqueId(id);
                if (!source) return where + ": no attribute " + std::to_string(id) + " for " + primitive.attributes[a].name;
                if (accessor->count != points) return where + ": " + primitive.attributes[a].name + " has " +
                                                      std::to_string(accessor->count) + " values, the mesh " +
                                                      std::to_string(points) + " points";
                const int components = static_cast<int>(cgltf_num_components(accessor->type));
                const size_t componentSize = cgltf_component_size(accessor->component_type);
                uint8_t* out = nullptr;
                cgltf_buffer_view* view = storage.add(points * components * componentSize, out);
                switch (accessor->component_type) {
                    case cgltf_component_type_r_8:   writeDracoValues<int8_t>(*source, *mesh, components, out); break;
                    case cgltf_component_type_r_8u:  writeDracoValues<uint8_t>(*source, *mesh, components, out); break;
                    case cgltf_component_type_r_16:  writeDracoValues<int16_t>(*source, *mesh, components, out); break;
                    case cgltf_component_type_r_16u: writeDracoValues<uint16_t>(*source, *mesh, components, out); break;
                    case cgltf_component_type_r_32u: writeDracoValues<uint32_t>(*source, *mesh, components, out); break;
                    case cgltf_component_type_r_32f: writeDracoValues<float>(*source, *mesh, components, out); break;
                    default: return where + ": unsupported component type for " + primitive.attributes[a].name;
                }
                accessor->buffer_view = view;
                accessor->offset = 0;
                accessor->stride = components * componentSize;
            }

            if (primitive.indices && !primitive.indices->buffer_view) {
                const size_t count = static_cast<size_t>(mesh->num_faces()) * 3;
                uint8_t* out = nullptr;
                cgltf_buffer_view* view = storage.add(count * sizeof(uint32_t), out);
                for (draco::FaceIndex f(0); f < mesh->num_faces(); ++f) {
                    const auto& face = mesh->face(f);
                    const uint32_t corners[3] = {face[0].value(), face[1].value(), face[2].value()};
                    std::memcpy(out, corners, sizeof(corners));
                    out += sizeof(corners);
                }
                primitive.indices->buffer_view = view;
                primitive.indices->offset = 0;
                primitive.indices->count = count;
                primitive.indices->component_type = cgltf_component_type_r_32u;
                primitive.indices->stride = sizeof(uint32_t);
            }
        }
    }
    return {};
}

// The KHR_materials_anisotropy params and texture of a material with the
// extension; anisotropyTexture is 1 when it has the texture.
void setAnisotropy(MaterialComponentV5& material, const GltfPrimitive& primitive) {
    if (!primitive.hasAnisotropy) return;
    material.setParam("anisotropyStrength", primitive.anisotropyStrength);
    material.setParam("anisotropyRotation", primitive.anisotropyRotation);
    if (primitive.anisotropyMap.isValid()) {
        material.textures["anisotropyMap"] = primitive.anisotropyMap;
        material.setParam("anisotropyTexture", 1.0f);
    }
}

// The KHR_materials_iridescence params and textures of a material with the
// extension.
void setIridescence(MaterialComponentV5& material, const GltfPrimitive& primitive) {
    if (primitive.hasIridescence) {
        material.setParam("iridescenceFactor", primitive.iridescenceFactor);
        material.setParam("iridescenceIor", primitive.iridescenceIor);
        material.setParam("iridescenceThicknessMinimum", primitive.iridescenceThicknessMinimum);
        material.setParam("iridescenceThicknessMaximum", primitive.iridescenceThicknessMaximum);
    }
    if (primitive.iridescenceMap.isValid()) material.textures["iridescenceMap"] = primitive.iridescenceMap;
    if (primitive.iridescenceThicknessMap.isValid()) {
        material.textures["iridescenceThicknessMap"] = primitive.iridescenceThicknessMap;
    }
}

// The MaterialComponentV5 of a glTF material: its factors, uv rows,
// alpha mode and textures, as the loader read them into `primitive`.
MaterialComponentV5 makeMaterial(const GltfPrimitive& primitive) {
    MaterialComponentV5 material;
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
    material.setParam("thicknessFactor", primitive.thicknessFactor);
    material.setParam("attenuationDistance", primitive.attenuationDistance);
    material.setParam("attenuationColor", glm::vec4(primitive.attenuationColor, 1.0f));
    for (const auto& [slot, rows] : primitive.uvTransforms) {
        material.setParam(slot + "UvRow0", rows[0]);
        material.setParam(slot + "UvRow1", rows[1]);
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
    if (primitive.transmissionMap.isValid()) {
        material.textures["transmissionMap"] = primitive.transmissionMap;
    }
    if (primitive.thicknessMap.isValid()) {
        material.textures["thicknessMap"] = primitive.thicknessMap;
    }
    setSheenAndSpecular(material, primitive);
    setIridescence(material, primitive);
    if (primitive.unlit) material.setParam("unlit", 1.0f);
    setAnisotropy(material, primitive);
    return material;
}

const std::array<glm::vec4, 2> kIdentityUvTransform = {glm::vec4(1.0f, 0.0f, 0.0f, 0.0f),
                                                       glm::vec4(0.0f, 1.0f, 0.0f, 0.0f)};

// A glTF primitive's attributes, with positions, normals and tangents under
// `transform`. Tangents come from the file's TANGENT attribute, or from
// MikkTSpace for triangles with uvs; otherwise they are 0.
PrimitiveGeometry readPrimitiveGeometry(const cgltf_primitive& primitive, const glm::mat4& transform) {
    const cgltf_accessor* posAccessor = nullptr;
    const cgltf_accessor* normalAccessor = nullptr;
    const cgltf_accessor* texCoordAccessor = nullptr;
    const cgltf_accessor* texCoord1Accessor = nullptr;
    const cgltf_accessor* colorAccessor = nullptr;
    const cgltf_accessor* tangentAccessor = nullptr;
    for (cgltf_size i = 0; i < primitive.attributes_count; ++i) {
        const cgltf_attribute& attr = primitive.attributes[i];
        switch (attr.type) {
            case cgltf_attribute_type_position:  posAccessor = attr.data; break;
            case cgltf_attribute_type_normal:    normalAccessor = attr.data; break;
            case cgltf_attribute_type_tangent:   tangentAccessor = attr.data; break;
            case cgltf_attribute_type_texcoord:
                if (attr.index == 0) texCoordAccessor = attr.data;
                if (attr.index == 1) texCoord1Accessor = attr.data;
                break;
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
    g.uvs1.assign(n, glm::vec2(0.0f));
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
        if (texCoord1Accessor) {
            cgltf_float uv[2] = {0, 0};
            cgltf_accessor_read_float(texCoord1Accessor, i, uv, 2);
            g.uvs1[i] = glm::vec2(uv[0], uv[1]);
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
    if (const std::string error = decodeMeshoptBuffers(data); !error.empty()) {
        cgltf_free(data);
        result.error = path.string() + ": " + error;
        return result;
    }
    DracoStorage dracoStorage;
    if (const std::string error = decodeDracoPrimitives(data, dracoStorage); !error.empty()) {
        cgltf_free(data);
        result.error = path.string() + ": " + error;
        return result;
    }
    for (cgltf_size i = 0; i < data->variants_count; ++i) {
        result.variants.emplace_back(data->variants[i].name ? data->variants[i].name : "");
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
                if (!instance.materialVariants.empty()) {
                    auto& registry = scene->getRegistry();
                    MaterialVariantsComponent variants;
                    variants.defaultMaterial = registry.get<MaterialComponentV5>(entity);
                    for (const auto& [index, material] : instance.materialVariants) {
                        variants.variants[index] = makeMaterial(*material);
                    }
                    registry.emplace_or_replace<MaterialVariantsComponent>(entity, std::move(variants));
                }
                if (nodeHidden(node)) {
                    scene->getRegistry().get<RenderableTagComponent>(entity).visible = false;
                }
                result.entities.push_back(entity);
            }

            result.primitives.push_back(std::move(instance));
        }
    }

    // The node's light, shining along its -Z: a child of the node's entity,
    // or, flattened, placed at the node's world position and direction.
    if (makeEntities && m_options.loadLights && node->light) {
        const std::string lightName = m_options.namePrefix + "_"
                                    + (node->light->name ? node->light->name : "light");
        entt::entity lightEntity = entt::null;
        if (flatten) {
            const glm::vec3 forward = -glm::vec3(worldTransform[2]);
            lightEntity = scene->createEntity(lightName)
                .withTransform(glm::vec3(worldTransform[3]))
                .build();
            if (glm::dot(forward, forward) > 1e-12f) {
                scene->getRegistry().get<ECS::TransformComponent>(lightEntity).rotation =
                    ECS::TransformComponent::rotationFacing(forward);
            }
        } else {
            lightEntity = scene->createEntity(lightName)
                .withTransform(glm::vec3(0.0f))
                .withParent(nodeEntity)
                .build();
        }
        scene->getRegistry().emplace<ECS::LightComponent>(lightEntity, lightFromGltf(*node->light));
        result.lights.push_back(lightEntity);
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

    // Generated levels of detail follow the full mesh in its index buffer.
    std::vector<uint32_t> indices = geometry.indices;
    if (m_options.generateLods && !skinned && !result.morphTargets &&
        primitive.type == cgltf_primitive_type_triangles) {
        result.lods = appendMeshLods(geometry.positions, indices, 4, geometry.normals);
    }
    result.indexBuffer = m_device.getDeleteQueue().adopt(
        buildIndexBuffer(indices, result.indexCount, result.indexType));
    if (!result.lods.empty()) result.indexCount = result.lods.front().firstIndex;

    if (primitive.material && m_options.loadMaterials) {
        readMaterial(data, primitive.material, result);
    }
    // KHR_materials_variants: each mapped material, read like the default.
    if (m_options.loadMaterials) {
        for (cgltf_size i = 0; i < primitive.mappings_count; ++i) {
            const cgltf_material_mapping& mapping = primitive.mappings[i];
            if (!mapping.material) continue;
            auto variant = std::make_shared<GltfPrimitive>();
            readMaterial(data, mapping.material, *variant);
            result.materialVariants.emplace_back(static_cast<uint32_t>(mapping.variant), std::move(variant));
        }
    }

    return result;
}

// A glTF material's factors, extensions, textures, uv rows and alpha mode,
// into the material fields of `result`.
void GltfSceneLoader::readMaterial(cgltf_data* data, const cgltf_material* material, GltfPrimitive& result) {

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

    if (material->has_transmission) {
        result.transmissionFactor = material->transmission.transmission_factor;
        if (m_options.loadTextures && material->transmission.transmission_texture.texture) {
            result.transmissionMap = loadTexture(data, material->transmission.transmission_texture, false);
        }
    }
    if (material->has_ior) result.ior = material->ior.ior;
    if (material->has_volume) {
        const auto& volume = material->volume;
        result.thicknessFactor = volume.thickness_factor;
        result.attenuationDistance = std::isfinite(volume.attenuation_distance) ? volume.attenuation_distance : 0.0f;
        result.attenuationColor = glm::vec3(volume.attenuation_color[0], volume.attenuation_color[1],
                                            volume.attenuation_color[2]);
        if (m_options.loadTextures && volume.thickness_texture.texture) {
            result.thicknessMap = loadTexture(data, volume.thickness_texture, false);
        }
    }

    if (material->has_sheen) {
        const auto& sheen = material->sheen;
        result.hasSheen = true;
        result.sheenColorFactor = glm::vec3(sheen.sheen_color_factor[0], sheen.sheen_color_factor[1],
                                            sheen.sheen_color_factor[2]);
        result.sheenRoughnessFactor = sheen.sheen_roughness_factor;
        if (m_options.loadTextures && sheen.sheen_color_texture.texture) {
            result.sheenColorMap = loadTexture(data, sheen.sheen_color_texture, true);
        }
        if (m_options.loadTextures && sheen.sheen_roughness_texture.texture) {
            result.sheenRoughnessMap = loadTexture(data, sheen.sheen_roughness_texture, false);
        }
    }
    if (material->has_specular) {
        const auto& specular = material->specular;
        result.hasSpecular = true;
        result.specularFactor = specular.specular_factor;
        result.specularColorFactor = glm::vec3(specular.specular_color_factor[0],
                                               specular.specular_color_factor[1],
                                               specular.specular_color_factor[2]);
        if (m_options.loadTextures && specular.specular_texture.texture) {
            result.specularMap = loadTexture(data, specular.specular_texture, false);
        }
        if (m_options.loadTextures && specular.specular_color_texture.texture) {
            result.specularColorMap = loadTexture(data, specular.specular_color_texture, true);
        }
    }

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

    // Each texture's uv set and KHR_texture_transform, for those that
    // differ from TEXCOORD_0 untransformed.
    const auto& pbr = material->pbr_metallic_roughness;
    const std::pair<const char*, const cgltf_texture_view*> views[] = {
        {"albedoMap", &pbr.base_color_texture},
        {"metallicRoughnessMap", &pbr.metallic_roughness_texture},
        {"normalMap", &material->normal_texture},
        {"aoMap", &material->occlusion_texture},
        {"emissiveMap", &material->emissive_texture},
        {"clearcoatMap", &material->clearcoat.clearcoat_texture},
        {"clearcoatRoughnessMap", &material->clearcoat.clearcoat_roughness_texture},
        {"transmissionMap", &material->transmission.transmission_texture},
        {"thicknessMap", &material->volume.thickness_texture},
        {"sheenColorMap", &material->sheen.sheen_color_texture},
        {"sheenRoughnessMap", &material->sheen.sheen_roughness_texture},
        {"specularMap", &material->specular.specular_texture},
        {"specularColorMap", &material->specular.specular_color_texture},
        {"iridescenceMap", &material->iridescence.iridescence_texture},
        {"iridescenceThicknessMap", &material->iridescence.iridescence_thickness_texture},
        {"anisotropyMap", &material->anisotropy.anisotropy_texture},
    };
    for (const auto& [slot, view] : views) {
        if (!view->texture) continue;
        const auto uv = textureUvTransform(*view);
        if (uv != kIdentityUvTransform) result.uvTransforms.emplace_back(slot, uv);
    }

    result.unlit = material->unlit != 0;
    if (material->has_anisotropy) {
        const auto& anisotropy = material->anisotropy;
        result.hasAnisotropy = true;
        result.anisotropyStrength = anisotropy.anisotropy_strength;
        result.anisotropyRotation = anisotropy.anisotropy_rotation;
        if (m_options.loadTextures && anisotropy.anisotropy_texture.texture) {
            result.anisotropyMap = loadTexture(data, anisotropy.anisotropy_texture, false);
        }
    }
    result.alphaMode = extractAlphaMode(material);
    result.alphaCutoff = material->alpha_cutoff;
    result.doubleSided = material->double_sided;
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
                       geometry.tangents[i], geometry.uvs1[i]};
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
    g.uvs1.assign(n, glm::vec2(0.0f));
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
        vertices[i] = {g.positions[i], g.colors[i], g.uvs[i], g.normals[i], g.tangents[i], glm::vec2(0.0f)};
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
    // KHR_texture_basisu: the KTX2 image in place of the texture's own.
    const cgltf_texture* source = textureView.texture;
    const bool basisu = source && source->has_basisu && source->basisu_image;
    const cgltf_image* image = !source ? nullptr : basisu ? source->basisu_image : source->image;
    if (!image) {
        return GPUTexture{};
    }
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
    // A made texture gets a sampler and goes into the cache.
    auto finish = [&](GPUTexture texture) {
        texture.sampler = GPUResourceFactory::createSampler(
            m_device.getLogicalDevice(),
            VK_FILTER_LINEAR, VK_FILTER_LINEAR,
            VK_SAMPLER_ADDRESS_MODE_REPEAT,
            16.0f,  // max anisotropy
            VK_SAMPLER_MIPMAP_MODE_LINEAR,
            static_cast<float>(texture.mipLevels)
        );
        texture.exists = true;
        m_textureCache[fullCacheKey] = texture;
        return sampled(texture);
    };

    RgbaImage decoded;
    if (basisu) {
        std::vector<uint8_t> fileBytes;
        const uint8_t* bytes = nullptr;
        size_t size = 0;
        if (isEmbedded) {
            bytes = cgltf_buffer_view_data(image->buffer_view);
            size = image->buffer_view->size;
        } else {
            std::ifstream file(cacheKey, std::ios::binary);
            fileBytes.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
            bytes = fileBytes.data();
            size = fileBytes.size();
        }
        KtxImage ktx = bytes && size ? decodeKtx2(bytes, size, srgb, m_device.getEnabledFeatures().textureCompressionBC,
                                                  maxSize)
                                     : KtxImage{};
        if (!bytes || !size) ktx.error = "no data";
        if (!ktx.error.empty()) {
            std::cerr << "[GltfSceneLoader] KTX2 texture " << cacheKey << ": " << ktx.error << std::endl;
            if (!source->image) return GPUTexture{};
            // The texture's own image, as a texture without the extension.
            cgltf_texture plain = *source;
            plain.has_basisu = 0;
            plain.basisu_image = nullptr;
            cgltf_texture_view view = textureView;
            view.texture = &plain;
            return loadTexture(nullptr, view, srgb, normalMap);
        }
        if (!ktx.levels.empty()) {
            return finish(GPUResourceFactory::createTexture2DWithLevels(
                m_device.getAllocator().getHandle(), m_device.getLogicalDevice(), m_device.getGraphicsQueue(),
                m_device.getCommandPool(), ktx.width, ktx.height, ktx.format, ktx.levels));
        }
        decoded = std::move(ktx.single);
    }

    // Load image with stb_image
    int width = 0, height = 0, channels = 0;
    stbi_uc* pixels = nullptr;

    if (basisu) {
        // Decoded from the KTX2 image above.
    } else if (isEmbedded) {
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

    if (pixels) {
        decoded.width = static_cast<uint32_t>(width);
        decoded.height = static_cast<uint32_t>(height);
        decoded.texels.assign(pixels, pixels + static_cast<size_t>(width) * height * 4);
        stbi_image_free(pixels);
    }
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

    return finish(texture);
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
    mesh.lods = primitive.lods;
    // Standard-vertex meshes are made with acceleration structure input usage
    // on a device with ray queries (rayTracingInputUsage); skinned ones are not.
    mesh.rayTracingInput = m_device.hasRayQuery() && mesh.vertexStride == sizeof(StandardVertex);

    // Add MaterialComponentV5
    scene->addComponent<MaterialComponentV5>(entity) = makeMaterial(primitive);

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
        v.texCoord1 = geometry.uvs1[i];

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
    scene->addComponent<MaterialComponentV5>(entity) = makeMaterial(primitive);

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
