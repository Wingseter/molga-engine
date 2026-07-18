#include "ECS/Components/Animator2D.h"

#include "Common/Log.h"
#include "Core/AssetDatabase.h"
#include "ECS/GameObject.h"
#include "ECS/ComponentFactory.h"
#include "ECS/Components/SpriteRenderer.h"

#include <algorithm>
#include <cmath>
#include <limits>

REGISTER_COMPONENT(Animator2D)

namespace {

template<typename T>
const T* GetValue(const std::unordered_map<std::string, molga::AnimatorParameterValue2D>& values,
                  const std::string& name) {
    const auto found = values.find(name);
    if (found == values.end()) return nullptr;
    return std::get_if<T>(&found->second);
}

float SafeSpeed(float value) {
    return std::isfinite(value) && value >= 0.0f ? value : 1.0f;
}

} // namespace

void Animator2D::SetControllerGuid(std::string guid) {
    if (controllerGuid_ == guid) return;
    RestoreAuthoredSprite();
    controllerGuid_ = std::move(guid);
    controller_.reset();
    clips_.clear();
    parameters_.clear();
    currentStateId_.clear();
    stateTimeSeconds_ = 0.0;
    currentFrameIndex_ = 0;
    controllerLoadAttempted_ = false;
    playbackState_ = controllerGuid_.empty()
        ? PlaybackState::Stopped
        : (autoPlay_ ? PlaybackState::Playing : PlaybackState::Stopped);
}

bool Animator2D::SetControllerAsset(
    std::shared_ptr<const molga::AnimatorController2D> controller) {
    RestoreAuthoredSprite();
    if (controller) {
        std::string error;
        if (!controller->Validate(&error)) {
            Log::Warn("Animator2D", "Rejected invalid controller: " + error);
            controller.reset();
        }
    }
    controller_ = std::move(controller);
    clips_.clear();
    currentStateId_.clear();
    stateTimeSeconds_ = 0.0;
    currentFrameIndex_ = 0;
    controllerLoadAttempted_ = true;
    InitializeParameters();
    playbackState_ = controller_ && autoPlay_
        ? PlaybackState::Playing : PlaybackState::Stopped;
    return controller_ != nullptr;
}

void Animator2D::SetClipAsset(
    std::string guid, std::shared_ptr<const molga::AnimationClip2D> clip) {
    if (guid.empty()) return;
    if (clip) clips_[std::move(guid)] = std::move(clip);
    else clips_.erase(guid);
}

void Animator2D::ClearController() {
    RestoreAuthoredSprite();
    controllerGuid_.clear();
    controller_.reset();
    clips_.clear();
    parameters_.clear();
    currentStateId_.clear();
    stateTimeSeconds_ = 0.0;
    currentFrameIndex_ = 0;
    controllerLoadAttempted_ = false;
    playbackState_ = PlaybackState::Stopped;
}

bool Animator2D::ReloadController(std::string* errorOut) {
    RestoreAuthoredSprite();
    controller_.reset();
    clips_.clear();
    parameters_.clear();
    currentStateId_.clear();
    stateTimeSeconds_ = 0.0;
    currentFrameIndex_ = 0;
    controllerLoadAttempted_ = false;
    return EnsureControllerLoaded(errorOut);
}

bool Animator2D::EnsureControllerLoaded(std::string* errorOut) {
    if (controller_) {
        if (errorOut) errorOut->clear();
        return true;
    }
    if (controllerLoadAttempted_) {
        if (errorOut) *errorOut = "controller is unavailable";
        return false;
    }
    controllerLoadAttempted_ = true;
    if (controllerGuid_.empty()) {
        if (errorOut) *errorOut = "controllerGuid is empty";
        return false;
    }

    const molga::AssetRecord* record = molga::AssetDatabase::Get().Find(controllerGuid_);
    const std::filesystem::path path =
        molga::AssetDatabase::Get().AbsoluteSourcePath(controllerGuid_);
    if (!record || record->importFailed || path.empty()) {
        if (errorOut) *errorOut = "controller asset is missing or failed import";
        return false;
    }

    auto loaded = std::make_shared<molga::AnimatorController2D>();
    std::string error;
    if (!loaded->LoadFromFile(path, &error)) {
        if (errorOut) *errorOut = error;
        return false;
    }
    controller_ = std::move(loaded);
    InitializeParameters();

    for (const molga::AnimatorState2D& state : controller_->GetStates()) {
        if (clips_.count(state.clipGuid) != 0) continue;
        const molga::AssetRecord* clipRecord =
            molga::AssetDatabase::Get().Find(state.clipGuid);
        const std::filesystem::path clipPath =
            molga::AssetDatabase::Get().AbsoluteSourcePath(state.clipGuid);
        if (!clipRecord || clipRecord->importFailed || clipPath.empty()) continue;
        auto clip = std::make_shared<molga::AnimationClip2D>();
        if (clip->LoadFromFile(clipPath, &error)) {
            clips_[state.clipGuid] = std::move(clip);
        } else {
            Log::Warn("Animator2D", "Could not load clip " + state.clipGuid + ": " + error);
        }
    }
    if (errorOut) errorOut->clear();
    return true;
}

void Animator2D::InitializeParameters() {
    parameters_.clear();
    if (!controller_) return;
    for (const molga::AnimatorParameter2D& parameter : controller_->GetParameters()) {
        if (parameter.type == molga::AnimatorParameterType2D::Trigger) {
            parameters_[parameter.name] = false;
        } else {
            parameters_[parameter.name] = parameter.defaultValue;
        }
    }
}

bool Animator2D::Play() {
    if (!EnsureControllerLoaded()) {
        RestoreAuthoredSprite();
        playbackState_ = PlaybackState::Stopped;
        return false;
    }
    if (currentStateId_.empty() &&
        !EnterState(controller_->GetDefaultStateId(), 0.0f)) {
        playbackState_ = PlaybackState::Stopped;
        RestoreAuthoredSprite();
        return false;
    }
    playbackState_ = PlaybackState::Playing;
    ApplyCurrentFrame();
    return true;
}

bool Animator2D::Play(const std::string& stateIdOrName, float normalizedTime) {
    if (!EnsureControllerLoaded() ||
        !EnterState(stateIdOrName, normalizedTime)) {
        return false;
    }
    playbackState_ = PlaybackState::Playing;
    ApplyCurrentFrame();
    return true;
}

void Animator2D::Stop() {
    playbackState_ = PlaybackState::Stopped;
    currentStateId_.clear();
    stateTimeSeconds_ = 0.0;
    currentFrameIndex_ = 0;
    resumeAfterEnable_ = false;
    RestoreAuthoredSprite();
}

void Animator2D::Pause() {
    if (playbackState_ == PlaybackState::Playing) {
        playbackState_ = PlaybackState::Paused;
    }
}

void Animator2D::Resume() {
    if (playbackState_ == PlaybackState::Paused) {
        playbackState_ = PlaybackState::Playing;
    }
}

bool Animator2D::SetBool(const std::string& name, bool value) {
    if (!EnsureControllerLoaded()) return false;
    const molga::AnimatorParameter2D* definition = controller_->FindParameter(name);
    if (!definition || definition->type != molga::AnimatorParameterType2D::Bool) return false;
    parameters_[name] = value;
    return true;
}

bool Animator2D::GetBool(const std::string& name) const {
    const bool* value = GetValue<bool>(parameters_, name);
    return value ? *value : false;
}

bool Animator2D::SetInt(const std::string& name, int value) {
    if (!EnsureControllerLoaded()) return false;
    const molga::AnimatorParameter2D* definition = controller_->FindParameter(name);
    if (!definition || definition->type != molga::AnimatorParameterType2D::Int) return false;
    parameters_[name] = value;
    return true;
}

int Animator2D::GetInt(const std::string& name) const {
    const int* value = GetValue<int>(parameters_, name);
    return value ? *value : 0;
}

bool Animator2D::SetFloat(const std::string& name, float value) {
    if (!std::isfinite(value) || !EnsureControllerLoaded()) return false;
    const molga::AnimatorParameter2D* definition = controller_->FindParameter(name);
    if (!definition || definition->type != molga::AnimatorParameterType2D::Float) return false;
    parameters_[name] = value;
    return true;
}

float Animator2D::GetFloat(const std::string& name) const {
    const float* value = GetValue<float>(parameters_, name);
    return value ? *value : 0.0f;
}

bool Animator2D::SetTrigger(const std::string& name) {
    if (!EnsureControllerLoaded()) return false;
    const molga::AnimatorParameter2D* definition = controller_->FindParameter(name);
    if (!definition || definition->type != molga::AnimatorParameterType2D::Trigger) return false;
    parameters_[name] = true;
    return true;
}

bool Animator2D::ResetTrigger(const std::string& name) {
    if (!EnsureControllerLoaded()) return false;
    const molga::AnimatorParameter2D* definition = controller_->FindParameter(name);
    if (!definition || definition->type != molga::AnimatorParameterType2D::Trigger) return false;
    parameters_[name] = false;
    return true;
}

bool Animator2D::IsTriggerSet(const std::string& name) const {
    const bool* value = GetValue<bool>(parameters_, name);
    if (!value || !controller_) return false;
    const molga::AnimatorParameter2D* definition = controller_->FindParameter(name);
    return definition && definition->type == molga::AnimatorParameterType2D::Trigger && *value;
}

void Animator2D::SetSpeed(float value) {
    speed_ = SafeSpeed(value);
}

const molga::AnimatorState2D* Animator2D::CurrentState() const {
    return controller_ ? controller_->FindState(currentStateId_) : nullptr;
}

const molga::AnimationClip2D* Animator2D::ClipForState(
    const molga::AnimatorState2D* state) const {
    if (!state) return nullptr;
    const auto found = clips_.find(state->clipGuid);
    return found == clips_.end() ? nullptr : found->second.get();
}

bool Animator2D::EnterState(const std::string& stateIdOrName,
                            float normalizedTime) {
    if (!controller_) return false;
    const molga::AnimatorState2D* state = controller_->FindState(stateIdOrName);
    if (!state) return false;
    currentStateId_ = state->id;
    const molga::AnimationClip2D* clip = ClipForState(state);
    const float duration = clip ? clip->GetDurationSeconds() : 0.0f;
    const double normalized = std::isfinite(normalizedTime)
        ? std::max(0.0, static_cast<double>(normalizedTime)) : 0.0;
    stateTimeSeconds_ = duration > 0.0f ? normalized * duration : 0.0;
    if (clip && !clip->IsLooping()) {
        stateTimeSeconds_ = std::min(stateTimeSeconds_, static_cast<double>(duration));
    }
    currentFrameIndex_ = clip
        ? clip->GetFrameIndexAt(static_cast<float>(stateTimeSeconds_)) : 0;
    return true;
}

std::string Animator2D::GetCurrentStateName() const {
    const molga::AnimatorState2D* state = CurrentState();
    return state ? state->name : std::string{};
}

float Animator2D::GetNormalizedTime() const {
    const molga::AnimationClip2D* clip = ClipForState(CurrentState());
    const float duration = clip ? clip->GetDurationSeconds() : 0.0f;
    if (duration <= 0.0f || !std::isfinite(stateTimeSeconds_)) return 0.0f;
    double normalized = stateTimeSeconds_ / duration;
    if (clip && !clip->IsLooping()) normalized = std::min(1.0, normalized);
    normalized = std::max(0.0, normalized);
    return static_cast<float>(std::min(
        normalized, static_cast<double>(std::numeric_limits<float>::max())));
}

bool Animator2D::ConditionMatches(
    const molga::AnimatorCondition2D& condition) const {
    if (!controller_) return false;
    const molga::AnimatorParameter2D* definition =
        controller_->FindParameter(condition.parameter);
    const auto current = parameters_.find(condition.parameter);
    if (!definition || current == parameters_.end()) return false;

    using Operator = molga::AnimatorConditionOperator2D;
    if (definition->type == molga::AnimatorParameterType2D::Bool ||
        definition->type == molga::AnimatorParameterType2D::Trigger) {
        const bool* actual = std::get_if<bool>(&current->second);
        const bool* expected = std::get_if<bool>(&condition.value);
        if (!actual || !expected) return false;
        if (condition.op == Operator::IsTrue) return *actual;
        if (condition.op == Operator::IsFalse) return !*actual;
        if (condition.op == Operator::Equals) return *actual == *expected;
        if (condition.op == Operator::NotEqual) return *actual != *expected;
        return false;
    }

    auto compare = [&](auto actual, auto expected) {
        switch (condition.op) {
            case Operator::Equals: return actual == expected;
            case Operator::NotEqual: return actual != expected;
            case Operator::Greater: return actual > expected;
            case Operator::Less: return actual < expected;
            case Operator::GreaterOrEqual: return actual >= expected;
            case Operator::LessOrEqual: return actual <= expected;
            default: return false;
        }
    };
    if (definition->type == molga::AnimatorParameterType2D::Int) {
        const int* actual = std::get_if<int>(&current->second);
        const int* expected = std::get_if<int>(&condition.value);
        return actual && expected && compare(*actual, *expected);
    }
    const float* actual = std::get_if<float>(&current->second);
    const float* expected = std::get_if<float>(&condition.value);
    return actual && expected && compare(*actual, *expected);
}

const molga::AnimatorTransition2D* Animator2D::SelectTransition(
    float normalizedTime) const {
    if (!controller_) return nullptr;
    for (const molga::AnimatorTransition2D& transition : controller_->GetTransitions()) {
        if (transition.fromStateId != currentStateId_) continue;
        if (transition.hasExitTime && normalizedTime < transition.exitTime) continue;
        bool allMatch = true;
        for (const molga::AnimatorCondition2D& condition : transition.conditions) {
            if (!ConditionMatches(condition)) {
                allMatch = false;
                break;
            }
        }
        if (allMatch) return &transition;
    }
    return nullptr;
}

void Animator2D::ConsumeSelectedTriggers(
    const molga::AnimatorTransition2D& transition) {
    if (!controller_) return;
    for (const molga::AnimatorCondition2D& condition : transition.conditions) {
        const molga::AnimatorParameter2D* definition =
            controller_->FindParameter(condition.parameter);
        if (definition && definition->type == molga::AnimatorParameterType2D::Trigger) {
            parameters_[condition.parameter] = false;
        }
    }
}

void Animator2D::ApplyCurrentFrame() {
    const molga::AnimationClip2D* clip = ClipForState(CurrentState());
    if (!clip || clip->GetFrames().empty()) {
        RestoreAuthoredSprite();
        return;
    }
    double localTime = stateTimeSeconds_;
    const float duration = clip->GetDurationSeconds();
    if (clip->IsLooping() && duration > 0.0f) {
        localTime = std::fmod(localTime, static_cast<double>(duration));
        if (localTime < 0.0) localTime += duration;
    }
    currentFrameIndex_ = clip->GetFrameIndexAt(static_cast<float>(localTime));
    const molga::SpriteRef sprite = clip->GetSpriteRef(currentFrameIndex_);
    if (sprite.Empty()) {
        RestoreAuthoredSprite();
        return;
    }
    if (gameObject) {
        if (auto* renderer = gameObject->GetComponent<SpriteRenderer>()) {
            renderer->SetRuntimeSpriteOverride(sprite);
        }
    }
}

void Animator2D::RestoreAuthoredSprite() {
    if (!gameObject) return;
    if (auto* renderer = gameObject->GetComponent<SpriteRenderer>()) {
        renderer->ClearRuntimeSpriteOverride();
    }
}

void Animator2D::Evaluate(float dt) {
    if (playbackState_ != PlaybackState::Playing || !IsEnabled()) return;
    if (!EnsureControllerLoaded()) {
        RestoreAuthoredSprite();
        return;
    }
    if (currentStateId_.empty() &&
        !EnterState(controller_->GetDefaultStateId(), 0.0f)) {
        RestoreAuthoredSprite();
        return;
    }

    const molga::AnimatorState2D* state = CurrentState();
    const molga::AnimationClip2D* clip = ClipForState(state);
    const float duration = clip ? clip->GetDurationSeconds() : 0.0f;
    if (state && clip && duration > 0.0f && std::isfinite(dt) && dt > 0.0f) {
        const double delta = static_cast<double>(dt) * SafeSpeed(speed_) *
                             SafeSpeed(state->speed);
        if (std::isfinite(delta)) {
            stateTimeSeconds_ += delta;
            if (!clip->IsLooping()) {
                stateTimeSeconds_ = std::min(
                    stateTimeSeconds_, static_cast<double>(duration));
            }
        }
    }

    const molga::AnimatorTransition2D* selected =
        SelectTransition(GetNormalizedTime());
    if (selected) {
        const std::string target = selected->toStateId;
        ConsumeSelectedTriggers(*selected);
        EnterState(target, 0.0f);
    }
    ApplyCurrentFrame();
}

void Animator2D::ResolveAssets() {
    std::string error;
    if (!EnsureControllerLoaded(&error) && !controllerGuid_.empty()) {
        Log::Warn("Animator2D", "Could not resolve controller " + controllerGuid_ +
                                ": " + error);
    }
}

void Animator2D::OnEnable() {
    if (resumeAfterEnable_ || autoPlay_) {
        playbackState_ = PlaybackState::Playing;
    }
    resumeAfterEnable_ = false;
}

void Animator2D::OnDisable() {
    resumeAfterEnable_ = playbackState_ != PlaybackState::Stopped;
    playbackState_ = PlaybackState::Stopped;
    currentStateId_.clear();
    stateTimeSeconds_ = 0.0;
    currentFrameIndex_ = 0;
    RestoreAuthoredSprite();
}

void Animator2D::OnDetach() {
    RestoreAuthoredSprite();
}

void Animator2D::OnDestroy() {
    RestoreAuthoredSprite();
}

void Animator2D::Serialize(nlohmann::json& json) const {
    json["controllerGuid"] = controllerGuid_;
    json["speed"] = speed_;
    json["autoPlay"] = autoPlay_;
}

void Animator2D::Deserialize(const nlohmann::json& json) {
    RestoreAuthoredSprite();
    controllerGuid_ = json.value("controllerGuid", std::string{});
    speed_ = SafeSpeed(json.value("speed", 1.0f));
    autoPlay_ = json.value("autoPlay", true);
    controller_.reset();
    clips_.clear();
    parameters_.clear();
    currentStateId_.clear();
    stateTimeSeconds_ = 0.0;
    currentFrameIndex_ = 0;
    controllerLoadAttempted_ = false;
    playbackState_ = autoPlay_ && !controllerGuid_.empty()
        ? PlaybackState::Playing : PlaybackState::Stopped;
}
