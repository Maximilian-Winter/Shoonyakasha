//
// Compute.cpp - Context, Buffer, Image and Kernel (include/Compute/Compute.h)
//

#include "Compute/Compute.h"

#include "GPU/VkFormatUtils.h"
#include "Vulkan/FrameGraph/FrameGraphJson.h"
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
    VkSampler linearSampler = VK_NULL_HANDLE;
    VkSampler nearestSampler = VK_NULL_HANDLE;
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

    /// The sampler sampled images are read with: clamped to the edge,
    /// linear or nearest filtering. Made on first use.
    VkSampler sampler(bool linear) {
        VkSampler& slot = linear ? linearSampler : nearestSampler;
        if (slot != VK_NULL_HANDLE) return slot;
        VkSamplerCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        info.magFilter = info.minFilter = linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
        info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        info.addressModeU = info.addressModeV = info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        info.maxLod = 0.0f;
        if (vkCreateSampler(vk(), &info, nullptr, &slot) != VK_SUCCESS) {
            throw std::runtime_error("compute: could not create a sampler");
        }
        return slot;
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
            if (linearSampler != VK_NULL_HANDLE) vkDestroySampler(vk(), linearSampler, nullptr);
            if (nearestSampler != VK_NULL_HANDLE) vkDestroySampler(vk(), nearestSampler, nullptr);
            if (fence != VK_NULL_HANDLE) vkDestroyFence(vk(), fence, nullptr);
            if (pool != VK_NULL_HANDLE) vkDestroyCommandPool(vk(), pool, nullptr);
        }
        linearSampler = nearestSampler = VK_NULL_HANDLE;
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

struct ImageState : Resource {
    std::shared_ptr<ContextState> context;
    VkImage image = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    VkFormat vkFormat = VK_FORMAT_UNDEFINED;
    std::string format;
    uint32_t width = 0, height = 0, depth = 1, layers = 1;
    size_t texelSize = 0;
    bool storage = false;          // usable as a storage image
    bool sampled = false;          // usable as a sampled image
    bool linearFilter = false;     // sampled with linear filtering
    bool cubeCompatible = false;

    size_t byteSize() const { return size_t(width) * height * depth * layers * texelSize; }

    void release() override {
        if (image != VK_NULL_HANDLE) vmaDestroyImage(context->allocator(), image, allocation);
        image = VK_NULL_HANDLE;
        allocation = VK_NULL_HANDLE;
    }
    ~ImageState() override {
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
using Type = KernelBinding::Type;
using Dim = KernelBinding::Dim;

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

detail::ImageState& checked(const std::shared_ptr<detail::ImageState>& state) {
    if (!state) throw std::runtime_error("compute: the image is empty");
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

void checkImageBytes(const detail::ImageState& state, size_t bytes) {
    if (bytes != state.byteSize()) {
        throw std::invalid_argument("compute: the image holds " + std::to_string(state.byteSize()) +
                                    " bytes, " + std::to_string(bytes) + " were given");
    }
}

VkBufferImageCopy wholeImage(const detail::ImageState& state) {
    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, state.layers};
    region.imageExtent = {state.width, state.height, state.depth};
    return region;
}

std::string bindingName(const SpvReflectDescriptorBinding& b) {
    if (b.name && *b.name) return b.name;
    if (b.type_description && b.type_description->type_name) return b.type_description->type_name;
    return {};
}

/// The pipeline JSON format name of a storage image's format qualifier;
/// empty for none. Throws for qualifiers without a matching engine format.
std::string formatName(SpvImageFormat format, const std::string& where) {
    switch (format) {
        case SpvImageFormatUnknown: return {};
        case SpvImageFormatRgba32f: return "R32G32B32A32_SFLOAT";
        case SpvImageFormatRgba16f: return "R16G16B16A16_SFLOAT";
        case SpvImageFormatRg32f: return "R32G32_SFLOAT";
        case SpvImageFormatRg16f: return "R16G16_SFLOAT";
        case SpvImageFormatR32f: return "R32_SFLOAT";
        case SpvImageFormatR16f: return "R16_SFLOAT";
        case SpvImageFormatR11fG11fB10f: return "B10G11R11_UFLOAT_PACK32";
        case SpvImageFormatRgba16: return "R16G16B16A16_UNORM";
        case SpvImageFormatRg16: return "R16G16_UNORM";
        case SpvImageFormatR16: return "R16_UNORM";
        case SpvImageFormatRgb10A2: return "A2B10G10R10_UNORM_PACK32";
        case SpvImageFormatRgba8: return "R8G8B8A8_UNORM";
        case SpvImageFormatRg8: return "R8G8_UNORM";
        case SpvImageFormatR8: return "R8_UNORM";
        case SpvImageFormatRgba8Snorm: return "R8G8B8A8_SNORM";
        case SpvImageFormatRg8Snorm: return "R8G8_SNORM";
        case SpvImageFormatR8Snorm: return "R8_SNORM";
        case SpvImageFormatRgba32ui: return "R32G32B32A32_UINT";
        case SpvImageFormatRg32ui: return "R32G32_UINT";
        case SpvImageFormatR32ui: return "R32_UINT";
        case SpvImageFormatR16ui: return "R16_UINT";
        case SpvImageFormatR8ui: return "R8_UINT";
        case SpvImageFormatR32i: return "R32_SINT";
        case SpvImageFormatR16i: return "R16_SINT";
        case SpvImageFormatR8i: return "R8_SINT";
        default:
            throw std::runtime_error("compute: " + where + " has a format qualifier with no matching engine format");
    }
}

const char* dimName(Dim dim, bool arrayed) {
    switch (dim) {
        case Dim::Image2D: return arrayed ? "2D array" : "2D";
        case Dim::Image3D: return "3D";
        case Dim::Cube: return arrayed ? "cube array" : "cube";
        default: return "?";
    }
}

/// The view type `binding` needs for `image`, or a reason it cannot be bound.
VkImageViewType viewTypeFor(const KernelBinding& binding, const detail::ImageState& image, const VulkanDevice& device,
                            std::string& error) {
    const bool is3D = image.depth > 1;
    switch (binding.dim) {
        case Dim::Image2D:
            if (is3D) break;
            if (!binding.arrayed) {
                if (image.layers == 1) return VK_IMAGE_VIEW_TYPE_2D;
                break;
            }
            return VK_IMAGE_VIEW_TYPE_2D_ARRAY;
        case Dim::Image3D:
            if (is3D) return VK_IMAGE_VIEW_TYPE_3D;
            break;
        case Dim::Cube:
            if (!image.cubeCompatible) break;
            if (!binding.arrayed) {
                if (image.layers == 6) return VK_IMAGE_VIEW_TYPE_CUBE;
                break;
            }
            if (!device.getEnabledFeatures().imageCubeArray) {
                error = "the device has no cube array images";
                return VK_IMAGE_VIEW_TYPE_MAX_ENUM;
            }
            return VK_IMAGE_VIEW_TYPE_CUBE_ARRAY;
        default:
            break;
    }
    error = "the shader declares a " + std::string(dimName(binding.dim, binding.arrayed)) + " image, the image is " +
            std::to_string(image.width) + "x" + std::to_string(image.height) +
            (is3D ? "x" + std::to_string(image.depth) + " (3D)" : "") +
            " with " + std::to_string(image.layers) + " layer(s)";
    return VK_IMAGE_VIEW_TYPE_MAX_ENUM;
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
        const bool nonWritable = (b->decoration_flags & SPV_REFLECT_DECORATION_NON_WRITABLE) != 0;
        switch (b->descriptor_type) {
            case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_BUFFER:
                out.type = Type::StorageBuffer;
                out.readOnly = nonWritable || (b->block.decoration_flags & SPV_REFLECT_DECORATION_NON_WRITABLE) != 0;
                break;
            case SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
                out.type = Type::UniformBuffer;
                out.readOnly = true;
                break;
            case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_IMAGE:
            case SPV_REFLECT_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER: {
                const bool storage = b->descriptor_type == SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_IMAGE;
                out.type = storage ? Type::StorageImage : Type::SampledImage;
                out.readOnly = !storage || nonWritable;
                switch (b->image.dim) {
                    case SpvDim2D: out.dim = Dim::Image2D; break;
                    case SpvDim3D: out.dim = Dim::Image3D; break;
                    case SpvDimCube: out.dim = Dim::Cube; break;
                    default:
                        throw std::runtime_error("compute: " + where + " is not a 2D, 3D or cube image");
                }
                if (b->image.ms) throw std::runtime_error("compute: " + where + " is multisampled");
                out.arrayed = b->image.arrayed != 0;
                if (storage) out.format = formatName(b->image.image_format, where);
                break;
            }
            default:
                throw std::runtime_error("compute: " + where +
                                         " is not a storage buffer, uniform buffer, storage image or sampler");
        }
        if (b->count > 1) {
            throw std::runtime_error("compute: " + where + " is an array of descriptors, which is not supported");
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

// ─── Image ───────────────────────────────────────────────────

bool Image::valid() const {
    return m_state && m_state->context->alive && m_state->image != VK_NULL_HANDLE;
}

uint32_t Image::width() const { return m_state ? m_state->width : 0; }
uint32_t Image::height() const { return m_state ? m_state->height : 0; }
uint32_t Image::depth() const { return m_state ? m_state->depth : 0; }
uint32_t Image::layers() const { return m_state ? m_state->layers : 0; }
size_t Image::texelSize() const { return m_state ? m_state->texelSize : 0; }
size_t Image::byteSize() const { return m_state ? m_state->byteSize() : 0; }

const std::string& Image::format() const {
    static const std::string none;
    return m_state ? m_state->format : none;
}

void Image::upload(const void* data, size_t bytes) {
    auto& state = checked(m_state);
    checkImageBytes(state, bytes);
    Staging staging(state.context->allocator(), bytes, false);
    std::memcpy(staging.mapped, data, bytes);
    state.context->submit([&](VkCommandBuffer cmd) {
        const VkBufferImageCopy region = wholeImage(state);
        vkCmdCopyBufferToImage(cmd, staging.buffer, state.image, VK_IMAGE_LAYOUT_GENERAL, 1, &region);
    });
}

void Image::download(void* out, size_t bytes) const {
    auto& state = checked(m_state);
    checkImageBytes(state, bytes);
    Staging staging(state.context->allocator(), bytes, true);
    state.context->submit([&](VkCommandBuffer cmd) {
        const VkBufferImageCopy region = wholeImage(state);
        vkCmdCopyImageToBuffer(cmd, state.image, VK_IMAGE_LAYOUT_GENERAL, staging.buffer, 1, &region);
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

Image Context::createImage(const std::string& format, uint32_t width, uint32_t height, uint32_t depth,
                           uint32_t layers) {
    m_state->check();
    if (width == 0 || height == 0 || depth == 0 || layers == 0) {
        throw std::invalid_argument("compute: an image needs a size of at least 1 in every dimension");
    }
    if (depth > 1 && layers > 1) {
        throw std::invalid_argument("compute: a 3D image (depth > 1) cannot have layers");
    }
    VkFormat vkFormat = VK_FORMAT_UNDEFINED;
    try {
        vkFormat = FrameGraph::JsonUtils::stringToFormat(format);
    } catch (const std::exception&) {
    }
    const uint32_t texelSize = copiedTexelSize(vkFormat);
    if (vkFormat == VK_FORMAT_UNDEFINED || texelSize == 0 || isDepthFormat(vkFormat) || isStencilFormat(vkFormat)) {
        throw std::invalid_argument("compute: '" + format + "' is not a colour format with a known texel size");
    }

    VkFormatProperties props{};
    vkGetPhysicalDeviceFormatProperties(m_state->device->getPhysicalDevice(), vkFormat, &props);
    const VkFormatFeatureFlags features = props.optimalTilingFeatures;

    auto state = std::make_shared<detail::ImageState>();
    state->context = m_state;
    state->vkFormat = vkFormat;
    state->format = format;
    state->width = width;
    state->height = height;
    state->depth = depth;
    state->layers = layers;
    state->texelSize = texelSize;
    state->storage = (features & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT) != 0;
    state->sampled = (features & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) != 0;
    state->linearFilter = (features & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT) != 0;
    state->cubeCompatible = depth == 1 && width == height && layers % 6 == 0;
    if (!state->storage && !state->sampled) {
        throw std::invalid_argument("compute: the device can neither store to nor sample '" + format + "' images");
    }

    VkImageCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    info.flags = state->cubeCompatible ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0;
    info.imageType = depth > 1 ? VK_IMAGE_TYPE_3D : VK_IMAGE_TYPE_2D;
    info.format = vkFormat;
    info.extent = {width, height, depth};
    info.mipLevels = 1;
    info.arrayLayers = layers;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                 (state->storage ? VK_IMAGE_USAGE_STORAGE_BIT : 0) | (state->sampled ? VK_IMAGE_USAGE_SAMPLED_BIT : 0);
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    m_state->device->shareWithCompute(info);

    VkImageFormatProperties limits{};
    if (vkGetPhysicalDeviceImageFormatProperties(m_state->device->getPhysicalDevice(), vkFormat, info.imageType,
                                                 info.tiling, info.usage, info.flags, &limits) != VK_SUCCESS ||
        width > limits.maxExtent.width || height > limits.maxExtent.height || depth > limits.maxExtent.depth ||
        layers > limits.maxArrayLayers) {
        throw std::invalid_argument("compute: the device does not support a " + std::to_string(width) + "x" +
                                    std::to_string(height) + "x" + std::to_string(depth) + " '" + format +
                                    "' image with " + std::to_string(layers) + " layer(s)");
    }

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    if (vmaCreateImage(m_state->allocator(), &info, &allocInfo, &state->image, &state->allocation, nullptr) !=
        VK_SUCCESS) {
        throw std::runtime_error("compute: could not create a '" + format + "' image");
    }
    m_state->track(state);

    // Kept in the general layout from here on, filled with zeros.
    m_state->submit([&](VkCommandBuffer cmd) {
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = state->image;
        barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, layers};
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr,
                             0, nullptr, 1, &barrier);
        VkClearColorValue zero{};
        vkCmdClearColorImage(cmd, state->image, VK_IMAGE_LAYOUT_GENERAL, &zero, 1, &barrier.subresourceRange);
    });

    Image out;
    out.m_state = std::move(state);
    return out;
}

Image Context::uploadImage(const std::string& format, uint32_t width, uint32_t height, const void* data,
                           size_t bytes, uint32_t depth, uint32_t layers) {
    Image image = createImage(format, width, height, depth, layers);
    image.upload(data, bytes);
    return image;
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
        switch (b.type) {
            case Type::StorageBuffer: lb.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; break;
            case Type::UniformBuffer: lb.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER; break;
            case Type::StorageImage: lb.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE; break;
            case Type::SampledImage: lb.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; break;
        }
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

void Context::run(const Kernel& kernel, const std::vector<Binding>& bindings, std::array<uint32_t, 3> groups,
                  const void* push, size_t pushBytes) {
    m_state->check();
    if (!kernel.m_state || kernel.m_state->context != m_state || !kernel.m_state->pipeline) {
        throw std::invalid_argument("compute: the kernel is empty or belongs to another context");
    }
    const auto& k = *kernel.m_state;
    if (bindings.size() != k.info.bindings.size()) {
        throw std::invalid_argument("compute: the kernel has " + std::to_string(k.info.bindings.size()) +
                                    " binding(s), " + std::to_string(bindings.size()) + " were given");
    }

    // Check every binding before anything is created.
    std::vector<VkImageViewType> viewTypes(bindings.size(), VK_IMAGE_VIEW_TYPE_MAX_ENUM);
    for (size_t i = 0; i < bindings.size(); ++i) {
        const KernelBinding& want = k.info.bindings[i];
        const std::string where = "binding " + std::to_string(want.binding) + " ('" + want.name + "')";
        const bool wantsImage = want.type == Type::StorageImage || want.type == Type::SampledImage;
        const auto& buffer = bindings[i].m_buffer;
        const auto& image = bindings[i].m_image;
        if (wantsImage) {
            if (!image) {
                throw std::invalid_argument("compute: " + where +
                                            (buffer ? " is an image, a buffer was given" : ": the image is empty"));
            }
            if (image->context != m_state || image->image == VK_NULL_HANDLE) {
                throw std::invalid_argument("compute: the image for " + where + " is empty or belongs to another context");
            }
            if (want.type == Type::StorageImage && !image->storage) {
                throw std::invalid_argument("compute: " + where + ": the device cannot use '" + image->format +
                                            "' images as storage images");
            }
            if (want.type == Type::SampledImage && !image->sampled) {
                throw std::invalid_argument("compute: " + where + ": the device cannot sample '" + image->format +
                                            "' images");
            }
            if (!want.format.empty() && want.format != image->format) {
                throw std::invalid_argument("compute: " + where + " is declared " + want.format + ", the image is " +
                                            image->format);
            }
            std::string error;
            viewTypes[i] = viewTypeFor(want, *image, *m_state->device, error);
            if (viewTypes[i] == VK_IMAGE_VIEW_TYPE_MAX_ENUM) {
                throw std::invalid_argument("compute: " + where + ": " + error);
            }
        } else {
            if (!buffer) {
                throw std::invalid_argument("compute: " + where +
                                            (image ? " is a buffer, an image was given" : ": the buffer is empty"));
            }
            if (buffer->context != m_state || buffer->buffer == VK_NULL_HANDLE) {
                throw std::invalid_argument("compute: the buffer for " + where +
                                            " is empty or belongs to another context");
            }
        }
    }
    if (pushBytes > k.info.pushConstantSize) {
        throw std::invalid_argument("compute: " + std::to_string(pushBytes) +
                                    " bytes of push constants given, the kernel declares " +
                                    std::to_string(k.info.pushConstantSize));
    }
    if (groups[0] == 0 || groups[1] == 0 || groups[2] == 0) return;

    // One descriptor pool and the image views per run, freed when it ends.
    struct Scratch {
        VkDevice device;
        VkDescriptorPool pool = VK_NULL_HANDLE;
        std::vector<VkImageView> views;
        ~Scratch() {
            for (VkImageView view : views) vkDestroyImageView(device, view, nullptr);
            if (pool != VK_NULL_HANDLE) vkDestroyDescriptorPool(device, pool, nullptr);
        }
    } scratch{m_state->vk()};

    uint32_t counts[4] = {0, 0, 0, 0};
    for (const auto& b : k.info.bindings) counts[static_cast<int>(b.type)]++;
    const VkDescriptorType descriptorTypes[4] = {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                                                 VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                                                 VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER};
    std::vector<VkDescriptorPoolSize> sizes;
    for (int t = 0; t < 4; ++t) {
        if (counts[t]) sizes.push_back({descriptorTypes[t], counts[t]});
    }

    VkDescriptorSet set = VK_NULL_HANDLE;
    if (!sizes.empty()) {
        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.maxSets = 1;
        poolInfo.poolSizeCount = static_cast<uint32_t>(sizes.size());
        poolInfo.pPoolSizes = sizes.data();
        if (vkCreateDescriptorPool(m_state->vk(), &poolInfo, nullptr, &scratch.pool) != VK_SUCCESS) {
            throw std::runtime_error("compute: could not create a descriptor pool");
        }
        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = scratch.pool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &k.setLayout;
        if (vkAllocateDescriptorSets(m_state->vk(), &allocInfo, &set) != VK_SUCCESS) {
            throw std::runtime_error("compute: could not allocate a descriptor set");
        }

        const VkDeviceSize maxUniform = m_state->device->getLimits().maxUniformBufferRange;
        std::vector<VkDescriptorBufferInfo> bufferInfos(bindings.size());
        std::vector<VkDescriptorImageInfo> imageInfos(bindings.size());
        std::vector<VkWriteDescriptorSet> writes(bindings.size());
        for (size_t i = 0; i < bindings.size(); ++i) {
            const KernelBinding& want = k.info.bindings[i];
            writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[i].dstSet = set;
            writes[i].dstBinding = want.binding;
            writes[i].descriptorCount = 1;
            writes[i].descriptorType = descriptorTypes[static_cast<int>(want.type)];
            if (const auto& image = bindings[i].m_image) {
                VkImageViewCreateInfo viewInfo{};
                viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
                viewInfo.image = image->image;
                viewInfo.viewType = viewTypes[i];
                viewInfo.format = image->vkFormat;
                viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, image->layers};
                VkImageView view = VK_NULL_HANDLE;
                if (vkCreateImageView(m_state->vk(), &viewInfo, nullptr, &view) != VK_SUCCESS) {
                    throw std::runtime_error("compute: could not create an image view");
                }
                scratch.views.push_back(view);
                imageInfos[i].imageView = view;
                imageInfos[i].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
                if (want.type == Type::SampledImage) imageInfos[i].sampler = m_state->sampler(image->linearFilter);
                writes[i].pImageInfo = &imageInfos[i];
            } else {
                const auto& buffer = bindings[i].m_buffer;
                bufferInfos[i].buffer = buffer->buffer;
                bufferInfos[i].offset = 0;
                bufferInfos[i].range = want.type == Type::UniformBuffer
                                           ? std::min<VkDeviceSize>(buffer->size, maxUniform)
                                           : VK_WHOLE_SIZE;
                writes[i].pBufferInfo = &bufferInfos[i];
            }
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

void Context::runThreads(const Kernel& kernel, const std::vector<Binding>& bindings, uint32_t threadsX,
                         uint32_t threadsY, uint32_t threadsZ, const void* push, size_t pushBytes) {
    const auto& local = kernel.info().localSize;
    auto groupsFor = [](uint32_t threads, uint32_t size) { return (threads + size - 1) / size; };
    run(kernel, bindings,
        {groupsFor(threadsX, local[0]), groupsFor(threadsY, local[1]), groupsFor(threadsZ, local[2])}, push,
        pushBytes);
}

} // namespace Shoonyakasha::Compute
