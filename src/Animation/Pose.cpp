//
// Pose.cpp - Local joint transforms of a skeleton, and operations on them
//

#include "Animation/Pose.h"
#include "Animation/AnimationEvaluator.h"
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>
#include <algorithm>

namespace Shoonyakasha {

void Pose::setRest(const Skeleton& skeleton) {
    AnimationEvaluator::setDefaultPose(skeleton, translations, rotations, scales);
}

void Pose::setIdentity(uint32_t count) {
    translations.assign(count, glm::vec3(0.0f));
    rotations.assign(count, glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
    scales.assign(count, glm::vec3(1.0f));
}

namespace PoseOps {

namespace {

glm::quat slerpShortest(const glm::quat& a, glm::quat b, float weight) {
    if (glm::dot(a, b) < 0.0f) b = -b;
    return glm::normalize(glm::slerp(a, b, weight));
}

void resizeLike(const Pose& source, Pose& out) {
    const size_t count = source.rotations.size();
    out.translations.resize(count);
    out.rotations.resize(count);
    out.scales.resize(count);
}

} // namespace

void sample(const AnimationClip& clip, float time, const Skeleton& skeleton, Pose& out) {
    out.setRest(skeleton);
    AnimationEvaluator::sampleClip(clip, time, skeleton, out.translations, out.rotations, out.scales);
}

void blend(const Pose& a, const Pose& b, float weight, Pose& out) {
    const size_t count = std::min(a.rotations.size(), b.rotations.size());
    resizeLike(a, out);
    for (size_t i = 0; i < count; ++i) {
        out.translations[i] = glm::mix(a.translations[i], b.translations[i], weight);
        out.rotations[i] = slerpShortest(a.rotations[i], b.rotations[i], weight);
        out.scales[i] = glm::mix(a.scales[i], b.scales[i], weight);
    }
}

void blendPerJoint(const Pose& a, const Pose& b, const std::vector<float>& weights, Pose& out) {
    const size_t count = std::min(a.rotations.size(), b.rotations.size());
    resizeLike(a, out);
    for (size_t i = 0; i < count; ++i) {
        const float w = i < weights.size() ? weights[i] : 0.0f;
        out.translations[i] = glm::mix(a.translations[i], b.translations[i], w);
        out.rotations[i] = slerpShortest(a.rotations[i], b.rotations[i], w);
        out.scales[i] = glm::mix(a.scales[i], b.scales[i], w);
    }
}

void makeAdditive(const Pose& pose, const Pose& reference, Pose& outDelta) {
    const size_t count = std::min(pose.rotations.size(), reference.rotations.size());
    outDelta.setIdentity(static_cast<uint32_t>(count));
    for (size_t i = 0; i < count; ++i) {
        outDelta.translations[i] = pose.translations[i] - reference.translations[i];
        outDelta.rotations[i] = glm::normalize(glm::inverse(reference.rotations[i]) * pose.rotations[i]);
        const glm::vec3 ref = reference.scales[i];
        outDelta.scales[i] = glm::vec3(ref.x != 0.0f ? pose.scales[i].x / ref.x : 1.0f,
                                       ref.y != 0.0f ? pose.scales[i].y / ref.y : 1.0f,
                                       ref.z != 0.0f ? pose.scales[i].z / ref.z : 1.0f);
    }
}

void applyAdditive(const Pose& base, const Pose& delta, float weight, Pose& out) {
    const size_t count = std::min(base.rotations.size(), delta.rotations.size());
    resizeLike(base, out);
    const glm::quat identity(1.0f, 0.0f, 0.0f, 0.0f);
    for (size_t i = 0; i < count; ++i) {
        out.translations[i] = base.translations[i] + weight * delta.translations[i];
        out.rotations[i] = glm::normalize(base.rotations[i] * slerpShortest(identity, delta.rotations[i], weight));
        out.scales[i] = base.scales[i] * glm::mix(glm::vec3(1.0f), delta.scales[i], weight);
    }
}

void globalTransforms(const Skeleton& skeleton, const Pose& pose, std::vector<glm::mat4>& out) {
    const int count = static_cast<int>(std::min<size_t>(skeleton.joints.size(), pose.rotations.size()));
    out.assign(count, glm::mat4(1.0f));

    std::vector<int> computedOrder;
    const std::vector<int>* order = &skeleton.evaluationOrder;
    if (order->size() != skeleton.joints.size()) {
        computedOrder = skeleton.computeEvaluationOrder();
        order = &computedOrder;
    }

    std::vector<char> visited(count, 0);
    for (int i : *order) {
        if (i < 0 || i >= count) continue;
        const glm::mat4 local = glm::translate(glm::mat4(1.0f), pose.translations[i])
                              * glm::toMat4(pose.rotations[i])
                              * glm::scale(glm::mat4(1.0f), pose.scales[i]);
        const int parent = skeleton.joints[i].parentIndex;
        out[i] = (parent >= 0 && parent < count && visited[parent]) ? out[parent] * local : local;
        visited[i] = 1;
    }
}

void boneMatrices(const Skeleton& skeleton, const std::vector<glm::mat4>& globals,
                  std::vector<glm::mat4>& out) {
    const size_t count = std::min(skeleton.joints.size(), globals.size());
    out.resize(count);
    for (size_t i = 0; i < count; ++i) {
        out[i] = globals[i] * skeleton.joints[i].inverseBindMatrix;
    }
}

} // namespace PoseOps

} // namespace Shoonyakasha
