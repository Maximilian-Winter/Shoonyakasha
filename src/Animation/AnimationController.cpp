//
// AnimationController.cpp - Parse animation state machine definitions
//

#include "Animation/AnimationController.h"
#include <nlohmann/json.hpp>
#include <initializer_list>
#include <stdexcept>

namespace Shoonyakasha {

namespace {

using Json = nlohmann::ordered_json;

[[noreturn]] void fail(const std::string& path, const std::string& message) {
    throw std::runtime_error(path + ": " + message);
}

void checkKeys(const Json& object, const std::string& path, std::initializer_list<const char*> allowed) {
    if (!object.is_object()) fail(path, "expected an object");
    for (auto it = object.begin(); it != object.end(); ++it) {
        bool known = false;
        for (const char* key : allowed) known = known || it.key() == key;
        if (!known) fail(path, "unknown key '" + it.key() + "'");
    }
}

const Json& require(const Json& object, const char* key, const std::string& path) {
    auto it = object.find(key);
    if (it == object.end()) fail(path, std::string("missing '") + key + "'");
    return *it;
}

std::string stringAt(const Json& value, const std::string& path) {
    if (!value.is_string()) fail(path, "expected a string");
    return value.get<std::string>();
}

float numberAt(const Json& value, const std::string& path) {
    if (!value.is_number()) fail(path, "expected a number");
    return value.get<float>();
}

float scalarAt(const Json& value, const std::string& path) {
    if (value.is_boolean()) return value.get<bool>() ? 1.0f : 0.0f;
    return numberAt(value, path);
}

int parameterAt(const ControllerDefinition& definition, const Json& value, const std::string& path) {
    const std::string name = stringAt(value, path);
    const int index = definition.findParameter(name);
    if (index < 0) fail(path, "no parameter '" + name + "'");
    return index;
}

int numericParameterAt(const ControllerDefinition& definition, const Json& value, const std::string& path) {
    const int index = parameterAt(definition, value, path);
    const auto type = definition.parameters[index].type;
    if (type != ControllerParameterType::Float && type != ControllerParameterType::Int) {
        fail(path, "parameter '" + definition.parameters[index].name + "' is not a float or int");
    }
    return index;
}

ControllerMotion::Child childAt(const Json& entry, const std::string& path, size_t numbers) {
    if (!entry.is_array() || entry.size() != numbers + 1) {
        fail(path, numbers == 1 ? "expected [clip, threshold]" : "expected [clip, x, y]");
    }
    ControllerMotion::Child child;
    child.clip = stringAt(entry[0], path + "[0]");
    child.position.x = numberAt(entry[1], path + "[1]");
    if (numbers == 2) child.position.y = numberAt(entry[2], path + "[2]");
    return child;
}

ControllerState parseState(const ControllerDefinition& definition, const std::string& name, const Json& json,
                           const std::string& path) {
    checkKeys(json, path, {"clip", "blend1d", "blend2d", "speed", "speed_parameter", "loop", "events"});
    ControllerState state;
    state.name = name;

    const int kinds = int(json.contains("clip")) + int(json.contains("blend1d")) + int(json.contains("blend2d"));
    if (kinds != 1) fail(path, "needs exactly one of 'clip', 'blend1d', 'blend2d'");

    ControllerMotion& motion = state.motion;
    if (json.contains("clip")) {
        motion.kind = ControllerMotion::Kind::Clip;
        motion.children.push_back({stringAt(json["clip"], path + ".clip"), glm::vec2(0.0f)});
    } else if (json.contains("blend1d")) {
        const std::string at = path + ".blend1d";
        const Json& blend = json["blend1d"];
        checkKeys(blend, at, {"parameter", "clips"});
        motion.kind = ControllerMotion::Kind::Blend1D;
        motion.parameterX = numericParameterAt(definition, require(blend, "parameter", at), at + ".parameter");
        const Json& clips = require(blend, "clips", at);
        if (!clips.is_array() || clips.empty()) fail(at + ".clips", "expected a non-empty list");
        for (size_t i = 0; i < clips.size(); ++i) {
            const std::string childPath = at + ".clips[" + std::to_string(i) + "]";
            motion.children.push_back(childAt(clips[i], childPath, 1));
            if (i > 0 && motion.children[i].position.x <= motion.children[i - 1].position.x) {
                fail(childPath, "thresholds must ascend");
            }
        }
    } else {
        const std::string at = path + ".blend2d";
        const Json& blend = json["blend2d"];
        checkKeys(blend, at, {"parameters", "clips"});
        motion.kind = ControllerMotion::Kind::Blend2D;
        const Json& parameters = require(blend, "parameters", at);
        if (!parameters.is_array() || parameters.size() != 2) fail(at + ".parameters", "expected [x, y]");
        motion.parameterX = numericParameterAt(definition, parameters[0], at + ".parameters[0]");
        motion.parameterY = numericParameterAt(definition, parameters[1], at + ".parameters[1]");
        const Json& clips = require(blend, "clips", at);
        if (!clips.is_array() || clips.empty()) fail(at + ".clips", "expected a non-empty list");
        for (size_t i = 0; i < clips.size(); ++i) {
            motion.children.push_back(childAt(clips[i], at + ".clips[" + std::to_string(i) + "]", 2));
        }
    }

    if (json.contains("speed")) state.speed = numberAt(json["speed"], path + ".speed");
    if (json.contains("speed_parameter")) {
        state.speedParameter = numericParameterAt(definition, json["speed_parameter"], path + ".speed_parameter");
    }
    if (json.contains("loop")) {
        if (!json["loop"].is_boolean()) fail(path + ".loop", "expected true or false");
        state.loop = json["loop"].get<bool>();
    }
    if (json.contains("events")) {
        const Json& events = json["events"];
        if (!events.is_array()) fail(path + ".events", "expected a list");
        for (size_t i = 0; i < events.size(); ++i) {
            const std::string at = path + ".events[" + std::to_string(i) + "]";
            if (!events[i].is_array() || events[i].size() != 2) fail(at, "expected [time, name]");
            ControllerEvent event;
            event.time = numberAt(events[i][0], at + "[0]");
            if (event.time < 0.0f || event.time > 1.0f) fail(at + "[0]", "normalized time must be in 0..1");
            event.name = stringAt(events[i][1], at + "[1]");
            state.events.push_back(std::move(event));
        }
    }
    return state;
}

ControllerCondition parseCondition(const ControllerDefinition& definition, const Json& json, const std::string& path) {
    if (!json.is_array() || (json.size() != 1 && json.size() != 3)) {
        fail(path, "expected [parameter] or [parameter, op, value]");
    }
    ControllerCondition condition;
    condition.parameter = parameterAt(definition, json[0], path + "[0]");
    const auto type = definition.parameters[condition.parameter].type;
    if (json.size() == 1) {
        if (type != ControllerParameterType::Bool && type != ControllerParameterType::Trigger) {
            fail(path, "[parameter] needs a bool or trigger parameter");
        }
        condition.op = ControllerConditionOp::IsSet;
        return condition;
    }
    if (type == ControllerParameterType::Trigger) fail(path, "a trigger takes the form [parameter]");
    const std::string op = stringAt(json[1], path + "[1]");
    if (op == ">") condition.op = ControllerConditionOp::Greater;
    else if (op == "<") condition.op = ControllerConditionOp::Less;
    else if (op == "==") condition.op = ControllerConditionOp::Equal;
    else if (op == "!=") condition.op = ControllerConditionOp::NotEqual;
    else fail(path + "[1]", "unknown operator '" + op + "'");
    if (type == ControllerParameterType::Bool &&
        condition.op != ControllerConditionOp::Equal && condition.op != ControllerConditionOp::NotEqual) {
        fail(path + "[1]", "a bool compares with == or !=");
    }
    condition.value = scalarAt(json[2], path + "[2]");
    return condition;
}

ControllerLayer parseLayer(const ControllerDefinition& definition, const Json& json, const std::string& path) {
    checkKeys(json, path, {"name", "default", "weight", "mode", "mask", "states", "transitions"});
    ControllerLayer layer;
    layer.name = stringAt(require(json, "name", path), path + ".name");
    if (json.contains("weight")) layer.weight = numberAt(json["weight"], path + ".weight");
    if (json.contains("mode")) {
        const std::string mode = stringAt(json["mode"], path + ".mode");
        if (mode == "override") layer.mode = ControllerLayerMode::Override;
        else if (mode == "additive") layer.mode = ControllerLayerMode::Additive;
        else fail(path + ".mode", "expected 'override' or 'additive'");
    }
    if (json.contains("mask")) {
        const Json& mask = json["mask"];
        if (!mask.is_array()) fail(path + ".mask", "expected a list of joint names");
        for (size_t i = 0; i < mask.size(); ++i) {
            layer.mask.push_back(stringAt(mask[i], path + ".mask[" + std::to_string(i) + "]"));
        }
    }

    const Json& states = require(json, "states", path);
    if (!states.is_object() || states.empty()) fail(path + ".states", "expected an object of states");
    for (auto it = states.begin(); it != states.end(); ++it) {
        layer.states.push_back(parseState(definition, it.key(), it.value(), path + ".states." + it.key()));
    }

    if (json.contains("default")) {
        const std::string name = stringAt(json["default"], path + ".default");
        layer.defaultState = layer.findState(name);
        if (layer.defaultState < 0) fail(path + ".default", "no state '" + name + "'");
    }

    if (json.contains("transitions")) {
        const Json& transitions = json["transitions"];
        if (!transitions.is_array()) fail(path + ".transitions", "expected a list");
        for (size_t i = 0; i < transitions.size(); ++i) {
            const std::string at = path + ".transitions[" + std::to_string(i) + "]";
            const Json& entry = transitions[i];
            checkKeys(entry, at, {"from", "to", "duration", "exit_time", "when"});
            ControllerTransition transition;
            const std::string from = stringAt(require(entry, "from", at), at + ".from");
            if (from != "any") {
                transition.from = layer.findState(from);
                if (transition.from < 0) fail(at + ".from", "no state '" + from + "'");
            }
            const std::string to = stringAt(require(entry, "to", at), at + ".to");
            transition.to = layer.findState(to);
            if (transition.to < 0) fail(at + ".to", "no state '" + to + "'");
            if (entry.contains("duration")) {
                transition.duration = numberAt(entry["duration"], at + ".duration");
                if (transition.duration < 0.0f) fail(at + ".duration", "must not be negative");
            }
            if (entry.contains("exit_time")) {
                transition.exitTime = numberAt(entry["exit_time"], at + ".exit_time");
                if (transition.exitTime < 0.0f || transition.exitTime > 1.0f) {
                    fail(at + ".exit_time", "normalized time must be in 0..1");
                }
            }
            if (entry.contains("when")) {
                const Json& when = entry["when"];
                if (!when.is_array()) fail(at + ".when", "expected a list of conditions");
                for (size_t c = 0; c < when.size(); ++c) {
                    transition.conditions.push_back(
                        parseCondition(definition, when[c], at + ".when[" + std::to_string(c) + "]"));
                }
            }
            if (transition.conditions.empty() && transition.exitTime < 0.0f) {
                fail(at, "needs conditions ('when') or an exit time");
            }
            layer.transitions.push_back(std::move(transition));
        }
    }
    return layer;
}

} // namespace

std::shared_ptr<const ControllerDefinition> parseAnimationController(const std::string& text) {
    Json json;
    try {
        json = Json::parse(text);
    } catch (const nlohmann::json::parse_error& error) {
        fail("controller", std::string("invalid JSON: ") + error.what());
    }
    checkKeys(json, "controller", {"parameters", "layers"});

    auto definition = std::make_shared<ControllerDefinition>();
    if (json.contains("parameters")) {
        const Json& parameters = json["parameters"];
        if (!parameters.is_object()) fail("parameters", "expected an object");
        for (auto it = parameters.begin(); it != parameters.end(); ++it) {
            const std::string path = "parameters." + it.key();
            checkKeys(it.value(), path, {"type", "default"});
            ControllerParameter parameter;
            parameter.name = it.key();
            const std::string type = stringAt(require(it.value(), "type", path), path + ".type");
            if (type == "float") parameter.type = ControllerParameterType::Float;
            else if (type == "int") parameter.type = ControllerParameterType::Int;
            else if (type == "bool") parameter.type = ControllerParameterType::Bool;
            else if (type == "trigger") parameter.type = ControllerParameterType::Trigger;
            else fail(path + ".type", "expected 'float', 'int', 'bool' or 'trigger'");
            if (it.value().contains("default")) {
                if (parameter.type == ControllerParameterType::Trigger) fail(path + ".default", "a trigger has no default");
                parameter.defaultValue = scalarAt(it.value()["default"], path + ".default");
            }
            definition->parameters.push_back(std::move(parameter));
        }
    }

    const Json& layers = require(json, "layers", "controller");
    if (!layers.is_array() || layers.empty()) fail("layers", "expected a non-empty list");
    for (size_t i = 0; i < layers.size(); ++i) {
        definition->layers.push_back(parseLayer(*definition, layers[i], "layers[" + std::to_string(i) + "]"));
    }
    return definition;
}

} // namespace Shoonyakasha
