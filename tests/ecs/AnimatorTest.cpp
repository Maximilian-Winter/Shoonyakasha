//
// AnimatorTest.cpp - Tests for AnimatorComponent, AnimatorEvaluator and
// AnimationClipLoader
//
// Tier 2: CPU-side animation evaluation — no GPU
//

#include <gtest/gtest.h>
#include "Animation/AnimatorEvaluator.h"
#include "ECS/Core.h"
#include "ECS/SkeletonComponents.h"
#include "Resources/AnimationClipLoader.h"
#include "TestHelpers.h"

#include <glm/gtc/matrix_transform.hpp>
#include <filesystem>
#include <stdexcept>

using namespace Shoonyakasha;

namespace {

std::shared_ptr<Skeleton> makeChain() {
    auto skeleton = std::make_shared<Skeleton>();
    for (int i = 0; i < 3; ++i) {
        Joint joint;
        joint.name = i == 0 ? "Root" : (i == 1 ? "Mid" : "Tip");
        joint.parentIndex = i - 1;
        joint.defaultTranslation = i == 0 ? glm::vec3(0.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
        skeleton->joints.push_back(joint);
    }
    skeleton->buildNameLookup();
    skeleton->buildEvaluationOrder();
    return skeleton;
}

// Root translation from (0,0,0) to (x,0,0) over `duration` seconds.
std::shared_ptr<AnimationClip> makeSlide(const std::string& name, float x, float duration = 1.0f) {
    auto clip = std::make_shared<AnimationClip>();
    clip->name = name;
    clip->duration = duration;
    AnimationChannel channel;
    channel.targetJointIndex = 0;
    channel.targetJointName = "Root";
    channel.property = AnimationChannel::Property::Translation;
    channel.timestamps = {0.0f, duration};
    channel.values = {glm::vec4(0.0f), glm::vec4(x, 0.0f, 0.0f, 0.0f)};
    clip->channels.push_back(channel);
    return clip;
}

void link(entt::registry& registry, entt::entity parent, entt::entity child) {
    registry.get_or_emplace<ECS::HierarchyComponent>(parent).addChild(child);
    registry.get_or_emplace<ECS::HierarchyComponent>(child).parent = parent;
}

struct Model {
    entt::entity root;
    entt::entity meshA;
    entt::entity meshB;
    std::shared_ptr<Skeleton> skeleton;
};

// A root with two skinned meshes of one skeleton below it. meshA carries an
// AnimationPlaybackComponent playing "slide" at 0.25 s.
Model makeModel(entt::registry& registry) {
    Model model;
    model.skeleton = makeChain();
    model.root = registry.create();
    registry.emplace<ECS::TransformComponent>(model.root);
    model.meshA = registry.create();
    model.meshB = registry.create();
    for (entt::entity mesh : {model.meshA, model.meshB}) {
        registry.emplace<ECS::TransformComponent>(mesh);
        auto& skeleton = registry.emplace<SkeletonComponent>(mesh);
        skeleton.skeleton = model.skeleton;
        skeleton.allocate();
        link(registry, model.root, mesh);
    }
    auto& playback = registry.emplace<AnimationPlaybackComponent>(model.meshA);
    playback.clips = {makeSlide("slide", 4.0f)};
    playback.play(0);
    playback.currentTime = 0.25f;
    playback.speed = 2.0f;
    return model;
}

} // namespace

// ═══════════════════════════════════════════════════════════════
// createAnimator
// ═══════════════════════════════════════════════════════════════

TEST(Animator, CreateCollectsSkinnedMeshesAndPlayback) {
    entt::registry registry;
    Model model = makeModel(registry);

    AnimatorComponent* animator = AnimatorEvaluator::createAnimator(registry, model.root);
    ASSERT_NE(animator, nullptr);
    EXPECT_EQ(animator->skeleton, model.skeleton);
    ASSERT_EQ(animator->skinnedMeshes.size(), 2u);
    ASSERT_EQ(animator->clips.size(), 1u);
    EXPECT_EQ(animator->current.clip, 0);
    EXPECT_FLOAT_EQ(animator->current.time, 0.25f);
    EXPECT_TRUE(animator->current.playing);
    EXPECT_FLOAT_EQ(animator->speed, 2.0f);
    EXPECT_EQ(registry.get<SkeletonComponent>(model.meshA).animator, model.root);
    EXPECT_EQ(registry.get<SkeletonComponent>(model.meshB).animator, model.root);
}

TEST(Animator, CreateWithoutSkinnedMeshReturnsNull) {
    entt::registry registry;
    entt::entity root = registry.create();
    EXPECT_EQ(AnimatorEvaluator::createAnimator(registry, root), nullptr);
    EXPECT_FALSE(registry.all_of<AnimatorComponent>(root));
}

TEST(Animator, CreateTwiceReturnsExisting) {
    entt::registry registry;
    Model model = makeModel(registry);
    AnimatorComponent* first = AnimatorEvaluator::createAnimator(registry, model.root);
    EXPECT_EQ(AnimatorEvaluator::createAnimator(registry, model.root), first);
}

TEST(Animator, CreateSkipsMeshesOfOtherSkeletons) {
    entt::registry registry;
    Model model = makeModel(registry);
    registry.get<SkeletonComponent>(model.meshB).skeleton = makeChain();
    AnimatorComponent* animator = AnimatorEvaluator::createAnimator(registry, model.root);
    ASSERT_EQ(animator->skinnedMeshes.size(), 1u);
    EXPECT_EQ(registry.get<SkeletonComponent>(model.meshB).animator, entt::entity{entt::null});
}

// ═══════════════════════════════════════════════════════════════
// advance / evaluate
// ═══════════════════════════════════════════════════════════════

TEST(Animator, LoopingClipWraps) {
    AnimatorComponent animator;
    animator.skeleton = makeChain();
    animator.clips = {makeSlide("slide", 1.0f, 1.0f)};
    animator.play(0);
    AnimatorEvaluator::advance(animator, 1.25f);
    EXPECT_NEAR(animator.current.time, 0.25f, 1e-5f);
    EXPECT_TRUE(animator.current.playing);
}

TEST(Animator, OneShotClipHoldsLastFrame) {
    AnimatorComponent animator;
    animator.skeleton = makeChain();
    animator.clips = {makeSlide("slide", 3.0f, 1.0f)};
    animator.play(0, 0.0f, false);
    AnimatorEvaluator::advance(animator, 2.0f);
    EXPECT_FLOAT_EQ(animator.current.time, 1.0f);
    EXPECT_FALSE(animator.current.playing);
    AnimatorEvaluator::evaluate(animator);
    TestHelpers::ExpectVec3Near(animator.pose.translations[0], glm::vec3(3.0f, 0.0f, 0.0f));
}

TEST(Animator, SpeedScalesAdvance) {
    AnimatorComponent animator;
    animator.skeleton = makeChain();
    animator.clips = {makeSlide("slide", 1.0f, 10.0f)};
    animator.play(0);
    animator.speed = 0.5f;
    AnimatorEvaluator::advance(animator, 1.0f);
    EXPECT_NEAR(animator.current.time, 0.5f, 1e-5f);
    animator.speed = 0.0f;
    AnimatorEvaluator::advance(animator, 1.0f);
    EXPECT_NEAR(animator.current.time, 0.5f, 1e-5f);
}

TEST(Animator, NoClipGivesRestPose) {
    AnimatorComponent animator;
    animator.skeleton = makeChain();
    AnimatorEvaluator::evaluate(animator);
    ASSERT_EQ(animator.globalTransforms.size(), 3u);
    TestHelpers::ExpectVec3Near(glm::vec3(animator.globalTransforms[2][3]), glm::vec3(0.0f, 2.0f, 0.0f));
}

TEST(Animator, CrossFadeBlendsAndEnds) {
    AnimatorComponent animator;
    animator.skeleton = makeChain();
    // "left" holds Root at x = -2 throughout, "right" at x = +2.
    auto left = makeSlide("left", -2.0f);
    left->channels[0].values[0] = glm::vec4(-2.0f, 0.0f, 0.0f, 0.0f);
    auto right = makeSlide("right", 2.0f);
    right->channels[0].values[0] = glm::vec4(2.0f, 0.0f, 0.0f, 0.0f);
    animator.clips = {left, right};

    animator.play(0);
    ASSERT_TRUE(animator.play(1, 1.0f));
    EXPECT_TRUE(animator.isFading());

    AnimatorEvaluator::advance(animator, 0.5f);
    AnimatorEvaluator::evaluate(animator);
    TestHelpers::ExpectVec3Near(animator.pose.translations[0], glm::vec3(0.0f), 1e-4f);

    AnimatorEvaluator::advance(animator, 0.6f);
    EXPECT_FALSE(animator.isFading());
    EXPECT_EQ(animator.previous.clip, -1);
    AnimatorEvaluator::evaluate(animator);
    TestHelpers::ExpectVec3Near(animator.pose.translations[0], glm::vec3(2.0f, 0.0f, 0.0f));
}

TEST(Animator, PlayWithoutFadeOrOutOfRange) {
    AnimatorComponent animator;
    animator.clips = {makeSlide("a", 1.0f)};
    EXPECT_FALSE(animator.play(1));
    EXPECT_TRUE(animator.play(0, 0.5f));    // nothing playing before: no fade
    EXPECT_FALSE(animator.isFading());
    EXPECT_EQ(animator.findClip("a"), 0);
    EXPECT_EQ(animator.findClip("b"), -1);
    animator.stop();
    EXPECT_EQ(animator.current.clip, -1);
}

// ═══════════════════════════════════════════════════════════════
// update / placeAttachments
// ═══════════════════════════════════════════════════════════════

TEST(Animator, UpdateWritesBoneMatricesOfEveryMesh) {
    entt::registry registry;
    Model model = makeModel(registry);
    AnimatorEvaluator::createAnimator(registry, model.root);
    registry.get<SkeletonComponent>(model.meshA).dirty = false;
    registry.get<SkeletonComponent>(model.meshB).dirty = false;

    AnimatorEvaluator::update(registry, 0.0f);

    const auto& a = registry.get<SkeletonComponent>(model.meshA);
    const auto& b = registry.get<SkeletonComponent>(model.meshB);
    EXPECT_TRUE(a.dirty);
    EXPECT_TRUE(b.dirty);
    ASSERT_EQ(a.boneMatrices.size(), 3u);
    // Clip "slide" at 0.25 s of 1 s puts Root at x = 1.
    TestHelpers::ExpectVec3Near(glm::vec3(a.boneMatrices[0][3]), glm::vec3(1.0f, 0.0f, 0.0f));
    for (int j = 0; j < 3; ++j) TestHelpers::ExpectMat4Near(a.boneMatrices[j], b.boneMatrices[j]);
}

TEST(Animator, AttachmentFollowsJointAndCarriesChildren) {
    entt::registry registry;
    Model model = makeModel(registry);
    AnimatorEvaluator::createAnimator(registry, model.root);
    registry.get<ECS::TransformComponent>(model.meshA).worldMatrix =
        glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, 10.0f));

    entt::entity sword = registry.create();
    registry.emplace<ECS::TransformComponent>(sword);
    JointAttachmentComponent attachment;
    attachment.animator = model.root;
    attachment.joint = 2;  // Tip
    attachment.offset = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.5f, 0.0f));
    registry.emplace<JointAttachmentComponent>(sword, attachment);

    entt::entity gem = registry.create();
    auto& gemTransform = registry.emplace<ECS::TransformComponent>(gem);
    gemTransform.localMatrix = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, 1.0f));
    link(registry, sword, gem);

    AnimatorEvaluator::update(registry, 0.0f);

    // Mesh at z = 10, Root at x = 1, Tip 2 above Root, offset 0.5 up.
    const glm::vec3 swordAt(registry.get<ECS::TransformComponent>(sword).worldMatrix[3]);
    TestHelpers::ExpectVec3Near(swordAt, glm::vec3(1.0f, 2.5f, 10.0f));
    const glm::vec3 gemAt(registry.get<ECS::TransformComponent>(gem).worldMatrix[3]);
    TestHelpers::ExpectVec3Near(gemAt, glm::vec3(1.0f, 2.5f, 11.0f));
}

TEST(Animator, AttachmentWithoutAnimatorIsLeftAlone) {
    entt::registry registry;
    entt::entity thing = registry.create();
    auto& transform = registry.emplace<ECS::TransformComponent>(thing);
    transform.worldMatrix = glm::translate(glm::mat4(1.0f), glm::vec3(3.0f));
    registry.emplace<JointAttachmentComponent>(thing);
    AnimatorEvaluator::placeAttachments(registry);
    TestHelpers::ExpectVec3Near(glm::vec3(transform.worldMatrix[3]), glm::vec3(3.0f));
}

// ═══════════════════════════════════════════════════════════════
// addClips
// ═══════════════════════════════════════════════════════════════

TEST(Animator, AddClipsBindsAndNames) {
    AnimatorComponent animator;
    animator.skeleton = makeChain();

    auto unbound = makeSlide("mixamo.com", 1.0f);
    unbound->channels[0].targetJointIndex = -1;
    auto foreign = makeSlide("other", 1.0f);
    foreign->channels[0].targetJointName = "Tail";

    auto added = AnimatorEvaluator::addClips(animator, {unbound, foreign}, "walk");
    ASSERT_EQ(added.size(), 1u);
    EXPECT_EQ(added[0], "walk");
    ASSERT_EQ(animator.clips.size(), 1u);
    EXPECT_EQ(animator.clips[0]->channels[0].targetJointIndex, 0);

    added = AnimatorEvaluator::addClips(animator, {makeSlide("a", 1.0f), makeSlide("b", 1.0f)}, "set");
    ASSERT_EQ(added.size(), 2u);
    EXPECT_EQ(added[0], "set:a");
    EXPECT_EQ(added[1], "set:b");

    added = AnimatorEvaluator::addClips(animator, {makeSlide("plain", 1.0f)});
    ASSERT_EQ(added.size(), 1u);
    EXPECT_EQ(added[0], "plain");
}

// ═══════════════════════════════════════════════════════════════
// AnimationClipLoader
// ═══════════════════════════════════════════════════════════════

TEST(AnimationClipLoader, FoxClipsAreUnboundWithNames) {
    const auto clips = loadAnimationClips(std::string(SHOONYAKASHA_SOURCE_DIR) + "/assets/models/Fox.glb");
    ASSERT_EQ(clips.size(), 3u);
    EXPECT_EQ(clips[0]->name, "Survey");
    EXPECT_EQ(clips[1]->name, "Walk");
    EXPECT_EQ(clips[2]->name, "Run");
    for (const auto& clip : clips) {
        EXPECT_GT(clip->duration, 0.0f);
        ASSERT_FALSE(clip->channels.empty());
        for (const auto& channel : clip->channels) {
            EXPECT_EQ(channel.targetJointIndex, -1);
            EXPECT_FALSE(channel.targetJointName.empty());
        }
    }
}

TEST(AnimationClipLoader, MixamoClip) {
    const std::filesystem::path path = std::filesystem::path(SHOONYAKASHA_SOURCE_DIR)
        / "assets/models/rumba-dancing-mixamo/gltf/Rumba-Dancing.gltf";
    if (!std::filesystem::exists(path)) GTEST_SKIP() << "missing " << path.string();

    const auto clips = loadAnimationClips(path);
    ASSERT_EQ(clips.size(), 1u);
    EXPECT_NEAR(clips[0]->duration, 2.3667f, 1e-3f);
    EXPECT_EQ(clips[0]->channels.size(), 156u);
    for (const auto& channel : clips[0]->channels) {
        EXPECT_EQ(channel.targetJointName.rfind("mixamorig_", 0), 0u) << channel.targetJointName;
    }
}

TEST(AnimationClipLoader, MissingFileThrows) {
    EXPECT_THROW(loadAnimationClips("does/not/exist.glb"), std::runtime_error);
}
