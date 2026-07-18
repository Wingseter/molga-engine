#pragma once

#include <filesystem>
#include <nlohmann/json.hpp>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace molga {

enum class AnimatorParameterType2D { Bool, Int, Float, Trigger };
enum class AnimatorConditionOperator2D {
    Equals,
    NotEqual,
    Greater,
    Less,
    GreaterOrEqual,
    LessOrEqual,
    IsTrue,
    IsFalse
};

using AnimatorParameterValue2D = std::variant<bool, int, float>;

struct AnimatorParameter2D {
    std::string name;
    AnimatorParameterType2D type = AnimatorParameterType2D::Bool;
    AnimatorParameterValue2D defaultValue = false;
};

struct AnimatorCondition2D {
    std::string parameter;
    AnimatorConditionOperator2D op = AnimatorConditionOperator2D::IsTrue;
    AnimatorParameterValue2D value = true;
};

struct AnimatorState2D {
    std::string id;
    std::string name;
    std::string clipGuid;
    float speed = 1.0f;
};

struct AnimatorTransition2D {
    std::string fromStateId;
    std::string toStateId;
    bool hasExitTime = false;
    float exitTime = 0.0f;
    std::vector<AnimatorCondition2D> conditions;
};

// Ordered transitions are the priority contract: when several transitions are
// eligible during one evaluation, the first matching serialized entry wins.
class AnimatorController2D {
public:
    static constexpr int SchemaVersion = 1;

    const std::vector<AnimatorParameter2D>& GetParameters() const { return parameters_; }
    std::vector<AnimatorParameter2D>& GetParameters() { return parameters_; }
    void SetParameters(std::vector<AnimatorParameter2D> value) {
        parameters_ = std::move(value);
    }

    const std::vector<AnimatorState2D>& GetStates() const { return states_; }
    std::vector<AnimatorState2D>& GetStates() { return states_; }
    void SetStates(std::vector<AnimatorState2D> value) { states_ = std::move(value); }

    const std::vector<AnimatorTransition2D>& GetTransitions() const { return transitions_; }
    std::vector<AnimatorTransition2D>& GetTransitions() { return transitions_; }
    void SetTransitions(std::vector<AnimatorTransition2D> value) {
        transitions_ = std::move(value);
    }

    const std::string& GetDefaultStateId() const { return defaultStateId_; }
    void SetDefaultStateId(std::string value) { defaultStateId_ = std::move(value); }

    const AnimatorParameter2D* FindParameter(const std::string& name) const;
    const AnimatorState2D* FindState(const std::string& idOrName) const;

    bool Validate(std::string* errorOut = nullptr) const;
    nlohmann::json ToJson() const;
    bool FromJson(const nlohmann::json& document, std::string* errorOut = nullptr);
    bool LoadFromFile(const std::filesystem::path& path, std::string* errorOut = nullptr);
    bool SaveToFile(const std::filesystem::path& path, std::string* errorOut = nullptr) const;

private:
    std::vector<AnimatorParameter2D> parameters_;
    std::vector<AnimatorState2D> states_;
    std::vector<AnimatorTransition2D> transitions_;
    std::string defaultStateId_;
};

const char* ToString(AnimatorParameterType2D value);
const char* ToString(AnimatorConditionOperator2D value);

} // namespace molga

using AnimatorController2D = molga::AnimatorController2D;
using AnimatorParameter2D = molga::AnimatorParameter2D;
using AnimatorState2D = molga::AnimatorState2D;
using AnimatorTransition2D = molga::AnimatorTransition2D;
using AnimatorCondition2D = molga::AnimatorCondition2D;
using AnimatorParameterType2D = molga::AnimatorParameterType2D;
using AnimatorConditionOperator2D = molga::AnimatorConditionOperator2D;
