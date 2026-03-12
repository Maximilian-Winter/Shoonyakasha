//
// IBLGenerator.h - Image-Based Lighting texture generation
//
// 朱雀司變  光明萬丈
// The Vermilion Bird governs transformation — radiance in all directions
//

#pragma once

#include "Vulkan/VulkanDevice.h"
#include "Vulkan/VulkanCubemap.h"
#include "Vulkan/VulkanComputePipeline.h"
#include "Vulkan/VulkanDescriptorSystem.h"
#include "Vulkan/VulkanTexture.h"
#include <memory>
#include <string>

namespace Shoonyakasha {

struct IBLGenerationParams {
    uint32_t environmentSize = 1024;   // Environment cubemap face size
    uint32_t irradianceSize = 32;      // Irradiance cubemap face size
    uint32_t prefilterSize = 512;      // Prefilter cubemap face size (max)
    uint32_t brdfLUTSize = 512;        // BRDF LUT dimensions
    uint32_t irradianceSamples = 2048; // Hemisphere samples for irradiance
    uint32_t prefilterSamples = 1024;  // GGX samples per roughness level
    uint32_t brdfSamples = 1024;       // BRDF integration samples
};

struct IBLResources {
    VulkanCubemap* environmentMap = nullptr;  // Converted from equirectangular
    VulkanCubemap* irradianceMap = nullptr;   // Diffuse IBL
    VulkanCubemap* prefilterMap = nullptr;    // Specular IBL with roughness mips
    VulkanTexture* brdfLUT = nullptr;         // Split-sum approximation LUT

    // Check if all resources are valid
    bool isValid() const {
        return environmentMap && irradianceMap && prefilterMap && brdfLUT;
    }

    // Cleanup resources (caller's responsibility)
    void destroy() {
        delete environmentMap;
        delete irradianceMap;
        delete prefilterMap;
        delete brdfLUT;
        environmentMap = nullptr;
        irradianceMap = nullptr;
        prefilterMap = nullptr;
        brdfLUT = nullptr;
    }
};

class IBLGenerator {
public:
    explicit IBLGenerator(VulkanDevice& device, const std::string& shaderBasePath = "shaders/ibl/");
    ~IBLGenerator();

    // Non-copyable
    IBLGenerator(const IBLGenerator&) = delete;
    IBLGenerator& operator=(const IBLGenerator&) = delete;

    // Main generation entry point
    // Takes path to equirectangular HDR and returns all IBL resources
    IBLResources generate(const std::string& hdrPath,
                          const IBLGenerationParams& params = IBLGenerationParams{});

    // Step-by-step generation (for custom pipelines)
    VulkanCubemap* convertEquirectToCubemap(VulkanTexture* equirect, uint32_t cubeSize);
    VulkanCubemap* generateIrradianceMap(VulkanCubemap* environment, uint32_t size, uint32_t samples);
    VulkanCubemap* generatePrefilterMap(VulkanCubemap* environment, uint32_t size, uint32_t samples);
    VulkanTexture* generateBRDFLUT(uint32_t size, uint32_t samples);

private:
    VulkanDevice& m_device;
    std::string m_shaderBasePath;

    // Descriptor set layout for compute pipelines
    VkDescriptorSetLayout m_equirectLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_convolutionLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_brdfLayout = VK_NULL_HANDLE;

    // Descriptor pool
    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;

    // Samplers
    VkSampler m_linearSampler = VK_NULL_HANDLE;

    // Pipeline initialization
    void createDescriptorLayouts();
    void createDescriptorPool();
    void createSamplers();
    void cleanup();

    // Helper to load HDR texture
    VulkanTexture* loadHDRTexture(const std::string& path);

    // Get bytes per pixel for format
    static uint32_t getBytesPerPixel(VkFormat format);
};

} // namespace Shoonyakasha
using Shoonyakasha::IBLGenerationParams;
using Shoonyakasha::IBLResources;
using Shoonyakasha::IBLGenerator;
