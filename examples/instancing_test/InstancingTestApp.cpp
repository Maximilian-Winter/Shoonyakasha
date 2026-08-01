//
// InstancingTestApp.cpp
//

#include "InstancingTestApp.h"

#include "ECS/Core.h"
#include "ECS/RenderComponents.h"
#include "Vulkan/VulkanDevice.h"
#include "Vulkan/VulkanWindow.h"

#include <GLFW/glfw3.h>

#include <cstdio>
#include <cmath>
#include <iostream>

using namespace Shoonyakasha;

namespace {

// Distinct colours so individual instances are tellable apart. Shared geometry,
// separate materials — the material component is copied per entity, only the
// buffers are shared.
const glm::vec3 kPalette[] = {
    {0.90f, 0.25f, 0.20f}, {0.95f, 0.65f, 0.15f}, {0.85f, 0.90f, 0.25f},
    {0.30f, 0.85f, 0.35f}, {0.20f, 0.75f, 0.85f}, {0.30f, 0.45f, 0.95f},
    {0.65f, 0.35f, 0.90f}, {0.95f, 0.40f, 0.70f},
};
constexpr size_t kPaletteSize = sizeof(kPalette) / sizeof(kPalette[0]);

} // namespace

InstancingTestApp::InstancingTestApp(const ApplicationConfig& config, bool selfTest)
    : ApplicationBase(config)
    , m_selfTest(selfTest)
{
}

void InstancingTestApp::onInit() {
    createCamera(glm::vec3(0.0f, 6.0f, 22.0f), 60.0f, 12.0f);

    createDirectionalLight(glm::normalize(glm::vec3(-0.4f, -1.0f, -0.3f)),
                           glm::vec3(1.0f, 0.97f, 0.92f), 3.0f);
    createPointLight(glm::vec3(0.0f, 8.0f, 8.0f), glm::vec3(0.6f, 0.75f, 1.0f), 40.0f, 40.0f);

    // ── The load ────────────────────────────────────────────────
    //
    // Every one of the file's mesh nodes references mesh 0. If geometry is shared,
    // the loader uploads one vertex buffer and one index buffer for all of them.

    const size_t buffersBefore = getDevice().getDeleteQueue().liveCount();

    GltfLoadOptions options;
    options.namePrefix = "box";
    auto result = loadGltfScene("instanced_boxes.gltf", options);

    const size_t buffersAfter = getDevice().getDeleteQueue().liveCount();

    if (!result.success) {
        getLogger().log(LogLevel::Error, "Failed to load instanced_boxes.gltf: %s",
                        result.error.c_str());
        return;
    }

    std::cout << "\n  ── What the loader produced ──────────────────────────\n";
    std::cout << "    glTF nodes            : " << result.nodeEntities.size() << "\n";
    std::cout << "    renderable entities   : " << result.entities.size() << "\n";
    std::cout << "    GPU buffers allocated : " << (buffersAfter - buffersBefore)
              << "   <- 2 (one vertex, one index) if geometry is shared;\n";
    std::cout << "                                 2 per instance if it is not.\n";
    std::cout << "  ──────────────────────────────────────────────────────\n" << std::endl;

    m_loadedRoots = result.rootEntities;
    if (!result.rootEntities.empty()) {
        m_carousel = result.rootEntities[0];
    }
    if (!result.entities.empty()) {
        m_meshSource = result.entities[0];
    }

    // Recolour each loaded instance. Geometry is shared; materials are not.
    auto& registry = getRegistry();
    for (size_t i = 0; i < result.entities.size(); ++i) {
        if (auto* material = registry.try_get<MaterialComponentV5>(result.entities[i])) {
            material->setParam("baseColorFactor",
                               glm::vec4(kPalette[i % kPaletteSize], 1.0f));
            material->setParam("roughnessFactor", 0.35f);
            material->setParam("metallicFactor", 0.1f);
        }
    }
}

entt::entity InstancingTestApp::cloneMeshEntity(entt::entity source,
                                                const glm::vec3& position,
                                                const glm::vec3& color,
                                                float scale) {
    auto& registry = getRegistry();
    auto* sourceMesh = registry.try_get<MeshComponent>(source);
    if (!sourceMesh) {
        return entt::null;
    }

    auto entity = getScene().createEntity("clone")
        .withTransform(position, glm::vec3(0.0f), glm::vec3(scale))
        .build();

    // The copy that matters. MeshComponent holds GpuBufferRefs, so this shares the
    // source's buffers rather than duplicating them — and neither entity can free
    // them while the other is alive.
    registry.emplace<MeshComponent>(entity, *sourceMesh);

    auto& material = registry.emplace<MaterialComponentV5>(entity);
    material.setParam("baseColorFactor", glm::vec4(color, 1.0f));
    material.setParam("roughnessFactor", 0.25f);
    material.setParam("metallicFactor", 0.6f);

    auto& tag = registry.emplace<RenderableTagComponent>(entity);
    tag.visible = true;

    return entity;
}

void InstancingTestApp::spawnRing(int count) {
    if (m_meshSource == entt::null) {
        return;
    }

    const float radius = 11.0f + 2.5f * static_cast<float>(m_ringsSpawned);
    const float height = -6.0f + 1.5f * static_cast<float>(m_ringsSpawned);

    for (int i = 0; i < count; ++i) {
        const float angle = (2.0f * 3.14159265f * static_cast<float>(i)) / static_cast<float>(count);
        const glm::vec3 pos(radius * std::cos(angle), height, radius * std::sin(angle));

        auto entity = cloneMeshEntity(m_meshSource, pos,
                                      kPalette[static_cast<size_t>(i) % kPaletteSize], 0.5f);
        if (entity != entt::null) {
            m_spawned.push_back(entity);
        }
    }

    ++m_ringsSpawned;
    getLogger().log(LogLevel::Info, "Spawned %d clones; %zu total, still %zu GPU buffers alive",
                    count, m_spawned.size(), getDevice().getDeleteQueue().liveCount());
}

void InstancingTestApp::destroySpawned(size_t count) {
    count = std::min(count, m_spawned.size());
    for (size_t i = 0; i < count; ++i) {
        getScene().destroyEntity(m_spawned.back());
        m_spawned.pop_back();
    }
    if (m_ringsSpawned > 0 && m_spawned.empty()) {
        m_ringsSpawned = 0;
    }
    getLogger().log(LogLevel::Info, "Destroyed %zu clones; %zu remain", count, m_spawned.size());
}

void InstancingTestApp::onUpdate(float dt) {
    m_elapsed += dt;

    auto& registry = getRegistry();

    // Rotate only the root. Every arm and satellite below it must follow, without
    // this code touching any of them — that is the scene graph doing the work.
    if (m_rotateCarousel && registry.valid(m_carousel)) {
        if (auto* transform = registry.try_get<ECS::TransformComponent>(m_carousel)) {
            transform->rotation.y += dt * 0.4f;
            transform->isDirty = true;
        }
    }

    // Spin each satellite about its own origin. Only possible because the vertices
    // are in mesh space now; baked into world space they would orbit the origin of
    // the scene instead of turning in place.
    if (m_spinSatellites) {
        auto view = registry.view<ECS::TransformComponent, ECS::NameComponent>();
        for (auto entity : view) {
            const auto& name = view.get<ECS::NameComponent>(entity).name;
            if (name.find("Satellite") == std::string::npos) {
                continue;
            }
            auto& transform = view.get<ECS::TransformComponent>(entity);
            transform.rotation.x += dt * 1.6f;
            transform.rotation.z += dt * 0.9f;
            transform.isDirty = true;
        }
    }

    updateWindowTitle();

    if (m_selfTest) {
        runSelfTest();
    }
}

void InstancingTestApp::check(const char* what, bool ok, const std::string& detail) {
    if (!ok) {
        ++m_selfTestFailures;
    }
    std::cout << "    [" << (ok ? "PASS" : "FAIL") << "] " << what
              << "  (" << detail << ")" << std::endl;
}

void InstancingTestApp::runSelfTest() {
    auto& queue = getDevice().getDeleteQueue();

    // One step per second, so the frames between them actually run and the
    // deferred frees get a chance to happen the way they would in a real app.
    const int step = static_cast<int>(m_elapsed);
    if (step <= m_selfTestStep) {
        return;
    }
    m_selfTestStep = step;

    const std::string counts = "alive " + std::to_string(queue.liveCount())
                             + ", pending " + std::to_string(queue.pendingCount())
                             + ", clones " + std::to_string(m_spawned.size());

    switch (step) {
        case 1:
            m_baselineBuffers = queue.liveCount();
            check("18 boxes from one mesh use one vertex and one index buffer",
                  m_baselineBuffers == 2, counts);
            break;

        case 2:
            spawnRing(24);
            break;

        case 3:
            check("24 clones added no new buffers",
                  queue.liveCount() == m_baselineBuffers, counts);
            spawnRing(24);
            break;

        case 4:
            check("48 clones added no new buffers",
                  queue.liveCount() == m_baselineBuffers, counts);
            destroySpawned(m_spawned.size() / 2);
            break;

        case 5:
            check("destroying half the clones frees nothing — the rest still hold it",
                  queue.liveCount() == m_baselineBuffers && queue.pendingCount() == 0,
                  counts);
            if (!m_spawned.empty()) {
                if (auto* mesh = getRegistry().try_get<MeshComponent>(m_spawned.front())) {
                    mesh->release();
                }
            }
            break;

        case 6:
            check("release() on one clone leaves its siblings' geometry alone",
                  queue.liveCount() == m_baselineBuffers, counts);
            destroySpawned(m_spawned.size());
            break;

        case 7:
            check("destroying every clone still leaves the loaded boxes' buffers",
                  queue.liveCount() == m_baselineBuffers && m_spawned.empty(), counts);
            vkDeviceWaitIdle(getDevice().getLogicalDevice());
            queue.flush();
            break;

        case 8: {
            check("flush() drains the pending list",
                  queue.pendingCount() == 0, counts);

            // Now take the last references. Destroying the entities is not enough
            // on its own: the loader's cache holds the geometry too, so a second
            // load of the same file is free.
            for (auto root : m_loadedRoots) {
                if (getRegistry().valid(root)) {
                    getScene().destroyEntity(root);
                }
            }
            m_loadedRoots.clear();
            getGltfLoader().releaseCachedGeometry();

            // Sample in this frame, not the next step. A second later the queue has
            // long since drained, so a check there would pass whether the free was
            // deferred or immediate.
            m_pendingAtDropTime = queue.pendingCount();
            check("dropping the last reference retires the buffers instead of freeing them",
                  m_pendingAtDropTime == m_baselineBuffers && queue.liveCount() == 0,
                  "pending " + std::to_string(m_pendingAtDropTime)
                      + " in the same frame as the drop, alive "
                      + std::to_string(queue.liveCount()));
            break;
        }

        case 9:
            check("the retired buffers are freed once the frames using them have passed",
                  queue.pendingCount() == 0 && queue.liveCount() == 0, counts);
            std::cout << "\n  Self-test finished with " << m_selfTestFailures
                      << " failure(s).\n" << std::endl;
            glfwSetWindowShouldClose(getWindow().getWindow(), GLFW_TRUE);
            break;

        default:
            break;
    }
}

void InstancingTestApp::updateWindowTitle() {
    auto& queue = getDevice().getDeleteQueue();

    char buffer[256];
    std::snprintf(buffer, sizeof(buffer),
                  "Instancing Test  |  clones: %zu  |  GPU buffers alive: %zu  |  awaiting free: %zu",
                  m_spawned.size(), queue.liveCount(), queue.pendingCount());

    if (m_lastTitle != buffer) {
        m_lastTitle = buffer;
        glfwSetWindowTitle(getWindow().getWindow(), buffer);
    }
}

void InstancingTestApp::onKeyPressed(int keyCode) {
    auto& queue = getDevice().getDeleteQueue();

    switch (keyCode) {
        case GLFW_KEY_SPACE:
            spawnRing(24);
            break;

        case GLFW_KEY_X:
            destroySpawned(m_spawned.size() / 2 + 1);
            break;

        case GLFW_KEY_C:
            destroySpawned(m_spawned.size());
            break;

        case GLFW_KEY_R: {
            // Release one clone's claim on the geometry. It stops rendering; every
            // other entity holding the same buffers keeps working. Under the old
            // by-value GPUBuffer this is where a naive free would have taken the
            // whole scene's geometry with it.
            if (!m_spawned.empty()) {
                if (auto* mesh = getRegistry().try_get<MeshComponent>(m_spawned.front())) {
                    mesh->release();
                    getLogger().log(LogLevel::Info,
                                    "Released one clone's mesh; %zu buffers still alive",
                                    queue.liveCount());
                }
            }
            break;
        }

        case GLFW_KEY_F:
            // Manual reclaim. Safe here only because we wait for the device first —
            // the queue frees on demand and does not check for you.
            vkDeviceWaitIdle(getDevice().getLogicalDevice());
            queue.flush();
            getLogger().log(LogLevel::Info, "Flushed; %zu pending, %zu alive",
                            queue.pendingCount(), queue.liveCount());
            break;

        case GLFW_KEY_H:
            m_rotateCarousel = !m_rotateCarousel;
            break;

        case GLFW_KEY_J:
            m_spinSatellites = !m_spinSatellites;
            break;

        default:
            break;
    }
}
