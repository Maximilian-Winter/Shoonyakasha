//
// Compute.cpp - Context, Buffer and Kernel (include/Compute/Compute.h)
//

#include "Compute/Compute.h"

#include "Vulkan/VulkanComputePipeline.h"
#include "Vulkan/VulkanDevice.h"
#include "Vulkan/VulkanInstance.h"
#include "Vulkan/VulkanMemoryAllocator.h"

#include <spirv-reflect/spirv_reflect.h>
#include <vk_mem_alloc.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <functional>
#include <stdexcept>

namespace Shoonyakasha::Compute {

namespace detail {

/// Something a context frees on shutdown.
struct Resource {
    virtual ~Resource() = default;
    virtual void release() = 0;
};

struct ContextState {
    std::unique_ptr<VulkanInstance> ownedInstance;
    std::unique_ptr<VulkanDevice> ownedDevice;
    VulkanDevice* device = nullptr;

    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    bool alive = false;

    std::vector<std::weak_ptr<Resource>> resources;

    VkDevice vk() const { return device->getLogicalDevice(); }
    VmaAllocator allocator() const { return device->getAllocator().getHandle(); }

    void check() const {
        if (!alive) throw std::runtime_error("the compute context was shut down");
    }

    void track(const std::shared_ptr<Resource>& resource) {
        resources.erase(std::remove_if(resources.begin(), resources.end(),
                                       [](const std::weak_ptr<Resource>& r) { return r.expired(); }),
                        resources.end());
        resources.push_back(resource);
    }

    /// Records commands with `record`, submits them to the graphics queue
    /// and waits. A barrier before and after makes earlier GPU writes
    /// visible to the commands, and their writes visible to the host.
    void submit(const std::function<void(VkCommandBuffer)>& record) {
        check();
        if (vkResetCommandBuffer(cmd, 0) != VK_SUCCESS) {
            throw std::runtime_error("compute: vkResetCommandBuffer failed");
        }
        VkCommandBufferBeginInfo begin{};
        begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd, &begin);

        VkMemoryBarrier before{};
        before.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        before.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
        before.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0,
                             1, &before, 0, nullptr, 0, nullptr);

        record(cmd);

        VkMemoryBarrier after{};
        after.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        after.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
        after.dstAccessMask = VK_ACCESS_HOST_READ_BIT | VK_ACCESS_MEMORY_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                             VK_PIPELINE_STAGE_HOST_BIT | VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0,
                             1, &after, 0, nullptr, 0, nullptr);

        if (vkEndCommandBuffer(cmd) != VK_SUCCESS) {
            throw std::runtime_error("compute: vkEndCommandBuffer failed");
        }
        VkSubmitInfo info{};
        info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        info.commandBufferCount = 1;
        info.pCommandBuffers = &cmd;
        if (vkQueueSubmit(device->getGraphicsQueue(), 1, &info, fence) != VK_SUCCESS) {
            throw std::runtime_error("compute: vkQueueSubmit failed");
        }
        const VkResult waited = vkWaitForFences(vk(), 1, &fence, VK_TRUE, UINT64_MAX);
        vkResetFences(vk(), 1, &fence);
        if (waited != VK_SUCCESS) {
            throw std::runtime_error("compute: waiting for the GPU failed (device lost?)");
        }
    }

    void create(VulkanDevice& dev) {
        device = &dev;
        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        poolInfo.queueFamilyIndex = dev.getGraphicsQueueFamily();
        if (vkCreateCommandPool(vk(), &poolInfo, nullptr, &pool) != VK_SUCCESS) {
            throw std::runtime_error("compute: could not create a command pool");
        }
        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = pool;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = 1;
        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        if (vkAllocateCommandBuffers(vk(), &allocInfo, &cmd) != VK_SUCCESS ||
            vkCreateFence(vk(), &fenceInfo, nullptr, &fence) != VK_SUCCESS) {
            destroy();
            throw std::runtime_error("compute: could not create a command buffer and fence");
        }
        alive = true;
    }

    void destroy() {
        for (auto& weak : resources) {
            if (auto resource = weak.lock()) resource->release();
        }
        resources.clear();
        if (device) {
            if (fence != VK_NULL_HANDLE) vkDestroyFence(vk(), fence, nullptr);
            if (pool != VK_NULL_HANDLE) vkDestroyCommandPool(vk(), pool, nullptr);
        }
        fence = VK_NULL_HANDLE;
        pool = VK_NULL_HANDLE;
        cmd = VK_NULL_HANDLE;
        alive = false;
        if (ownedDevice) vkDeviceWaitIdle(ownedDevice->getLogicalDevice());
        ownedDevice.reset();
        ownedInstance.reset();
        device = nullptr;
    }
};

struct BufferState : Resource {
    std::shared_ptr<ContextState> context;
    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    size_t size = 0;

    void release() override {
        if (buffer != VK_NULL_HANDLE) vmaDestroyBuffer(context->allocator(), buffer, allocation);
        buffer = VK_NULL_HANDLE;
        allocation = VK_NULL_HANDLE;
    }
    ~BufferState() override {
        if (context->alive) release();
    }
};

struct KernelState : Resource {
    std::shared_ptr<ContextState> context;
    KernelInfo info;
    VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;
    std::unique_ptr<VulkanComputePipeline> pipeline;

    void release() override {
        pipeline.reset();
        if (setLayout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(context->vk(), setLayout, nullptr);
        setLayout = VK_NULL_HANDLE;
    }
    ~KernelState() override {
        if (context->alive) release();
    }
};

} // namespace detail

namespace {

using detail::ContextState;

/// A host-visible buffer for one copy, freed when it goes out of scope.
struct Staging {
    VmaAllocator allocator;
    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    void* mapped = nullptr;

    Staging(VmaAllocator alloc, size_t bytes, bool forReading) : allocator(alloc) {
        VkBufferCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        info.size = bytes;
        info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
        allocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT |
                          (forReading ? VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT
                                      : VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
        allocInfo.requiredFlags = VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        VmaAllocationInfo result{};
        if (vmaCreateBuffer(allocator, &info, &allocInfo, &buffer, &allocation, &result) != VK_SUCCESS) {
            throw std::runtime_error("compute: could not create a staging buffer of " +
                                     std::to_string(bytes) + " bytes");
        }
        mapped = result.pMappedData;
    }
    ~Staging() { vmaDestroyBuffer(allocator, buffer, allocation); }
    Staging(const Staging&) = delete;
    Staging& operator=(const Staging&) = delete;
};

detail::BufferState& checked(const std::shared_ptr<detail::BufferState>& state) {
    if (!state) throw std::runtime_error("compute: the buffer is empty");
    state->context->check();
    return *state;
}

void checkRange(const detail::BufferState& state, size_t bytes, size_t offset) {
    if (offset > state.size || bytes > state.size - offset) {
        throw std::out_of_range("compute: " + std::to_string(bytes) + " bytes at offset " +
                                std::to_string(offset) + " do not fit in a buffer of " +
                                std::to_string(state.size) + " bytes");
    }
}

std::string bindingName(const SpvReflectDescriptorBinding& b) {
    if (b.name && *b.name) return b.name;
    if (b.type_description && b.type_description->type_name) return b.type_description->type_name;
    return {};
}

} // namespace

// ─── reflectKernel ───────────────────────────────────────────

KernelInfo reflectKernel(const void* spirv, size_t sizeBytes) {
    SpvReflectShaderModule module{};
    if (spvReflectCreateShaderModule(sizeBytes, spirv, &module) != SPV_REFLECT_RESULT_SUCCESS) {
        throw std::runtime_error("compute: SPIRV-Reflect could not parse the module");
    }
    struct Guard {
        SpvReflectShaderModule* m;
        ~Guard() { spvReflectDestroyShaderModule(m); }
    } guard{&module};

    if (module.shader_stage != SPV_REFLECT_SHADER_STAGE_COMPUTE_BIT || module.entry_point_count == 0) {
        throw std::runtime_error("compute: the module is not a compute shader");
    }

    KernelInfo info;
    const auto& entry = module.entry_points[0];
    info.localSize = {std::max(entry.local_size.x, 1u), std::max(entry.local_size.y, 1u),
                      std::max(entry.local_size.z, 1u)};

    uint32_t count = 0;
    spvReflectEnumerateDescriptorBindings(&module, &count, nullptr);
    std::vector<SpvReflectDescriptorBinding*> bindings(count);
    spvReflectEnumerateDescriptorBindings(&module, &count, bindings.data());
    for (const auto* b : bindings) {
        const std::string where = "binding " + std::to_string(b->binding) + " ('" + bindingName(*b) + "')";
        if (b->set != 0) {
            throw std::runtime_error("compute: " + where + " is in set " + std::to_string(b->set) +
                                     "; only set 0 is supported");
        }
        KernelBinding out;
        out.binding = b->binding;
        out.name = bindingName(*b);
        if (b->descriptor_type == SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_BUFFER) {
            out.type = KernelBinding::Type::StorageBuffer;
            out.readOnly = (b->decoration_flags & SPV_REFLECT_DECORATION_NON_WRITABLE) != 0 ||
                           (b->block.decoration_flags & SPV_REFLECT_DECORATION_NON_WRITABLE) != 0;
        } else if (b->descriptor_type == SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER) {
            out.type = KernelBinding::Type::UniformBuffer;
            out.readOnly = true;
        } else {
            throw std::runtime_error("compute: " + where +
                                     " is not a storage or uniform buffer; only buffers are supported");
        }
        if (b->count > 1) {
            throw std::runtime_error("compute: " + where + " is an array of buffers, which is not supported");
        }
        info.bindings.push_back(out);
    }
    std::sort(info.bindings.begin(), info.bindings.end(),
              [](const KernelBinding& a, const KernelBinding& b) { return a.binding < b.binding; });

    spvReflectEnumeratePushConstantBlocks(&module, &count, nullptr);
    std::vector<SpvReflectBlockVariable*> push(count);
    spvReflectEnumeratePushConstantBlocks(&module, &count, push.data());
    for (const auto* block : push) {
        info.pushConstantSize = std::max(info.pushConstantSize, block->offset + block->size);
    }
    return info;
}

// ─── Buffer ──────────────────────────────────────────────────

bool Buffer::valid() const {
    return m_state && m_state->context->alive && m_state->buffer != VK_NULL_HANDLE;
}

size_t Buffer::size() const {
    return m_state ? m_state->size : 0;
}

void Buffer::upload(const void* data, size_t bytes, size_t offset) {
    auto& state = checked(m_state);
    checkRange(state, bytes, offset);
    if (bytes == 0) return;
    Staging staging(state.context->allocator(), bytes, false);
    std::memcpy(staging.mapped, data, bytes);
    state.context->submit([&](VkCommandBuffer cmd) {
        VkBufferCopy region{0, offset, bytes};
        vkCmdCopyBuffer(cmd, staging.buffer, state.buffer, 1, &region);
    });
}

void Buffer::download(void* out, size_t bytes, size_t offset) const {
    auto& state = checked(m_state);
    checkRange(state, bytes, offset);
    if (bytes == 0) return;
    Staging staging(state.context->allocator(), bytes, true);
    state.context->submit([&](VkCommandBuffer cmd) {
        VkBufferCopy region{offset, 0, bytes};
        vkCmdCopyBuffer(cmd, state.buffer, staging.buffer, 1, &region);
    });
    std::memcpy(out, staging.mapped, bytes);
}

// ─── Kernel ──────────────────────────────────────────────────

bool Kernel::valid() const {
    return m_state && m_state->context->alive && m_state->pipeline != nullptr;
}

const KernelInfo& Kernel::info() const {
    if (!m_state) throw std::runtime_error("compute: the kernel is empty");
    return m_state->info;
}

// ─── Context ─────────────────────────────────────────────────

Context::Context(bool enableValidation) : m_state(std::make_shared<ContextState>()) {
    m_state->ownedInstance = std::make_unique<VulkanInstance>(enableValidation, /*forWindow=*/false);
    m_state->ownedDevice = std::make_unique<VulkanDevice>(*m_state->ownedInstance, VK_NULL_HANDLE);
    m_state->create(*m_state->ownedDevice);
}

Context::Context(VulkanDevice& device) : m_state(std::make_shared<ContextState>()) {
    m_state->create(device);
}

Context::~Context() {
    shutdown();
}

void Context::shutdown() {
    if (m_state->alive || m_state->ownedDevice) m_state->destroy();
}

bool Context::isShutdown() const {
    return !m_state->alive;
}

VulkanDevice& Context::device() const {
    m_state->check();
    return *m_state->device;
}

Buffer Context::createBuffer(size_t bytes) {
    m_state->check();
    if (bytes == 0) throw std::invalid_argument("compute: a buffer needs at least one byte");

    auto state = std::make_shared<detail::BufferState>();
    state->context = m_state;
    state->size = bytes;

    VkBufferCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    info.size = (bytes + 3) & ~size_t(3);   // vkCmdFillBuffer works in whole 4-byte words
    info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT |
                 VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    m_state->device->shareWithCompute(info);
    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    if (vmaCreateBuffer(m_state->allocator(), &info, &allocInfo, &state->buffer, &state->allocation, nullptr) !=
        VK_SUCCESS) {
        throw std::runtime_error("compute: could not create a buffer of " + std::to_string(bytes) + " bytes");
    }
    m_state->track(state);

    m_state->submit([&](VkCommandBuffer cmd) { vkCmdFillBuffer(cmd, state->buffer, 0, VK_WHOLE_SIZE, 0); });

    Buffer out;
    out.m_state = std::move(state);
    return out;
}

Buffer Context::upload(const void* data, size_t bytes) {
    Buffer buffer = createBuffer(bytes);
    buffer.upload(data, bytes);
    return buffer;
}

Kernel Context::loadKernel(const std::string& spvPath) {
    m_state->check();
    std::ifstream file(spvPath, std::ios::binary | std::ios::ate);
    if (!file) throw std::runtime_error("compute: cannot open '" + spvPath + "'");
    std::vector<char> code(static_cast<size_t>(file.tellg()));
    file.seekg(0);
    file.read(code.data(), static_cast<std::streamsize>(code.size()));

    auto state = std::make_shared<detail::KernelState>();
    state->context = m_state;
    try {
        state->info = reflectKernel(code.data(), code.size());
    } catch (const std::runtime_error& e) {
        throw std::runtime_error(std::string(e.what()) + " (" + spvPath + ")");
    }

    std::vector<VkDescriptorSetLayoutBinding> layoutBindings;
    for (const auto& b : state->info.bindings) {
        VkDescriptorSetLayoutBinding lb{};
        lb.binding = b.binding;
        lb.descriptorType = b.type == KernelBinding::Type::UniformBuffer ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
                                                                         : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        lb.descriptorCount = 1;
        lb.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        layoutBindings.push_back(lb);
    }
    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(layoutBindings.size());
    layoutInfo.pBindings = layoutBindings.data();
    if (vkCreateDescriptorSetLayout(m_state->vk(), &layoutInfo, nullptr, &state->setLayout) != VK_SUCCESS) {
        throw std::runtime_error("compute: could not create the descriptor set layout for '" + spvPath + "'");
    }
    m_state->track(state);

    std::vector<VkPushConstantRange> push;
    if (state->info.pushConstantSize > 0) {
        push.push_back({VK_SHADER_STAGE_COMPUTE_BIT, 0, state->info.pushConstantSize});
    }
    state->pipeline = std::make_unique<VulkanComputePipeline>(
        *m_state->device, spvPath, std::vector<VkDescriptorSetLayout>{state->setLayout}, push);

    Kernel out;
    out.m_state = std::move(state);
    return out;
}

void Context::run(const Kernel& kernel, const std::vector<Buffer>& bindings, std::array<uint32_t, 3> groups,
                  const void* push, size_t pushBytes) {
    m_state->check();
    if (!kernel.m_state || kernel.m_state->context != m_state || !kernel.m_state->pipeline) {
        throw std::invalid_argument("compute: the kernel is empty or belongs to another context");
    }
    const auto& k = *kernel.m_state;
    if (bindings.size() != k.info.bindings.size()) {
        throw std::invalid_argument("compute: the kernel has " + std::to_string(k.info.bindings.size()) +
                                    " binding(s), " + std::to_string(bindings.size()) + " buffer(s) were given");
    }
    for (size_t i = 0; i < bindings.size(); ++i) {
        const auto& b = bindings[i].m_state;
        if (!b || b->context != m_state || b->buffer == VK_NULL_HANDLE) {
            throw std::invalid_argument("compute: the buffer for binding " +
                                        std::to_string(k.info.bindings[i].binding) +
                                        " is empty or belongs to another context");
        }
    }
    if (pushBytes > k.info.pushConstantSize) {
        throw std::invalid_argument("compute: " + std::to_string(pushBytes) +
                                    " bytes of push constants given, the kernel declares " +
                                    std::to_string(k.info.pushConstantSize));
    }
    if (groups[0] == 0 || groups[1] == 0 || groups[2] == 0) return;

    // One pool per run, sized for this kernel.
    uint32_t storage = 0, uniform = 0;
    for (const auto& b : k.info.bindings) {
        (b.type == KernelBinding::Type::UniformBuffer ? uniform : storage)++;
    }
    std::vector<VkDescriptorPoolSize> sizes;
    if (storage) sizes.push_back({VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, storage});
    if (uniform) sizes.push_back({VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, uniform});
    VkDescriptorPool pool = VK_NULL_HANDLE;
    VkDescriptorSet set = VK_NULL_HANDLE;
    if (!sizes.empty()) {
        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.maxSets = 1;
        poolInfo.poolSizeCount = static_cast<uint32_t>(sizes.size());
        poolInfo.pPoolSizes = sizes.data();
        if (vkCreateDescriptorPool(m_state->vk(), &poolInfo, nullptr, &pool) != VK_SUCCESS) {
            throw std::runtime_error("compute: could not create a descriptor pool");
        }
    }
    struct PoolGuard {
        VkDevice device;
        VkDescriptorPool pool;
        ~PoolGuard() {
            if (pool != VK_NULL_HANDLE) vkDestroyDescriptorPool(device, pool, nullptr);
        }
    } poolGuard{m_state->vk(), pool};

    if (pool != VK_NULL_HANDLE) {
        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = pool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &k.setLayout;
        if (vkAllocateDescriptorSets(m_state->vk(), &allocInfo, &set) != VK_SUCCESS) {
            throw std::runtime_error("compute: could not allocate a descriptor set");
        }
        const VkDeviceSize maxUniform = m_state->device->getLimits().maxUniformBufferRange;
        std::vector<VkDescriptorBufferInfo> infos(bindings.size());
        std::vector<VkWriteDescriptorSet> writes(bindings.size());
        for (size_t i = 0; i < bindings.size(); ++i) {
            const bool isUniform = k.info.bindings[i].type == KernelBinding::Type::UniformBuffer;
            infos[i].buffer = bindings[i].m_state->buffer;
            infos[i].offset = 0;
            infos[i].range = isUniform ? std::min<VkDeviceSize>(bindings[i].m_state->size, maxUniform)
                                       : VK_WHOLE_SIZE;
            writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[i].dstSet = set;
            writes[i].dstBinding = k.info.bindings[i].binding;
            writes[i].descriptorCount = 1;
            writes[i].descriptorType = isUniform ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
                                                 : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[i].pBufferInfo = &infos[i];
        }
        vkUpdateDescriptorSets(m_state->vk(), static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }

    std::vector<uint8_t> constants(k.info.pushConstantSize, 0);
    if (pushBytes > 0) std::memcpy(constants.data(), push, pushBytes);

    m_state->submit([&](VkCommandBuffer cmd) {
        k.pipeline->bind(cmd);
        if (set != VK_NULL_HANDLE) {
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, k.pipeline->getLayout(), 0, 1, &set, 0,
                                    nullptr);
        }
        if (!constants.empty()) {
            vkCmdPushConstants(cmd, k.pipeline->getLayout(), VK_SHADER_STAGE_COMPUTE_BIT, 0,
                               static_cast<uint32_t>(constants.size()), constants.data());
        }
        k.pipeline->dispatch(cmd, groups[0], groups[1], groups[2]);
    });
}

void Context::runThreads(const Kernel& kernel, const std::vector<Buffer>& bindings, uint32_t threadsX,
                         uint32_t threadsY, uint32_t threadsZ, const void* push, size_t pushBytes) {
    const auto& local = kernel.info().localSize;
    auto groupsFor = [](uint32_t threads, uint32_t size) { return (threads + size - 1) / size; };
    run(kernel, bindings,
        {groupsFor(threadsX, local[0]), groupsFor(threadsY, local[1]), groupsFor(threadsZ, local[2])}, push,
        pushBytes);
}

} // namespace Shoonyakasha::Compute
