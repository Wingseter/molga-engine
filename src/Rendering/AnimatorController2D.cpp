#include "Rendering/AnimatorController2D.h"

#include "Core/Guid.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <system_error>
#include <unordered_map>
#include <unordered_set>

namespace molga {
namespace {

void SetError(std::string* out, std::string value) {
    if (out) *out = std::move(value);
}

bool WriteJsonAtomically(const std::filesystem::path& path,
                         const nlohmann::json& value,
                         std::string* errorOut) {
    const std::filesystem::path temporary = path.string() + ".tmp";
    try {
        if (!path.parent_path().empty()) {
            std::filesystem::create_directories(path.parent_path());
        }
        {
            std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
            if (!stream) {
                SetError(errorOut, "could not open temporary asset: " + temporary.string());
                return false;
            }
            stream << value.dump(2) << '\n';
            if (!stream) {
                SetError(errorOut, "could not write temporary asset: " + temporary.string());
                return false;
            }
        }
        std::error_code ec;
        std::filesystem::rename(temporary, path, ec);
        if (ec) {
            std::filesystem::remove(temporary);
            SetError(errorOut, "could not replace asset: " + ec.message());
            return false;
        }
        SetError(errorOut, {});
        return true;
    } catch (const std::exception& error) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        SetError(errorOut, error.what());
        return false;
    }
}

bool ParseParameterType(const std::string& text, AnimatorParameterType2D& result) {
    if (text == "Bool") result = AnimatorParameterType2D::Bool;
    else if (text == "Int") result = AnimatorParameterType2D::Int;
    else if (text == "Float") result = AnimatorParameterType2D::Float;
    else if (text == "Trigger") result = AnimatorParameterType2D::Trigger;
    else return false;
    return true;
}

bool ParseConditionOperator(const std::string& text,
                            AnimatorConditionOperator2D& result) {
    if (text == "Equals" || text == "Equal") result = AnimatorConditionOperator2D::Equals;
    else if (text == "NotEqual" || text == "NotEquals") result = AnimatorConditionOperator2D::NotEqual;
    else if (text == "Greater") result = AnimatorConditionOperator2D::Greater;
    else if (text == "Less") result = AnimatorConditionOperator2D::Less;
    else if (text == "GreaterOrEqual") result = AnimatorConditionOperator2D::GreaterOrEqual;
    else if (text == "LessOrEqual") result = AnimatorConditionOperator2D::LessOrEqual;
    else if (text == "IsTrue" || text == "If" || text == "True") result = AnimatorConditionOperator2D::IsTrue;
    else if (text == "IsFalse" || text == "IfNot" || text == "False") result = AnimatorConditionOperator2D::IsFalse;
    else return false;
    return true;
}

bool ReadTypedValue(const nlohmann::json& value,
                    AnimatorParameterType2D type,
                    AnimatorParameterValue2D& result) {
    switch (type) {
        case AnimatorParameterType2D::Bool:
        case AnimatorParameterType2D::Trigger:
            if (!value.is_boolean()) return false;
            result = value.get<bool>();
            return true;
        case AnimatorParameterType2D::Int:
            if (!value.is_number_integer()) return false;
            result = value.get<int>();
            return true;
        case AnimatorParameterType2D::Float:
            if (!value.is_number()) return false;
            result = value.get<float>();
            return std::isfinite(std::get<float>(result));
    }
    return false;
}

nlohmann::json SerializeValue(const AnimatorParameterValue2D& value) {
    return std::visit([](auto typed) { return nlohmann::json(typed); }, value);
}

bool IsOperatorValidFor(AnimatorParameterType2D type,
                        AnimatorConditionOperator2D op) {
    if (type == AnimatorParameterType2D::Bool ||
        type == AnimatorParameterType2D::Trigger) {
        return op == AnimatorConditionOperator2D::Equals ||
               op == AnimatorConditionOperator2D::NotEqual ||
               op == AnimatorConditionOperator2D::IsTrue ||
               op == AnimatorConditionOperator2D::IsFalse;
    }
    return op != AnimatorConditionOperator2D::IsTrue &&
           op != AnimatorConditionOperator2D::IsFalse;
}

} // namespace

const char* ToString(AnimatorParameterType2D value) {
    switch (value) {
        case AnimatorParameterType2D::Bool: return "Bool";
        case AnimatorParameterType2D::Int: return "Int";
        case AnimatorParameterType2D::Float: return "Float";
        case AnimatorParameterType2D::Trigger: return "Trigger";
    }
    return "Bool";
}

const char* ToString(AnimatorConditionOperator2D value) {
    switch (value) {
        case AnimatorConditionOperator2D::Equals: return "Equals";
        case AnimatorConditionOperator2D::NotEqual: return "NotEqual";
        case AnimatorConditionOperator2D::Greater: return "Greater";
        case AnimatorConditionOperator2D::Less: return "Less";
        case AnimatorConditionOperator2D::GreaterOrEqual: return "GreaterOrEqual";
        case AnimatorConditionOperator2D::LessOrEqual: return "LessOrEqual";
        case AnimatorConditionOperator2D::IsTrue: return "IsTrue";
        case AnimatorConditionOperator2D::IsFalse: return "IsFalse";
    }
    return "Equals";
}

const AnimatorParameter2D* AnimatorController2D::FindParameter(
    const std::string& name) const {
    const auto found = std::find_if(parameters_.begin(), parameters_.end(),
        [&](const AnimatorParameter2D& parameter) { return parameter.name == name; });
    return found == parameters_.end() ? nullptr : &*found;
}

const AnimatorState2D* AnimatorController2D::FindState(
    const std::string& idOrName) const {
    auto found = std::find_if(states_.begin(), states_.end(),
        [&](const AnimatorState2D& state) { return state.id == idOrName; });
    if (found == states_.end()) {
        found = std::find_if(states_.begin(), states_.end(),
            [&](const AnimatorState2D& state) { return state.name == idOrName; });
    }
    return found == states_.end() ? nullptr : &*found;
}

bool AnimatorController2D::Validate(std::string* errorOut) const {
    std::unordered_map<std::string, AnimatorParameterType2D> parameterTypes;
    for (const AnimatorParameter2D& parameter : parameters_) {
        if (parameter.name.empty() || !parameterTypes.emplace(parameter.name, parameter.type).second) {
            SetError(errorOut, "parameter names must be non-empty and unique");
            return false;
        }
        const bool typed =
            ((parameter.type == AnimatorParameterType2D::Bool ||
              parameter.type == AnimatorParameterType2D::Trigger) &&
             std::holds_alternative<bool>(parameter.defaultValue)) ||
            (parameter.type == AnimatorParameterType2D::Int &&
             std::holds_alternative<int>(parameter.defaultValue)) ||
            (parameter.type == AnimatorParameterType2D::Float &&
             std::holds_alternative<float>(parameter.defaultValue) &&
             std::isfinite(std::get<float>(parameter.defaultValue)));
        if (!typed) {
            SetError(errorOut, "parameter defaultValue does not match type: " + parameter.name);
            return false;
        }
        if (parameter.type == AnimatorParameterType2D::Trigger &&
            std::get<bool>(parameter.defaultValue)) {
            SetError(errorOut, "trigger defaultValue must be false");
            return false;
        }
    }

    std::unordered_set<std::string> stateIds;
    for (const AnimatorState2D& state : states_) {
        if (state.id.empty() || !stateIds.insert(state.id).second) {
            SetError(errorOut, "state IDs must be non-empty and unique");
            return false;
        }
        if (!Guid::IsValid(state.clipGuid)) {
            SetError(errorOut, "state clipGuid must be a 32-character GUID: " + state.id);
            return false;
        }
        if (!std::isfinite(state.speed) || state.speed < 0.0f) {
            SetError(errorOut, "state speed must be finite and non-negative: " + state.id);
            return false;
        }
    }
    if (states_.empty() || stateIds.count(defaultStateId_) == 0) {
        SetError(errorOut, "defaultStateId must reference an existing state");
        return false;
    }

    for (const AnimatorTransition2D& transition : transitions_) {
        if (stateIds.count(transition.fromStateId) == 0 ||
            stateIds.count(transition.toStateId) == 0) {
            SetError(errorOut, "transition references a missing state");
            return false;
        }
        if (!std::isfinite(transition.exitTime) || transition.exitTime < 0.0f) {
            SetError(errorOut, "transition exitTime must be finite and non-negative");
            return false;
        }
        for (const AnimatorCondition2D& condition : transition.conditions) {
            const auto parameter = parameterTypes.find(condition.parameter);
            if (parameter == parameterTypes.end()) {
                SetError(errorOut, "transition condition references a missing parameter: " +
                                       condition.parameter);
                return false;
            }
            if (!IsOperatorValidFor(parameter->second, condition.op)) {
                SetError(errorOut, "condition operator does not match parameter type: " +
                                       condition.parameter);
                return false;
            }
            const bool typed =
                ((parameter->second == AnimatorParameterType2D::Bool ||
                  parameter->second == AnimatorParameterType2D::Trigger) &&
                 std::holds_alternative<bool>(condition.value)) ||
                (parameter->second == AnimatorParameterType2D::Int &&
                 std::holds_alternative<int>(condition.value)) ||
                (parameter->second == AnimatorParameterType2D::Float &&
                 std::holds_alternative<float>(condition.value) &&
                 std::isfinite(std::get<float>(condition.value)));
            if (!typed) {
                SetError(errorOut, "condition value does not match parameter type: " +
                                       condition.parameter);
                return false;
            }
        }
    }
    SetError(errorOut, {});
    return true;
}

nlohmann::json AnimatorController2D::ToJson() const {
    nlohmann::json parameters = nlohmann::json::array();
    for (const AnimatorParameter2D& parameter : parameters_) {
        parameters.push_back({
            {"name", parameter.name},
            {"type", ToString(parameter.type)},
            {"defaultValue", SerializeValue(parameter.defaultValue)}
        });
    }

    nlohmann::json states = nlohmann::json::array();
    for (const AnimatorState2D& state : states_) {
        states.push_back({
            {"id", state.id},
            {"name", state.name},
            {"clipGuid", state.clipGuid},
            {"speed", state.speed}
        });
    }

    nlohmann::json transitions = nlohmann::json::array();
    for (const AnimatorTransition2D& transition : transitions_) {
        nlohmann::json conditions = nlohmann::json::array();
        for (const AnimatorCondition2D& condition : transition.conditions) {
            conditions.push_back({
                {"parameter", condition.parameter},
                {"operator", ToString(condition.op)},
                {"value", SerializeValue(condition.value)}
            });
        }
        transitions.push_back({
            {"fromStateId", transition.fromStateId},
            {"toStateId", transition.toStateId},
            {"hasExitTime", transition.hasExitTime},
            {"exitTime", transition.exitTime},
            {"conditions", std::move(conditions)}
        });
    }

    return {
        {"schemaVersion", SchemaVersion},
        {"parameters", std::move(parameters)},
        {"states", std::move(states)},
        {"defaultStateId", defaultStateId_},
        {"transitions", std::move(transitions)}
    };
}

bool AnimatorController2D::FromJson(const nlohmann::json& document,
                                    std::string* errorOut) {
    try {
        if (!document.is_object()) {
            SetError(errorOut, "animator controller root must be an object");
            return false;
        }
        if (document.value("schemaVersion", SchemaVersion) != SchemaVersion) {
            SetError(errorOut, "unsupported animator controller schemaVersion");
            return false;
        }
        if (!document.contains("parameters") || !document["parameters"].is_array() ||
            !document.contains("states") || !document["states"].is_array() ||
            !document.contains("transitions") || !document["transitions"].is_array() ||
            !document.contains("defaultStateId") || !document["defaultStateId"].is_string()) {
            SetError(errorOut, "animator controller arrays and defaultStateId are required");
            return false;
        }

        AnimatorController2D candidate;
        candidate.defaultStateId_ = document["defaultStateId"].get<std::string>();

        for (const nlohmann::json& value : document["parameters"]) {
            if (!value.is_object() || !value.contains("name") || !value["name"].is_string() ||
                !value.contains("type") || !value["type"].is_string()) {
                SetError(errorOut, "each animator parameter requires name and type");
                return false;
            }
            AnimatorParameter2D parameter;
            parameter.name = value["name"].get<std::string>();
            if (!ParseParameterType(value["type"].get<std::string>(), parameter.type)) {
                SetError(errorOut, "unknown animator parameter type");
                return false;
            }
            nlohmann::json defaultValue;
            if (value.contains("defaultValue")) defaultValue = value["defaultValue"];
            else if (parameter.type == AnimatorParameterType2D::Int) defaultValue = 0;
            else if (parameter.type == AnimatorParameterType2D::Float) defaultValue = 0.0f;
            else defaultValue = false;
            if (!ReadTypedValue(defaultValue, parameter.type, parameter.defaultValue)) {
                SetError(errorOut, "animator parameter defaultValue has the wrong type");
                return false;
            }
            if (parameter.type == AnimatorParameterType2D::Trigger) parameter.defaultValue = false;
            candidate.parameters_.push_back(std::move(parameter));
        }

        for (const nlohmann::json& value : document["states"]) {
            if (!value.is_object() || !value.contains("id") || !value["id"].is_string() ||
                !value.contains("clipGuid") || !value["clipGuid"].is_string()) {
                SetError(errorOut, "each animator state requires id and clipGuid");
                return false;
            }
            AnimatorState2D state;
            state.id = value["id"].get<std::string>();
            state.name = value.value("name", state.id);
            state.clipGuid = value["clipGuid"].get<std::string>();
            state.speed = value.value("speed", 1.0f);
            candidate.states_.push_back(std::move(state));
        }

        for (const nlohmann::json& value : document["transitions"]) {
            if (!value.is_object()) {
                SetError(errorOut, "each animator transition must be an object");
                return false;
            }
            AnimatorTransition2D transition;
            transition.fromStateId = value.value(
                "fromStateId", value.value("from", std::string{}));
            transition.toStateId = value.value(
                "toStateId", value.value("to", std::string{}));
            transition.hasExitTime = value.value("hasExitTime", false);
            transition.exitTime = value.value("exitTime", 0.0f);
            if (value.contains("conditions")) {
                if (!value["conditions"].is_array()) {
                    SetError(errorOut, "transition conditions must be an array");
                    return false;
                }
                for (const nlohmann::json& conditionValue : value["conditions"]) {
                    if (!conditionValue.is_object()) {
                        SetError(errorOut, "each transition condition must be an object");
                        return false;
                    }
                    AnimatorCondition2D condition;
                    condition.parameter = conditionValue.value(
                        "parameter", conditionValue.value("parameterName", std::string{}));
                    const auto definition = std::find_if(
                        candidate.parameters_.begin(), candidate.parameters_.end(),
                        [&](const AnimatorParameter2D& item) {
                            return item.name == condition.parameter;
                        });
                    if (definition == candidate.parameters_.end()) {
                        SetError(errorOut, "condition references an unknown parameter: " +
                                               condition.parameter);
                        return false;
                    }
                    const std::string opText = conditionValue.value(
                        "operator", conditionValue.value("op",
                            conditionValue.value("mode", std::string("Equals"))));
                    if (!ParseConditionOperator(opText, condition.op)) {
                        SetError(errorOut, "unknown animator condition operator: " + opText);
                        return false;
                    }
                    nlohmann::json expected;
                    if (conditionValue.contains("value")) expected = conditionValue["value"];
                    else if (definition->type == AnimatorParameterType2D::Int) expected = 0;
                    else if (definition->type == AnimatorParameterType2D::Float) expected = 0.0f;
                    else expected = condition.op != AnimatorConditionOperator2D::IsFalse;
                    if (!ReadTypedValue(expected, definition->type, condition.value)) {
                        SetError(errorOut, "animator condition value has the wrong type");
                        return false;
                    }
                    transition.conditions.push_back(std::move(condition));
                }
            }
            candidate.transitions_.push_back(std::move(transition));
        }

        if (!candidate.Validate(errorOut)) return false;
        *this = std::move(candidate);
        return true;
    } catch (const std::exception& error) {
        SetError(errorOut, std::string("invalid animator controller: ") + error.what());
        return false;
    }
}

bool AnimatorController2D::LoadFromFile(const std::filesystem::path& path,
                                        std::string* errorOut) {
    try {
        std::ifstream stream(path, std::ios::binary);
        if (!stream) {
            SetError(errorOut, "could not open animator controller: " + path.string());
            return false;
        }
        nlohmann::json document;
        stream >> document;
        return FromJson(document, errorOut);
    } catch (const std::exception& error) {
        SetError(errorOut, std::string("could not parse animator controller: ") + error.what());
        return false;
    }
}

bool AnimatorController2D::SaveToFile(const std::filesystem::path& path,
                                      std::string* errorOut) const {
    if (!Validate(errorOut)) return false;
    return WriteJsonAtomically(path, ToJson(), errorOut);
}

} // namespace molga
