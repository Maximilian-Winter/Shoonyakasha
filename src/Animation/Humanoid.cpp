//
// Humanoid.cpp - Standard humanoid bones and their mapping onto a skeleton
//

#include "Animation/Humanoid.h"
#include "Animation/Pose.h"

#include <algorithm>
#include <cctype>
#include <functional>
#include <set>
#include <stdexcept>
#include <unordered_map>

namespace Shoonyakasha {

namespace {

using B = HumanoidBone;

const char* const kNames[kHumanoidBoneCount] = {
    "Hips", "Spine", "Chest", "UpperChest", "Neck", "Head", "Jaw", "LeftEye", "RightEye",
    "LeftShoulder", "LeftUpperArm", "LeftLowerArm", "LeftHand",
    "RightShoulder", "RightUpperArm", "RightLowerArm", "RightHand",
    "LeftUpperLeg", "LeftLowerLeg", "LeftFoot", "LeftToes",
    "RightUpperLeg", "RightLowerLeg", "RightFoot", "RightToes",
    "LeftThumbProximal", "LeftThumbIntermediate", "LeftThumbDistal",
    "LeftIndexProximal", "LeftIndexIntermediate", "LeftIndexDistal",
    "LeftMiddleProximal", "LeftMiddleIntermediate", "LeftMiddleDistal",
    "LeftRingProximal", "LeftRingIntermediate", "LeftRingDistal",
    "LeftLittleProximal", "LeftLittleIntermediate", "LeftLittleDistal",
    "RightThumbProximal", "RightThumbIntermediate", "RightThumbDistal",
    "RightIndexProximal", "RightIndexIntermediate", "RightIndexDistal",
    "RightMiddleProximal", "RightMiddleIntermediate", "RightMiddleDistal",
    "RightRingProximal", "RightRingIntermediate", "RightRingDistal",
    "RightLittleProximal", "RightLittleIntermediate", "RightLittleDistal",
};

int idx(B bone) { return static_cast<int>(bone); }
B boneAt(int index) { return static_cast<B>(index); }

// First finger bone of each side; fingers follow in kFingers order, three
// segments each.
constexpr B kFirstFinger[2] = {B::LeftThumbProximal, B::RightThumbProximal};
const char* const kFingers[5] = {"thumb", "index", "middle", "ring", "little"};

B fingerBone(int side, int finger, int segment) {
    return boneAt(idx(kFirstFinger[side]) + finger * 3 + segment);
}

bool isFinger(B bone, int* side = nullptr, int* finger = nullptr, int* segment = nullptr) {
    for (int s = 0; s < 2; ++s) {
        const int offset = idx(bone) - idx(kFirstFinger[s]);
        if (offset >= 0 && offset < 15) {
            if (side) *side = s;
            if (finger) *finger = offset / 3;
            if (segment) *segment = offset % 3;
            return true;
        }
    }
    return false;
}

std::string lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return std::tolower(c); });
    return text;
}

// Lower case, without a namespace prefix ("rig:Hips") or a Mixamo prefix
// ("mixamorig_Hips", "mixamorig1:Hips").
std::string normalized(const std::string& name) {
    std::string n = lower(name);
    const size_t colon = n.rfind(':');
    if (colon != std::string::npos) n = n.substr(colon + 1);
    if (n.rfind("mixamorig", 0) == 0) {
        size_t i = 9;
        while (i < n.size() && std::isdigit(static_cast<unsigned char>(n[i]))) ++i;
        if (i < n.size() && n[i] == '_') n = n.substr(i + 1);
    }
    return n;
}

using Preset = std::vector<std::pair<B, std::string>>;

Preset sided(std::initializer_list<std::pair<B, const char*>> center,
             std::initializer_list<std::tuple<B, B, const char*>> limbs,
             const std::function<std::string(const char* part, bool left)>& sideName,
             const std::function<std::string(int finger, int segment, bool left)>& fingerName) {
    Preset preset;
    for (const auto& [bone, name] : center) preset.emplace_back(bone, name);
    for (const auto& [left, right, part] : limbs) {
        preset.emplace_back(left, sideName(part, true));
        preset.emplace_back(right, sideName(part, false));
    }
    for (int side = 0; side < 2; ++side) {
        for (int finger = 0; finger < 5; ++finger) {
            for (int segment = 0; segment < 3; ++segment) {
                preset.emplace_back(fingerBone(side, finger, segment), fingerName(finger, segment, side == 0));
            }
        }
    }
    return preset;
}

const std::vector<Preset>& presets() {
    static const std::vector<Preset> all = [] {
        std::vector<Preset> list;
        // Mixamo (prefix removed by normalized())
        list.push_back(sided(
            {{B::Hips, "hips"}, {B::Spine, "spine"}, {B::Chest, "spine1"}, {B::UpperChest, "spine2"},
             {B::Neck, "neck"}, {B::Head, "head"}},
            {{B::LeftShoulder, B::RightShoulder, "shoulder"}, {B::LeftUpperArm, B::RightUpperArm, "arm"},
             {B::LeftLowerArm, B::RightLowerArm, "forearm"}, {B::LeftHand, B::RightHand, "hand"},
             {B::LeftUpperLeg, B::RightUpperLeg, "upleg"}, {B::LeftLowerLeg, B::RightLowerLeg, "leg"},
             {B::LeftFoot, B::RightFoot, "foot"}, {B::LeftToes, B::RightToes, "toebase"},
             {B::LeftEye, B::RightEye, "eye"}},
            [](const char* part, bool left) { return std::string(left ? "left" : "right") + part; },
            [](int finger, int segment, bool left) {
                static const char* const names[5] = {"thumb", "index", "middle", "ring", "pinky"};
                return std::string(left ? "lefthand" : "righthand") + names[finger] + std::to_string(segment + 1);
            }));
        // Unreal mannequin
        list.push_back(sided(
            {{B::Hips, "pelvis"}, {B::Spine, "spine_01"}, {B::Chest, "spine_02"}, {B::UpperChest, "spine_03"},
             {B::Neck, "neck_01"}, {B::Head, "head"}},
            {{B::LeftShoulder, B::RightShoulder, "clavicle"}, {B::LeftUpperArm, B::RightUpperArm, "upperarm"},
             {B::LeftLowerArm, B::RightLowerArm, "lowerarm"}, {B::LeftHand, B::RightHand, "hand"},
             {B::LeftUpperLeg, B::RightUpperLeg, "thigh"}, {B::LeftLowerLeg, B::RightLowerLeg, "calf"},
             {B::LeftFoot, B::RightFoot, "foot"}, {B::LeftToes, B::RightToes, "ball"}},
            [](const char* part, bool left) { return std::string(part) + (left ? "_l" : "_r"); },
            [](int finger, int segment, bool left) {
                static const char* const names[5] = {"thumb", "index", "middle", "ring", "pinky"};
                return std::string(names[finger]) + "_0" + std::to_string(segment + 1) + (left ? "_l" : "_r");
            }));
        // VRM / VRoid
        list.push_back(sided(
            {{B::Hips, "j_bip_c_hips"}, {B::Spine, "j_bip_c_spine"}, {B::Chest, "j_bip_c_chest"},
             {B::UpperChest, "j_bip_c_upperchest"}, {B::Neck, "j_bip_c_neck"}, {B::Head, "j_bip_c_head"}},
            {{B::LeftShoulder, B::RightShoulder, "shoulder"}, {B::LeftUpperArm, B::RightUpperArm, "upperarm"},
             {B::LeftLowerArm, B::RightLowerArm, "lowerarm"}, {B::LeftHand, B::RightHand, "hand"},
             {B::LeftUpperLeg, B::RightUpperLeg, "upperleg"}, {B::LeftLowerLeg, B::RightLowerLeg, "lowerleg"},
             {B::LeftFoot, B::RightFoot, "foot"}, {B::LeftToes, B::RightToes, "toebase"}},
            [](const char* part, bool left) { return std::string(left ? "j_bip_l_" : "j_bip_r_") + part; },
            [](int finger, int segment, bool left) {
                return std::string(left ? "j_bip_l_" : "j_bip_r_") + kFingers[finger] + std::to_string(segment + 1);
            }));
        // Blender Rigify deform bones
        list.push_back(sided(
            {{B::Hips, "def-spine"}, {B::Spine, "def-spine.001"}, {B::Chest, "def-spine.002"},
             {B::UpperChest, "def-spine.003"}, {B::Neck, "def-spine.004"}, {B::Head, "def-spine.006"}},
            {{B::LeftShoulder, B::RightShoulder, "def-shoulder"}, {B::LeftUpperArm, B::RightUpperArm, "def-upper_arm"},
             {B::LeftLowerArm, B::RightLowerArm, "def-forearm"}, {B::LeftHand, B::RightHand, "def-hand"},
             {B::LeftUpperLeg, B::RightUpperLeg, "def-thigh"}, {B::LeftLowerLeg, B::RightLowerLeg, "def-shin"},
             {B::LeftFoot, B::RightFoot, "def-foot"}, {B::LeftToes, B::RightToes, "def-toe"}},
            [](const char* part, bool left) { return std::string(part) + (left ? ".l" : ".r"); },
            [](int finger, int segment, bool left) {
                static const char* const names[5] = {"def-thumb", "def-f_index", "def-f_middle", "def-f_ring",
                                                     "def-f_pinky"};
                return std::string(names[finger]) + ".0" + std::to_string(segment + 1) + (left ? ".l" : ".r");
            }));
        return list;
    }();
    return all;
}

// Name parts: split at non-alphanumerics, lower-to-upper case changes and
// letter/digit changes; lower case.
std::vector<std::string> tokens(const std::string& name) {
    std::string n = name;
    const size_t colon = n.rfind(':');
    if (colon != std::string::npos) n = n.substr(colon + 1);
    std::vector<std::string> parts;
    std::string current;
    auto flush = [&] {
        if (!current.empty()) parts.push_back(lower(current));
        current.clear();
    };
    for (size_t i = 0; i < n.size(); ++i) {
        const unsigned char c = n[i];
        if (!std::isalnum(c)) {
            flush();
            continue;
        }
        if (!current.empty()) {
            const unsigned char p = current.back();
            const bool caseChange = std::islower(p) && std::isupper(c);
            const bool digitChange = bool(std::isdigit(p)) != bool(std::isdigit(c));
            if (caseChange || digitChange) flush();
        }
        current.push_back(static_cast<char>(c));
    }
    flush();
    return parts;
}

struct NameParts {
    std::set<std::string> words;
    int side = -1;   // 0 left, 1 right, -1 neither
    bool has(const char* word) const { return words.count(word) > 0; }
};

NameParts partsOf(const std::string& name) {
    NameParts parts;
    for (const std::string& t : tokens(name)) parts.words.insert(t);
    if (parts.has("left") || parts.has("l") || parts.has("lf") || parts.has("lft")) parts.side = 0;
    else if (parts.has("right") || parts.has("r") || parts.has("rt") || parts.has("rgt")) parts.side = 1;
    return parts;
}

bool hasFingerWord(const NameParts& p) {
    return p.has("thumb") || p.has("index") || p.has("middle") || p.has("ring") || p.has("pinky") ||
           p.has("little") || p.has("finger");
}

// Whether a joint's name parts describe a non-finger bone.
bool namesBone(B bone, const NameParts& p) {
    const bool leftBone = (bone >= B::LeftShoulder && bone <= B::LeftHand) || (bone >= B::LeftUpperLeg && bone <= B::LeftToes) ||
                          bone == B::LeftEye;
    const bool rightBone = (bone >= B::RightShoulder && bone <= B::RightHand) ||
                           (bone >= B::RightUpperLeg && bone <= B::RightToes) || bone == B::RightEye;
    if (leftBone && p.side != 0) return false;
    if (rightBone && p.side != 1) return false;
    if (hasFingerWord(p)) return false;
    const bool upper = p.has("upper") || p.has("up");
    const bool lowerPart = p.has("lower") || p.has("low");
    switch (bone) {
        case B::Hips: return p.side < 0 && (p.has("hips") || p.has("hip") || p.has("pelvis"));
        case B::Neck: return p.side < 0 && p.has("neck");
        case B::Head: return p.side < 0 && p.has("head") && !p.has("top") && !p.has("end");
        case B::Jaw: return p.has("jaw");
        case B::LeftEye: case B::RightEye: return p.has("eye");
        case B::LeftShoulder: case B::RightShoulder:
            return p.has("shoulder") || p.has("clavicle") || p.has("collar");
        case B::LeftUpperArm: case B::RightUpperArm:
            return p.has("upperarm") || p.has("uparm") || (upper && p.has("arm"));
        case B::LeftLowerArm: case B::RightLowerArm:
            return p.has("forearm") || p.has("lowerarm") || p.has("elbow") ||
                   ((lowerPart || p.has("fore")) && p.has("arm"));
        case B::LeftHand: case B::RightHand: return p.has("hand") || p.has("wrist");
        case B::LeftUpperLeg: case B::RightUpperLeg:
            return p.has("thigh") || p.has("upleg") || p.has("upperleg") || (upper && p.has("leg"));
        case B::LeftLowerLeg: case B::RightLowerLeg:
            return p.has("calf") || p.has("shin") || p.has("knee") || p.has("lowerleg") || (lowerPart && p.has("leg"));
        case B::LeftFoot: case B::RightFoot: return p.has("foot") || p.has("ankle");
        case B::LeftToes: case B::RightToes:
            return p.has("toe") || p.has("toes") || p.has("toebase") || p.has("ball");
        default: return false;
    }
}

int depthOf(const Skeleton& skeleton, int joint) {
    int depth = 0;
    const int count = static_cast<int>(skeleton.joints.size());
    for (int p = skeleton.joints[joint].parentIndex; p >= 0 && p < count && depth <= count;
         p = skeleton.joints[p].parentIndex) {
        ++depth;
    }
    return depth;
}

bool isBelow(const Skeleton& skeleton, int joint, int ancestor) {
    const int count = static_cast<int>(skeleton.joints.size());
    int steps = 0;
    for (int p = skeleton.joints[joint].parentIndex; p >= 0 && p < count && steps <= count;
         p = skeleton.joints[p].parentIndex, ++steps) {
        if (p == ancestor) return true;
    }
    return false;
}

// The joint of the nearest mapped bone above `bone`; -1 if none is mapped.
int mappedAncestorJoint(const std::array<int, kHumanoidBoneCount>& joints, B bone) {
    for (B b = humanoidBoneParent(bone); b != B::Count; b = humanoidBoneParent(b)) {
        if (joints[idx(b)] >= 0) return joints[idx(b)];
    }
    return -1;
}

void fillByNameParts(const Skeleton& skeleton, std::array<int, kHumanoidBoneCount>& joints) {
    const int count = static_cast<int>(skeleton.joints.size());
    std::vector<NameParts> parts;
    for (const Joint& joint : skeleton.joints) parts.push_back(partsOf(joint.name));
    auto used = [&](int joint) { return std::find(joints.begin(), joints.end(), joint) != joints.end(); };

    for (int b = 0; b < kHumanoidBoneCount; ++b) {
        const B bone = boneAt(b);
        if (joints[b] >= 0 || isFinger(bone) || bone == B::Spine || bone == B::Chest || bone == B::UpperChest) continue;
        const int ancestor = mappedAncestorJoint(joints, bone);
        int best = -1;
        for (int j = 0; j < count; ++j) {
            if (used(j) || !namesBone(bone, parts[j])) continue;
            if (ancestor >= 0 && !isBelow(skeleton, j, ancestor)) continue;
            if (best < 0 || depthOf(skeleton, j) < depthOf(skeleton, best)) best = j;
        }
        joints[b] = best;
    }

    // Spine, Chest, UpperChest: the joints between the hips and the neck (or head).
    const int hips = joints[idx(B::Hips)];
    const int top = joints[idx(B::Neck)] >= 0 ? joints[idx(B::Neck)] : joints[idx(B::Head)];
    if (hips >= 0 && top >= 0 && isBelow(skeleton, top, hips)) {
        std::vector<int> chain;
        for (int p = skeleton.joints[top].parentIndex; p >= 0 && p != hips; p = skeleton.joints[p].parentIndex) {
            chain.insert(chain.begin(), p);
            if (chain.size() > static_cast<size_t>(count)) break;
        }
        const B spineBones[3] = {B::Spine, B::Chest, B::UpperChest};
        for (size_t i = 0; i < chain.size() && i < 3; ++i) {
            if (joints[idx(spineBones[i])] < 0 && !used(chain[i])) joints[idx(spineBones[i])] = chain[i];
        }
    }

    // Fingers: joints below the hand naming the finger, by depth.
    for (int side = 0; side < 2; ++side) {
        const int hand = joints[idx(side == 0 ? B::LeftHand : B::RightHand)];
        if (hand < 0) continue;
        for (int finger = 0; finger < 5; ++finger) {
            std::vector<int> found;
            for (int j = 0; j < count; ++j) {
                const NameParts& p = parts[j];
                const bool named = p.has(kFingers[finger]) || (finger == 4 && p.has("pinky"));
                if (named && isBelow(skeleton, j, hand) && !used(j)) found.push_back(j);
            }
            std::sort(found.begin(), found.end(),
                      [&](int a, int c) { return depthOf(skeleton, a) < depthOf(skeleton, c); });
            for (int segment = 0; segment < 3 && segment < static_cast<int>(found.size()); ++segment) {
                int& slot = joints[idx(fingerBone(side, finger, segment))];
                if (slot < 0) slot = found[segment];
            }
        }
    }
}

glm::vec3 restPosition(const HumanoidAvatar& avatar, B bone) {
    return glm::vec3(avatar.restGlobals[avatar.joint(bone)][3]);
}

} // namespace

const char* humanoidBoneName(HumanoidBone bone) {
    const int i = idx(bone);
    return (i >= 0 && i < kHumanoidBoneCount) ? kNames[i] : "";
}

bool humanoidBoneFromName(const std::string& name, HumanoidBone& out) {
    for (int i = 0; i < kHumanoidBoneCount; ++i) {
        if (name == kNames[i]) {
            out = boneAt(i);
            return true;
        }
    }
    return false;
}

bool humanoidBoneRequired(HumanoidBone bone) {
    switch (bone) {
        case B::Hips: case B::Spine: case B::Head:
        case B::LeftUpperArm: case B::LeftLowerArm: case B::LeftHand:
        case B::RightUpperArm: case B::RightLowerArm: case B::RightHand:
        case B::LeftUpperLeg: case B::LeftLowerLeg: case B::LeftFoot:
        case B::RightUpperLeg: case B::RightLowerLeg: case B::RightFoot:
            return true;
        default:
            return false;
    }
}

HumanoidBone humanoidBoneParent(HumanoidBone bone) {
    int side = 0, finger = 0, segment = 0;
    if (isFinger(bone, &side, &finger, &segment)) {
        if (segment > 0) return fingerBone(side, finger, segment - 1);
        return side == 0 ? B::LeftHand : B::RightHand;
    }
    switch (bone) {
        case B::Hips: return B::Count;
        case B::Spine: return B::Hips;
        case B::Chest: return B::Spine;
        case B::UpperChest: return B::Chest;
        case B::Neck: return B::UpperChest;
        case B::Head: return B::Neck;
        case B::Jaw: case B::LeftEye: case B::RightEye: return B::Head;
        case B::LeftShoulder: case B::RightShoulder: return B::UpperChest;
        case B::LeftUpperArm: return B::LeftShoulder;
        case B::RightUpperArm: return B::RightShoulder;
        case B::LeftLowerArm: return B::LeftUpperArm;
        case B::RightLowerArm: return B::RightUpperArm;
        case B::LeftHand: return B::LeftLowerArm;
        case B::RightHand: return B::RightLowerArm;
        case B::LeftUpperLeg: case B::RightUpperLeg: return B::Hips;
        case B::LeftLowerLeg: return B::LeftUpperLeg;
        case B::RightLowerLeg: return B::RightUpperLeg;
        case B::LeftFoot: return B::LeftLowerLeg;
        case B::RightFoot: return B::RightLowerLeg;
        case B::LeftToes: return B::LeftFoot;
        case B::RightToes: return B::RightFoot;
        default: return B::Count;
    }
}

std::array<int, kHumanoidBoneCount> autoMapHumanoid(const Skeleton& skeleton) {
    std::unordered_map<std::string, int> byName;
    for (int j = 0; j < static_cast<int>(skeleton.joints.size()); ++j) {
        byName.emplace(normalized(skeleton.joints[j].name), j);
    }

    std::array<int, kHumanoidBoneCount> best;
    best.fill(-1);
    int bestCount = 0;
    for (const Preset& preset : presets()) {
        std::array<int, kHumanoidBoneCount> joints;
        joints.fill(-1);
        int matched = 0;
        for (const auto& [bone, name] : preset) {
            auto it = byName.find(name);
            if (it != byName.end() && joints[idx(bone)] < 0) {
                joints[idx(bone)] = it->second;
                ++matched;
            }
        }
        if (matched > bestCount) {
            bestCount = matched;
            best = joints;
        }
    }
    fillByNameParts(skeleton, best);
    return best;
}

std::shared_ptr<HumanoidAvatar> makeHumanoidAvatar(
    std::shared_ptr<const Skeleton> skeleton,
    const std::vector<std::pair<std::string, std::string>>& overrides) {
    if (!skeleton) throw std::runtime_error("humanoid: no skeleton");
    auto avatar = std::make_shared<HumanoidAvatar>();
    avatar->skeleton = skeleton;
    avatar->joints = autoMapHumanoid(*skeleton);

    for (const auto& [boneName, jointName] : overrides) {
        HumanoidBone bone;
        if (!humanoidBoneFromName(boneName, bone)) throw std::runtime_error("humanoid: no bone '" + boneName + "'");
        int joint = -1;
        if (!jointName.empty()) {
            joint = skeleton->findJoint(jointName);
            if (joint < 0) throw std::runtime_error("humanoid: " + boneName + ": no joint '" + jointName + "'");
        }
        avatar->joints[idx(bone)] = joint;
    }

    std::string missing;
    for (int b = 0; b < kHumanoidBoneCount; ++b) {
        if (humanoidBoneRequired(boneAt(b)) && avatar->joints[b] < 0) {
            missing += (missing.empty() ? "" : ", ") + std::string(kNames[b]);
        }
    }
    if (!missing.empty()) throw std::runtime_error("humanoid: no joint for " + missing);

    Pose rest;
    rest.setRest(*skeleton);
    PoseOps::globalTransforms(*skeleton, rest, avatar->restGlobals);

    const glm::vec3 hips = restPosition(*avatar, B::Hips);
    glm::vec3 up = restPosition(*avatar, B::Head) - hips;
    glm::vec3 left = restPosition(*avatar, B::LeftUpperLeg) - restPosition(*avatar, B::RightUpperLeg);
    if (glm::dot(up, up) < 1e-12f) throw std::runtime_error("humanoid: the head is at the hips in the rest pose");
    up = glm::normalize(up);
    left -= glm::dot(left, up) * up;
    if (glm::dot(left, left) < 1e-12f) {
        throw std::runtime_error("humanoid: the upper legs are not side by side in the rest pose");
    }
    left = glm::normalize(left);
    const glm::vec3 forward = glm::cross(left, up);
    avatar->bodyFrame = glm::normalize(glm::inverse(glm::quat_cast(glm::mat3(left, up, forward))));

    const glm::vec3 feet = 0.5f * (restPosition(*avatar, B::LeftFoot) + restPosition(*avatar, B::RightFoot));
    const float height = glm::dot(hips - feet, up);
    avatar->hipHeight = height > 1e-6f ? height : 1.0f;
    return avatar;
}

std::vector<std::pair<std::string, std::string>> humanoidMapping(const HumanoidAvatar& avatar) {
    std::vector<std::pair<std::string, std::string>> mapping;
    for (int b = 0; b < kHumanoidBoneCount; ++b) {
        const int joint = avatar.joints[b];
        if (joint >= 0 && avatar.skeleton && joint < static_cast<int>(avatar.skeleton->joints.size())) {
            mapping.emplace_back(kNames[b], avatar.skeleton->joints[joint].name);
        }
    }
    return mapping;
}

} // namespace Shoonyakasha
