# AnimationAPI

Access: `engine.getAnimation()`. [Other language reference](../python/animation.md).

Include `Facade/AnimationAPI.h`; namespace `Shoonyakasha::Facade`. [Source declaration](../../../include/Facade/AnimationAPI.h).

Controls per-model animators. `createAnimator(root)` adds an `AnimatorComponent` to `root`. The animator drives every skinned mesh in root's subtree that holds the first skeleton found there. Every other call takes that root entity as `animator`.

- Clips are addressed by name.
- `loadClips` reads a glTF / GLB file and adds the clips whose channels match the skeleton's joint names. It throws `std::runtime_error` when the file cannot be read or the entity has no animator.
- `getJointWorldPosition` throws for an unknown animator or joint.
- `makeHumanoid`, `retargetClips` and `loadHumanoidClips` throw `std::runtime_error` for unknown bones, joints or clips, missing required bones, or animators that are not humanoids.
- `setIKChain`, `setIKGoal` and `getAnimatedJointWorldPosition` throw `std::runtime_error` for unknown animators, joints or chains.
- `setController` throws `std::runtime_error` naming the place in the definition that is wrong. The parameter, layer and state calls throw for an unknown name, a parameter of another type, or an animator without a controller.

Playback, cross-fades and attachments follow the [Python reference](../python/animation.md#clips-and-playback). The same engine types are available without the facade:

- `AnimatorComponent` and `JointAttachmentComponent` in `ECS/AnimatorComponents.h`
- `AnimatorEvaluator` in `Animation/AnimatorEvaluator.h`
- `ControllerDefinition` and `parseAnimationController` (with the JSON form) in `Animation/AnimationController.h`
- `ControllerEvaluator` in `Animation/ControllerEvaluator.h`
- `HumanoidBone`, `HumanoidAvatar`, `autoMapHumanoid` and `makeHumanoidAvatar` in `Animation/Humanoid.h`
- `retargetClip` in `Animation/Retarget.h`
- `TwoBoneIKGoal` and `solveTwoBoneIK` in `Animation/IK.h`; `IKChain` and `IKGoalState` in `ECS/AnimatorComponents.h`
- `loadAnimationFile` and `readGltfSkeleton` in `Resources/AnimationClipLoader.h`
- `Pose` and `PoseOps` in `Animation/Pose.h`
- `bindClip` in `Animation/ClipBinding.h`
- `loadAnimationClips` in `Resources/AnimationClipLoader.h`

<!-- BEGIN SOURCE API -->

## Members

Declarations below are extracted from the public facade header; test constructors and internal wiring are omitted.

```cpp
AnimationAPI();
~AnimationAPI();
bool createAnimator(EntityHandle root);
bool hasAnimator(EntityHandle entity) const;
std::vector<std::string> loadClips(EntityHandle animator, const std::string& path, const std::string& name = "");
std::vector<std::string> getClipNames(EntityHandle animator) const;
float getClipDuration(EntityHandle animator, const std::string& clip) const;
bool play(EntityHandle animator, const std::string& clip, float fadeSeconds = 0.0f, bool loop = true);
void stop(EntityHandle animator);
bool isPlaying(EntityHandle animator) const;
std::string getCurrentClip(EntityHandle animator) const;
bool isFading(EntityHandle animator) const;
float getTime(EntityHandle animator) const;
void setTime(EntityHandle animator, float seconds);
float getSpeed(EntityHandle animator) const;
void setSpeed(EntityHandle animator, float speed);
std::vector<std::string> getJointNames(EntityHandle animator) const;
glm::vec3 getJointWorldPosition(EntityHandle animator, const std::string& joint) const;
bool attach(EntityHandle entity, EntityHandle animator, const std::string& joint, const glm::vec3& offsetPosition = glm::vec3(0.0f), const glm::vec3& offsetRotation = glm::vec3(0.0f));
void detach(EntityHandle entity);
void setController(EntityHandle animator, const std::string& json);
void clearController(EntityHandle animator);
bool hasController(EntityHandle animator) const;
void setFloat(EntityHandle animator, const std::string& parameter, float value);
void setInt(EntityHandle animator, const std::string& parameter, int value);
void setBool(EntityHandle animator, const std::string& parameter, bool value);
void setTrigger(EntityHandle animator, const std::string& parameter);
void resetTrigger(EntityHandle animator, const std::string& parameter);
float getParameter(EntityHandle animator, const std::string& parameter) const;
std::string getCurrentState(EntityHandle animator, const std::string& layer) const;
bool isInTransition(EntityHandle animator, const std::string& layer) const;
void setLayerWeight(EntityHandle animator, const std::string& layer, float weight);
float getLayerWeight(EntityHandle animator, const std::string& layer) const;
std::vector<std::string> takeEvents(EntityHandle animator);
void setRootMotion(EntityHandle animator, bool enabled, const std::string& joint = "");
bool isRootMotion(EntityHandle animator) const;
glm::vec3 takeRootMotion(EntityHandle animator);
using BoneMapping = std::vector<std::pair<std::string, std::string>>;
BoneMapping makeHumanoid(EntityHandle animator, const BoneMapping& mapping = {});
bool isHumanoid(EntityHandle animator) const;
BoneMapping getHumanoidMapping(EntityHandle animator) const;
std::vector<std::string> retargetClips(EntityHandle target, EntityHandle source, const std::vector<std::string>& clips = {}, const std::string& name = "");
std::vector<std::string> loadHumanoidClips(EntityHandle target, const std::string& path, const std::string& name = "", const BoneMapping& mapping = {});
void setIKChain(EntityHandle animator, const std::string& name, const std::string& upper, const std::string& middle, const std::string& end);
void setIKGoal(EntityHandle animator, const std::string& chain, const glm::vec3& position, float weight = 1.0f, bool hasPole = false, const glm::vec3& pole = glm::vec3(0.0f), bool hasRotation = false, const glm::vec3& rotation = glm::vec3(0.0f), bool hasNormal = false, const glm::vec3& normal = glm::vec3(0.0f, 1.0f, 0.0f));
void clearIKGoal(EntityHandle animator, const std::string& chain);
void clearIKGoals(EntityHandle animator);
void setHipsOffset(EntityHandle animator, const glm::vec3& offset);
glm::vec3 getHipsOffset(EntityHandle animator) const;
glm::vec3 getAnimatedJointWorldPosition(EntityHandle animator, const std::string& joint) const;
```

<!-- END SOURCE API -->
