//
// Shoonyakasha Engine - Render Graph Orchestrator
//
// 黃帝司中  調和而統御
// The Yellow Emperor governs the center — harmonizing and commanding
//

#include "Vulkan/FrameGraph/FrameGraph.h"
#include "Vulkan/FrameGraph/FrameGraphJson.h"
#include "Vulkan/FrameGraph/FrameGraphAnalyzer.h"
#include "Vulkan/FrameGraph/FrameGraphExport.h"
#include "Vulkan/FrameGraph/FrameGraphDebugger.h"
// ECS-based rendering replaces legacy SceneGeometryRenderer
#include "Vulkan/VulkanDevice.h"
#include "Vulkan/VulkanImage.h"
#include "Vulkan/VulkanBuffer.h"
#include "Vulkan/VulkanPipeline.h"
#include "Vulkan/VulkanCommandBuffer.h"  // For VulkanCommandBuilder::getHandle()
#include "Vulkan/VulkanDescriptorSystem.h"
#include "Resources/ResourceManager.h"
// ECS components used instead of legacy ShaderData
#include "ECS/Scene.h"
#include "Core/Logger.h"

// DotPathResolver Integration for declarative binding
#include "FrameGraph/DotPathResolver.h"
#include "FrameGraph/FrameGraphRenderer.h"  // Auto geometry rendering
#include "FrameGraph/StagingBufferManager.h"  // Phase 3: Ring-buffered CPU↔GPU staging
#include "Vulkan/FrameGraph/RenderTargetSaver.h"  // Render target screenshot support
#include "ECS/RenderComponents.h"
#include "ECS/SkeletonComponents.h"

#include <stdexcept>
#include <random>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <filesystem>

namespace Shoonyakasha {
namespace FrameGraph {

RenderGraph::RenderGraph(VulkanDevice& device, VulkanCommandManager& cmdManager)
    : m_device(device)
    , m_cmdManager(cmdManager)
    , m_executor(device, cmdManager)
{
    m_logger = new Logger("render_graph.log");
    m_logger->log(LogLevel::Info, "Render Graph created");
    createSyncPrimitives();
    initSystems();
}

RenderGraph::~RenderGraph() {
    m_logger->log(LogLevel::Info, "Destroying Render Graph");

    // Phase 3: Destroy staging manager before SSBO cleanup
    m_stagingManager.reset();

    // Phase 2: Unregister our targets from shared registry
    if (m_sharedBufferRegistry) {
        for (const auto& desc : m_builder.getBufferLayouts()) {
            if (!desc.target.empty()) {
                m_sharedBufferRegistry->unregisterBuffer(desc.target);
            }
        }
        // Unregister image targets
        for (const auto& [name, rt] : m_trackedRenderTargets) {
            if (!rt.target.empty()) {
                m_sharedBufferRegistry->unregisterImage(rt.target);
            }
        }
    }
    m_trackedRenderTargets.clear();

    // Clean up framebuffers before physical resources
    for (auto& compiled : m_compiled.compiledPasses) {
        for (auto fb : compiled.framebuffers) {
            if (fb != VK_NULL_HANDLE) {
                vkDestroyFramebuffer(m_device.getLogicalDevice(), fb, nullptr);
            }
        }
        compiled.framebuffers.clear();
    }

    // Clean up samplers
    for (auto& [name, sampler] : m_compiled.samplers) {
        if (sampler != VK_NULL_HANDLE) {
            vkDestroySampler(m_device.getLogicalDevice(), sampler, nullptr);
        }
    }
    m_compiled.samplers.clear();

    destroySyncPrimitives();
    delete m_logger;
}

// ═══════════════════════════════════════════════════════════════
// JSON Loading
// ═══════════════════════════════════════════════════════════════

void RenderGraph::loadFromFile(const std::string& filePath) {
    m_logger->log(LogLevel::Info, "Loading render graph from '%s'", filePath.c_str());
    loadGraphFromFile(m_builder, filePath);
    m_logger->log(LogLevel::Info, "Loaded %zu resources, %zu passes from JSON",
                  m_builder.getResourceDeclarations().size(),
                  m_builder.getPassDeclarations().size());
}

// ═══════════════════════════════════════════════════════════════
// Pass Callback Registration
// ═══════════════════════════════════════════════════════════════

void RenderGraph::registerPassCallback(const std::string& passName, PassExecuteFn fn) {
    m_callbacks[passName] = std::move(fn);
    m_logger->log(LogLevel::Info, "Registered callback for pass '%s'", passName.c_str());
}

void RenderGraph::registerGeometryRenderer(const std::string& executionType, PassExecuteFn fn) {
    // Normalize the type name (allow both "opaque" and "opaque_geometry")
    std::string normalizedType = executionType;
    if (executionType == "opaque") normalizedType = "opaque_geometry";
    else if (executionType == "transparent") normalizedType = "transparent_geometry";
    else if (executionType == "shadow") normalizedType = "shadow_casters";

    m_geometryTypeRenderers[normalizedType] = std::move(fn);
    m_logger->log(LogLevel::Info, "Registered geometry renderer for type '%s'", normalizedType.c_str());
}

void RenderGraph::registerPassPipeline(const std::string& passName, std::shared_ptr<VulkanPipeline> pipeline) {
    m_manualPipelines[passName] = std::move(pipeline);
    m_logger->log(LogLevel::Info, "Registered manual pipeline for pass '%s'", passName.c_str());
}

// ═══════════════════════════════════════════════════════════════
// Scene Binding (Declarative v3)
// 場景綁定 — Binding the scene for declarative rendering
// ═══════════════════════════════════════════════════════════════

void RenderGraph::bindScene(ECS::Scene* scene, ResourceManager* resourceManager) {
    m_boundScene = scene;
    m_resourceManager = resourceManager;

    if (scene) {
        m_logger->log(LogLevel::Info, "Bound ECS scene for declarative rendering");
    }
}

void RenderGraph::unbindScene() {
    m_boundScene = nullptr;
    m_logger->log(LogLevel::Info, "Unbound ECS scene");
}

void RenderGraph::updateStandardBuffers(float deltaTime, uint32_t frameIndex) {
    m_lastDeltaTime = deltaTime;

    // Fill dot-path UBOs from SceneContext via dot-path resolution
    updateDotPathUBOs(frameIndex);
}

// createStandardBuffers removed — replaced by createDotPathUBOs

// ═══════════════════════════════════════════════════════════════
// Dot-Path UBOs — Declarative buffer creation and per-frame filling
// The JSON declares buffer layouts with source fields. The engine fills them.
// ═══════════════════════════════════════════════════════════════

void RenderGraph::createDotPathUBOs(uint32_t maxFramesInFlight) {
    const auto& compiledLayouts = m_compiled.bufferLayouts;

    for (const auto& [name, layout] : compiledLayouts) {
        // Only process UBO layouts that have dot-path sources
        if (layout.usage != BufferUsageType::UniformBuffer) continue;
        if (!layout.usesDotPathSources()) continue;
        if (layout.totalSize == 0) continue;

        m_logger->log(LogLevel::Info, "Creating dot-path UBO '%s' (%u bytes, %s)",
                      name.c_str(), layout.totalSize,
                      layout.updateFrequency == BufferUpdateFrequency::PerFrame ? "per_frame" : "manual");

        // Find the original BufferLayoutDesc for policies
        const auto& builderLayouts = m_builder.getBufferLayouts();
        auto descIt = std::find_if(builderLayouts.begin(), builderLayouts.end(),
            [&name](const BufferLayoutDesc& desc) { return desc.name == name; });

        DotPathUBO ubo;
        ubo.layoutName = name;
        ubo.size = layout.totalSize;
        ubo.updateFrequency = layout.updateFrequency;

        // Copy target/readback/save/memory policies from JSON declaration
        if (descIt != builderLayouts.end()) {
            ubo.target = descIt->target;
            ubo.memoryPolicy = descIt->memoryPolicy;
            ubo.readbackPolicy = descIt->readbackPolicy;
            ubo.savePolicy = descIt->savePolicy;
        }

        // Pre-convert the FrameGraph layout to Shoonyakasha resolver layout
        ubo.resolvedLayout.name = layout.name;
        ubo.resolvedLayout.totalSize = layout.totalSize;
        ubo.resolvedLayout.hasSceneSources = layout.hasSceneSources;
        ubo.resolvedLayout.hasEntitySources = layout.hasEntitySources;
        ubo.resolvedLayout.hasConstSources = layout.hasConstSources;

        for (const auto& field : layout.fields) {
            Shoonyakasha::BufferField resolvedField;
            resolvedField.name = field.name;
            resolvedField.source = field.source;
            resolvedField.offset = field.offset;

            switch (field.type) {
                case BufferFieldType::Float:  resolvedField.type = Shoonyakasha::MaterialParam::Type::Float; resolvedField.size = 4; break;
                case BufferFieldType::Vec2:   resolvedField.type = Shoonyakasha::MaterialParam::Type::Vec2;  resolvedField.size = 8; break;
                case BufferFieldType::Vec3:   resolvedField.type = Shoonyakasha::MaterialParam::Type::Vec3;  resolvedField.size = 12; break;
                case BufferFieldType::Vec4:   resolvedField.type = Shoonyakasha::MaterialParam::Type::Vec4;  resolvedField.size = 16; break;
                case BufferFieldType::Mat3:   resolvedField.type = Shoonyakasha::MaterialParam::Type::Mat3;  resolvedField.size = 36; break;
                case BufferFieldType::Mat4:   resolvedField.type = Shoonyakasha::MaterialParam::Type::Mat4;  resolvedField.size = 64; break;
                case BufferFieldType::Int:    resolvedField.type = Shoonyakasha::MaterialParam::Type::Int;   resolvedField.size = 4; break;
                case BufferFieldType::UInt:   resolvedField.type = Shoonyakasha::MaterialParam::Type::UInt;  resolvedField.size = 4; break;
                default:                      resolvedField.type = Shoonyakasha::MaterialParam::Type::Float; resolvedField.size = 4; break;
            }

            // Propagate array info for [i] expansion in fillSceneBuffer
            resolvedField.arrayCount = field.arrayCount;
            resolvedField.arrayStride = field.arrayStride;

            ubo.resolvedLayout.fields.push_back(resolvedField);
        }

        // Create per-frame VulkanBuffers
        ubo.perFrameBuffers.resize(maxFramesInFlight);
        for (uint32_t i = 0; i < maxFramesInFlight; i++) {
            ubo.perFrameBuffers[i] = std::make_unique<VulkanBuffer>(
                m_device,
                layout.totalSize,
                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
            );

            // Zero-initialize
            std::vector<uint8_t> zeros(layout.totalSize, 0);
            ubo.perFrameBuffers[i]->update(zeros.data(), layout.totalSize);
        }

        // Register in m_registeredUBOs for autoBindBuffer to find
        RegisteredUBO regUbo;
        for (uint32_t i = 0; i < maxFramesInFlight; i++) {
            regUbo.perFrameBuffers.push_back(ubo.perFrameBuffers[i]->getBuffer());
        }
        regUbo.size = layout.totalSize;
        m_registeredUBOs[name] = regUbo;

        m_dotPathUBOs[name] = std::move(ubo);

        m_logger->log(LogLevel::Info, "  Dot-path UBO '%s' created (%u frames, %u bytes)",
                      name.c_str(), maxFramesInFlight, layout.totalSize);
    }
}

void RenderGraph::updateDotPathUBOs(uint32_t frameIndex) {
    if (!m_sceneContext || !m_bufferResolver) return;

    for (auto& [name, ubo] : m_dotPathUBOs) {
        if (ubo.updateFrequency != BufferUpdateFrequency::PerFrame) continue;
        if (frameIndex >= ubo.perFrameBuffers.size()) continue;

        // Allocate temp buffer and fill from SceneContext via dot-paths
        std::vector<uint8_t> data(ubo.size, 0);
        m_bufferResolver->fillSceneBuffer(data.data(), ubo.resolvedLayout, *m_sceneContext);

        // Upload to per-frame GPU buffer
        ubo.perFrameBuffers[frameIndex]->update(data.data(), ubo.size);
    }
}

// ═══════════════════════════════════════════════════════════════
// Dot-Path SSBOs — Declarative storage buffer creation with initialization
// JSON declares struct layout, element count, and init strategy.
// Engine creates device-local buffers, generates initial data, uploads via staging.
// ═══════════════════════════════════════════════════════════════

static uint32_t getFieldSizeForInit(BufferFieldType type) {
    switch (type) {
        case BufferFieldType::Float:  case BufferFieldType::Int:
        case BufferFieldType::UInt:   case BufferFieldType::Bool:   return 4;
        case BufferFieldType::Vec2:   case BufferFieldType::IVec2:
        case BufferFieldType::UVec2:                                return 8;
        case BufferFieldType::Vec3:   case BufferFieldType::IVec3:
        case BufferFieldType::UVec3:                                return 12;
        case BufferFieldType::Vec4:   case BufferFieldType::IVec4:
        case BufferFieldType::UVec4:                                return 16;
        case BufferFieldType::Mat3:                                  return 36;
        case BufferFieldType::Mat4:                                  return 64;
        default:                                                     return 4;
    }
}

void RenderGraph::createDotPathSSBOs(uint32_t maxFramesInFlight) {
    const auto& compiledLayouts = m_compiled.bufferLayouts;
    const auto& builderLayouts = m_builder.getBufferLayouts();

    for (const auto& [name, layout] : compiledLayouts) {
        // Only process StorageBuffer layouts
        if (layout.usage != BufferUsageType::StorageBuffer) continue;

        // Find the original BufferLayoutDesc for elementCount and initConfig
        auto descIt = std::find_if(builderLayouts.begin(), builderLayouts.end(),
            [&name](const BufferLayoutDesc& desc) { return desc.name == name; });
        if (descIt == builderLayouts.end()) continue;
        const BufferLayoutDesc& desc = *descIt;

        if (desc.initConfig.type == "buffer_ref") continue;  // Handled by resolveBufferRefs()
        if (desc.elementCount == 0) continue;   // Not an SSBO array
        if (layout.totalSize == 0) continue;     // No fields

        uint32_t elementStride = layout.totalSize;
        VkDeviceSize bufferSize = static_cast<VkDeviceSize>(elementStride) * desc.elementCount;

        m_logger->log(LogLevel::Info, "Creating dot-path SSBO '%s' (%u elements x %u bytes = %.2f MB)",
                      name.c_str(), desc.elementCount, elementStride,
                      bufferSize / (1024.0f * 1024.0f));

        // ─── Generate initial data ────────────────────────────────────
        std::vector<uint8_t> initialData(bufferSize, 0);

        // Phase 4: Load binary data from file
        if (desc.initConfig.type == "file") {
            std::ifstream file(desc.initConfig.filePath, std::ios::binary | std::ios::ate);
            if (!file.is_open()) {
                m_logger->log(LogLevel::Warning,
                    "  SSBO '%s': Cannot open file '%s' — using zeros",
                    name.c_str(), desc.initConfig.filePath.c_str());
            } else {
                size_t fileSize = static_cast<size_t>(file.tellg());
                file.seekg(0, std::ios::beg);
                if (fileSize != static_cast<size_t>(bufferSize)) {
                    m_logger->log(LogLevel::Warning,
                        "  SSBO '%s': File size mismatch — expected %zu, got %zu. Using zeros.",
                        name.c_str(), static_cast<size_t>(bufferSize), fileSize);
                } else {
                    file.read(reinterpret_cast<char*>(initialData.data()), fileSize);
                    m_logger->log(LogLevel::Info, "  Loaded %zu bytes from '%s'",
                                  fileSize, desc.initConfig.filePath.c_str());
                }
                file.close();
            }
        } else if (desc.initConfig.type == "initializer") {
            std::mt19937 rng(desc.initConfig.seed);

            // Build field offset/type map for quick lookup
            struct FieldInfo {
                uint32_t offset;
                BufferFieldType type;
                uint32_t size;
            };
            std::unordered_map<std::string, FieldInfo> fieldMap;
            for (const auto& field : layout.fields) {
                fieldMap[field.name] = { field.offset, field.type, getFieldSizeForInit(field.type) };
            }

            // Initialize each element
            for (uint32_t elemIdx = 0; elemIdx < desc.elementCount; ++elemIdx) {
                uint8_t* elementBase = initialData.data() + (static_cast<size_t>(elemIdx) * elementStride);

                for (const auto& fieldInit : desc.initConfig.fieldInits) {
                    auto it = fieldMap.find(fieldInit.fieldName);
                    if (it == fieldMap.end()) {
                        if (elemIdx == 0) {
                            m_logger->log(LogLevel::Warning, "  SSBO '%s': init field '%s' not found in layout",
                                          name.c_str(), fieldInit.fieldName.c_str());
                        }
                        continue;
                    }

                    const FieldInfo& fi = it->second;
                    uint8_t* fieldPtr = elementBase + fi.offset;

                    switch (fieldInit.type) {
                        case SSBOFieldInitType::RandomRange: {
                            // Generate random values per component, write only what the field needs
                            float values[4];
                            for (int c = 0; c < 4; ++c) {
                                std::uniform_real_distribution<float> dist(fieldInit.min[c], fieldInit.max[c]);
                                values[c] = dist(rng);
                            }
                            std::memcpy(fieldPtr, values, fi.size);
                            break;
                        }
                        case SSBOFieldInitType::Constant: {
                            std::memcpy(fieldPtr, fieldInit.value.data(), fi.size);
                            break;
                        }

                        // Phase 5: Normal distribution per component
                        case SSBOFieldInitType::GaussianRange: {
                            float values[4];
                            for (int c = 0; c < 4; ++c) {
                                float sigma = std::max(1e-7f, fieldInit.stddev[c]);
                                std::normal_distribution<float> dist(fieldInit.mean[c], sigma);
                                values[c] = dist(rng);
                            }
                            std::memcpy(fieldPtr, values, fi.size);
                            break;
                        }

                        // Phase 5: 3D lattice placement from element index
                        case SSBOFieldInitType::Grid: {
                            uint32_t nx = std::max(1u, fieldInit.gridDimensions[0]);
                            uint32_t ny = std::max(1u, fieldInit.gridDimensions[1]);
                            uint32_t ix = elemIdx % nx;
                            uint32_t iy = (elemIdx / nx) % ny;
                            uint32_t iz = elemIdx / (nx * ny);
                            float values[4] = {
                                fieldInit.gridOrigin[0] + static_cast<float>(ix) * fieldInit.gridSpacing[0],
                                fieldInit.gridOrigin[1] + static_cast<float>(iy) * fieldInit.gridSpacing[1],
                                fieldInit.gridOrigin[2] + static_cast<float>(iz) * fieldInit.gridSpacing[2],
                                fieldInit.gridW
                            };
                            std::memcpy(fieldPtr, values, fi.size);
                            break;
                        }

                        // Phase 5: Spherical distribution (surface or volume)
                        case SSBOFieldInitType::Sphere: {
                            std::uniform_real_distribution<float> uDist(0.f, 1.f);
                            float u = uDist(rng);
                            float v = uDist(rng);
                            float theta = 2.0f * 3.14159265358979f * u;
                            float phi = std::acos(2.0f * v - 1.0f);

                            float r = fieldInit.sphereRadius;
                            if (!fieldInit.sphereSurface) {
                                // Volume: cube-root scaling for uniform density
                                float w = uDist(rng);
                                r *= std::cbrt(w);
                            }

                            float values[4] = {
                                fieldInit.sphereCenter[0] + r * std::sin(phi) * std::cos(theta),
                                fieldInit.sphereCenter[1] + r * std::sin(phi) * std::sin(theta),
                                fieldInit.sphereCenter[2] + r * std::cos(phi),
                                fieldInit.sphereW
                            };
                            std::memcpy(fieldPtr, values, fi.size);
                            break;
                        }
                    }
                }
            }

            m_logger->log(LogLevel::Info, "  Generated %u elements with seed %u",
                          desc.elementCount, desc.initConfig.seed);
        }

        // ─── Create device-local buffer ────────────────────────────────
        DotPathSSBO ssbo;
        ssbo.layoutName = name;
        ssbo.size = bufferSize;
        ssbo.elementCount = desc.elementCount;
        ssbo.elementStride = elementStride;

        // Phase 3: Copy memory/transfer policies from JSON declaration
        ssbo.memoryPolicy = desc.memoryPolicy;
        ssbo.readbackPolicy = desc.readbackPolicy;
        ssbo.updateFrequency = desc.updateFrequency;
        ssbo.updateFrequencyN = desc.updateFrequencyN;

        // Phase 4: Copy save policy
        ssbo.savePolicy = desc.savePolicy;

        // Phase 3: Add TRANSFER_SRC_BIT when readback or gpu_to_cpu/bidirectional is needed
        VkBufferUsageFlags usageFlags = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        if (desc.readbackPolicy.enabled ||
            desc.memoryPolicy.transferDirection == TransferDirection::GpuToCpu ||
            desc.memoryPolicy.transferDirection == TransferDirection::Bidirectional) {
            usageFlags |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        }

        ssbo.buffer = std::make_unique<VulkanBuffer>(
            m_device,
            bufferSize,
            usageFlags,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
        );

        // ─── Upload via staging buffer ─────────────────────────────────
        {
            auto staging = std::make_unique<VulkanBuffer>(
                m_device,
                bufferSize,
                VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
            );
            staging->map();
            staging->copyFrom(initialData.data(), bufferSize);
            staging->unmap();
            m_device.copyBuffer(staging->getBuffer(), ssbo.buffer->getBuffer(), bufferSize);
        }

        // ─── Register in m_registeredSSBOs for autoBindBuffer ──────────
        RegisteredSSBO regSsbo;
        regSsbo.buffers.push_back(ssbo.buffer->getBuffer());
        regSsbo.size = bufferSize;
        m_registeredSSBOs[name] = regSsbo;

        m_dotPathSSBOs[name] = std::move(ssbo);

        m_logger->log(LogLevel::Info, "  Dot-path SSBO '%s' created and registered", name.c_str());
    }
}

// ═══════════════════════════════════════════════════════════════
// Phase 2: Shared Buffer Registry — Target registration & buffer_ref resolution
// 共有の道 — The Way of Sharing
// ═══════════════════════════════════════════════════════════════

void RenderGraph::setSharedBufferRegistry(SharedBufferRegistry* registry) {
    m_sharedBufferRegistry = registry;
    m_logger->log(LogLevel::Info, "Shared buffer registry %s",
                  registry ? "connected" : "disconnected");
}

void RenderGraph::registerTargets() {
    if (!m_sharedBufferRegistry) return;

    const auto& builderLayouts = m_builder.getBufferLayouts();

    // ── Register SSBO targets ──
    for (const auto& desc : builderLayouts) {
        if (desc.target.empty()) continue;
        if (desc.usage != BufferUsageType::StorageBuffer) continue;

        auto ssboIt = m_dotPathSSBOs.find(desc.name);
        if (ssboIt != m_dotPathSSBOs.end()) {
            const DotPathSSBO& ssbo = ssboIt->second;

            SharedBufferEntry entry;
            entry.buffers = { ssbo.buffer->getBuffer() };
            entry.size = ssbo.size;
            entry.elementCount = ssbo.elementCount;
            entry.elementStride = ssbo.elementStride;
            m_sharedBufferRegistry->registerBuffer(desc.target, entry);

            m_logger->log(LogLevel::Info, "  Registered SSBO target '%s' from '%s' (%zu bytes)",
                          desc.target.c_str(), desc.name.c_str(), static_cast<size_t>(ssbo.size));
        } else {
            auto regIt = m_registeredSSBOs.find(desc.name);
            if (regIt != m_registeredSSBOs.end()) {
                SharedBufferEntry entry;
                entry.buffers = regIt->second.buffers;
                entry.size = regIt->second.size;
                entry.elementCount = desc.elementCount;
                entry.elementStride = 0;
                m_sharedBufferRegistry->registerBuffer(desc.target, entry);

                m_logger->log(LogLevel::Info, "  Registered target '%s' from external SSBO '%s'",
                              desc.target.c_str(), desc.name.c_str());
            } else {
                m_logger->log(LogLevel::Warning,
                    "  Target '%s' on layout '%s': buffer not found",
                    desc.target.c_str(), desc.name.c_str());
            }
        }
    }

    // ── Register UBO targets ──
    for (const auto& [name, ubo] : m_dotPathUBOs) {
        if (ubo.target.empty()) continue;

        SharedBufferEntry entry;
        for (const auto& buf : ubo.perFrameBuffers) {
            entry.buffers.push_back(buf->getBuffer());
        }
        entry.size = ubo.size;
        m_sharedBufferRegistry->registerBuffer(ubo.target, entry);

        m_logger->log(LogLevel::Info, "  Registered UBO target '%s' from '%s' (%zu bytes)",
                      ubo.target.c_str(), name.c_str(), static_cast<size_t>(ubo.size));
    }

    // ── Register render target image targets ──
    for (const auto& resDecl : m_builder.getResourceDeclarations()) {
        if (resDecl.target.empty()) continue;
        if (resDecl.kind != ResourceKind::Image) continue;

        auto handle = m_builder.getResource(resDecl.name);
        if (!handle.valid() || handle.index >= m_compiled.physicalResources.size()) continue;

        const auto* img = std::get_if<PhysicalImage>(&m_compiled.physicalResources[handle.index]);
        if (!img || img->vkImage == VK_NULL_HANDLE) continue;

        SharedImageEntry entry;
        entry.image = img->vkImage;
        entry.view = img->view;
        entry.format = img->format;
        entry.extent = img->extent;
        m_sharedBufferRegistry->registerImage(resDecl.target, entry);

        m_logger->log(LogLevel::Info, "  Registered render target '%s' from '%s' (%ux%u)",
                      resDecl.target.c_str(), resDecl.name.c_str(),
                      img->extent.width, img->extent.height);
    }
}

void RenderGraph::resolveBufferRefs() {
    if (!m_sharedBufferRegistry) return;

    const auto& builderLayouts = m_builder.getBufferLayouts();

    for (const auto& desc : builderLayouts) {
        if (desc.initConfig.type != "buffer_ref") continue;
        if (desc.initConfig.bufferRef.empty()) continue;

        // Skip if already created or registered
        if (m_dotPathSSBOs.contains(desc.name)) continue;
        if (m_registeredSSBOs.contains(desc.name)) continue;

        const SharedBufferEntry* entry = m_sharedBufferRegistry->getBuffer(desc.initConfig.bufferRef);
        if (!entry) {
            m_logger->log(LogLevel::Error,
                "  buffer_ref '%s' on layout '%s': target not found in shared registry. "
                "Ensure the producing graph is compiled first.",
                desc.initConfig.bufferRef.c_str(), desc.name.c_str());
            continue;
        }

        // Register the referenced buffer — no new buffer created!
        RegisteredSSBO regSsbo;
        regSsbo.buffers = entry->buffers;
        regSsbo.size = entry->size;
        m_registeredSSBOs[desc.name] = regSsbo;

        m_logger->log(LogLevel::Info,
            "  Resolved buffer_ref '%s' -> layout '%s' (%zu bytes, version %zu)",
            desc.initConfig.bufferRef.c_str(), desc.name.c_str(),
            static_cast<size_t>(entry->size), static_cast<size_t>(entry->version));
    }
}

// ═══════════════════════════════════════════════════════════════
// Phase 3: Staging Buffer Management — CPU↔GPU data transfer
// 輪廻之流 — The wheel of data flows between worlds
// ═══════════════════════════════════════════════════════════════

void RenderGraph::createStagingBuffers(uint32_t maxFramesInFlight) {
    std::vector<StagingBufferManager::BufferConfig> configs;

    // ── SSBO staging configs ──
    for (const auto& [name, ssbo] : m_dotPathSSBOs) {
        bool needsUpload = (ssbo.memoryPolicy.transferDirection == TransferDirection::CpuToGpu ||
                            ssbo.memoryPolicy.transferDirection == TransferDirection::Bidirectional);
        bool needsReadback = ssbo.readbackPolicy.enabled ||
                             ssbo.memoryPolicy.transferDirection == TransferDirection::GpuToCpu ||
                             ssbo.memoryPolicy.transferDirection == TransferDirection::Bidirectional;

        if (!needsUpload && !needsReadback) continue;

        StagingBufferManager::BufferConfig cfg;
        cfg.name = name;
        cfg.gpuBuffer = ssbo.buffer->getBuffer();
        cfg.size = ssbo.size;
        cfg.elementCount = ssbo.elementCount;
        cfg.elementStride = ssbo.elementStride;

        cfg.hasUpload = needsUpload;
        cfg.uploadFrequency = ssbo.updateFrequency;
        cfg.uploadN = ssbo.updateFrequencyN;

        cfg.hasReadback = needsReadback;
        cfg.readbackFrequency = ssbo.readbackPolicy.frequency;
        cfg.readbackN = ssbo.readbackPolicy.n;
        cfg.ringDepthOverride = ssbo.readbackPolicy.ringDepth;

        configs.push_back(std::move(cfg));
    }

    // ── UBO staging configs ──
    for (const auto& [name, ubo] : m_dotPathUBOs) {
        bool needsReadback = ubo.readbackPolicy.enabled ||
                             ubo.memoryPolicy.transferDirection == TransferDirection::GpuToCpu ||
                             ubo.memoryPolicy.transferDirection == TransferDirection::Bidirectional;

        if (!needsReadback) continue;

        // UBOs are host-visible, so readback = direct memcpy (no GPU copy needed)
        // But we still use the staging manager for consistent callback/polling API
        StagingBufferManager::BufferConfig cfg;
        cfg.name = name;
        cfg.gpuBuffer = ubo.perFrameBuffers[0]->getBuffer();  // Use first frame buffer
        cfg.size = ubo.size;
        cfg.elementCount = 1;
        cfg.elementStride = static_cast<uint32_t>(ubo.size);

        cfg.hasUpload = false;  // UBOs are written by CPU directly
        cfg.hasReadback = true;
        cfg.readbackFrequency = ubo.readbackPolicy.frequency;
        cfg.readbackN = ubo.readbackPolicy.n;
        cfg.ringDepthOverride = ubo.readbackPolicy.ringDepth;

        configs.push_back(std::move(cfg));
    }

    // ── Image staging configs (render target readback) ──
    std::vector<StagingBufferManager::ImageConfig> imageConfigs;
    for (const auto& resDecl : m_builder.getResourceDeclarations()) {
        if (!resDecl.readbackPolicy.enabled) continue;
        if (resDecl.kind != ResourceKind::Image) continue;

        auto handle = m_builder.getResource(resDecl.name);
        if (!handle.valid() || handle.index >= m_compiled.physicalResources.size()) continue;

        const auto* img = std::get_if<PhysicalImage>(&m_compiled.physicalResources[handle.index]);
        if (!img || img->vkImage == VK_NULL_HANDLE) continue;

        // Calculate bytes per pixel
        uint32_t bpp = 4;  // default
        switch (img->format) {
            case VK_FORMAT_R8_UNORM: bpp = 1; break;
            case VK_FORMAT_R8G8_UNORM: bpp = 2; break;
            case VK_FORMAT_R8G8B8A8_UNORM: case VK_FORMAT_R8G8B8A8_SRGB:
            case VK_FORMAT_B8G8R8A8_UNORM: case VK_FORMAT_B8G8R8A8_SRGB:
            case VK_FORMAT_D32_SFLOAT: case VK_FORMAT_D24_UNORM_S8_UINT: bpp = 4; break;
            case VK_FORMAT_R16G16B16A16_SFLOAT: bpp = 8; break;
            case VK_FORMAT_R32G32B32A32_SFLOAT: bpp = 16; break;
            default: bpp = 4; break;
        }

        bool isDepth = (img->format == VK_FORMAT_D16_UNORM || img->format == VK_FORMAT_D32_SFLOAT ||
                        img->format == VK_FORMAT_D16_UNORM_S8_UINT || img->format == VK_FORMAT_D24_UNORM_S8_UINT ||
                        img->format == VK_FORMAT_D32_SFLOAT_S8_UINT);

        StagingBufferManager::ImageConfig imgCfg;
        imgCfg.name = resDecl.name;
        imgCfg.gpuImage = img->vkImage;
        imgCfg.format = img->format;
        imgCfg.extent = img->extent;
        imgCfg.bufferSize = static_cast<VkDeviceSize>(img->extent.width) * img->extent.height * bpp;
        imgCfg.aspectMask = isDepth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
        imgCfg.hasReadback = true;

        // Convert string frequency to enum
        if (resDecl.readbackPolicy.frequency == "per_frame") {
            imgCfg.readbackFrequency = BufferUpdateFrequency::PerFrame;
        } else if (resDecl.readbackPolicy.frequency == "every_n_frames") {
            imgCfg.readbackFrequency = BufferUpdateFrequency::EveryNFrames;
        } else if (resDecl.readbackPolicy.frequency == "once") {
            imgCfg.readbackFrequency = BufferUpdateFrequency::Once;
        } else {
            imgCfg.readbackFrequency = BufferUpdateFrequency::Manual;
        }
        imgCfg.readbackN = resDecl.readbackPolicy.n;
        imgCfg.ringDepthOverride = resDecl.readbackPolicy.ringDepth;

        imageConfigs.push_back(std::move(imgCfg));

        // Track this render target for save policy
        TrackedRenderTarget rt;
        rt.resourceName = resDecl.name;
        rt.target = resDecl.target;
        rt.readbackPolicy.enabled = resDecl.readbackPolicy.enabled;
        rt.readbackPolicy.callbackEnabled = resDecl.readbackPolicy.callbackEnabled;
        rt.readbackPolicy.n = resDecl.readbackPolicy.n;
        rt.savePolicy.enabled = resDecl.savePolicy.enabled;
        rt.savePolicy.path = resDecl.savePolicy.path;
        rt.savePolicy.trigger = resDecl.savePolicy.trigger;
        rt.savePolicy.n = resDecl.savePolicy.n;
        rt.savePolicy.autoCreateDirectories = resDecl.savePolicy.autoCreateDirectories;
        m_trackedRenderTargets[resDecl.name] = std::move(rt);
    }

    bool hasBufferStaging = !configs.empty();
    bool hasImageStaging = !imageConfigs.empty();

    if (!hasBufferStaging && !hasImageStaging) return;

    m_stagingManager = std::make_unique<StagingBufferManager>(m_device, m_logger);

    if (hasBufferStaging) {
        m_stagingManager->create(configs, maxFramesInFlight);
    }
    if (hasImageStaging) {
        m_stagingManager->createImages(imageConfigs, maxFramesInFlight);
    }

    // Re-register any previously registered callbacks
    for (const auto& [name, cb] : m_readbackCallbacks) {
        m_stagingManager->registerReadbackCallback(name, cb);
    }

    // Phase 4: Register file save callbacks
    // 保存之流 — The flow of preservation from GPU to disk
    for (auto& [name, ssbo] : m_dotPathSSBOs) {
        if (!ssbo.savePolicy.enabled) continue;

        std::string bufName = name;  // capture by value for lambda

        auto saveCallback = [this, bufName](const ReadbackResult& result) {
            // Chain: dynamically look up user callback (may be registered after compile)
            auto cbIt = m_readbackCallbacks.find(bufName);
            if (cbIt != m_readbackCallbacks.end() && cbIt->second) {
                cbIt->second(result);
            }

            auto ssboIt = m_dotPathSSBOs.find(bufName);
            if (ssboIt == m_dotPathSSBOs.end()) return;
            auto& ssbo = ssboIt->second;
            const auto& policy = ssbo.savePolicy;

            bool shouldSave = false;
            if (policy.trigger == "manual") {
                shouldSave = ssbo.savePending;
                if (shouldSave) ssbo.savePending = false;
            } else if (policy.trigger == "every_n_frames") {
                shouldSave = (policy.n > 0) && (result.frameNumber % policy.n == 0);
            } else if (policy.trigger == "on_readback") {
                shouldSave = true;
            }
            if (!shouldSave) return;

            // Ensure parent directory exists
            if (policy.autoCreateDirectories) {
                std::filesystem::path fp(policy.path);
                if (!fp.parent_path().empty()) {
                    std::error_code ec;
                    std::filesystem::create_directories(fp.parent_path(), ec);
                }
            }

            std::ofstream file(policy.path, std::ios::binary);
            if (file.is_open()) {
                file.write(static_cast<const char*>(result.data), result.size);
                file.close();
                m_logger->log(LogLevel::Info, "Saved %zu bytes from '%s' to '%s' (frame %llu)",
                    static_cast<size_t>(result.size), bufName.c_str(), policy.path.c_str(),
                    static_cast<unsigned long long>(result.frameNumber));
            } else {
                m_logger->log(LogLevel::Error, "Failed to open '%s' for saving buffer '%s'",
                    policy.path.c_str(), bufName.c_str());
            }
        };

        m_stagingManager->registerReadbackCallback(bufName, saveCallback);
        m_logger->log(LogLevel::Info, "  Registered save callback for SSBO '%s' (trigger: %s, path: %s)",
                      bufName.c_str(), ssbo.savePolicy.trigger.c_str(), ssbo.savePolicy.path.c_str());
    }

    // ── UBO save callbacks ──
    for (auto& [name, ubo] : m_dotPathUBOs) {
        if (!ubo.savePolicy.enabled) continue;

        std::string bufName = name;

        auto saveCallback = [this, bufName](const ReadbackResult& result) {
            auto cbIt = m_readbackCallbacks.find(bufName);
            if (cbIt != m_readbackCallbacks.end() && cbIt->second) {
                cbIt->second(result);
            }

            auto uboIt = m_dotPathUBOs.find(bufName);
            if (uboIt == m_dotPathUBOs.end()) return;
            auto& ubo = uboIt->second;
            const auto& policy = ubo.savePolicy;

            bool shouldSave = false;
            if (policy.trigger == "manual") {
                shouldSave = ubo.savePending;
                if (shouldSave) ubo.savePending = false;
            } else if (policy.trigger == "every_n_frames") {
                shouldSave = (policy.n > 0) && (result.frameNumber % policy.n == 0);
            } else if (policy.trigger == "on_readback") {
                shouldSave = true;
            }
            if (!shouldSave) return;

            if (policy.autoCreateDirectories) {
                std::filesystem::path fp(policy.path);
                if (!fp.parent_path().empty()) {
                    std::error_code ec;
                    std::filesystem::create_directories(fp.parent_path(), ec);
                }
            }

            std::ofstream file(policy.path, std::ios::binary);
            if (file.is_open()) {
                file.write(static_cast<const char*>(result.data), result.size);
                file.close();
                m_logger->log(LogLevel::Info, "Saved UBO %zu bytes from '%s' to '%s' (frame %llu)",
                    static_cast<size_t>(result.size), bufName.c_str(), policy.path.c_str(),
                    static_cast<unsigned long long>(result.frameNumber));
            } else {
                m_logger->log(LogLevel::Error, "Failed to open '%s' for saving UBO '%s'",
                    policy.path.c_str(), bufName.c_str());
            }
        };

        m_stagingManager->registerReadbackCallback(bufName, saveCallback);
        m_logger->log(LogLevel::Info, "  Registered save callback for UBO '%s' (trigger: %s, path: %s)",
                      bufName.c_str(), ubo.savePolicy.trigger.c_str(), ubo.savePolicy.path.c_str());
    }

    // ── Render target image save callbacks ──
    for (auto& [name, rt] : m_trackedRenderTargets) {
        if (!rt.savePolicy.enabled) continue;

        std::string resName = name;

        auto imageSaveCallback = [this, resName](const ReadbackResult& result) {
            // Chain to user callback
            auto cbIt = m_readbackCallbacks.find(resName);
            if (cbIt != m_readbackCallbacks.end() && cbIt->second) {
                cbIt->second(result);
            }

            auto rtIt = m_trackedRenderTargets.find(resName);
            if (rtIt == m_trackedRenderTargets.end()) return;
            auto& rt = rtIt->second;
            const auto& policy = rt.savePolicy;

            bool shouldSave = false;
            if (policy.trigger == "manual") {
                shouldSave = rt.savePending;
                if (shouldSave) rt.savePending = false;
            } else if (policy.trigger == "every_n_frames") {
                shouldSave = (policy.n > 0) && (result.frameNumber % policy.n == 0);
            } else if (policy.trigger == "on_readback") {
                shouldSave = true;
            }
            if (!shouldSave) return;

            if (policy.autoCreateDirectories) {
                std::filesystem::path fp(policy.path);
                if (!fp.parent_path().empty()) {
                    std::error_code ec;
                    std::filesystem::create_directories(fp.parent_path(), ec);
                }
            }

            // Use RenderTargetSaver's format conversion for image files
            // The readback result contains raw pixel data — save using stbi
            FrameGraph::RenderTargetSaver saver(m_device);
            // For images, we save the raw readback data through RenderTargetSaver's convert methods
            // But since we already have the pixel data, write directly for binary,
            // or use saveRenderTarget for image formats
            std::string ext = std::filesystem::path(policy.path).extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

            if (ext == ".bin" || ext == ".raw") {
                // Binary dump
                std::ofstream file(policy.path, std::ios::binary);
                if (file.is_open()) {
                    file.write(static_cast<const char*>(result.data), result.size);
                    file.close();
                    m_logger->log(LogLevel::Info, "Saved render target '%s' raw data to '%s' (frame %llu)",
                        resName.c_str(), policy.path.c_str(),
                        static_cast<unsigned long long>(result.frameNumber));
                }
            } else {
                // Image format (png, hdr, jpg, etc.) — use synchronous save since we have CPU data
                // Re-use saveRenderTarget which does the GPU readback internally
                // For periodic saves, we save directly from the readback data
                auto handle = m_builder.getResource(resName);
                if (handle.valid() && handle.index < m_compiled.physicalResources.size()) {
                    const auto* img = std::get_if<PhysicalImage>(&m_compiled.physicalResources[handle.index]);
                    if (img) {
                        saver.save(img->vkImage, img->format, img->extent,
                                   img->currentLayout, policy.path, m_logger);
                    }
                }
            }
        };

        m_stagingManager->registerImageReadbackCallback(resName, imageSaveCallback);
        m_logger->log(LogLevel::Info, "  Registered save callback for render target '%s' (trigger: %s, path: %s)",
                      resName.c_str(), rt.savePolicy.trigger.c_str(), rt.savePolicy.path.c_str());
    }
}

void RenderGraph::registerReadbackCallback(const std::string& bufferName, ReadbackCallbackFn cb) {
    m_readbackCallbacks[bufferName] = cb;

    // Only forward directly to staging manager if this buffer/image has no save callback.
    // When a save callback exists, it dynamically chains to m_readbackCallbacks.
    if (m_stagingManager) {
        // Check SSBOs
        auto ssboIt = m_dotPathSSBOs.find(bufferName);
        bool hasSaveCallback = (ssboIt != m_dotPathSSBOs.end() && ssboIt->second.savePolicy.enabled);

        // Check UBOs
        if (!hasSaveCallback) {
            auto uboIt = m_dotPathUBOs.find(bufferName);
            hasSaveCallback = (uboIt != m_dotPathUBOs.end() && uboIt->second.savePolicy.enabled);
        }

        // Check render targets
        if (!hasSaveCallback) {
            auto rtIt = m_trackedRenderTargets.find(bufferName);
            hasSaveCallback = (rtIt != m_trackedRenderTargets.end() && rtIt->second.savePolicy.enabled);
        }

        if (!hasSaveCallback) {
            m_stagingManager->registerReadbackCallback(bufferName, cb);
            m_stagingManager->registerImageReadbackCallback(bufferName, cb);
        }
    }
}

std::vector<ReadbackResult> RenderGraph::pollReadbacks() {
    if (m_stagingManager) {
        return m_stagingManager->pollReadbacks();
    }
    return {};
}

void RenderGraph::triggerSave(const std::string& bufferName) {
    // Check SSBOs
    auto ssboIt = m_dotPathSSBOs.find(bufferName);
    if (ssboIt != m_dotPathSSBOs.end()) {
        if (!ssboIt->second.savePolicy.enabled) {
            m_logger->log(LogLevel::Warning, "triggerSave: SSBO '%s' has no save policy", bufferName.c_str());
            return;
        }
        ssboIt->second.savePending = true;
        if (m_stagingManager) m_stagingManager->triggerReadback(bufferName);
        m_logger->log(LogLevel::Info, "Triggered save for SSBO '%s'", bufferName.c_str());
        return;
    }

    // Check UBOs
    auto uboIt = m_dotPathUBOs.find(bufferName);
    if (uboIt != m_dotPathUBOs.end()) {
        if (!uboIt->second.savePolicy.enabled) {
            m_logger->log(LogLevel::Warning, "triggerSave: UBO '%s' has no save policy", bufferName.c_str());
            return;
        }
        uboIt->second.savePending = true;
        if (m_stagingManager) m_stagingManager->triggerReadback(bufferName);
        m_logger->log(LogLevel::Info, "Triggered save for UBO '%s'", bufferName.c_str());
        return;
    }

    // Check render targets
    auto rtIt = m_trackedRenderTargets.find(bufferName);
    if (rtIt != m_trackedRenderTargets.end()) {
        if (!rtIt->second.savePolicy.enabled) {
            m_logger->log(LogLevel::Warning, "triggerSave: render target '%s' has no save policy", bufferName.c_str());
            return;
        }
        rtIt->second.savePending = true;
        if (m_stagingManager) m_stagingManager->triggerImageReadback(bufferName);
        m_logger->log(LogLevel::Info, "Triggered save for render target '%s'", bufferName.c_str());
        return;
    }

    m_logger->log(LogLevel::Warning, "triggerSave: '%s' not found (not SSBO, UBO, or render target)", bufferName.c_str());
}

// ═══════════════════════════════════════════════════════════════
// Render Target Save (Screenshot)
// ═══════════════════════════════════════════════════════════════

bool RenderGraph::saveRenderTarget(const std::string& resourceName, const std::string& path) {
    if (!m_compiled.valid) {
        if (m_logger) m_logger->log(LogLevel::Error, "saveRenderTarget: graph not compiled");
        return false;
    }

    // Look up resource by name
    auto handle = m_builder.getResource(resourceName);
    if (!handle.valid() || handle.index >= m_compiled.physicalResources.size()) {
        if (m_logger) m_logger->log(LogLevel::Error, "saveRenderTarget: resource '%s' not found",
                                     resourceName.c_str());
        return false;
    }

    const auto* img = std::get_if<PhysicalImage>(&m_compiled.physicalResources[handle.index]);
    if (!img) {
        if (m_logger) m_logger->log(LogLevel::Error, "saveRenderTarget: '%s' is not an image resource",
                                     resourceName.c_str());
        return false;
    }

    if (img->vkImage == VK_NULL_HANDLE) {
        if (m_logger) m_logger->log(LogLevel::Error, "saveRenderTarget: '%s' has null VkImage",
                                     resourceName.c_str());
        return false;
    }

    // Delegate to RenderTargetSaver
    FrameGraph::RenderTargetSaver saver(m_device);
    return saver.save(img->vkImage, img->format, img->extent,
                      img->currentLayout, path, m_logger);
}

// ═══════════════════════════════════════════════════════════════
// Named Parameters (for push constants)
// ═══════════════════════════════════════════════════════════════

void RenderGraph::setParameter(const std::string& name, float value) {
    m_parameters[name] = value;
}

void RenderGraph::setParameter(const std::string& name, int32_t value) {
    m_parameters[name] = value;
}

void RenderGraph::setParameter(const std::string& name, uint32_t value) {
    m_parameters[name] = value;
}

void RenderGraph::setParameter(const std::string& name, const std::array<float, 2>& value) {
    m_parameters[name] = value;
}

void RenderGraph::setParameter(const std::string& name, const std::array<float, 3>& value) {
    m_parameters[name] = value;
}

void RenderGraph::setParameter(const std::string& name, const std::array<float, 4>& value) {
    m_parameters[name] = value;
}

void RenderGraph::setParameter(const std::string& name, const std::array<float, 16>& value) {
    m_parameters[name] = value;
}

const ParameterValue* RenderGraph::getParameter(const std::string& name) const {
    auto it = m_parameters.find(name);
    if (it == m_parameters.end()) return nullptr;
    return &it->second;
}

// ═══════════════════════════════════════════════════════════════
// Uniform Buffer Management
// ═══════════════════════════════════════════════════════════════

void RenderGraph::registerUniformBuffer(const std::string& name,
                                         const std::vector<VkBuffer>& perFrameBuffers,
                                         VkDeviceSize size) {
    m_registeredUBOs[name] = RegisteredUBO{perFrameBuffers, size};
    m_logger->log(LogLevel::Info, "Registered UBO '%s' (%zu bytes, %zu frames)",
                  name.c_str(), size, perFrameBuffers.size());
}

void RenderGraph::registerStorageBuffer(const std::string& name, VkBuffer buffer, VkDeviceSize size) {
    m_registeredSSBOs[name] = RegisteredSSBO{{buffer}, size};
    m_logger->log(LogLevel::Info, "Registered SSBO '%s' (%zu bytes)",
                  name.c_str(), size);
}

void RenderGraph::registerStorageBuffer(const std::string& name,
                                         const std::vector<VkBuffer>& buffers,
                                         VkDeviceSize size) {
    m_registeredSSBOs[name] = RegisteredSSBO{buffers, size};
    m_logger->log(LogLevel::Info, "Registered SSBO '%s' (%zu bytes, %zu buffers)",
                  name.c_str(), size, buffers.size());
}

// Helper template for writing field data to managed UBOs
template<typename T>
void RenderGraph::writeUniformField(const std::string& uboName, const std::string& fieldName, const T& value) {
    auto it = m_managedUBOs.find(uboName);
    if (it == m_managedUBOs.end()) {
        m_logger->log(LogLevel::Warning, "setUniformField: UBO '%s' not found (is it framework-managed?)",
                      uboName.c_str());
        return;
    }

    ManagedUBO& ubo = it->second;

    auto offsetIt = ubo.fieldOffsets.find(fieldName);
    if (offsetIt == ubo.fieldOffsets.end()) {
        m_logger->log(LogLevel::Warning, "setUniformField: Field '%s' not found in UBO '%s'",
                      fieldName.c_str(), uboName.c_str());
        return;
    }

    uint32_t offset = offsetIt->second;

    // Write to all per-frame buffers using update() with offset
    for (auto& buffer : ubo.perFrameBuffers) {
        if (buffer) {
            buffer->update(&value, sizeof(T), offset);
        }
    }
}

void RenderGraph::setUniformField(const std::string& uboName, const std::string& fieldName, float value) {
    writeUniformField(uboName, fieldName, value);
}

void RenderGraph::setUniformField(const std::string& uboName, const std::string& fieldName, int32_t value) {
    writeUniformField(uboName, fieldName, value);
}

void RenderGraph::setUniformField(const std::string& uboName, const std::string& fieldName, const std::array<float, 2>& value) {
    writeUniformField(uboName, fieldName, value);
}

void RenderGraph::setUniformField(const std::string& uboName, const std::string& fieldName, const std::array<float, 3>& value) {
    writeUniformField(uboName, fieldName, value);
}

void RenderGraph::setUniformField(const std::string& uboName, const std::string& fieldName, const std::array<float, 4>& value) {
    writeUniformField(uboName, fieldName, value);
}

void RenderGraph::setUniformField(const std::string& uboName, const std::string& fieldName, const std::array<float, 16>& value) {
    writeUniformField(uboName, fieldName, value);
}

// ═══════════════════════════════════════════════════════════════
// External Resource Import
// ═══════════════════════════════════════════════════════════════

void RenderGraph::importSwapchainImage(uint32_t swapchainIndex,
                                        VkImage image, VkImageView view,
                                        VkFormat format, VkExtent2D extent) {
    auto& data = m_importedImages["swapchain"];
    data.format = format;
    data.extent = extent;
    if (data.entries.size() <= swapchainIndex) {
        data.entries.resize(swapchainIndex + 1);
    }
    data.entries[swapchainIndex] = {image, view};
}

void RenderGraph::importExternalImage(const std::string& name,
                                       VkImage image, VkImageView view,
                                       VkFormat format, VkExtent2D extent) {
    auto& data = m_importedImages[name];
    data.format = format;
    data.extent = extent;
    data.entries = {{image, view}};
}

void RenderGraph::importExternalBuffer(const std::string& name,
                                        VkBuffer buffer, VkDeviceSize size) {
    m_importedBuffers[name] = ImportedBufferData{buffer, size};
}

// ═══════════════════════════════════════════════════════════════
// Build Import Map — translate name-based imports to index-based
// ═══════════════════════════════════════════════════════════════

ImportedImageMap RenderGraph::buildImportMap() const {
    ImportedImageMap importMap;

    for (const auto& [name, data] : m_importedImages) {
        auto handle = m_builder.getResource(name);
        if (!handle.valid()) continue;

        ImportedImageInfo info;
        info.format = data.format;
        info.extent = data.extent;

        for (const auto& entry : data.entries) {
            info.images.push_back(entry.image);
            info.views.push_back(entry.view);
        }

        importMap[handle.index] = std::move(info);
    }

    return importMap;
}

// ═══════════════════════════════════════════════════════════════
// Framework-Managed UBO Creation
// 氣之流轉 — The flow of qi (data)
// ═══════════════════════════════════════════════════════════════

void RenderGraph::createManagedUBOs(uint32_t maxFramesInFlight) {
    const auto& uboDescs = m_builder.getUniformBuffers();

    for (const auto& desc : uboDescs) {
        // Skip if not framework-managed or already registered externally
        if (!desc.frameworkManaged) continue;
        if (m_registeredUBOs.contains(desc.name)) continue;
        if (m_managedUBOs.contains(desc.name)) continue;

        ManagedUBO ubo;
        ubo.size = desc.size;

        // Build field offset/size maps
        for (const auto& field : desc.fields) {
            ubo.fieldOffsets[field.name] = field.offset;
            ubo.fieldSizes[field.name] = field.size;
        }

        // Create per-frame buffers
        uint32_t frameCount = desc.perFrame ? maxFramesInFlight : 1;
        ubo.perFrameBuffers.resize(frameCount);

        for (uint32_t i = 0; i < frameCount; ++i) {
            ubo.perFrameBuffers[i] = std::make_unique<VulkanBuffer>(
                m_device,
                desc.size,
                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
            );

            // Zero-initialize the buffer
            std::vector<uint8_t> zeros(desc.size, 0);
            ubo.perFrameBuffers[i]->update(zeros.data(), desc.size);
        }

        m_logger->log(LogLevel::Info, "Created framework-managed UBO '%s' (%u bytes, %u frames, %zu fields)",
                      desc.name.c_str(), desc.size, frameCount, desc.fields.size());

        m_managedUBOs[desc.name] = std::move(ubo);
    }
}

void RenderGraph::bindUBOsToDescriptorSets(uint32_t maxFramesInFlight) {
    const auto& layoutDescs = m_builder.getDescriptorSetLayouts();

    for (const auto& layoutDesc : layoutDescs) {
        auto setIt = m_compiled.namedDescriptorSets.find(layoutDesc.name);
        if (setIt == m_compiled.namedDescriptorSets.end()) continue;

        auto& descriptorSet = setIt->second;

        for (const auto& binding : layoutDesc.bindings) {
            if (binding.autoBindBuffer.empty()) continue;

            const std::string& uboName = binding.autoBindBuffer;
            std::string bindingName = binding.name.empty()
                ? "binding_" + std::to_string(binding.binding)
                : binding.name;

            // Check managed UBOs first
            auto managedIt = m_managedUBOs.find(uboName);
            if (managedIt != m_managedUBOs.end()) {
                ManagedUBO& ubo = managedIt->second;
                for (uint32_t frame = 0; frame < maxFramesInFlight && frame < ubo.perFrameBuffers.size(); ++frame) {
                    if (ubo.perFrameBuffers[frame]) {
                        BufferResource bufRes{
                            ubo.perFrameBuffers[frame]->getBuffer(),
                            0,
                            ubo.size
                        };
                        descriptorSet->bindBuffer(bindingName, frame, bufRes);
                        descriptorSet->updateSet(frame);
                    }
                }
                m_logger->log(LogLevel::Info, "  Bound managed UBO '%s' -> '%s' in layout '%s'",
                              uboName.c_str(), bindingName.c_str(), layoutDesc.name.c_str());
                continue;
            }

            // Check registered external UBOs
            auto registeredIt = m_registeredUBOs.find(uboName);
            if (registeredIt != m_registeredUBOs.end()) {
                RegisteredUBO& ubo = registeredIt->second;
                for (uint32_t frame = 0; frame < maxFramesInFlight && frame < ubo.perFrameBuffers.size(); ++frame) {
                    BufferResource bufRes{
                        ubo.perFrameBuffers[frame],
                        0,
                        ubo.size
                    };
                    descriptorSet->bindBuffer(bindingName, frame, bufRes);
                    descriptorSet->updateSet(frame);
                }
                m_logger->log(LogLevel::Info, "  Bound registered UBO '%s' -> '%s' in layout '%s'",
                              uboName.c_str(), bindingName.c_str(), layoutDesc.name.c_str());
                continue;
            }

            // Check registered SSBOs (storage buffers)
            auto ssboIt = m_registeredSSBOs.find(uboName);
            if (ssboIt != m_registeredSSBOs.end()) {
                RegisteredSSBO& ssbo = ssboIt->second;
                // SSBOs may have one buffer used for all frames, or per-frame buffers
                for (uint32_t frame = 0; frame < maxFramesInFlight; ++frame) {
                    uint32_t bufIdx = (frame < ssbo.buffers.size()) ? frame : 0;
                    BufferResource bufRes{
                        ssbo.buffers[bufIdx],
                        0,
                        ssbo.size
                    };
                    descriptorSet->bindBuffer(bindingName, frame, bufRes);
                    descriptorSet->updateSet(frame);
                }
                m_logger->log(LogLevel::Info, "  Bound registered SSBO '%s' -> '%s' in layout '%s'",
                              uboName.c_str(), bindingName.c_str(), layoutDesc.name.c_str());
                continue;
            }

            // Per-entity buffers (entity.*) are bound at draw time, not globally
            if (uboName.size() > 7 && uboName.substr(0, 7) == "entity.") {
                m_logger->log(LogLevel::Info, "  Buffer '%s' is per-entity — will be bound at draw time in layout '%s'",
                              uboName.c_str(), layoutDesc.name.c_str());
                continue;
            }

            m_logger->log(LogLevel::Warning, "  Buffer '%s' not found for auto-bind in layout '%s'",
                          uboName.c_str(), layoutDesc.name.c_str());
        }
    }
}

// ═══════════════════════════════════════════════════════════════
// Compilation
// ═══════════════════════════════════════════════════════════════

bool RenderGraph::compile(VkExtent2D referenceExtent, uint32_t swapchainImageCount,
                          uint32_t maxFramesInFlight) {
    m_logger->log(LogLevel::Info, "Compiling render graph (%ux%u, %u swapchain images, %u frames in flight)",
                  referenceExtent.width, referenceExtent.height, swapchainImageCount, maxFramesInFlight);

    // Store for DescriptorSetCache creation
    m_maxFramesInFlight = maxFramesInFlight;

    // Invalidate cached analysis
    m_cachedAnalysis.reset();  // unique_ptr::reset() clears the pointer

    // Apply callbacks to pass declarations before compilation
    applyCallbacks();

    // Create framework-managed UBOs from builder declarations
    createManagedUBOs(maxFramesInFlight);

    // Build import map: name-based imports → resource-index-based for compiler
    ImportedImageMap importMap = buildImportMap();

    // Compile with full import data, descriptor sets, and pipeline overrides
    m_compiled = m_compiler.compile(m_device, m_builder, referenceExtent,
                                     swapchainImageCount, importMap,
                                     maxFramesInFlight, m_manualPipelines);

    if (!m_compiled.valid) {
        m_logger->log(LogLevel::Error, "Render graph compilation failed: %s",
                      m_compiled.errorMessage.c_str());
    } else {
        // Create dot-path UBOs from compiled buffer layouts (replaces StandardBufferManager)
        createDotPathUBOs(maxFramesInFlight);

        // Create dot-path SSBOs from compiled buffer layouts (declarative storage buffers)
        createDotPathSSBOs(maxFramesInFlight);

        // Phase 2: Register SSBO targets in shared registry, then resolve buffer_ref sources
        registerTargets();
        resolveBufferRefs();

        // Phase 3: Create staging buffers for CPU↔GPU transfer (after SSBOs exist)
        createStagingBuffers(maxFramesInFlight);

        // Bind UBOs to descriptor sets after successful compilation
        bindUBOsToDescriptorSets(maxFramesInFlight);

        // Set up auto geometry renderers for passes with entityDataBinding
        setupAutoGeometryRenderers();

        m_logger->log(LogLevel::Info, "Render graph compiled successfully");
    }

    return m_compiled.valid;
}

bool RenderGraph::recompile(VkExtent2D referenceExtent, uint32_t swapchainImageCount,
                            uint32_t maxFramesInFlight) {
    m_logger->log(LogLevel::Info, "Recompiling render graph");

    // Phase 3: Destroy staging manager before recompile (will be recreated after)
    if (m_stagingManager) {
        m_stagingManager->destroy();
        m_stagingManager.reset();
    }

    // Phase 2: Unregister our targets before recompile (they'll be re-registered after)
    if (m_sharedBufferRegistry) {
        for (const auto& desc : m_builder.getBufferLayouts()) {
            if (!desc.target.empty()) {
                m_sharedBufferRegistry->unregisterBuffer(desc.target);
            }
        }
        // Unregister image targets
        for (const auto& [name, rt] : m_trackedRenderTargets) {
            if (!rt.target.empty()) {
                m_sharedBufferRegistry->unregisterImage(rt.target);
            }
        }
    }
    m_trackedRenderTargets.clear();

    // Clean up old framebuffers
    for (auto& compiled : m_compiled.compiledPasses) {
        for (auto fb : compiled.framebuffers) {
            if (fb != VK_NULL_HANDLE) {
                vkDestroyFramebuffer(m_device.getLogicalDevice(), fb, nullptr);
            }
        }
    }

    // Physical resources (owned VulkanImages/Buffers) are cleaned up
    // by their unique_ptrs when the CompileResult is replaced

    return compile(referenceExtent, swapchainImageCount, maxFramesInFlight);
}

// ═══════════════════════════════════════════════════════════════
// Execution
// ═══════════════════════════════════════════════════════════════

void RenderGraph::execute(uint32_t frameIndex, uint32_t swapchainImageIndex,
                           VkCommandBuffer commandBuffer) {
    if (!m_compiled.valid) {
        m_logger->log(LogLevel::Error, "Cannot execute: render graph not compiled");
        return;
    }

    // Update imported resources for this frame's swapchain image (for barriers)
    applyImports(swapchainImageIndex);

    // Phase 3: Process completed readbacks (fence guaranteed by app's vkWaitForFences)
    if (m_stagingManager) {
        m_stagingManager->processCompletedReadbacks(frameIndex, m_globalFrameNumber);
        m_stagingManager->processCompletedImageReadbacks(frameIndex, m_globalFrameNumber);
    }

    // Phase 3: Upload CPU→GPU before passes
    if (m_stagingManager) {
        m_stagingManager->recordUploadCommands(commandBuffer, frameIndex, m_globalFrameNumber);
    }

    // Execute all passes
    m_executor.execute(m_compiled, m_builder, frameIndex, swapchainImageIndex, commandBuffer, &m_parameters);

    // Phase 3: Readback GPU→CPU after passes (buffers and images)
    if (m_stagingManager) {
        m_stagingManager->recordReadbackCommands(commandBuffer, frameIndex, m_globalFrameNumber);

        // Update image layouts for tracked render targets before recording image readback
        for (const auto& [name, rt] : m_trackedRenderTargets) {
            auto handle = m_builder.getResource(rt.resourceName);
            if (handle.valid() && handle.index < m_compiled.physicalResources.size()) {
                const auto* img = std::get_if<PhysicalImage>(&m_compiled.physicalResources[handle.index]);
                if (img) {
                    m_stagingManager->updateImageLayout(rt.resourceName, img->currentLayout);
                }
            }
        }
        m_stagingManager->recordImageReadbackCommands(commandBuffer, frameIndex, m_globalFrameNumber);
    }

    m_globalFrameNumber++;
}

// ═══════════════════════════════════════════════════════════════
// Query
// ═══════════════════════════════════════════════════════════════

std::shared_ptr<VulkanDescriptorSet> RenderGraph::getDescriptorSet(const std::string& layoutName) const {
    auto it = m_compiled.namedDescriptorSets.find(layoutName);
    if (it == m_compiled.namedDescriptorSets.end()) return nullptr;
    return it->second;
}

const CompiledBufferLayout* RenderGraph::getBufferLayout(const std::string& name) const {
    auto it = m_compiled.bufferLayouts.find(name);
    if (it == m_compiled.bufferLayouts.end()) return nullptr;
    return &it->second;
}

bool RenderGraph::hasBufferLayout(const std::string& name) const {
    return m_compiled.bufferLayouts.contains(name);
}

VkImageView RenderGraph::getResourceView(const std::string& name) const {
    auto handle = m_builder.getResource(name);
    if (!handle.valid() || handle.index >= m_compiled.physicalResources.size()) {
        return VK_NULL_HANDLE;
    }

    if (const auto* img = std::get_if<PhysicalImage>(&m_compiled.physicalResources[handle.index])) {
        return img->view;
    }
    return VK_NULL_HANDLE;
}

// ═══════════════════════════════════════════════════════════════
// Internal Helpers
// ═══════════════════════════════════════════════════════════════

void RenderGraph::applyCallbacks() {
    auto& passes = m_builder.getPassDeclarations();
    for (auto& pass : passes) {
        // Apply manual callbacks (highest priority)
        auto it = m_callbacks.find(pass.name);
        if (it != m_callbacks.end()) {
            pass.executeFn = it->second;
        }

        // Apply scene renderer callbacks (for "scene_geometry" execution type)
        // Priority 1: Check by pass name (legacy/specific override)
        auto sceneIt = m_sceneRenderers.find(pass.name);
        if (sceneIt != m_sceneRenderers.end()) {
            pass.sceneRendererFn = sceneIt->second;
        }

        // Priority 2: Check by execution type (pipeline-agnostic!)
        // 幾何之道 — The way of geometry: type determines renderer, not pass name!
        if (!pass.sceneRendererFn) {
            auto typeIt = m_geometryTypeRenderers.find(pass.execution.type);
            if (typeIt != m_geometryTypeRenderers.end()) {
                pass.sceneRendererFn = typeIt->second;
                m_logger->log(LogLevel::Info,
                    "Using geometry renderer for pass '%s' (execution type: %s)",
                    pass.name.c_str(), pass.execution.type.c_str());
            }
        }

        // Priority 3: Auto-registration happens in setupAutoGeometryRenderers() after compile
    }
}

// ═══════════════════════════════════════════════════════════════
// Auto Geometry Renderers
// For passes with entityDataBinding, auto-register callbacks that use
// FrameGraphRenderer. No manual registration needed.
// ═══════════════════════════════════════════════════════════════

void RenderGraph::setupAutoGeometryRenderers() {
    if (!m_compiled.valid) return;

    // Create the FrameGraphRenderer if it doesn't exist
    if (!m_frameGraphRenderer) {
        m_frameGraphRenderer = std::make_unique<Shoonyakasha::FrameGraphRenderer>(*this);
        m_logger->log(LogLevel::Info, "FrameGraphRenderer created for automatic geometry rendering");
    }

    // Set the registry if we have a bound scene
    if (m_boundScene) {
        m_frameGraphRenderer->setRegistry(&m_boundScene->getRegistry());
    }

    // For each pass with entityDataBinding, auto-register a callback
    auto& passes = m_builder.getPassDeclarations();
    for (size_t i = 0; i < passes.size(); ++i) {
        auto& passDecl = passes[i];
        const auto& compiledPass = m_compiled.compiledPasses[i];

        // Skip if pass already has a callback
        if (passDecl.sceneRendererFn) continue;

        // Skip if no entityDataBinding
        if (!compiledPass.hasEntityDataBinding) continue;

        // Skip if not a geometry type
        const auto& type = passDecl.execution.type;
        if (type != "opaque_geometry" &&
            type != "transparent_geometry" &&
            type != "shadow_casters" &&
            type != "skinned_geometry" &&
            type != "skinned_transparent") continue;

        // Auto-register the callback!
        // Capture by value since passDecl reference may be invalidated
        const uint32_t passIndex = static_cast<uint32_t>(i);
        passDecl.sceneRendererFn = [this, passIndex](const PassExecuteContext& ctx) {
            const auto& compiled = m_compiled.compiledPasses[passIndex];
            const auto& decl = m_builder.getPassDeclarations()[passIndex];

            m_frameGraphRenderer->executeGeometryPass(
                compiled,
                decl,
                ctx.cmd.getHandle(),
                ctx.frameIndex
            );
        };

        m_logger->log(LogLevel::Info,
            "Auto-registered geometry renderer for pass '%s' (type: %s, binding: %s)",
            passDecl.name.c_str(),
            type.c_str(),
            compiledPass.entityDataBinding.name.c_str());
    }
}

void RenderGraph::applyImports(uint32_t swapchainImageIndex) {
    if (!m_compiled.valid) return;

    for (auto& [name, data] : m_importedImages) {
        auto handle = m_builder.getResource(name);
        if (!handle.valid() || handle.index >= m_compiled.physicalResources.size()) continue;

        auto* physImg = std::get_if<PhysicalImage>(&m_compiled.physicalResources[handle.index]);
        if (physImg) {
            // Use the specific swapchain image for this frame (for barriers)
            uint32_t idx = (swapchainImageIndex < static_cast<uint32_t>(data.entries.size()))
                         ? swapchainImageIndex : 0;
            if (idx < data.entries.size()) {
                physImg->vkImage = data.entries[idx].image;
                physImg->view = data.entries[idx].view;
            }
            physImg->format = data.format;
            physImg->extent = data.extent;
            physImg->currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        }
    }

    for (auto& [name, data] : m_importedBuffers) {
        auto handle = m_builder.getResource(name);
        if (!handle.valid() || handle.index >= m_compiled.physicalResources.size()) continue;

        auto* physBuf = std::get_if<PhysicalBuffer>(&m_compiled.physicalResources[handle.index]);
        if (physBuf) {
            physBuf->vkBuffer = data.buffer;
            physBuf->size = data.size;
        }
    }
}

// ═══════════════════════════════════════════════════════════════
// Multi-Queue Execution
// 雙流並行 — Dual streams in parallel
// ═══════════════════════════════════════════════════════════════

bool RenderGraph::needsMultiQueueSubmit() const {
    return m_compiled.valid && m_compiled.queueBatches.batches.size() > 1;
}

void RenderGraph::executeMultiQueue(uint32_t frameIndex, uint32_t swapchainImageIndex,
                                     VkCommandBuffer graphicsCmdBuf, VkCommandBuffer computeCmdBuf) {
    if (!m_compiled.valid) {
        m_logger->log(LogLevel::Error, "Cannot execute: render graph not compiled");
        return;
    }

    // Update imported resources for this frame
    applyImports(swapchainImageIndex);

    const auto& batches = m_compiled.queueBatches;

    // For each batch, record commands into the appropriate command buffer
    for (const auto& [queueType, passIndices] : batches.batches) {
        VkCommandBuffer cmdBuf = (queueType == QueueType::Compute) ? computeCmdBuf : graphicsCmdBuf;
        m_executor.executePasses(m_compiled, m_builder, passIndices,
                                 frameIndex, swapchainImageIndex, cmdBuf, &m_parameters);
    }
}

// ═══════════════════════════════════════════════════════════════
// Synchronization Primitives
// ═══════════════════════════════════════════════════════════════

void RenderGraph::createSyncPrimitives() {
    // Create timeline semaphore for multi-queue synchronization
    VkSemaphoreTypeCreateInfo timelineInfo{};
    timelineInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
    timelineInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    timelineInfo.initialValue = 0;

    VkSemaphoreCreateInfo semInfo{};
    semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    semInfo.pNext = &timelineInfo;

    if (vkCreateSemaphore(m_device.getLogicalDevice(), &semInfo, nullptr, &m_timelineSemaphore) != VK_SUCCESS) {
        m_logger->log(LogLevel::Warning, "Failed to create timeline semaphore — multi-queue disabled");
        m_timelineSemaphore = VK_NULL_HANDLE;
    } else {
        m_logger->log(LogLevel::Info, "Timeline semaphore created for multi-queue sync");
    }
    m_timelineValue = 0;
}

void RenderGraph::destroySyncPrimitives() {
    if (m_timelineSemaphore != VK_NULL_HANDLE) {
        vkDestroySemaphore(m_device.getLogicalDevice(), m_timelineSemaphore, nullptr);
        m_timelineSemaphore = VK_NULL_HANDLE;
    }
}

// ═══════════════════════════════════════════════════════════════
// Analysis API
// 玄武司察  深潛而見真
// The Dark Warrior investigates — diving deep to see truth
// ═══════════════════════════════════════════════════════════════

FrameGraphAnalyzer& RenderGraph::getOrCreateAnalyzer() const {
    if (!m_analyzer) {
        m_analyzer = std::make_unique<FrameGraphAnalyzer>();
    }
    return *m_analyzer;
}

AnalysisResult RenderGraph::validateDeclarations() const {
    return getOrCreateAnalyzer().analyzeDeclarations(m_builder);
}

CullingReport RenderGraph::simulateCulling() const {
    return getOrCreateAnalyzer().simulateCulling(m_builder);
}

AnalysisResult RenderGraph::analyze(VkExtent2D referenceExtent) const {
    if (!m_compiled.valid) {
        m_logger->log(LogLevel::Warning, "analyze(): Graph not compiled, returning declaration analysis only");
        return getOrCreateAnalyzer().analyzeDeclarations(m_builder);
    }

    // Use cached analysis if available
    if (m_cachedAnalysis) {
        return *m_cachedAnalysis;
    }

    // Perform full analysis and cache it
    m_cachedAnalysis = std::make_unique<AnalysisResult>(
        getOrCreateAnalyzer().analyzeCompiled(m_builder, m_compiled, referenceExtent)
    );
    return *m_cachedAnalysis;
}

CullingReport RenderGraph::getCullingReport() const {
    if (!m_compiled.valid) {
        return simulateCulling();
    }

    // Extract from cached analysis or compute
    AnalysisResult result = analyze();
    return result.cullingReport;
}

std::vector<BarrierAnalysis> RenderGraph::getBarrierAnalysis() const {
    if (!m_compiled.valid) {
        m_logger->log(LogLevel::Warning, "getBarrierAnalysis(): Graph not compiled");
        return {};
    }

    return getOrCreateAnalyzer().analyzeBarriers(m_builder, m_compiled);
}

std::vector<ResourceLifetime> RenderGraph::getResourceLifetimes(VkExtent2D referenceExtent) const {
    if (!m_compiled.valid) {
        m_logger->log(LogLevel::Warning, "getResourceLifetimes(): Graph not compiled");
        return {};
    }

    return getOrCreateAnalyzer().analyzeResourceLifetimes(m_builder, m_compiled, referenceExtent);
}

std::vector<DependencyEdge> RenderGraph::getDependencyGraph() const {
    return getOrCreateAnalyzer().buildDependencyGraph(m_builder);
}

// ═══════════════════════════════════════════════════════════════
// Export API
// 青龍司生  生發而有序
// The Azure Dragon governs growth — making the invisible visible
// ═══════════════════════════════════════════════════════════════

std::string RenderGraph::exportToDot() const {
    return exportToDot(ExportOptions{});
}

std::string RenderGraph::exportToDot(const ExportOptions& options) const {
    AnalysisResult analysis = analyze();
    return FrameGraph::exportToDot(m_builder, analysis, options);
}

bool RenderGraph::exportToDotFile(const std::string& path) const {
    return exportToDotFile(path, ExportOptions{});
}

bool RenderGraph::exportToDotFile(const std::string& path, const ExportOptions& options) const {
    AnalysisResult analysis = analyze();
    return FrameGraph::exportToDotFile(path, m_builder, analysis, options);
}

std::string RenderGraph::exportToJson() const {
    return exportToJson(ExportOptions{});
}

std::string RenderGraph::exportToJson(const ExportOptions& options) const {
    AnalysisResult analysis = analyze();
    return FrameGraph::exportToJson(m_builder, analysis, options);
}

bool RenderGraph::exportToJsonFile(const std::string& path) const {
    return exportToJsonFile(path, ExportOptions{});
}

bool RenderGraph::exportToJsonFile(const std::string& path, const ExportOptions& options) const {
    AnalysisResult analysis = analyze();
    return FrameGraph::exportToJsonFile(path, m_builder, analysis, options);
}

std::string RenderGraph::generateReport() const {
    return generateReport(ExportOptions{});
}

std::string RenderGraph::generateReport(const ExportOptions& options) const {
    AnalysisResult analysis = analyze();
    return generateMarkdownReport(m_builder, analysis, options);
}

std::string RenderGraph::generateSummary() const {
    return generateSummary(ExportOptions{});
}

std::string RenderGraph::generateSummary(const ExportOptions& options) const {
    AnalysisResult analysis = analyze();
    return generateTextSummary(analysis, options);
}

// ═══════════════════════════════════════════════════════════════
// Runtime Debug API
// 朱雀司變  熾熱而速達
// The Vermilion Bird governs transformation — blazing heat, swift arrival
// ═══════════════════════════════════════════════════════════════

void RenderGraph::enableDebugging() {
    if (!m_debugger) {
        m_debugger = std::make_unique<FrameGraphDebugger>();
    }
    m_debugger->enable();
    m_executor.setDebugger(m_debugger.get());
    m_logger->log(LogLevel::Info, "Frame graph debugging enabled");
}

void RenderGraph::disableDebugging() {
    if (m_debugger) {
        m_debugger->disable();
    }
    m_executor.setDebugger(nullptr);
    m_logger->log(LogLevel::Info, "Frame graph debugging disabled");
}

bool RenderGraph::isDebuggingEnabled() const {
    return m_debugger && m_debugger->isEnabled();
}

void RenderGraph::enableGpuTiming(uint32_t queryPoolSize) {
    if (!m_debugger) {
        m_debugger = std::make_unique<FrameGraphDebugger>();
    }
    m_debugger->enableGpuTiming(m_device, queryPoolSize);
    m_executor.setDebugger(m_debugger.get());
    m_logger->log(LogLevel::Info, "Frame graph GPU timing enabled");
}

void RenderGraph::disableGpuTiming() {
    if (m_debugger) {
        m_debugger->disableGpuTiming();
    }
}

FrameGraphDebugger* RenderGraph::getDebugger() {
    return m_debugger.get();
}

const FrameGraphDebugger* RenderGraph::getDebugger() const {
    return m_debugger.get();
}

bool RenderGraph::wasPassExecuted(const std::string& passName) const {
    if (!m_debugger) return false;
    return m_debugger->wasPassExecuted(passName);
}

double RenderGraph::getPassTime(const std::string& passName) const {
    if (!m_debugger) return 0.0;
    return m_debugger->getPassCpuTime(passName);
}

double RenderGraph::getFrameTime() const {
    if (!m_debugger) return 0.0;
    return m_debugger->getFrameTime();
}

// ═══════════════════════════════════════════════════════════════
// DotPathResolver Integration
// Declarative binding via dot-path sources
// ═══════════════════════════════════════════════════════════════

void RenderGraph::initSystems() {
    // Create the path resolver systems
    m_pathResolver = std::make_unique<Shoonyakasha::DotPathResolver>();
    m_sceneContext = std::make_unique<Shoonyakasha::SceneContext>();
    m_bufferResolver = std::make_unique<Shoonyakasha::BufferLayoutResolver>(*m_pathResolver);

    m_logger->log(LogLevel::Info, "DotPathResolver integration initialized");
}

void RenderGraph::updateSceneContext(float deltaTime) {
    if (!m_ecsBindingEnabled || !m_sceneContext) return;

    // Update time data
    m_sceneContext->timeDelta = deltaTime;
    m_sceneContext->timeElapsed += deltaTime;
    m_sceneContext->timeFrame++;

    // Update screen data
    m_sceneContext->screenWidth = static_cast<float>(m_screenExtent.width);
    m_sceneContext->screenHeight = static_cast<float>(m_screenExtent.height);

    // If we have a bound ECS scene, update camera from it
    if (m_boundScene) {
        m_sceneContext->updateFromRegistry(m_boundScene->getRegistry());
    }
}

Shoonyakasha::SceneContext& RenderGraph::getSceneContext() {
    if (!m_sceneContext) {
        throw std::runtime_error("Scene context not initialized");
    }
    return *m_sceneContext;
}

const Shoonyakasha::SceneContext& RenderGraph::getSceneContext() const {
    if (!m_sceneContext) {
        throw std::runtime_error("Scene context not initialized");
    }
    return *m_sceneContext;
}

void RenderGraph::bindEntityData(entt::entity entity,
                                  entt::registry& registry,
                                  const std::string& pushConstantLayout,
                                  VkCommandBuffer cmd,
                                  VkPipelineLayout pipelineLayout) {
    if (!m_ecsBindingEnabled || !m_bufferResolver) {
        m_logger->log(LogLevel::Warning, "bindEntityData: ECS binding not enabled");
        return;
    }

    // Get the compiled buffer layout
    const CompiledBufferLayout* layout = getBufferLayout(pushConstantLayout);
    if (!layout) {
        m_logger->log(LogLevel::Warning, "bindEntityData: Layout '%s' not found", pushConstantLayout.c_str());
        return;
    }

    if (!layout->isPushConstant()) {
        m_logger->log(LogLevel::Warning, "bindEntityData: Layout '%s' is not a push constant layout", pushConstantLayout.c_str());
        return;
    }

    // Allocate buffer for push constants
    std::vector<uint8_t> pushData(layout->totalSize, 0);

    // Convert FrameGraph::CompiledBufferLayout to Shoonyakasha::CompiledBufferLayout
    // We need to build a Shoonyakasha::CompiledBufferLayout from the FrameGraph one
    Shoonyakasha::CompiledBufferLayout resolvedLayout;
    resolvedLayout.name = layout->name;
    resolvedLayout.totalSize = layout->totalSize;
    resolvedLayout.hasSceneSources = layout->hasSceneSources;
    resolvedLayout.hasEntitySources = layout->hasEntitySources;
    resolvedLayout.hasConstSources = layout->hasConstSources;

    // Convert fields
    uint32_t currentOffset = 0;
    for (const auto& field : layout->fields) {
        Shoonyakasha::BufferField resolvedField;
        resolvedField.name = field.name;
        resolvedField.source = field.source;

        // Convert type
        switch (field.type) {
            case BufferFieldType::Float:  resolvedField.type = Shoonyakasha::MaterialParam::Type::Float; resolvedField.size = 4; break;
            case BufferFieldType::Vec2:   resolvedField.type = Shoonyakasha::MaterialParam::Type::Vec2;  resolvedField.size = 8; break;
            case BufferFieldType::Vec3:   resolvedField.type = Shoonyakasha::MaterialParam::Type::Vec3;  resolvedField.size = 12; break;
            case BufferFieldType::Vec4:   resolvedField.type = Shoonyakasha::MaterialParam::Type::Vec4;  resolvedField.size = 16; break;
            case BufferFieldType::Mat3:   resolvedField.type = Shoonyakasha::MaterialParam::Type::Mat3;  resolvedField.size = 36; break;
            case BufferFieldType::Mat4:   resolvedField.type = Shoonyakasha::MaterialParam::Type::Mat4;  resolvedField.size = 64; break;
            case BufferFieldType::Int:    resolvedField.type = Shoonyakasha::MaterialParam::Type::Int;   resolvedField.size = 4; break;
            case BufferFieldType::UInt:   resolvedField.type = Shoonyakasha::MaterialParam::Type::UInt;  resolvedField.size = 4; break;
            default:                      resolvedField.type = Shoonyakasha::MaterialParam::Type::Float; resolvedField.size = 4; break;
        }

        // Use explicit offset if set, otherwise compute
        resolvedField.offset = (field.offset != 0) ? field.offset : currentOffset;
        currentOffset = resolvedField.offset + resolvedField.size;

        resolvedLayout.fields.push_back(resolvedField);
    }

    // Fill the buffer using DotPathResolver
    m_bufferResolver->fillBuffer(pushData.data(), resolvedLayout, *m_sceneContext, entity, registry);

    // Push the constants
    VkShaderStageFlags stages = layout->getShaderStages();
    uint32_t offset = layout->binding.offset;

    vkCmdPushConstants(cmd, pipelineLayout, stages, offset, static_cast<uint32_t>(pushData.size()), pushData.data());
}

void RenderGraph::fillBuffer(void* buffer,
                              const std::string& layoutName,
                              entt::entity entity,
                              entt::registry& registry) {
    if (!m_ecsBindingEnabled || !m_bufferResolver) {
        m_logger->log(LogLevel::Warning, "fillBuffer: ECS binding not enabled");
        return;
    }

    const CompiledBufferLayout* layout = getBufferLayout(layoutName);
    if (!layout) {
        m_logger->log(LogLevel::Warning, "fillBuffer: Layout '%s' not found", layoutName.c_str());
        return;
    }

    // Convert to internal layout format and fill
    Shoonyakasha::CompiledBufferLayout resolvedLayout;
    resolvedLayout.name = layout->name;
    resolvedLayout.totalSize = layout->totalSize;
    resolvedLayout.hasSceneSources = layout->hasSceneSources;
    resolvedLayout.hasEntitySources = layout->hasEntitySources;
    resolvedLayout.hasConstSources = layout->hasConstSources;

    uint32_t currentOffset = 0;
    for (const auto& field : layout->fields) {
        Shoonyakasha::BufferField resolvedField;
        resolvedField.name = field.name;
        resolvedField.source = field.source;

        switch (field.type) {
            case BufferFieldType::Float:  resolvedField.type = Shoonyakasha::MaterialParam::Type::Float; resolvedField.size = 4; break;
            case BufferFieldType::Vec2:   resolvedField.type = Shoonyakasha::MaterialParam::Type::Vec2;  resolvedField.size = 8; break;
            case BufferFieldType::Vec3:   resolvedField.type = Shoonyakasha::MaterialParam::Type::Vec3;  resolvedField.size = 12; break;
            case BufferFieldType::Vec4:   resolvedField.type = Shoonyakasha::MaterialParam::Type::Vec4;  resolvedField.size = 16; break;
            case BufferFieldType::Mat3:   resolvedField.type = Shoonyakasha::MaterialParam::Type::Mat3;  resolvedField.size = 36; break;
            case BufferFieldType::Mat4:   resolvedField.type = Shoonyakasha::MaterialParam::Type::Mat4;  resolvedField.size = 64; break;
            case BufferFieldType::Int:    resolvedField.type = Shoonyakasha::MaterialParam::Type::Int;   resolvedField.size = 4; break;
            case BufferFieldType::UInt:   resolvedField.type = Shoonyakasha::MaterialParam::Type::UInt;  resolvedField.size = 4; break;
            default:                      resolvedField.type = Shoonyakasha::MaterialParam::Type::Float; resolvedField.size = 4; break;
        }

        resolvedField.offset = (field.offset != 0) ? field.offset : currentOffset;
        currentOffset = resolvedField.offset + resolvedField.size;

        resolvedLayout.fields.push_back(resolvedField);
    }

    m_bufferResolver->fillBuffer(buffer, resolvedLayout, *m_sceneContext, entity, registry);
}

void RenderGraph::createMaterialDescriptorPool(uint32_t maxSets) {
    if (m_materialDescriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(m_device.getLogicalDevice(), m_materialDescriptorPool, nullptr);
    }

    // Pool sizes for per-entity descriptors (textures + skeleton SSBOs)
    std::array<VkDescriptorPoolSize, 2> poolSizes = {{
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, maxSets * 8 },  // Up to 8 textures per material
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, maxSets * 2 }           // Skeleton bone SSBOs
    }};

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = maxSets;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;

    if (vkCreateDescriptorPool(m_device.getLogicalDevice(), &poolInfo, nullptr, &m_materialDescriptorPool) != VK_SUCCESS) {
        m_logger->log(LogLevel::Error, "Failed to create material descriptor pool");
    } else {
        m_logger->log(LogLevel::Info, "Material descriptor pool created with %u max sets", maxSets);
    }
}

void RenderGraph::bindMaterialTextures(entt::entity entity,
                                        entt::registry& registry,
                                        const std::string& descriptorSetName,
                                        VkCommandBuffer cmd,
                                        VkPipelineLayout pipelineLayout,
                                        uint32_t frameIndex) {
    if (!m_ecsBindingEnabled) {
        m_logger->log(LogLevel::Warning, "bindMaterialTextures: ECS binding not enabled");
        return;
    }

    // Get MaterialComponentV5 from entity
    auto* material = registry.try_get<Shoonyakasha::MaterialComponentV5>(entity);
    if (!material) {
        m_logger->log(LogLevel::Warning, "bindMaterialTextures: Entity has no MaterialComponentV5");
        return;
    }

    // Get the descriptor set layout definition
    const DescriptorSetLayoutDesc* layoutDesc = m_builder.getDescriptorSetLayout(descriptorSetName);
    if (!layoutDesc) {
        m_logger->log(LogLevel::Warning, "bindMaterialTextures: Layout '%s' not found", descriptorSetName.c_str());
        return;
    }

    // Create descriptor pool if not exists
    if (m_materialDescriptorPool == VK_NULL_HANDLE) {
        createMaterialDescriptorPool(4096);
    }

    // Create cache key
    MaterialDescriptorCacheKey cacheKey;
    cacheKey.entityId = static_cast<uint32_t>(entity);
    cacheKey.layoutHash = std::hash<std::string>{}(descriptorSetName);
    cacheKey.frameIndex = frameIndex;

    // Check cache
    auto cacheIt = m_materialDescriptorCache.find(cacheKey);
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;

    if (cacheIt != m_materialDescriptorCache.end()) {
        descriptorSet = cacheIt->second;
    } else {
        // Find the compiled descriptor set layout and set index by looking at passes
        VkDescriptorSetLayout vkLayout = VK_NULL_HANDLE;
        uint32_t setIndex = 0;

        // Search through passes to find one that uses this layout
        for (const auto& pass : m_compiled.compiledPasses) {
            const auto& passDecl = m_builder.getPassDeclarations()[pass.declIndex];
            for (size_t i = 0; i < passDecl.descriptorSetRefs.size(); ++i) {
                if (passDecl.descriptorSetRefs[i] == descriptorSetName) {
                    if (i < pass.descriptorSetLayouts.size()) {
                        vkLayout = pass.descriptorSetLayouts[i];
                        setIndex = static_cast<uint32_t>(i);
                    }
                    break;
                }
            }
            if (vkLayout != VK_NULL_HANDLE) break;
        }

        if (vkLayout == VK_NULL_HANDLE) {
            m_logger->log(LogLevel::Warning, "bindMaterialTextures: Compiled layout '%s' not found in any pass", descriptorSetName.c_str());
            return;
        }

        // Allocate new descriptor set
        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = m_materialDescriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &vkLayout;

        if (vkAllocateDescriptorSets(m_device.getLogicalDevice(), &allocInfo, &descriptorSet) != VK_SUCCESS) {
            m_logger->log(LogLevel::Error, "bindMaterialTextures: Failed to allocate descriptor set");
            return;
        }

        // Create default textures if not yet created (lazy initialization)
        if (!m_defaultTexturesCreated) {
            m_defaultTextures = Shoonyakasha::GPUResourceFactory::createDefaultTextures(
                m_device.getAllocator().getHandle(),
                m_device.getLogicalDevice(),
                m_device.getGraphicsQueue(),
                m_device.getCommandPool()
            );
            m_defaultTexturesCreated = true;
            m_logger->log(LogLevel::Info, "Default textures created - white: view=%p sampler=%p, normal: view=%p sampler=%p, metalRough: view=%p sampler=%p",
                (void*)m_defaultTextures.white.view, (void*)m_defaultTextures.white.sampler,
                (void*)m_defaultTextures.normal.view, (void*)m_defaultTextures.normal.sampler,
                (void*)m_defaultTextures.metallicRoughness.view, (void*)m_defaultTextures.metallicRoughness.sampler);
        }

        // Write texture bindings from MaterialComponentV5
        std::vector<VkDescriptorImageInfo> imageInfos;
        std::vector<VkWriteDescriptorSet> writes;

        // Pre-reserve to prevent reallocation during loop (dangling pointer prevention)
        imageInfos.reserve(layoutDesc->bindings.size());
        writes.reserve(layoutDesc->bindings.size());

        for (const auto& binding : layoutDesc->bindings) {
            // Type is a string: "combined_image_sampler"
            if (binding.type != "combined_image_sampler") continue;

            // Find the texture in MaterialComponentV5 or use fallback
            const Shoonyakasha::GPUTexture* gpuTex = nullptr;
            auto texIt = material->textures.find(binding.name);
            if (texIt != material->textures.end() && texIt->second.isValid()) {
                gpuTex = &texIt->second;
            } else {
                // Use appropriate fallback based on texture name
                if (binding.name == "albedoMap" || binding.name == "baseColorMap") {
                    gpuTex = &m_defaultTextures.white;
                } else if (binding.name == "normalMap") {
                    gpuTex = &m_defaultTextures.normal;
                } else if (binding.name == "metallicRoughnessMap" || binding.name == "aoMap") {
                    gpuTex = &m_defaultTextures.metallicRoughness;
                } else {
                    gpuTex = &m_defaultTextures.white;  // Generic fallback
                }
            }

            if (!gpuTex || !gpuTex->isValid()) {
                m_logger->log(LogLevel::Warning, "bindMaterialTextures: Skipping invalid texture for binding '%s'", binding.name.c_str());
                continue;
            }

            // Extra validation - ensure view and sampler are not null
            if (gpuTex->view == VK_NULL_HANDLE || gpuTex->sampler == VK_NULL_HANDLE) {
                m_logger->log(LogLevel::Error, "bindMaterialTextures: Texture '%s' has null view or sampler! view=%p sampler=%p",
                    binding.name.c_str(), (void*)gpuTex->view, (void*)gpuTex->sampler);
                continue;
            }

            VkDescriptorImageInfo imageInfo{};
            imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            imageInfo.imageView = gpuTex->view;
            imageInfo.sampler = gpuTex->sampler;
            imageInfos.push_back(imageInfo);

            VkWriteDescriptorSet write{};
            write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write.dstSet = descriptorSet;
            write.dstBinding = binding.binding;
            write.dstArrayElement = 0;
            write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            write.descriptorCount = 1;
            write.pImageInfo = &imageInfos.back();
            writes.push_back(write);
        }

        if (!writes.empty()) {
            vkUpdateDescriptorSets(m_device.getLogicalDevice(),
                                    static_cast<uint32_t>(writes.size()),
                                    writes.data(), 0, nullptr);
        }

        // Cache the descriptor set (with setIndex encoded)
        m_materialDescriptorCache[cacheKey] = descriptorSet;
    }

    // Find set index again for binding (could cache this too)
    uint32_t setIndex = 0;
    for (const auto& pass : m_compiled.compiledPasses) {
        const auto& passDecl = m_builder.getPassDeclarations()[pass.declIndex];
        for (size_t i = 0; i < passDecl.descriptorSetRefs.size(); ++i) {
            if (passDecl.descriptorSetRefs[i] == descriptorSetName) {
                setIndex = static_cast<uint32_t>(i);
                break;
            }
        }
    }

    // Bind the descriptor set
    if (descriptorSet != VK_NULL_HANDLE) {
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout,
                                setIndex, 1, &descriptorSet, 0, nullptr);
    }
}

// ════════════════════════════════════════════════════════════════════
// bindSkeletonSSBO — Per-entity bone matrix SSBO descriptor binding
// 骨之繫 — The binding of bones
// ════════════════════════════════════════════════════════════════════

void RenderGraph::bindSkeletonSSBO(entt::entity entity,
                                   entt::registry& registry,
                                   const std::string& descriptorSetName,
                                   VkCommandBuffer cmd,
                                   VkPipelineLayout pipelineLayout,
                                   uint32_t frameIndex) {
    if (!m_ecsBindingEnabled) {
        return;
    }

    // Get SkeletonComponent from entity
    auto* skeleton = registry.try_get<Shoonyakasha::SkeletonComponent>(entity);
    if (!skeleton || !skeleton->boneSSBO.isValid()) {
        return;
    }

    // Get the descriptor set layout definition
    const DescriptorSetLayoutDesc* layoutDesc = m_builder.getDescriptorSetLayout(descriptorSetName);
    if (!layoutDesc) {
        return;
    }

    // Create descriptor pool if not exists (reuse material pool)
    if (m_materialDescriptorPool == VK_NULL_HANDLE) {
        createMaterialDescriptorPool(4096);
    }

    // Create cache key (same mechanism as material textures)
    MaterialDescriptorCacheKey cacheKey;
    cacheKey.entityId = static_cast<uint32_t>(entity);
    cacheKey.layoutHash = std::hash<std::string>{}(descriptorSetName);
    cacheKey.frameIndex = frameIndex;

    // Check cache
    auto cacheIt = m_materialDescriptorCache.find(cacheKey);
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;

    if (cacheIt != m_materialDescriptorCache.end()) {
        // Cached — but we still need to update the buffer info since bone matrices change every frame
        descriptorSet = cacheIt->second;
    } else {
        // Find the compiled descriptor set layout and set index
        VkDescriptorSetLayout vkLayout = VK_NULL_HANDLE;

        for (const auto& pass : m_compiled.compiledPasses) {
            const auto& passDecl = m_builder.getPassDeclarations()[pass.declIndex];
            for (size_t i = 0; i < passDecl.descriptorSetRefs.size(); ++i) {
                if (passDecl.descriptorSetRefs[i] == descriptorSetName) {
                    if (i < pass.descriptorSetLayouts.size()) {
                        vkLayout = pass.descriptorSetLayouts[i];
                    }
                    break;
                }
            }
            if (vkLayout != VK_NULL_HANDLE) break;
        }

        if (vkLayout == VK_NULL_HANDLE) {
            return;
        }

        // Allocate new descriptor set
        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = m_materialDescriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &vkLayout;

        if (vkAllocateDescriptorSets(m_device.getLogicalDevice(), &allocInfo, &descriptorSet) != VK_SUCCESS) {
            m_logger->log(LogLevel::Error, "bindSkeletonSSBO: Failed to allocate descriptor set");
            return;
        }

        m_materialDescriptorCache[cacheKey] = descriptorSet;
    }

    // Update the SSBO descriptor with the entity's bone buffer
    VkDescriptorBufferInfo bufferInfo{};
    bufferInfo.buffer = skeleton->boneSSBO.buffer;
    bufferInfo.offset = 0;
    bufferInfo.range = skeleton->ssboSize();

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = descriptorSet;
    write.dstBinding = 0;
    write.dstArrayElement = 0;
    write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    write.descriptorCount = 1;
    write.pBufferInfo = &bufferInfo;

    vkUpdateDescriptorSets(m_device.getLogicalDevice(), 1, &write, 0, nullptr);

    // Find set index for binding
    uint32_t setIndex = 0;
    bool foundSetIndex = false;
    for (const auto& pass : m_compiled.compiledPasses) {
        const auto& passDecl = m_builder.getPassDeclarations()[pass.declIndex];
        for (size_t i = 0; i < passDecl.descriptorSetRefs.size(); ++i) {
            if (passDecl.descriptorSetRefs[i] == descriptorSetName) {
                setIndex = static_cast<uint32_t>(i);
                foundSetIndex = true;
                break;
            }
        }
        if (foundSetIndex) break;
    }

    // Bind the descriptor set
    if (descriptorSet != VK_NULL_HANDLE) {
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout,
                                setIndex, 1, &descriptorSet, 0, nullptr);
    }
}

} // namespace FrameGraph
} // namespace Shoonyakasha
