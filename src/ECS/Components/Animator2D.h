#pragma once

#include "ECS/Component.h"
#include "Rendering/AnimationClip2D.h"
#include "Rendering/AnimatorController2D.h"

#include <memory>
#include <string>
#include <unordered_map>

class Animator2D : public Component {
public:
    COMPONENT_TYPE(Animator2D)

    enum class PlaybackState { Stopped, Playing, Paused };

    Animator2D() = default;

    void SetControllerGuid(std::string guid);
    const std::string& GetControllerGuid() const { return controllerGuid_; }
    const molga::AnimatorController2D* GetController() const { return controller_.get(); }

    // Runtime/in-memory installation is useful for generated controllers and
    // tests. Serialized scenes store only controllerGuid.
    bool SetControllerAsset(std::shared_ptr<const molga::AnimatorController2D> controller);
    void SetClipAsset(std::string guid,
                      std::shared_ptr<const molga::AnimationClip2D> clip);
    void ClearController();
    bool ReloadController(std::string* errorOut = nullptr);

    bool Play();
    bool Play(const std::string& stateIdOrName, float normalizedTime = 0.0f);
    void Stop();
    void Pause();
    void Resume();

    PlaybackState GetPlaybackState() const { return playbackState_; }
    bool IsPlaying() const { return playbackState_ == PlaybackState::Playing; }
    bool IsPaused() const { return playbackState_ == PlaybackState::Paused; }
    bool IsStopped() const { return playbackState_ == PlaybackState::Stopped; }

    bool SetBool(const std::string& name, bool value);
    bool GetBool(const std::string& name) const;
    bool SetInt(const std::string& name, int value);
    int GetInt(const std::string& name) const;
    bool SetFloat(const std::string& name, float value);
    float GetFloat(const std::string& name) const;
    bool SetTrigger(const std::string& name);
    bool ResetTrigger(const std::string& name);
    bool IsTriggerSet(const std::string& name) const;
    bool GetTrigger(const std::string& name) const { return IsTriggerSet(name); }

    const std::string& GetCurrentStateId() const { return currentStateId_; }
    const std::string& GetCurrentState() const { return currentStateId_; }
    std::string GetCurrentStateName() const;
    float GetNormalizedTime() const;
    float GetCurrentNormalizedTime() const { return GetNormalizedTime(); }
    std::size_t GetCurrentFrameIndex() const { return currentFrameIndex_; }

    void SetSpeed(float value);
    float GetSpeed() const { return speed_; }

    void SetAutoPlay(bool value) { autoPlay_ = value; }
    bool GetAutoPlay() const { return autoPlay_; }

    // Called by World::EvaluateAnimations after all component/script Update
    // callbacks and before LateUpdate.
    void Evaluate(float dt);

    void ResolveAssets() override;
    void OnEnable() override;
    void OnDisable() override;
    void OnDetach() override;
    void OnDestroy() override;
    void Serialize(nlohmann::json& json) const override;
    void Deserialize(const nlohmann::json& json) override;

private:
    bool EnsureControllerLoaded(std::string* errorOut = nullptr);
    void InitializeParameters();
    bool EnterState(const std::string& stateIdOrName, float normalizedTime);
    const molga::AnimatorState2D* CurrentState() const;
    const molga::AnimationClip2D* ClipForState(
        const molga::AnimatorState2D* state) const;
    bool ConditionMatches(const molga::AnimatorCondition2D& condition) const;
    const molga::AnimatorTransition2D* SelectTransition(float normalizedTime) const;
    void ConsumeSelectedTriggers(const molga::AnimatorTransition2D& transition);
    void ApplyCurrentFrame();
    void RestoreAuthoredSprite();

    std::string controllerGuid_;
    std::shared_ptr<const molga::AnimatorController2D> controller_;
    std::unordered_map<std::string, std::shared_ptr<const molga::AnimationClip2D>> clips_;
    std::unordered_map<std::string, molga::AnimatorParameterValue2D> parameters_;

    std::string currentStateId_;
    double stateTimeSeconds_ = 0.0;
    std::size_t currentFrameIndex_ = 0;
    float speed_ = 1.0f;
    bool autoPlay_ = true;
    PlaybackState playbackState_ = PlaybackState::Playing;
    bool controllerLoadAttempted_ = false;
    bool resumeAfterEnable_ = false;
};
