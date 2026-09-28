//
// RayTracingScene.cpp
//

#include "Vulkan/RayTracingScene.h"

#include "Core/Logger.h"
#include "ECS/Core.h"
#include "ECS/RenderComponents.h"
#include "ECS/SkeletonComponents.h"
#include "ECS/Sprite2DComponents.h"
#include "Vulkan/VulkanDevice.h"
#include "Vulkan/VulkanMemoryAllocator.h"

#include <algorithm>
#include <cstring>
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

void RayTracingScene::recordBuild(VkCommandBuffer cmd, uint32_t frameIndex, entt::registry& registry) {
    releaseExpired();
    TopLevel& top = m_topLevels[frameIndex % m_framesInFlight];

    // The static, opaque shadow casters.
    std::vector<VkAccelerationStructureInstanceKHR> instances;
    auto view = registry.view<MeshComponent, MaterialComponentV5, RenderableTagComponent, ECS::TransformComponent>();
    for (auto entity : view) {
        const auto& mesh = view.get<MeshComponent>(entity);
        const auto& tag = view.get<RenderableTagComponent>(entity);
        if (!mesh.rayTracingInput || !mesh.isValid() || !tag.shouldRender() || !tag.castShadows) continue;
        // Opaque materials only: alpha-tested ones need their texture to cut
        // the right holes, which a pipeline can leave to its shadow maps.
        if (!view.get<MaterialComponentV5>(entity).isOpaque()) continue;
        if (registry.all_of<SkeletonComponent>(entity) || registry.all_of<Sprite2DComponent>(entity)) continue;

        const BottomLevel* bottom = bottomLevelFor(mesh);
        const glm::mat4& world = view.get<ECS::TransformComponent>(entity).worldMatrix;
        VkAccelerationStructureInstanceKHR instance{};
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 4; ++c) instance.transform.matrix[r][c] = world[c][r];
        instance.instanceCustomIndex = static_cast<uint32_t>(entity) & 0xFFFFFF;
        instance.mask = 0xFF;
        instance.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR |
                         VK_GEOMETRY_INSTANCE_FORCE_OPAQUE_BIT_KHR;
        instance.accelerationStructureReference = bottom->structure.address;
        instances.push_back(instance);
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

} // namespace Shoonyakasha
