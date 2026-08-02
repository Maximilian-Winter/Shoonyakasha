//
// IBLGenerator.cpp - Image-Based Lighting texture generation implementation
//
// 朱雀司變  光明萬丈
// The Vermilion Bird governs transformation — radiance in all directions
//

#include "IBL/IBLGenerator.h"
#include "Vulkan/VulkanBuffer.h"
#include <stdexcept>
#include <filesystem>
#include <iostream>
#include <memory>
#include <cmath>
#include <algorithm>

#define STB_IMAGE_IMPLEMENTATION_SKIP  // Already defined elsewhere
#include <array>
#include <stb_image.h>

namespace Shoonyakasha {

// ═══════════════════════════════════════════════════════════════
// Push Constant Structures
// ═══════════════════════════════════════════════════════════════

struct EquirectPushConstants {
    uint32_t faceIndex;
    uint32_t faceSize;
};

struct ConvolutionPushConstants {
    uint32_t faceIndex;
    uint32_t faceSize;
    uint32_t sampleCount;
    float roughnessOrPadding;
};

struct BRDFPushConstants {
    uint32_t lutSize;
    uint32_t sampleCount;
};

// ═══════════════════════════════════════════════════════════════
// Constructor / Destructor
// ═══════════════════════════════════════════════════════════════

IBLGenerator::IBLGenerator(VulkanDevice& device, const std::string& shaderBasePath)
    : m_device(device)
    , m_shaderBasePath(shaderBasePath)
{
    createDescriptorLayouts();
    createDescriptorPool();
    createSamplers();
}

IBLGenerator::~IBLGenerator() {
    cleanup();
}

void IBLGenerator::cleanup() {
    VkDevice logicalDevice = m_device.getLogicalDevice();

    if (m_linearSampler != VK_NULL_HANDLE) {
        vkDestroySampler(logicalDevice, m_linearSampler, nullptr);
        m_linearSampler = VK_NULL_HANDLE;
    }

    if (m_descriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(logicalDevice, m_descriptorPool, nullptr);
        m_descriptorPool = VK_NULL_HANDLE;
    }

    if (m_equirectLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(logicalDevice, m_equirectLayout, nullptr);
        m_equirectLayout = VK_NULL_HANDLE;
    }

    if (m_convolutionLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(logicalDevice, m_convolutionLayout, nullptr);
        m_convolutionLayout = VK_NULL_HANDLE;
    }

    if (m_brdfLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(logicalDevice, m_brdfLayout, nullptr);
        m_brdfLayout = VK_NULL_HANDLE;
    }
}

// ═══════════════════════════════════════════════════════════════
// Initialization
// ═══════════════════════════════════════════════════════════════

void IBLGenerator::createDescriptorLayouts() {
    VkDevice logicalDevice = m_device.getLogicalDevice();

    // Equirect to cubemap layout: sampler2D input, image2D output
    {
        std::array<VkDescriptorSetLayoutBinding, 2> bindings{};

        bindings[0].binding = 0;
        bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        bindings[1].binding = 1;
        bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();

        if (vkCreateDescriptorSetLayout(logicalDevice, &layoutInfo, nullptr, &m_equirectLayout) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create equirect descriptor layout!");
        }
    }

    // Convolution layout: samplerCube input, image2D output
    {
        std::array<VkDescriptorSetLayoutBinding, 2> bindings{};

        bindings[0].binding = 0;
        bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        bindings[1].binding = 1;
        bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();

        if (vkCreateDescriptorSetLayout(logicalDevice, &layoutInfo, nullptr, &m_convolutionLayout) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create convolution descriptor layout!");
        }
    }

    // BRDF LUT layout: image2D output only
    {
        VkDescriptorSetLayoutBinding binding{};
        binding.binding = 0;
        binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        binding.descriptorCount = 1;
        binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = 1;
        layoutInfo.pBindings = &binding;

        if (vkCreateDescriptorSetLayout(logicalDevice, &layoutInfo, nullptr, &m_brdfLayout) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create BRDF descriptor layout!");
        }
    }
}

void IBLGenerator::createDescriptorPool() {
    // Every recorded dispatch needs its own descriptor set — see the comment in
    // convertEquirectToCubemap. The prefilter pass is the greediest: 6 faces x one
    // mip chain, so 6 * mipLevels sets (60 at the default 512px / 10 mips). Sized
    // well above that so a larger prefilterSize does not silently exhaust the pool.
    constexpr uint32_t kMaxIBLDescriptorSets = 256;

    std::array<VkDescriptorPoolSize, 2> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[0].descriptorCount = kMaxIBLDescriptorSets;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    poolSizes[1].descriptorCount = kMaxIBLDescriptorSets;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = kMaxIBLDescriptorSets;

    if (vkCreateDescriptorPool(m_device.getLogicalDevice(), &poolInfo, nullptr, &m_descriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create IBL descriptor pool!");
    }
}

void IBLGenerator::createSamplers() {
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.maxAnisotropy = 1.0f;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = VK_LOD_CLAMP_NONE;
    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;

    if (vkCreateSampler(m_device.getLogicalDevice(), &samplerInfo, nullptr, &m_linearSampler) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create IBL linear sampler!");
    }
}

// ═══════════════════════════════════════════════════════════════
// HDR Loading
// ═══════════════════════════════════════════════════════════════

VulkanTexture* IBLGenerator::loadHDRTexture(const std::string& path) {
    // Report a missing or unreadable file as such, before stb sees it. The
    // failure is otherwise indistinguishable from a malformed one, and a wrong
    // relative path is by far the more common case — the message needs to say
    // which path was tried and from where.
    if (!std::filesystem::exists(path)) {
        throw std::runtime_error(
            "HDR environment map not found: '" + path + "' (working directory: " +
            std::filesystem::current_path().string() + ")");
    }

    int width = 0, height = 0, channels = 0;
    float* pixels = stbi_loadf(path.c_str(), &width, &height, &channels, STBI_rgb_alpha);

    if (!pixels) {
        const char* reason = stbi_failure_reason();
        throw std::runtime_error("Failed to load HDR image '" + path + "': " +
                                 (reason ? reason : "unknown error"));
    }

    if (width <= 0 || height <= 0) {
        stbi_image_free(pixels);
        throw std::runtime_error("HDR image '" + path + "' has zero extent");
    }

    auto* texture = new VulkanTexture(m_device, pixels,
                                       static_cast<uint32_t>(width),
                                       static_cast<uint32_t>(height),
                                       4, VK_FORMAT_R32G32B32A32_SFLOAT);

    stbi_image_free(pixels);
    return texture;
}

// ═══════════════════════════════════════════════════════════════
// Main Generation
// ═══════════════════════════════════════════════════════════════

IBLResources IBLGenerator::generate(const std::string& hdrPath, const IBLGenerationParams& params) {
    std::cout << "[IBL] Generating IBL textures from: " << hdrPath << std::endl;

    IBLResources resources{};

    // Every step below can throw — a missing compute shader is enough, which is
    // what happens when an application points at an HDR without shipping
    // shaders/ibl/. Anything already built would then be orphaned: one failed
    // generation leaked two images, two samplers and 79 image views, because a
    // cubemap owns a view per face per mip.
    //
    // The caller only ever sees the exception, so cleaning up here is the only
    // place it can happen.
    std::unique_ptr<VulkanTexture> equirect;
    try {
        // Step 1: Load HDR equirectangular image
        std::cout << "[IBL] Loading HDR texture..." << std::endl;
        equirect.reset(loadHDRTexture(hdrPath));

        // Step 2: Convert to cubemap
        std::cout << "[IBL] Converting equirectangular to cubemap..." << std::endl;
        resources.environmentMap = convertEquirectToCubemap(equirect.get(), params.environmentSize);

        // Wait for GPU to finish before destroying the input texture
        vkDeviceWaitIdle(m_device.getLogicalDevice());
        equirect.reset();

        // Step 3: Generate irradiance map
        std::cout << "[IBL] Generating irradiance map..." << std::endl;
        resources.irradianceMap = generateIrradianceMap(resources.environmentMap,
                                                         params.irradianceSize,
                                                         params.irradianceSamples);

        // Step 4: Generate prefiltered environment map
        std::cout << "[IBL] Generating prefiltered environment map..." << std::endl;
        resources.prefilterMap = generatePrefilterMap(resources.environmentMap,
                                                       params.prefilterSize,
                                                       params.prefilterSamples);

        // Step 5: Generate BRDF LUT
        std::cout << "[IBL] Generating BRDF LUT..." << std::endl;
        resources.brdfLUT = generateBRDFLUT(params.brdfLUTSize, params.brdfSamples);
    } catch (...) {
        // The GPU may still be reading what we are about to free.
        vkDeviceWaitIdle(m_device.getLogicalDevice());
        resources.destroy();
        throw;
    }

    std::cout << "[IBL] IBL generation complete!" << std::endl;
    return resources;
}

// ═══════════════════════════════════════════════════════════════
// Equirectangular to Cubemap
// ═══════════════════════════════════════════════════════════════

VulkanCubemap* IBLGenerator::convertEquirectToCubemap(VulkanTexture* equirect, uint32_t cubeSize) {
    VkDevice logicalDevice = m_device.getLogicalDevice();

    std::cout << "[IBL]   Creating environment cubemap " << cubeSize << "x" << cubeSize << "..." << std::endl;

    // Create output cubemap
    // Owned locally until the whole step succeeds. Everything between here
    // and the return can throw -- loading a compute shader, allocating a
    // descriptor set -- and this object is the only reference to a cubemap
    // holding an image, a sampler and a view per face per mip.
    std::unique_ptr<VulkanCubemap> cubemap(
        VulkanCubemap::createEnvironmentMap(m_device, cubeSize));

    std::cout << "[IBL]   Creating compute pipeline..." << std::endl;

    // Create compute pipeline
    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushRange.offset = 0;
    pushRange.size = sizeof(EquirectPushConstants);

    std::string shaderPath = m_shaderBasePath + "equirect_to_cubemap.comp.spv";
    std::cout << "[IBL]   Shader path: " << shaderPath << std::endl;

    VulkanComputePipeline pipeline(m_device,
                                   shaderPath,
                                   {m_equirectLayout},
                                   {pushRange});

    // One descriptor set per dispatch.
    //
    // Descriptor sets are read by the shader at *execution* time, not at
    // vkCmdBindDescriptorSets time. All six dispatches below are recorded into a
    // single command buffer that is not submitted until the end of this function,
    // so a single set rewritten inside the loop would hold face 5's views by the
    // time any of them ran — every dispatch would write through face 5 and faces
    // 0-4 would be left as undefined memory.
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &m_equirectLayout;

    std::vector<VkDescriptorSet> descriptorSets(6, VK_NULL_HANDLE);
    for (auto& set : descriptorSets) {
        if (vkAllocateDescriptorSets(logicalDevice, &allocInfo, &set) != VK_SUCCESS) {
            throw std::runtime_error("Failed to allocate IBL equirect descriptor set!");
        }
    }

    // Record command buffer
    VkCommandBuffer cmd = m_device.beginSingleTimeCommands();

    // Transition cubemap to GENERAL for compute writes
    cubemap->transitionLayout(cmd,
                              VK_IMAGE_LAYOUT_UNDEFINED,
                              VK_IMAGE_LAYOUT_GENERAL,
                              VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                              VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                              0,
                              VK_ACCESS_SHADER_WRITE_BIT);

    // Process each face
    for (uint32_t face = 0; face < 6; ++face) {
        // Update descriptor for this face
        VkDescriptorImageInfo inputInfo{};
        inputInfo.sampler = m_linearSampler;
        inputInfo.imageView = equirect->getImageView();
        inputInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkDescriptorImageInfo outputInfo{};
        outputInfo.imageView = cubemap->getFaceView(face, 0);
        outputInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

        VkDescriptorSet descriptorSet = descriptorSets[face];

        std::array<VkWriteDescriptorSet, 2> writes{};
        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = descriptorSet;
        writes[0].dstBinding = 0;
        writes[0].descriptorCount = 1;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[0].pImageInfo = &inputInfo;

        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = descriptorSet;
        writes[1].dstBinding = 1;
        writes[1].descriptorCount = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        writes[1].pImageInfo = &outputInfo;

        vkUpdateDescriptorSets(logicalDevice, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);

        // Bind and dispatch
        pipeline.bind(cmd);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.getLayout(), 0, 1, &descriptorSet, 0, nullptr);

        EquirectPushConstants pc{face, cubeSize};
        vkCmdPushConstants(cmd, pipeline.getLayout(), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);

        uint32_t groupsX = (cubeSize + 15) / 16;
        uint32_t groupsY = (cubeSize + 15) / 16;
        pipeline.dispatch(cmd, groupsX, groupsY, 1);

        // Barrier between faces
        VkMemoryBarrier memBarrier{};
        memBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        memBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        memBarrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             0, 1, &memBarrier, 0, nullptr, 0, nullptr);
    }

    // ── Build the mip chain ──
    //
    // createEnvironmentMap allocates a full chain but only mip 0 was ever
    // written, and prefilter_convolution.comp samples the environment with
    // textureLod(..., roughness * 4.0) — so for every roughness above 0 it read
    // mips 1..4 as uninitialised device memory, and the sampler's maxLod let it.
    const uint32_t mipLevels = cubemap->getMipLevels();

    if (mipLevels > 1) {
        // mip 0 holds the compute results and becomes the first blit source.
        cubemap->transitionMipLayout(cmd, 0,
                                     VK_IMAGE_LAYOUT_GENERAL,
                                     VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                     VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                     VK_PIPELINE_STAGE_TRANSFER_BIT,
                                     VK_ACCESS_SHADER_WRITE_BIT,
                                     VK_ACCESS_TRANSFER_READ_BIT);

        int32_t srcSize = static_cast<int32_t>(cubeSize);

        for (uint32_t mip = 1; mip < mipLevels; ++mip) {
            const int32_t dstSize = srcSize > 1 ? srcSize / 2 : 1;

            cubemap->transitionMipLayout(cmd, mip,
                                         VK_IMAGE_LAYOUT_GENERAL,
                                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                                         0,
                                         VK_ACCESS_TRANSFER_WRITE_BIT);

            VkImageBlit blit{};
            blit.srcSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
            blit.srcSubresource.mipLevel       = mip - 1;
            blit.srcSubresource.baseArrayLayer = 0;
            blit.srcSubresource.layerCount     = 6;   // all faces in one blit
            blit.srcOffsets[0] = {0, 0, 0};
            blit.srcOffsets[1] = {srcSize, srcSize, 1};
            blit.dstSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
            blit.dstSubresource.mipLevel       = mip;
            blit.dstSubresource.baseArrayLayer = 0;
            blit.dstSubresource.layerCount     = 6;
            blit.dstOffsets[0] = {0, 0, 0};
            blit.dstOffsets[1] = {dstSize, dstSize, 1};

            vkCmdBlitImage(cmd,
                           cubemap->getImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           cubemap->getImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           1, &blit, VK_FILTER_LINEAR);

            // This level becomes the next level's source.
            cubemap->transitionMipLayout(cmd, mip,
                                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                         VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                                         VK_ACCESS_TRANSFER_WRITE_BIT,
                                         VK_ACCESS_TRANSFER_READ_BIT);

            srcSize = dstSize;
        }

        // Every level is now in TRANSFER_SRC_OPTIMAL.
        cubemap->transitionLayout(cmd,
                                  VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                  VK_PIPELINE_STAGE_TRANSFER_BIT,
                                  VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                  VK_ACCESS_TRANSFER_READ_BIT,
                                  VK_ACCESS_SHADER_READ_BIT);
    } else {
        cubemap->transitionLayout(cmd,
                                  VK_IMAGE_LAYOUT_GENERAL,
                                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                  VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                  VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                  VK_ACCESS_SHADER_WRITE_BIT,
                                  VK_ACCESS_SHADER_READ_BIT);
    }

    m_device.endSingleTimeCommands(cmd);

    // Free descriptor sets — safe now that endSingleTimeCommands has waited for
    // the submission to complete.
    vkFreeDescriptorSets(logicalDevice, m_descriptorPool,
                         static_cast<uint32_t>(descriptorSets.size()), descriptorSets.data());

    return cubemap.release();
}

// ═══════════════════════════════════════════════════════════════
// Irradiance Map Generation
// ═══════════════════════════════════════════════════════════════

VulkanCubemap* IBLGenerator::generateIrradianceMap(VulkanCubemap* environment, uint32_t size, uint32_t samples) {
    VkDevice logicalDevice = m_device.getLogicalDevice();

    // Owned locally until the whole step succeeds. Everything between here
    // and the return can throw -- loading a compute shader, allocating a
    // descriptor set -- and this object is the only reference to a cubemap
    // holding an image, a sampler and a view per face per mip.
    std::unique_ptr<VulkanCubemap> irradiance(
        VulkanCubemap::createIrradianceMap(m_device, size));

    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushRange.offset = 0;
    pushRange.size = sizeof(ConvolutionPushConstants);

    VulkanComputePipeline pipeline(m_device,
                                   m_shaderBasePath + "irradiance_convolution.comp.spv",
                                   {m_convolutionLayout},
                                   {pushRange});

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &m_convolutionLayout;

    // One set per dispatch — see convertEquirectToCubemap for why a shared set
    // silently collapses all six faces onto the last one.
    std::vector<VkDescriptorSet> descriptorSets(6, VK_NULL_HANDLE);
    for (auto& set : descriptorSets) {
        if (vkAllocateDescriptorSets(logicalDevice, &allocInfo, &set) != VK_SUCCESS) {
            throw std::runtime_error("Failed to allocate IBL irradiance descriptor set!");
        }
    }

    VkCommandBuffer cmd = m_device.beginSingleTimeCommands();

    irradiance->transitionLayout(cmd,
                                 VK_IMAGE_LAYOUT_UNDEFINED,
                                 VK_IMAGE_LAYOUT_GENERAL,
                                 VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                 VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                 0,
                                 VK_ACCESS_SHADER_WRITE_BIT);

    for (uint32_t face = 0; face < 6; ++face) {
        VkDescriptorImageInfo inputInfo{};
        inputInfo.sampler = environment->getSampler();
        inputInfo.imageView = environment->getCubeView();
        inputInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkDescriptorImageInfo outputInfo{};
        outputInfo.imageView = irradiance->getFaceView(face, 0);
        outputInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

        VkDescriptorSet descriptorSet = descriptorSets[face];

        std::array<VkWriteDescriptorSet, 2> writes{};
        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = descriptorSet;
        writes[0].dstBinding = 0;
        writes[0].descriptorCount = 1;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[0].pImageInfo = &inputInfo;

        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = descriptorSet;
        writes[1].dstBinding = 1;
        writes[1].descriptorCount = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        writes[1].pImageInfo = &outputInfo;

        vkUpdateDescriptorSets(logicalDevice, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);

        pipeline.bind(cmd);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.getLayout(), 0, 1, &descriptorSet, 0, nullptr);

        ConvolutionPushConstants pc{face, size, samples, 0.0f};
        vkCmdPushConstants(cmd, pipeline.getLayout(), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);

        uint32_t groupsX = (size + 7) / 8;
        uint32_t groupsY = (size + 7) / 8;
        pipeline.dispatch(cmd, groupsX, groupsY, 1);

        VkMemoryBarrier memBarrier{};
        memBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        memBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        memBarrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             0, 1, &memBarrier, 0, nullptr, 0, nullptr);
    }

    irradiance->transitionLayout(cmd,
                                 VK_IMAGE_LAYOUT_GENERAL,
                                 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                 VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                 VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                 VK_ACCESS_SHADER_WRITE_BIT,
                                 VK_ACCESS_SHADER_READ_BIT);

    m_device.endSingleTimeCommands(cmd);
    vkFreeDescriptorSets(logicalDevice, m_descriptorPool,
                         static_cast<uint32_t>(descriptorSets.size()), descriptorSets.data());

    return irradiance.release();
}

// ═══════════════════════════════════════════════════════════════
// Prefiltered Environment Map Generation
// ═══════════════════════════════════════════════════════════════

VulkanCubemap* IBLGenerator::generatePrefilterMap(VulkanCubemap* environment, uint32_t size, uint32_t samples) {
    VkDevice logicalDevice = m_device.getLogicalDevice();

    // Owned locally until the whole step succeeds. Everything between here
    // and the return can throw -- loading a compute shader, allocating a
    // descriptor set -- and this object is the only reference to a cubemap
    // holding an image, a sampler and a view per face per mip.
    std::unique_ptr<VulkanCubemap> prefilter(
        VulkanCubemap::createPrefilterMap(m_device, size));
    uint32_t mipLevels = prefilter->getMipLevels();

    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushRange.offset = 0;
    pushRange.size = sizeof(ConvolutionPushConstants);

    VulkanComputePipeline pipeline(m_device,
                                   m_shaderBasePath + "prefilter_convolution.comp.spv",
                                   {m_convolutionLayout},
                                   {pushRange});

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &m_convolutionLayout;

    // One set per dispatch: 6 faces x mipLevels mips, all recorded into a single
    // command buffer. See convertEquirectToCubemap for the failure a shared set
    // produces.
    std::vector<VkDescriptorSet> descriptorSets(static_cast<size_t>(mipLevels) * 6, VK_NULL_HANDLE);
    for (auto& set : descriptorSets) {
        if (vkAllocateDescriptorSets(logicalDevice, &allocInfo, &set) != VK_SUCCESS) {
            throw std::runtime_error("Failed to allocate IBL prefilter descriptor set!");
        }
    }

    VkCommandBuffer cmd = m_device.beginSingleTimeCommands();

    prefilter->transitionLayout(cmd,
                                VK_IMAGE_LAYOUT_UNDEFINED,
                                VK_IMAGE_LAYOUT_GENERAL,
                                VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                0,
                                VK_ACCESS_SHADER_WRITE_BIT);

    for (uint32_t mip = 0; mip < mipLevels; ++mip) {
        uint32_t mipSize = size >> mip;
        if (mipSize < 1) mipSize = 1;

        float roughness = static_cast<float>(mip) / static_cast<float>(mipLevels - 1);

        for (uint32_t face = 0; face < 6; ++face) {
            VkDescriptorImageInfo inputInfo{};
            inputInfo.sampler = environment->getSampler();
            inputInfo.imageView = environment->getCubeView();
            inputInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            VkDescriptorImageInfo outputInfo{};
            outputInfo.imageView = prefilter->getFaceView(face, mip);
            outputInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

            VkDescriptorSet descriptorSet = descriptorSets[static_cast<size_t>(mip) * 6 + face];

            std::array<VkWriteDescriptorSet, 2> writes{};
            writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[0].dstSet = descriptorSet;
            writes[0].dstBinding = 0;
            writes[0].descriptorCount = 1;
            writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[0].pImageInfo = &inputInfo;

            writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[1].dstSet = descriptorSet;
            writes[1].dstBinding = 1;
            writes[1].descriptorCount = 1;
            writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            writes[1].pImageInfo = &outputInfo;

            vkUpdateDescriptorSets(logicalDevice, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);

            pipeline.bind(cmd);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.getLayout(), 0, 1, &descriptorSet, 0, nullptr);

            ConvolutionPushConstants pc{face, mipSize, samples, roughness};
            vkCmdPushConstants(cmd, pipeline.getLayout(), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);

            uint32_t groupsX = (mipSize + 7) / 8;
            uint32_t groupsY = (mipSize + 7) / 8;
            pipeline.dispatch(cmd, std::max(1u, groupsX), std::max(1u, groupsY), 1);

            VkMemoryBarrier memBarrier{};
            memBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
            memBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            memBarrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                 0, 1, &memBarrier, 0, nullptr, 0, nullptr);
        }
    }

    prefilter->transitionLayout(cmd,
                                VK_IMAGE_LAYOUT_GENERAL,
                                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                VK_ACCESS_SHADER_WRITE_BIT,
                                VK_ACCESS_SHADER_READ_BIT);

    m_device.endSingleTimeCommands(cmd);
    vkFreeDescriptorSets(logicalDevice, m_descriptorPool,
                         static_cast<uint32_t>(descriptorSets.size()), descriptorSets.data());

    return prefilter.release();
}

// ═══════════════════════════════════════════════════════════════
// BRDF LUT Generation
// ═══════════════════════════════════════════════════════════════

VulkanTexture* IBLGenerator::generateBRDFLUT(uint32_t size, uint32_t samples) {
    // Generate BRDF LUT on CPU (compute shader version needs storage image support)
    // Using RGBA16F for broader compatibility, R=scale, G=bias, BA unused

    // CPU generation of BRDF LUT (more portable than compute shader version)
    std::vector<float> lutData(size * size * 4);  // RGBA format

    auto hammersley = [](uint32_t i, uint32_t N) -> std::pair<float, float> {
        uint32_t bits = i;
        bits = (bits << 16u) | (bits >> 16u);
        bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
        bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
        bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
        bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
        float radicalInverse = static_cast<float>(bits) * 2.3283064365386963e-10f;
        return {static_cast<float>(i) / static_cast<float>(N), radicalInverse};
    };

    const float PI = 3.14159265359f;

    for (uint32_t y = 0; y < size; ++y) {
        for (uint32_t x = 0; x < size; ++x) {
            float NdotV = std::max((static_cast<float>(x) + 0.5f) / static_cast<float>(size), 0.001f);
            float roughness = std::max((static_cast<float>(y) + 0.5f) / static_cast<float>(size), 0.001f);

            float Vx = std::sqrt(1.0f - NdotV * NdotV);
            float Vy = 0.0f;
            float Vz = NdotV;

            float A = 0.0f;
            float B = 0.0f;

            for (uint32_t i = 0; i < samples; ++i) {
                auto [xi1, xi2] = hammersley(i, samples);

                float a = roughness * roughness;
                float phi = 2.0f * PI * xi1;
                float cosTheta = std::sqrt((1.0f - xi2) / (1.0f + (a * a - 1.0f) * xi2));
                float sinTheta = std::sqrt(1.0f - cosTheta * cosTheta);

                float Hx = std::cos(phi) * sinTheta;
                float Hy = std::sin(phi) * sinTheta;
                float Hz = cosTheta;

                // L = reflect(-V, H). The normal is fixed at (0, 0, 1) for this
                // integration, so only Lz is ever read — Lx and Ly were computed
                // and discarded every iteration.
                float VdotH = Vx * Hx + Vy * Hy + Vz * Hz;
                float Lz = 2.0f * VdotH * Hz - Vz;

                float NdotL = std::max(Lz, 0.0f);
                float NdotH = std::max(Hz, 0.0f);
                VdotH = std::max(VdotH, 0.0f);

                if (NdotL > 0.0f) {
                    // Smith-Schlick k for the IBL case is roughness^2 / 2.
                    // `a` is already roughness^2 (used above for the GGX
                    // distribution), so squaring it again gave roughness^4 / 2 —
                    // far too little shadowing, so the LUT came out too bright
                    // across the mid-roughness band. brdf_lut.comp, sitting
                    // unused beside this, has always had it right.
                    float rough2 = roughness * roughness;
                    float k = rough2 / 2.0f;
                    float G1 = NdotV / (NdotV * (1.0f - k) + k);
                    float G2 = NdotL / (NdotL * (1.0f - k) + k);
                    float G = G1 * G2;
                    float G_Vis = (G * VdotH) / (NdotH * NdotV);
                    float Fc = std::pow(1.0f - VdotH, 5.0f);

                    A += (1.0f - Fc) * G_Vis;
                    B += Fc * G_Vis;
                }
            }

            A /= static_cast<float>(samples);
            B /= static_cast<float>(samples);

            uint32_t idx = (y * size + x) * 4;
            lutData[idx] = A;
            lutData[idx + 1] = B;
            lutData[idx + 2] = 0.0f;
            lutData[idx + 3] = 1.0f;
        }
    }

    // Create texture with computed BRDF data
    // Using R32G32B32A32 since we're providing float* data
    std::unique_ptr<VulkanTexture> brdfLUT(
        new VulkanTexture(m_device, lutData.data(), size, size, 4,
                          VK_FORMAT_R32G32B32A32_SFLOAT));

    return brdfLUT.release();
}

uint32_t IBLGenerator::getBytesPerPixel(VkFormat format) {
    switch (format) {
        case VK_FORMAT_R16G16_SFLOAT: return 4;
        case VK_FORMAT_R16G16B16A16_SFLOAT: return 8;
        case VK_FORMAT_R32G32B32A32_SFLOAT: return 16;
        default: return 4;
    }
}

} // namespace Shoonyakasha
