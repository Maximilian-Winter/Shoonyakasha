# AnimationAPI

Access: `engine.getAnimation()`. [Other language reference](../python/animation.md).

Include `Facade/AnimationAPI.h`; namespace `Shoonyakasha::Facade`. [Source declaration](../../../include/Facade/AnimationAPI.h).

Controls per-model animators. `createAnimator(root)` adds an `AnimatorComponent` to `root`. The animator drives every skinned mesh in root's subtree that holds the first skeleton found there. Every other call takes that root entity as `animator`.

- Clips are addressed by name.
- `loadClips` reads a glTF / GLB file and adds the clips whose channels match the skeleton's joint names. It throws `std::runtime_error` when the file cannot be read or the entity has no animator.
- `getJointWorldPosition` throws for an unknown animator or joint.
- `setController` throws `std::runtime_error` naming the place in the definition that is wrong. The parameter, layer and state calls throw for an unknown name, a parameter of another type, or an animator without a controller.

Playback, cross-fades and attachments follow the [Python reference](../python/animation.md#clips-and-playback). The same engine types are available without the facade:

- `AnimatorComponent` and `JointAttachmentComponent` in `ECS/AnimatorComponents.h`
- `AnimatorEvaluator` in `Animation/AnimatorEvaluator.h`
- `ControllerDefinition` and `parseAnimationController` (with the JSON form) in `Animation/AnimationController.h`
- `ControllerEvaluator` in `Animation/ControllerEvaluator.h`
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
```

<!-- END SOURCE API -->
