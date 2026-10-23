//
// RayTracingScene.cpp
//

#include "Vulkan/RayTracingScene.h"
#include "Vulkan/SceneTextureTable.h"

#include "Core/Logger.h"
#include "ECS/Core.h"
#include "ECS/RenderComponents.h"
#include "ECS/SkeletonComponents.h"
#include "ECS/MorphComponents.h"
#include "ECS/Sprite2DComponents.h"
#include "Vulkan/VertexTypes.h"
#include "Vulkan/VulkanDevice.h"
#include "Vulkan/VulkanMemoryAllocator.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <stdexcept>

namespace Shoonyakasha {

namespace {

template <typename T>
T deviceFunction(VkDevice device, const char* name) {
    auto f = reinterpret_cast<T>(vkGetDeviceProcAddr(device, name));
    if (!f) throw std::runtime_error(std::string("RayTracingScene: missing ") + name);
    return f;
}

VkDeviceSize alignUp(VkDeviceSize value, VkDeviceSize alignment) {
    return alignment ? (value + alignment - 1) / alignment * alignment : value;
}

// The skinned vertex the glTF loader builds (GltfSceneLoader.cpp,
// SkinnedVertex), which skin_vertices.comp reads.
constexpr uint32_t kSkinnedVertexStride = 88;

// skin_vertices.comp's push constants.
struct SkinPush {
    glm::uvec2 skinned;
    glm::uvec2 bones;
    glm::uvec2 posed;
    uint32_t vertexCount;
};

glm::uvec2 splitAddress(VkDeviceAddress address) {
    return {static_cast<uint32_t>(address), static_cast<uint32_t>(address >> 32)};
}

constexpr VkBuildAccelerationStructureFlagsKHR kPosedBuildFlags =
    VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR | VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR;

// The triangles of a posed mesh: its standard vertices, the skinned mesh's indices.
VkAccelerationStructureGeometryKHR posedGeometry(VkDeviceAddress vertices, VkDeviceAddress indices,
                                                 const MeshComponent& mesh, uint32_t& primitives) {
    VkAccelerationStructureGeometryTrianglesDataKHR triangles{};
    triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
    triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
    triangles.vertexData.deviceAddress = vertices;
    triangles.vertexStride = sizeof(Vertex);
    triangles.maxVertex = mesh.vertexCount > 0 ? mesh.vertexCount - 1 : 0;
    primitives = mesh.vertexCount / 3;
    if (mesh.hasIndices()) {
        triangles.indexType = mesh.indexType == IndexType::UInt16 ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32;
        triangles.indexData.deviceAddress = indices;
        primitives = mesh.indexCount / 3;
    } else {
        triangles.indexType = VK_INDEX_TYPE_NONE_KHR;
    }
    VkAccelerationStructureGeometryKHR geometry{};
    geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
    geometry.geometry.triangles = triangles;
    geometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
    return geometry;
}

} // namespace

RayTracingScene::RayTracingScene(VulkanDevice& device, uint32_t framesInFlight)
    : m_device(device), m_framesInFlight(std::max(framesInFlight, 1u)) {
    if (!device.hasRayQuery()) {
        throw std::runtime_error("RayTracingScene needs a device with ray queries");
    }
    m_logger = new Logger("ray_tracing.log");
    const VkDevice d = device.getLogicalDevice();
    m_create        = deviceFunction<PFN_vkCreateAccelerationStructureKHR>(d, "vkCreateAccelerationStructureKHR");
    m_destroy       = deviceFunction<PFN_vkDestroyAccelerationStructureKHR>(d, "vkDestroyAccelerationStructureKHR");
    m_buildSizes    = deviceFunction<PFN_vkGetAccelerationStructureBuildSizesKHR>(d, "vkGetAccelerationStructureBuildSizesKHR");
    m_cmdBuild      = deviceFunction<PFN_vkCmdBuildAccelerationStructuresKHR>(d, "vkCmdBuildAccelerationStructuresKHR");
    m_deviceAddress = deviceFunction<PFN_vkGetAccelerationStructureDeviceAddressKHR>(d, "vkGetAccelerationStructureDeviceAddressKHR");

    VkPhysicalDeviceAccelerationStructurePropertiesKHR properties{};
    properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR;
    VkPhysicalDeviceProperties2 properties2{};
    properties2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    properties2.pNext = &properties;
    vkGetPhysicalDeviceProperties2(device.getPhysicalDevice(), &properties2);
    m_scratchAlignment = std::max(properties.minAccelerationStructureScratchOffsetAlignment, 1u);

    m_topLevels.resize(m_framesInFlight);
    m_logger->log(LogLevel::Info, "Ray tracing scene created, %u frames in flight", m_framesInFlight);
}

RayTracingScene::~RayTracingScene() {
    vkDeviceWaitIdle(m_device.getLogicalDevice());
    const VmaAllocator allocator = m_device.getAllocator().getHandle();
    for (auto& top : m_topLevels) {
        destroyStructure(top.structure);
        if (top.mapped) vmaUnmapMemory(allocator, top.instances.allocation);
        if (top.instances.isValid()) vmaDestroyBuffer(allocator, top.instances.buffer, top.instances.allocation);
        if (top.scratch.isValid()) vmaDestroyBuffer(allocator, top.scratch.buffer, top.scratch.allocation);
    }
    for (auto& [key, bottom] : m_bottomLevels) destroyStructure(bottom.structure);
    for (auto& [structure, remaining] : m_retiring) destroyStructure(structure);
    for (auto& [entity, posed] : m_posed) {
        for (auto& frame : posed.frames) destroyPosed(frame);
    }
    for (auto& [posed, remaining] : m_retiringPosed) {
        for (auto& frame : posed.frames) destroyPosed(frame);
    }
    const VkDevice d = m_device.getLogicalDevice();
    if (m_skinPipeline != VK_NULL_HANDLE) vkDestroyPipeline(d, m_skinPipeline, nullptr);
    if (m_skinLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(d, m_skinLayout, nullptr);
    delete m_logger;
}

GPUBuffer RayTracingScene::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, bool hostVisible,
                                        VkDeviceSize alignment) {
    VkBufferCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    info.size = std::max<VkDeviceSize>(size, 16);
    info.usage = usage | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VmaAllocationCreateInfo alloc{};
    alloc.usage = hostVisible ? VMA_MEMORY_USAGE_CPU_TO_GPU : VMA_MEMORY_USAGE_GPU_ONLY;
    GPUBuffer buffer;
    buffer.size = info.size;
    const VkResult result = alignment
        ? vmaCreateBufferWithAlignment(m_device.getAllocator().getHandle(), &info, &alloc, alignment,
                                       &buffer.buffer, &buffer.allocation, nullptr)
        : vmaCreateBuffer(m_device.getAllocator().getHandle(), &info, &alloc,
                          &buffer.buffer, &buffer.allocation, nullptr);
    if (result != VK_SUCCESS) throw std::runtime_error("RayTracingScene: buffer allocation failed");
    return buffer;
}

VkDeviceAddress RayTracingScene::addressOf(VkBuffer buffer) const {
    VkBufferDeviceAddressInfo info{};
    info.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    info.buffer = buffer;
    return vkGetBufferDeviceAddress(m_device.getLogicalDevice(), &info);
}

RayTracingScene::AccelerationStructure RayTracingScene::createStructure(VkAccelerationStructureTypeKHR type,
                                                                        VkDeviceSize size) {
    AccelerationStructure s;
    s.buffer = createBuffer(size, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR, false);
    VkAccelerationStructureCreateInfoKHR info{};
    info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
    info.buffer = s.buffer.buffer;
    info.size = size;
    info.type = type;
    if (m_create(m_device.getLogicalDevice(), &info, nullptr, &s.handle) != VK_SUCCESS) {
        throw std::runtime_error("RayTracingScene: vkCreateAccelerationStructureKHR failed");
    }
    VkAccelerationStructureDeviceAddressInfoKHR addressInfo{};
    addressInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
    addressInfo.accelerationStructure = s.handle;
    s.address = m_deviceAddress(m_device.getLogicalDevice(), &addressInfo);
    return s;
}

void RayTracingScene::destroyStructure(AccelerationStructure& s) {
    if (s.handle != VK_NULL_HANDLE) m_destroy(m_device.getLogicalDevice(), s.handle, nullptr);
    if (s.buffer.isValid()) {
        vmaDestroyBuffer(m_device.getAllocator().getHandle(), s.buffer.buffer, s.buffer.allocation);
    }
    s = AccelerationStructure{};
}

const RayTracingScene::BottomLevel* RayTracingScene::bottomLevelFor(const MeshComponent& mesh) {
    const MeshKey key{mesh.vertexHandle(), mesh.hasIndices() ? mesh.indexHandle() : VK_NULL_HANDLE,
                      mesh.vertexCount, mesh.hasIndices() ? mesh.indexCount : 0};
    if (auto it = m_bottomLevels.find(key); it != m_bottomLevels.end()) return &it->second;

    VkAccelerationStructureGeometryTrianglesDataKHR triangles{};
    triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
    triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;   // position first in every vertex format
    triangles.vertexData.deviceAddress = addressOf(key.vertices);
    triangles.vertexStride = mesh.vertexStride;
    triangles.maxVertex = mesh.vertexCount > 0 ? mesh.vertexCount - 1 : 0;
    uint32_t primitives = mesh.vertexCount / 3;
    if (key.indices != VK_NULL_HANDLE) {
        triangles.indexType = mesh.indexType == IndexType::UInt16 ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32;
        triangles.indexData.deviceAddress = addressOf(key.indices);
        primitives = mesh.indexCount / 3;
    } else {
        triangles.indexType = VK_INDEX_TYPE_NONE_KHR;
    }

    VkAccelerationStructureGeometryKHR geometry{};
    geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
    geometry.geometry.triangles = triangles;
    geometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;

    VkAccelerationStructureBuildGeometryInfoKHR build{};
    build.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    build.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    build.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    build.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    build.geometryCount = 1;
    build.pGeometries = &geometry;

    VkAccelerationStructureBuildSizesInfoKHR sizes{};
    sizes.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
    m_buildSizes(m_device.getLogicalDevice(), VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
                 &build, &primitives, &sizes);

    BottomLevel bottom;
    bottom.structure = createStructure(VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
                                       sizes.accelerationStructureSize);
    bottom.vertices = mesh.vertexBuffer;
    bottom.indices = mesh.indexBuffer;
    GPUBuffer scratch = createBuffer(sizes.buildScratchSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, false,
                                     m_scratchAlignment);
    build.dstAccelerationStructure = bottom.structure.handle;
    build.scratchData.deviceAddress = alignUp(addressOf(scratch.buffer), m_scratchAlignment);

    VkAccelerationStructureBuildRangeInfoKHR range{};
    range.primitiveCount = primitives;
    const VkAccelerationStructureBuildRangeInfoKHR* ranges = &range;
    VkCommandBuffer cmd = m_device.beginSingleTimeCommands();
    m_cmdBuild(cmd, 1, &build, &ranges);
    m_device.endSingleTimeCommands(cmd);   // waits: the scratch buffer can go
    vmaDestroyBuffer(m_device.getAllocator().getHandle(), scratch.buffer, scratch.allocation);

    return &m_bottomLevels.emplace(key, std::move(bottom)).first->second;
}

bool RayTracingScene::skinningReady() {
    if (m_skinPipeline != VK_NULL_HANDLE) return true;
    if (m_skinningTried) return false;
    m_skinningTried = true;

    std::ifstream file(m_skinningShaderPath, std::ios::binary | std::ios::ate);
    if (m_skinningShaderPath.empty() || !file) {
        m_logger->log(LogLevel::Warning, "No skinning shader ('%s'): skinned meshes stay out of the acceleration "
                      "structure", m_skinningShaderPath.c_str());
        return false;
    }
    const std::streamsize size = file.tellg();
    std::vector<uint32_t> code(static_cast<size_t>(size) / 4);
    file.seekg(0);
    file.read(reinterpret_cast<char*>(code.data()), static_cast<std::streamsize>(code.size() * 4));

    const VkDevice d = m_device.getLogicalDevice();
    VkShaderModuleCreateInfo moduleInfo{};
    moduleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    moduleInfo.codeSize = code.size() * 4;
    moduleInfo.pCode = code.data();
    VkShaderModule module = VK_NULL_HANDLE;
    if (code.empty() || vkCreateShaderModule(d, &moduleInfo, nullptr, &module) != VK_SUCCESS) {
        m_logger->log(LogLevel::Warning, "Skinning shader '%s' could not be loaded: skinned meshes stay out of the "
                      "acceleration structure", m_skinningShaderPath.c_str());
        return false;
    }

    VkPushConstantRange range{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(SkinPush)};
    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &range;
    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    pipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    pipelineInfo.stage.module = module;
    pipelineInfo.stage.pName = "main";
    bool ok = vkCreatePipelineLayout(d, &layoutInfo, nullptr, &m_skinLayout) == VK_SUCCESS;
    if (ok) {
        pipelineInfo.layout = m_skinLayout;
        ok = vkCreateComputePipelines(d, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_skinPipeline) == VK_SUCCESS;
    }
    vkDestroyShaderModule(d, module, nullptr);
    if (!ok) {
        m_logger->log(LogLevel::Warning, "Skinning pipeline creation failed: skinned meshes stay out of the "
                      "acceleration structure");
        if (m_skinLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(d, m_skinLayout, nullptr);
        m_skinLayout = VK_NULL_HANDLE;
        m_skinPipeline = VK_NULL_HANDLE;
        return false;
    }
    m_logger->log(LogLevel::Info, "Skinning shader loaded: %s", m_skinningShaderPath.c_str());
    return true;
}

void RayTracingScene::destroyPosed(PosedMesh& posed) {
    const VmaAllocator allocator = m_device.getAllocator().getHandle();
    destroyStructure(posed.structure);
    if (posed.bonesMapped) vmaUnmapMemory(allocator, posed.bones.allocation);
    if (posed.bones.isValid()) vmaDestroyBuffer(allocator, posed.bones.buffer, posed.bones.allocation);
    if (posed.vertices.isValid()) vmaDestroyBuffer(allocator, posed.vertices.buffer, posed.vertices.allocation);
    if (posed.scratch.isValid()) vmaDestroyBuffer(allocator, posed.scratch.buffer, posed.scratch.allocation);
    posed = PosedMesh{};
}

RayTracingScene::PosedMesh& RayTracingScene::posedFor(entt::entity entity, uint32_t frame, const MeshComponent& mesh,
                                                      uint32_t jointCount, VkBuffer source) {
    PosedEntity& entry = m_posed[entity];
    if (entry.frames.size() != m_framesInFlight) entry.frames.resize(m_framesInFlight);
    entry.lastBuild = m_buildCount;
    PosedMesh& posed = entry.frames[frame];
    if (posed.source == source && posed.vertexCount == mesh.vertexCount && posed.jointCount == jointCount) {
        return posed;
    }

    // This frame's fence has passed, so its copy is idle.
    destroyPosed(posed);
    posed.source = source;
    posed.vertexCount = mesh.vertexCount;
    posed.jointCount = jointCount;
    posed.bones = createBuffer(sizeof(glm::mat4) * std::max(jointCount, 1u), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true);
    vmaMapMemory(m_device.getAllocator().getHandle(), posed.bones.allocation, &posed.bonesMapped);
    posed.vertices = createBuffer(sizeof(Vertex) * std::max(mesh.vertexCount, 1u),
                                  VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                                      VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
                                  false);

    uint32_t primitives = 0;
    const VkAccelerationStructureGeometryKHR geometry =
        posedGeometry(addressOf(posed.vertices.buffer), mesh.hasIndices() ? addressOf(mesh.indexHandle()) : 0, mesh,
                      primitives);
    VkAccelerationStructureBuildGeometryInfoKHR build{};
    build.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    build.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    build.flags = kPosedBuildFlags;
    build.geometryCount = 1;
    build.pGeometries = &geometry;
    VkAccelerationStructureBuildSizesInfoKHR sizes{};
    sizes.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
    m_buildSizes(m_device.getLogicalDevice(), VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &build, &primitives,
                 &sizes);
    posed.structure = createStructure(VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR, sizes.accelerationStructureSize);
    posed.scratch = createBuffer(std::max(sizes.buildScratchSize, sizes.updateScratchSize),
                                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, false, m_scratchAlignment);
    posed.built = false;
    return posed;
}

void RayTracingScene::recordPosedBuilds(VkCommandBuffer cmd, const std::vector<PosedMesh*>& posed,
                                        const std::vector<const MeshComponent*>& meshes) {
    if (posed.empty()) return;

    // Pose: one thread per vertex for skinned meshes; the others' vertices
    // are copied as they are.
    bool skinBound = false;
    for (const PosedMesh* p : posed) {
        if (p->jointCount == 0) {
            const VkBufferCopy region{0, 0, sizeof(Vertex) * static_cast<VkDeviceSize>(p->vertexCount)};
            vkCmdCopyBuffer(cmd, p->source, p->vertices.buffer, 1, &region);
            continue;
        }
        if (!skinBound) {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_skinPipeline);
            skinBound = true;
        }
        const SkinPush push{splitAddress(addressOf(p->source)), splitAddress(addressOf(p->bones.buffer)),
                            splitAddress(addressOf(p->vertices.buffer)), p->vertexCount};
        vkCmdPushConstants(cmd, m_skinLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push), &push);
        vkCmdDispatch(cmd, (p->vertexCount + 63) / 64, 1, 1);
    }
    VkMemoryBarrier posedWritten{};
    posedWritten.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    posedWritten.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
    posedWritten.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, 0, 1, &posedWritten, 0, nullptr, 0,
                         nullptr);

    // Build each structure the first time, refit it after.
    std::vector<VkAccelerationStructureGeometryKHR> geometries(posed.size());
    std::vector<VkAccelerationStructureBuildGeometryInfoKHR> builds(posed.size());
    std::vector<VkAccelerationStructureBuildRangeInfoKHR> ranges(posed.size());
    std::vector<const VkAccelerationStructureBuildRangeInfoKHR*> rangePointers(posed.size());
    for (size_t i = 0; i < posed.size(); ++i) {
        const MeshComponent& mesh = *meshes[i];
        uint32_t primitives = 0;
        geometries[i] = posedGeometry(addressOf(posed[i]->vertices.buffer),
                                      mesh.hasIndices() ? addressOf(mesh.indexHandle()) : 0, mesh, primitives);
        VkAccelerationStructureBuildGeometryInfoKHR& build = builds[i];
        build.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
        build.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
        build.flags = kPosedBuildFlags;
        build.mode = posed[i]->built ? VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR
                                     : VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
        build.srcAccelerationStructure = posed[i]->built ? posed[i]->structure.handle : VK_NULL_HANDLE;
        build.dstAccelerationStructure = posed[i]->structure.handle;
        build.geometryCount = 1;
        build.pGeometries = &geometries[i];
        build.scratchData.deviceAddress = alignUp(addressOf(posed[i]->scratch.buffer), m_scratchAlignment);
        ranges[i] = VkAccelerationStructureBuildRangeInfoKHR{};
        ranges[i].primitiveCount = primitives;
        rangePointers[i] = &ranges[i];
        posed[i]->built = true;
    }
    m_cmdBuild(cmd, static_cast<uint32_t>(builds.size()), builds.data(), rangePointers.data());
}

void RayTracingScene::releaseExpired() {
    // Retire structures whose meshes are gone; a frame in flight may still
    // trace against them, so they live until every frame has been rebuilt.
    for (auto it = m_bottomLevels.begin(); it != m_bottomLevels.end();) {
        const bool indexed = it->first.indices != VK_NULL_HANDLE;
        if (it->second.vertices.expired() || (indexed && it->second.indices.expired())) {
            m_retiring.emplace_back(it->second.structure, m_framesInFlight + 1);
            it = m_bottomLevels.erase(it);
        } else {
            ++it;
        }
    }
    for (auto it = m_retiring.begin(); it != m_retiring.end();) {
        if (--it->second == 0) {
            destroyStructure(it->first);
            it = m_retiring.erase(it);
        } else {
            ++it;
        }
    }
    for (auto it = m_retiringPosed.begin(); it != m_retiringPosed.end();) {
        if (--it->second == 0) {
            for (auto& frame : it->first.frames) destroyPosed(frame);
            it = m_retiringPosed.erase(it);
        } else {
            ++it;
        }
    }
}

void RayTracingScene::ensureTopLevelCapacity(TopLevel& top, uint32_t count) {
    if (top.structure.handle != VK_NULL_HANDLE && count <= top.capacity) return;
    const uint32_t capacity = std::max({count, top.capacity * 2, 64u});
    const VmaAllocator allocator = m_device.getAllocator().getHandle();
    // This frame's fence was waited on, so its old structure is idle.
    destroyStructure(top.structure);
    if (top.mapped) vmaUnmapMemory(allocator, top.instances.allocation);
    if (top.instances.isValid()) vmaDestroyBuffer(allocator, top.instances.buffer, top.instances.allocation);
    if (top.scratch.isValid()) vmaDestroyBuffer(allocator, top.scratch.buffer, top.scratch.allocation);

    top.instances = createBuffer(sizeof(VkAccelerationStructureInstanceKHR) * capacity,
                                 VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR, true);
    vmaMapMemory(allocator, top.instances.allocation, &top.mapped);

    VkAccelerationStructureGeometryKHR geometry{};
    geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    geometry.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    VkAccelerationStructureBuildGeometryInfoKHR build{};
    build.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    build.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    build.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    build.geometryCount = 1;
    build.pGeometries = &geometry;
    VkAccelerationStructureBuildSizesInfoKHR sizes{};
    sizes.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
    m_buildSizes(m_device.getLogicalDevice(), VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
                 &build, &capacity, &sizes);
    top.structure = createStructure(VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR, sizes.accelerationStructureSize);
    top.scratch = createBuffer(sizes.buildScratchSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, false, m_scratchAlignment);
    top.capacity = capacity;
    m_logger->log(LogLevel::Info, "Top-level structure sized for %u instances", capacity);
}

PackedRayInstance RayTracingScene::packInstance(const MaterialComponentV5& material, uint32_t entity,
                                                const MeshComponent& mesh, VkDeviceAddress vertices,
                                                VkDeviceAddress indices, const glm::uvec4& textureSlots,
                                                const glm::uvec2& filmSlots) {
    constexpr uint32_t kColorOffset = 12, kUvOffset = 24, kNormalOffset = 32;
    PackedRayInstance row;
    row.geometry = glm::uvec4(static_cast<uint32_t>(vertices), static_cast<uint32_t>(vertices >> 32),
                              static_cast<uint32_t>(indices), static_cast<uint32_t>(indices >> 32));
    const uint32_t indexType = !mesh.hasIndices() ? PackedRayInstance::kIndexNone
                             : mesh.indexType == IndexType::UInt16 ? 0u : 1u;
    row.layout = glm::uvec4(mesh.vertexStride, indexType, kNormalOffset | (kUvOffset << 8) | (kColorOffset << 16),
                            material.doubleSided ? PackedRayInstance::kDoubleSided : 0u);
    row.baseColor = material.getParam<glm::vec4>("baseColorFactor", glm::vec4(1.0f));
    row.emissive = glm::vec4(material.getParam<glm::vec3>("emissiveFactor", glm::vec3(0.0f)), 0.0f);
    float entityBits;
    std::memcpy(&entityBits, &entity, sizeof(entityBits));
    row.surface = glm::vec4(material.getParam<float>("metallicFactor", 0.0f),
                            material.getParam<float>("roughnessFactor", 0.5f), material.alphaCutoff, entityBits);
    row.textures = textureSlots;
    row.optics = glm::vec4(material.getParam<float>("transmissionFactor", 0.0f), material.getParam<float>("ior", 1.5f),
                           material.getParam<float>("iridescenceFactor", 0.0f),
                           material.getParam<float>("iridescenceIor", 1.3f));
    float slotBits[2];
    std::memcpy(slotBits, &filmSlots, sizeof(slotBits));
    row.film = glm::vec4(material.getParam<float>("iridescenceThicknessMinimum", 100.0f),
                         material.getParam<float>("iridescenceThicknessMaximum", 400.0f), slotBits[0], slotBits[1]);
    return row;
}

void RayTracingScene::recordBuild(VkCommandBuffer cmd, uint32_t frameIndex, entt::registry& registry,
                                  const SceneTextureTable* textures, bool alphaTested) {
    static_assert(SceneTextureTable::kNoTexture == PackedRayInstance::kNoTexture);
    releaseExpired();
    ++m_buildCount;
    const uint32_t frame = frameIndex % m_framesInFlight;
    TopLevel& top = m_topLevels[frame];
    top.rows.clear();
    auto slotOf = [&](const MaterialComponentV5& material, const char* name) -> uint32_t {
        return textures ? textures->slotOf(material.getTexture(name)) : PackedRayInstance::kNoTexture;
    };

    // One instance and its row. `mesh` describes the vertices at `vertices`.
    std::vector<VkAccelerationStructureInstanceKHR> instances;
    auto addInstance = [&](entt::entity entity, const MaterialComponentV5& material, const MeshComponent& mesh,
                           VkDeviceAddress vertices, VkDeviceAddress structure, const glm::mat4& world,
                           bool masked) {
        glm::uvec4 slots;
        for (int t = 0; t < 4; ++t) slots[t] = slotOf(material, kTextureNames[t]);
        glm::uvec2 filmSlots;
        for (int t = 0; t < 2; ++t) filmSlots[t] = slotOf(material, kFilmTextureNames[t]);
        top.rows.push_back(packInstance(material, static_cast<uint32_t>(entity), mesh, vertices,
                                        mesh.hasIndices() ? addressOf(mesh.indexHandle()) : 0, slots, filmSlots));
        VkAccelerationStructureInstanceKHR instance{};
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 4; ++c) instance.transform.matrix[r][c] = world[c][r];
        instance.instanceCustomIndex = static_cast<uint32_t>(entity) & 0xFFFFFF;
        instance.mask = 0xFF;
        instance.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR |
                         (masked ? VK_GEOMETRY_INSTANCE_FORCE_NO_OPAQUE_BIT_KHR
                                 : VK_GEOMETRY_INSTANCE_FORCE_OPAQUE_BIT_KHR);
        instance.accelerationStructureReference = structure;
        instances.push_back(instance);
    };
    // Shadow casters with an opaque material that transmits nothing, or an
    // alpha-tested one when asked: those need their texture to cut the right
    // holes, which only shaders that test the candidates do.
    auto caster = [&](const MeshComponent& mesh, const RenderableTagComponent& tag, const MaterialComponentV5& material,
                      bool& masked) {
        masked = alphaTested && material.isMasked();
        const bool transmissive = material.getParam<float>("transmissionFactor", 0.0f) > 0.0f;
        return mesh.isValid() && tag.shouldRender() && tag.castShadows && !transmissive &&
               (material.isOpaque() || masked);
    };

    // The static casters.
    auto view = registry.view<MeshComponent, MaterialComponentV5, RenderableTagComponent, ECS::TransformComponent>();
    for (auto entity : view) {
        const auto& mesh = view.get<MeshComponent>(entity);
        const auto& material = view.get<MaterialComponentV5>(entity);
        bool masked = false;
        if (!mesh.rayTracingInput || !caster(mesh, view.get<RenderableTagComponent>(entity), material, masked)) continue;
        if (registry.any_of<SkeletonComponent, MorphComponent, Sprite2DComponent>(entity)) continue;
        addInstance(entity, material, mesh, addressOf(mesh.vertexHandle()), bottomLevelFor(mesh)->structure.address,
                    view.get<ECS::TransformComponent>(entity).worldMatrix, masked);
    }

    // The skinned casters, posed for this frame from their vertices (their
    // morph targets blended in, when they have them), and the casters with
    // morph targets alone, their blended vertices copied.
    auto blendedOf = [&](entt::entity entity) {
        const auto* morph = registry.try_get<MorphComponent>(entity);
        return morph ? morph->bufferFor(frameIndex) : VK_NULL_HANDLE;
    };
    std::vector<entt::entity> skinned;
    std::vector<PosedMesh*> posed;
    std::vector<const MeshComponent*> posedMeshes;
    std::vector<bool> posedMasked;
    auto skinnedView = registry.view<MeshComponent, MaterialComponentV5, RenderableTagComponent,
                                     ECS::TransformComponent, SkeletonComponent>();
    for (auto entity : skinnedView) {
        const auto& mesh = skinnedView.get<MeshComponent>(entity);
        const auto& skeleton = skinnedView.get<SkeletonComponent>(entity);
        bool masked = false;
        if (!caster(mesh, skinnedView.get<RenderableTagComponent>(entity), skinnedView.get<MaterialComponentV5>(entity),
                    masked)) {
            continue;
        }
        if (mesh.vertexStride != kSkinnedVertexStride || mesh.vertexCount == 0 || skeleton.boneMatrices.empty() ||
            registry.all_of<Sprite2DComponent>(entity)) {
            continue;
        }
        if (!skinningReady()) break;
        const uint32_t joints = static_cast<uint32_t>(skeleton.boneMatrices.size());
        const VkBuffer blended = blendedOf(entity);
        PosedMesh& p = posedFor(entity, frame, mesh, joints, blended ? blended : mesh.vertexHandle());
        std::memcpy(p.bonesMapped, skeleton.boneMatrices.data(), joints * sizeof(glm::mat4));
        vmaFlushAllocation(m_device.getAllocator().getHandle(), p.bones.allocation, 0, VK_WHOLE_SIZE);
        skinned.push_back(entity);
        posed.push_back(&p);
        posedMeshes.push_back(&mesh);
        posedMasked.push_back(masked);
    }
    auto morphView = registry.view<MeshComponent, MaterialComponentV5, RenderableTagComponent,
                                   ECS::TransformComponent, MorphComponent>();
    for (auto entity : morphView) {
        const auto& mesh = morphView.get<MeshComponent>(entity);
        bool masked = false;
        if (registry.any_of<SkeletonComponent, Sprite2DComponent>(entity) || !mesh.rayTracingInput ||
            mesh.vertexStride != sizeof(Vertex) ||
            !caster(mesh, morphView.get<RenderableTagComponent>(entity), morphView.get<MaterialComponentV5>(entity),
                    masked)) {
            continue;
        }
        const VkBuffer blended = blendedOf(entity);
        if (!blended) continue;
        skinned.push_back(entity);
        posed.push_back(&posedFor(entity, frame, mesh, 0, blended));
        posedMeshes.push_back(&mesh);
        posedMasked.push_back(masked);
    }
    recordPosedBuilds(cmd, posed, posedMeshes);
    for (size_t i = 0; i < posed.size(); ++i) {
        MeshComponent rowMesh = *posedMeshes[i];   // the posed vertices are standard ones
        rowMesh.vertexStride = sizeof(Vertex);
        addInstance(skinned[i], registry.get<MaterialComponentV5>(skinned[i]), rowMesh,
                    addressOf(posed[i]->vertices.buffer), posed[i]->structure.address,
                    registry.get<ECS::TransformComponent>(skinned[i]).worldMatrix, posedMasked[i]);
    }

    // Skinned entities no longer drawn: their copies may still be traced by
    // frames in flight.
    for (auto it = m_posed.begin(); it != m_posed.end();) {
        if (it->second.lastBuild != m_buildCount) {
            m_retiringPosed.emplace_back(std::move(it->second), m_framesInFlight + 1);
            it = m_posed.erase(it);
        } else {
            ++it;
        }
    }

    ensureTopLevelCapacity(top, static_cast<uint32_t>(instances.size()));
    if (!instances.empty()) {
        std::memcpy(top.mapped, instances.data(), instances.size() * sizeof(instances[0]));
        vmaFlushAllocation(m_device.getAllocator().getHandle(), top.instances.allocation, 0, VK_WHOLE_SIZE);
    }
    top.count = static_cast<uint32_t>(instances.size());

    VkAccelerationStructureGeometryKHR geometry{};
    geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    geometry.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    geometry.geometry.instances.data.deviceAddress = addressOf(top.instances.buffer);
    VkAccelerationStructureBuildGeometryInfoKHR build{};
    build.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    build.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    build.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    build.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    build.geometryCount = 1;
    build.pGeometries = &geometry;
    build.dstAccelerationStructure = top.structure.handle;
    build.scratchData.deviceAddress = alignUp(addressOf(top.scratch.buffer), m_scratchAlignment);

    // The last frame's trace of this structure finished before its fence;
    // this frame's host write of the instances is visible at submission.
    VkMemoryBarrier before{};
    before.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    before.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
    before.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR | VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                         VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, 0, 1, &before, 0, nullptr, 0, nullptr);

    VkAccelerationStructureBuildRangeInfoKHR range{};
    range.primitiveCount = top.count;
    const VkAccelerationStructureBuildRangeInfoKHR* ranges = &range;
    m_cmdBuild(cmd, 1, &build, &ranges);

    VkMemoryBarrier after{};
    after.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    after.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
    after.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0, 1, &after, 0, nullptr, 0, nullptr);
}

VkAccelerationStructureKHR RayTracingScene::topLevel(uint32_t frameIndex) const {
    return m_topLevels[frameIndex % m_framesInFlight].structure.handle;
}

uint32_t RayTracingScene::instanceCount(uint32_t frameIndex) const {
    return m_topLevels[frameIndex % m_framesInFlight].count;
}

const std::vector<PackedRayInstance>& RayTracingScene::instances(uint32_t frameIndex) const {
    return m_topLevels[frameIndex % m_framesInFlight].rows;
}

} // namespace Shoonyakasha
