#include "Scripting/Script.h"
#include "Scripting/ScriptManager.h"
#include "ECS/Component.h"
#include "ECS/GameObject.h"
#include "ECS/Components/Animator2D.h"
#include "ECS/Components/ParticleSystem.h"
#include "Systems/Audio.h"

#include <cstdlib>
#include <fstream>
#include <memory>
#include <stdexcept>

namespace {

constexpr const char* kP1MusicIntroGuid =
    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
constexpr const char* kP1MusicLoopGuid =
    "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
constexpr const char* kP1JumpSfxGuid =
    "cccccccccccccccccccccccccccccccc";

bool gP1SequenceExecuted = false;

void AppendP2FaultReport(const char* key) {
    const char* reportPath = std::getenv("MOLGA_P2_SCRIPT_FAULT_REPORT");
    if (!reportPath || reportPath[0] == '\0') return;
    std::ofstream report(reportPath, std::ios::app);
    report << key << "=true\n";
}

template<typename T>
T* FindComponentBySerializedType(GameObject* object, const char* typeName) {
    if (!object) return nullptr;
    for (Component* component : object->GetComponents()) {
        if (component && component->GetTypeName() == typeName) {
            return static_cast<T*>(component);
        }
    }
    return nullptr;
}

} // namespace

class MyUserScript : public Script {
public:
    SCRIPT_CLASS(MyUserScript)

    void Start() override {
        if (gP1SequenceExecuted) {
            phase_ = 5;
            return;
        }
        intro_ = Audio::PlayMusic(kP1MusicIntroGuid, true, 0.0f);
        if (gameObject) {
            if (ParticleSystem* particles = FindComponentBySerializedType<ParticleSystem>(
                    gameObject, "ParticleSystem")) {
                particles->Emit(24);
                particleBurstEmitted_ = true;
            }
        }
        if (GameObject* player = Find("Player")) {
            animator_ = FindComponentBySerializedType<Animator2D>(
                player, "Animator2D");
        }
        phase_ = 1;
    }

    void Update(float) override {
        if (phase_ == 3) {
            const bool runObserved = animator_ &&
                animator_->GetCurrentStateId() == "run-state";
            AppendReport("animatorRunObserved", runObserved);
            phase_ = 4;
            return;
        }
        if (phase_ != 1 || gP1SequenceExecuted) return;

        const bool speedSet = animator_ && animator_->SetFloat("Speed", 1.0f);
        loop_ = Audio::CrossFadeMusic(kP1MusicLoopGuid, 0.25f, true);
        sfx_ = Audio::PlayOneShot(kP1JumpSfxGuid, AudioBus::SFX);
        gP1SequenceExecuted = true;
        phase_ = 2;

        // The process E2E supplies this path. Recording the public calls here
        // proves that the packaged user script reached the P1 audio surface;
        // no runtime-only probe or engine mutation is needed.
        const char* reportPath = std::getenv("MOLGA_P1_AUDIO_REPORT");
        if (!reportPath || reportPath[0] == '\0') return;
        std::ofstream report(reportPath, std::ios::trunc);
        report << "playMusicGuid=" << kP1MusicIntroGuid << '\n'
               << "crossFadeMusicGuid=" << kP1MusicLoopGuid << '\n'
               << "oneShotGuid=" << kP1JumpSfxGuid << '\n'
               << "crossFadeSeconds=0.25\n"
               << "oneShotBus=SFX\n"
               << "particleBurstCount=24\n"
               << "particleBurstEmitted="
               << (particleBurstEmitted_ ? "true" : "false") << '\n'
               << "animatorSpeedSet=" << (speedSet ? "true" : "false") << '\n'
               << "playMusicHandleValid=" << (intro_ ? "true" : "false") << '\n'
               << "crossFadeHandleValid=" << (loop_ ? "true" : "false") << '\n'
               << "oneShotHandleValid=" << (sfx_ ? "true" : "false") << '\n';
    }

    void LateUpdate(float) override {
        if (phase_ == 2) {
            const bool jumpTriggered = animator_ && animator_->SetTrigger("Jump");
            AppendReport("animatorJumpTriggered", jumpTriggered);
            phase_ = 3;
        } else if (phase_ == 4) {
            const bool jumpObserved = animator_ &&
                animator_->GetCurrentStateId() == "jump-state";
            AppendReport("animatorJumpObserved", jumpObserved);
            phase_ = 5;
        }
    }

private:
    static void AppendReport(const char* key, bool value) {
        const char* reportPath = std::getenv("MOLGA_P1_AUDIO_REPORT");
        if (!reportPath || reportPath[0] == '\0') return;
        std::ofstream report(reportPath, std::ios::app);
        report << key << '=' << (value ? "true" : "false") << '\n';
    }

    int phase_ = 0;
    VoiceHandle intro_{};
    VoiceHandle loop_{};
    VoiceHandle sfx_{};
    Animator2D* animator_ = nullptr;
    bool particleBurstEmitted_ = false;
};

// Opt-in packaged-runtime probe. It is inert for normal fixture users and only
// faults when the E2E supplies an explicit report path.
class FaultIsolationProbeScript : public Script {
public:
    SCRIPT_CLASS(FaultIsolationProbeScript)

    void Update(float) override {
        if (attempted_) return;
        const char* reportPath = std::getenv("MOLGA_P2_SCRIPT_FAULT_REPORT");
        if (!reportPath || reportPath[0] == '\0') return;
        attempted_ = true;
        AppendP2FaultReport("faultCallbackEntered");
        throw std::runtime_error("packaged Script isolation probe");
    }

    void OnDisable() override {
        AppendP2FaultReport("faultOnDisable");
    }

private:
    bool attempted_ = false;
};

class FaultIsolationPeerScript : public Script {
public:
    SCRIPT_CLASS(FaultIsolationPeerScript)

    void Update(float) override {
        if (reported_) return;
        const char* reportPath = std::getenv("MOLGA_P2_SCRIPT_FAULT_REPORT");
        if (!reportPath || reportPath[0] == '\0') return;
        reported_ = true;
        AppendP2FaultReport("peerContinued");
    }

private:
    bool reported_ = false;
};

#if defined(_WIN32)
#define EXPORT __declspec(dllexport)
#else
#define EXPORT __attribute__((visibility("default")))
#endif

extern "C" {
    EXPORT void RegisterScripts() {
        ScriptManager::Get().RegisterScript("MyUserScript", []() -> std::unique_ptr<Script> {
            return std::make_unique<MyUserScript>();
        });
        ScriptManager::Get().RegisterScript(
            "FaultIsolationProbeScript", []() -> std::unique_ptr<Script> {
                return std::make_unique<FaultIsolationProbeScript>();
            });
        ScriptManager::Get().RegisterScript(
            "FaultIsolationPeerScript", []() -> std::unique_ptr<Script> {
                return std::make_unique<FaultIsolationPeerScript>();
            });
    }
    EXPORT int GetScriptApiVersion() {
        return 1;
    }
}
