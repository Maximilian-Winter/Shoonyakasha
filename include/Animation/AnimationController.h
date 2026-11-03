//
// AnimationController.h - Animation state machine definitions
//
// A ControllerDefinition names its parameters, layers, states, clips and
// joints by string; it is not tied to a skeleton. ControllerEvaluator::bind
// (Animation/ControllerEvaluator.h) resolves the names against an animator.
//
// JSON form (parseAnimationController):
//
//   {
//     "parameters": { "<name>": {"type": "float|int|bool|trigger", "default": <number|bool>} },
//     "layers": [ {
//       "name": "<name>",
//       "default": "<state>",              // optional; the first state otherwise
//       "weight": 1.0,                     // optional
//       "mode": "override|additive",       // optional; override
//       "mask": ["<joint>", ...],          // optional; each joint with its descendants
//       "states": { "<name>": {
//         "clip": "<clip>"                                          // one of these three
//         "blend1d": {"parameter": "<p>", "clips": [["<clip>", <threshold>], ...]}
//         "blend2d": {"parameters": ["<px>", "<py>"], "clips": [["<clip>", <x>, <y>], ...]}
//         "speed": 1.0, "speed_parameter": "<p>", "loop": true,     // optional
//         "events": [[<normalized time>, "<name>"], ...]            // optional
//       } },
//       "transitions": [ {
//         "from": "<state>|any", "to": "<state>",
//         "duration": <seconds>,            // optional; 0
//         "exit_time": <normalized time>,   // optional
//         "when": [["<p>"], ["<p>", "<op>", <value>], ...]   // optional; op: > < == !=
//       } ]
//     } ]
//   }
//

#pragma once

#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <vector>

namespace Shoonyakasha {

enum class ControllerParameterType { Float, Int, Bool, Trigger };

struct ControllerParameter {
    std::string name;
    ControllerParameterType type = ControllerParameterType::Float;
    float defaultValue = 0.0f;   // bools and triggers: 0 or 1
};

struct ControllerMotion {
    enum class Kind { Clip, Blend1D, Blend2D } kind = Kind::Clip;

    struct Child {
        std::string clip;
        glm::vec2 position{0.0f};   // Blend1D: threshold in x
    };

    // Clip: one child. Blend1D: children by ascending threshold.
    std::vector<Child> children;
    int parameterX = -1;   // Blend1D and Blend2D
    int parameterY = -1;   // Blend2D
};

struct ControllerEvent {
    float time = 0.0f;   // normalized, 0..1
    std::string name;
};

struct ControllerState {
    std::string name;
    ControllerMotion motion;
    float speed = 1.0f;
    int speedParameter = -1;   // multiplies speed when set
    bool loop = true;
    std::vector<ControllerEvent> events;
};

enum class ControllerConditionOp { IsSet, Greater, Less, Equal, NotEqual };

struct ControllerCondition {
    int parameter = -1;
    ControllerConditionOp op = ControllerConditionOp::IsSet;
    float value = 0.0f;
};

struct ControllerTransition {
    int from = -1;            // state index; -1 = any state
    int to = -1;
    float duration = 0.0f;    // seconds
    float exitTime = -1.0f;   // normalized; < 0 = none
    std::vector<ControllerCondition> conditions;
};

enum class ControllerLayerMode { Override, Additive };

struct ControllerLayer {
    std::string name;
    float weight = 1.0f;
    ControllerLayerMode mode = ControllerLayerMode::Override;
    std::vector<std::string> mask;   // empty = every joint
    int defaultState = 0;
    std::vector<ControllerState> states;
    std::vector<ControllerTransition> transitions;

    int findState(const std::string& stateName) const {
        for (size_t i = 0; i < states.size(); ++i) {
            if (states[i].name == stateName) return static_cast<int>(i);
        }
        return -1;
    }
};

struct ControllerDefinition {
    std::vector<ControllerParameter> parameters;
    std::vector<ControllerLayer> layers;

    int findParameter(const std::string& parameterName) const {
        for (size_t i = 0; i < parameters.size(); ++i) {
            if (parameters[i].name == parameterName) return static_cast<int>(i);
        }
        return -1;
    }

    int findLayer(const std::string& layerName) const {
        for (size_t i = 0; i < layers.size(); ++i) {
            if (layers[i].name == layerName) return static_cast<int>(i);
        }
        return -1;
    }
};

/// Parse the JSON form above.
/// @throws std::runtime_error "<path>: <message>" for malformed JSON, unknown
///         keys, unknown parameter or state names, a 1D blend whose thresholds
///         do not ascend, a condition on a parameter of the wrong type, a
///         transition with neither conditions nor exit time, or a layer
///         without states.
std::shared_ptr<const ControllerDefinition> parseAnimationController(const std::string& json);

} // namespace Shoonyakasha
