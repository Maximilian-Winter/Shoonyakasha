//
// Skinned Mesh Test Application
//
// 骨之舞 — The Dance of Bones
//
// Demonstrates skeletal animation:
//   - Load a skinned glTF/glb model (Fox)
//   - Play bone animations evaluated on CPU
//   - GPU skinning via bone matrix SSBO in vertex shader
//   - Declarative JSON pipeline with "skinned_geometry" pass
//   - Declarative vertex formats ("standard" + "skinned")
//

#pragma once

#include "App/ApplicationBase.h"
#include "ECS/SkeletonComponents.h"
#include "ECS/SkeletalAnimationSystem.h"

#include <memory>

class SkinnedMeshApp : public ApplicationBase {
public:
    explicit SkinnedMeshApp(const ApplicationConfig& config);

protected:
    void onInit() override;
    void onPreRender(float dt) override;
    void onKeyPressed(int keyCode) override;

private:
    // Skeletal animation system (owns bone SSBOs, evaluates keyframes)
    std::unique_ptr<Shoonyakasha::SkeletalAnimationSystem> m_animationSystem;

    // Animation control
    int m_currentClipIndex = 0;
    int m_totalClips = 0;

    void loadSkinnedModel();
    void switchAnimation(int clipIndex);
};
