//
// InstancingTestApp.h - Visual confirmation of shared geometry and node transforms
//
// एकं रूपं बहुधा भाति — one form appearing manifold.
//

#pragma once

#include "App/ApplicationBase.h"

#include <vector>

class InstancingTestApp : public ApplicationBase {
public:
    /// selfTest drives the same operations the keys do, on a timer, checking the
    /// buffer counts after each and closing the window when done. Lets the demo
    /// be run unattended — the interactive paths are otherwise only ever
    /// exercised by someone sitting at the keyboard.
    InstancingTestApp(const ApplicationConfig& config, bool selfTest = false);

protected:
    void onInit() override;
    void onUpdate(float dt) override;
    void onKeyPressed(int keyCode) override;

private:
    /// Copy a loaded entity's MeshComponent onto a new entity. This is the whole
    /// point of the demo: the copy shares the same GpuBufferRef, so the ring costs
    /// no extra vertex data no matter how large it gets.
    entt::entity cloneMeshEntity(entt::entity source,
                                 const glm::vec3& position,
                                 const glm::vec3& color,
                                 float scale);

    void spawnRing(int count);
    void destroySpawned(size_t count);
    void updateWindowTitle();
    void runSelfTest();
    void check(const char* what, bool ok, const std::string& detail);

    // The entity the glTF's own scene graph hangs from. Rotating this must carry
    // every arm and satellite with it.
    entt::entity m_carousel = entt::null;

    // A source of geometry for the runtime-spawned clones.
    entt::entity m_meshSource = entt::null;

    // Roots of the loaded glTF scene, so the self-test can tear it all down.
    std::vector<entt::entity> m_loadedRoots;

    // Clones created at runtime, all sharing m_meshSource's buffers.
    std::vector<entt::entity> m_spawned;

    bool  m_selfTest = false;
    int   m_selfTestStep = 0;
    int   m_selfTestFailures = 0;
    size_t m_baselineBuffers = 0;
    // Sampled in the same frame the last reference is dropped. Read a second
    // later it is always zero — the queue has long since drained, which proves
    // nothing about whether the free was deferred.
    size_t m_pendingAtDropTime = 0;

    bool  m_rotateCarousel = true;
    bool  m_spinSatellites = true;
    float m_elapsed = 0.0f;
    int   m_ringsSpawned = 0;

    // Cached so the title only changes when something it reports changes.
    std::string m_lastTitle;
};
