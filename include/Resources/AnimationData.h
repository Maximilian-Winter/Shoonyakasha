//
// AnimationData.h - Skeletal animation data structures
//
// Defines the core data types for skeletal animation:
//   - Joint: A single bone in a skeleton hierarchy
//   - Skeleton: Complete bone hierarchy with inverse bind matrices
//   - AnimationChannel: Keyframed TRS data targeting a single joint
//   - AnimationClip: A complete animation (collection of channels)
//
// 骨之舞 — The dance of bones
//

#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <cstdint>

namespace Shoonyakasha {

// ============================================================================
// Joint - A single bone in the skeleton
// ============================================================================

struct Joint {
    std::string name;
    int parentIndex = -1;          // -1 = root joint (no parent)
    glm::mat4 inverseBindMatrix = glm::mat4(1.0f);  // Mesh space -> joint local space

    // Default local transform from glTF node (used when no animation channel targets this joint)
    glm::vec3 defaultTranslation = glm::vec3(0.0f);
    glm::quat defaultRotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);  // w,x,y,z identity
    glm::vec3 defaultScale = glm::vec3(1.0f);
};

// ============================================================================
// Skeleton - Complete bone hierarchy
// ============================================================================

struct Skeleton {
    std::string name;
    std::vector<Joint> joints;
    int rootJointIndex = 0;

    // Fast lookup: joint name -> index in joints vector
    std::unordered_map<std::string, int> jointNameToIndex;

    // Number of bones
    uint32_t jointCount() const { return static_cast<uint32_t>(joints.size()); }

    // Build the name lookup map (call after populating joints)
    void buildNameLookup() {
        jointNameToIndex.clear();
        for (int i = 0; i < static_cast<int>(joints.size()); ++i) {
            if (!joints[i].name.empty()) {
                jointNameToIndex[joints[i].name] = i;
            }
        }
    }

    // Find joint index by name (-1 if not found)
    int findJoint(const std::string& name) const {
        auto it = jointNameToIndex.find(name);
        return it != jointNameToIndex.end() ? it->second : -1;
    }
};

// ============================================================================
// AnimationInterpolation - Keyframe interpolation method
// ============================================================================

enum class AnimationInterpolation {
    Step,           // Constant value until next keyframe
    Linear,         // Linear interpolation (lerp/slerp)
    CubicSpline     // Hermite cubic spline with in/out tangents
};

// ============================================================================
// AnimationChannel - Keyframed data for one joint property
// ============================================================================

struct AnimationChannel {
    int targetJointIndex = -1;

    enum class Property {
        Translation,
        Rotation,
        Scale
    } property = Property::Translation;

    AnimationInterpolation interpolation = AnimationInterpolation::Linear;

    // Keyframe timestamps (seconds, ascending order)
    std::vector<float> timestamps;

    // Keyframe values:
    // - Translation: vec4(x, y, z, 0)
    // - Rotation: vec4(x, y, z, w) as quaternion
    // - Scale: vec4(x, y, z, 0)
    //
    // For CubicSpline: stored as [inTangent, value, outTangent] triples,
    // so values.size() == timestamps.size() * 3
    std::vector<glm::vec4> values;

    // Number of keyframes
    uint32_t keyframeCount() const { return static_cast<uint32_t>(timestamps.size()); }
};

// ============================================================================
// AnimationClip - A complete animation (e.g., "Walk", "Idle", "Attack")
// ============================================================================

struct AnimationClip {
    std::string name;
    float duration = 0.0f;  // Max timestamp across all channels (seconds)
    std::vector<AnimationChannel> channels;
};

} // namespace Shoonyakasha
