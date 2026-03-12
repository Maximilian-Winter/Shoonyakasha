//
// IBLGenerator.cpp - Image-Based Lighting texture generation implementation
//
// 朱雀司變  光明萬丈
// The Vermilion Bird governs transformation — radiance in all directions
//

#include "IBL/IBLGenerator.h"
#include "Vulkan/VulkanBuffer.h"
#include <stdexcept>
#include <iostream>
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
    // Pool sizes for all our descriptor sets
    std::array<VkDescriptorPoolSize, 2> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[0].descriptorCount = 64;  // Enough for multiple passes
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    poolSizes[1].descriptorCount = 64;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = 64;

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
    int width, height, channels;
    float* pixels = stbi_loadf(path.c_str(), &width, &height, &channels, STBI_rgb_alpha);

    if (!pixels) {
        throw std::runtime_error("Failed to load HDR image: " + path);
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

    // Step 1: Load HDR equirectangular image
    std::cout << "[IBL] Loading HDR texture..." << std::endl;
    VulkanTexture* equirect = loadHDRTexture(hdrPath);

    // Step 2: Convert to cubemap
    std::cout << "[IBL] Converting equirectangular to cubemap..." << std::endl;
    resources.environmentMap = convertEquirectToCubemap(equirect, params.environmentSize);

    // Wait for GPU to finish before destroying the input texture
    vkDeviceWaitIdle(m_device.getLogicalDevice());

    // Cleanup equirect texture (no longer needed)
    delete equirect;

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
    auto* cubemap = VulkanCubemap::createEnvironmentMap(m_device, cubeSize);

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

    // Allocate descriptor set
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &m_equirectLayout;

    VkDescriptorSet descriptorSet;
    vkAllocateDescriptorSets(logicalDevice, &allocInfo, &descriptorSet);

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

    // Transition to shader read optimal
    cubemap->transitionLayout(cmd,
                              VK_IMAGE_LAYOUT_GENERAL,
                              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                              VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                              VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                              VK_ACCESS_SHADER_WRITE_BIT,
                              VK_ACCESS_SHADER_READ_BIT);

    m_device.endSingleTimeCommands(cmd);

    // Free descriptor set
    vkFreeDescriptorSets(logicalDevice, m_descriptorPool, 1, &descriptorSet);

    return cubemap;
}

// ═══════════════════════════════════════════════════════════════
// Irradiance Map Generation
// ═══════════════════════════════════════════════════════════════

VulkanCubemap* IBLGenerator::generateIrradianceMap(VulkanCubemap* environment, uint32_t size, uint32_t samples) {
    VkDevice logicalDevice = m_device.getLogicalDevice();

    auto* irradiance = VulkanCubemap::createIrradianceMap(m_device, size);

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

    VkDescriptorSet descriptorSet;
    vkAllocateDescriptorSets(logicalDevice, &allocInfo, &descriptorSet);

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
    vkFreeDescriptorSets(logicalDevice, m_descriptorPool, 1, &descriptorSet);

    return irradiance;
}

// ═══════════════════════════════════════════════════════════════
// Prefiltered Environment Map Generation
// ═══════════════════════════════════════════════════════════════

VulkanCubemap* IBLGenerator::generatePrefilterMap(VulkanCubemap* environment, uint32_t size, uint32_t samples) {
    VkDevice logicalDevice = m_device.getLogicalDevice();

    auto* prefilter = VulkanCubemap::createPrefilterMap(m_device, size);
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

    VkDescriptorSet descriptorSet;
    vkAllocateDescriptorSets(logicalDevice, &allocInfo, &descriptorSet);

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
    vkFreeDescriptorSets(logicalDevice, m_descriptorPool, 1, &descriptorSet);

    return prefilter;
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

                float VdotH = Vx * Hx + Vy * Hy + Vz * Hz;
                float Lx = 2.0f * VdotH * Hx - Vx;
                float Ly = 2.0f * VdotH * Hy - Vy;
                float Lz = 2.0f * VdotH * Hz - Vz;

                float NdotL = std::max(Lz, 0.0f);
                float NdotH = std::max(Hz, 0.0f);
                VdotH = std::max(VdotH, 0.0f);

                if (NdotL > 0.0f) {
                    float k = (a * a) / 2.0f;
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
    auto* brdfLUT = new VulkanTexture(m_device, lutData.data(), size, size, 4, VK_FORMAT_R32G32B32A32_SFLOAT);

    return brdfLUT;
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
