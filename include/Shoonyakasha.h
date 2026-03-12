//
// Shoonyakasha.h — Convenience header for the Shoonyakasha Engine
//
// शून्याकाश — Void-Space Engine
// Include this single header to access all major engine systems.
//
// 道生一  一生二  二生三  三生萬物
// The Dao generates One, One generates Two, Two generates Three,
// Three generates the ten thousand things
//

#pragma once

// ── Core ────────────────────────────────────────────────────────
#include "Core/Logger.h"
#include "Core/EventSystem.h"

// ── Vulkan Subsystem ────────────────────────────────────────────
#include "Vulkan/VulkanInstance.h"
#include "Vulkan/VulkanDevice.h"
#include "Vulkan/VulkanWindow.h"
#include "Vulkan/VulkanSwapChain.h"
#include "Vulkan/VulkanBuffer.h"
#include "Vulkan/VulkanImage.h"
#include "Vulkan/VulkanTexture.h"
#include "Vulkan/VulkanCubemap.h"
#include "Vulkan/VulkanPipeline.h"
#include "Vulkan/VulkanComputePipeline.h"
#include "Vulkan/VulkanCommandBuffer.h"
#include "Vulkan/VulkanRenderPass.h"
#include "Vulkan/VulkanMemoryAllocator.h"
#include "Vulkan/VulkanDescriptorSystem.h"
#include "Vulkan/UniformBuffer.h"
#include "Vulkan/VertexTypes.h"

// ── GPU Types ───────────────────────────────────────────────────
#include "GPU/GPUTypes.h"
#include "GPU/GPUResourceFactory.h"

// ── ECS ─────────────────────────────────────────────────────────
#include "ECS/Core.h"
#include "ECS/Systems.h"
#include "ECS/Scene.h"
#include "ECS/RenderComponents.h"
#include "ECS/CameraController.h"
#include "ECS/CameraControllerBuilders.h"
#include "ECS/InputSystem.h"

// ── Frame Graph ─────────────────────────────────────────────────
#include "Vulkan/FrameGraph/FrameGraph.h"
#include "FrameGraph/FrameGraphRenderer.h"
#include "FrameGraph/SharedBufferRegistry.h"
#include "FrameGraph/DotPathResolver.h"

// ── Resources ───────────────────────────────────────────────────
#include "Resources/ResourceManager.h"
#include "Resources/GltfSceneLoader.h"

// ── IBL ─────────────────────────────────────────────────────────
#include "IBL/IBLGenerator.h"
