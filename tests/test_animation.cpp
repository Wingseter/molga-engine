#include "Core/AssetDatabase.h"
#include "Core/SceneSerializer.h"
#include "Core/World.h"
#include "ECS/BuiltinComponents.h"
#include "ECS/Components/Animator2D.h"
#include "ECS/Components/SpriteRenderer.h"
#include "ECS/Components/Transform.h"
#include "ECS/GameObject.h"
#include "Rendering/AnimationClip2D.h"
#include "Rendering/AnimatorController2D.h"
#include "Scripting/Script.h"
#include "doctest.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <unordered_map>
#include <vector>

namespace {

constexpr const char* TextureGuid = "11111111111111111111111111111111";
constexpr const char* SliceA = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
constexpr const char* SliceB = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
constexpr const char* SliceC = "cccccccccccccccccccccccccccccccc";
constexpr const char* IdleClipGuid = "22222222222222222222222222222222";
constexpr const char* RunClipGuid = "33333333333333333333333333333333";
constexpr const char* JumpClipGuid = "44444444444444444444444444444444";
constexpr const char* ControllerGuid = "55555555555555555555555555555555";

std::shared_ptr<molga::AnimationClip2D> MakeClip(
    bool loop = true,
    std::vector<molga::AnimationFrame2D> frames = {
        {SliceA, 0.1f}, {SliceB, 0.2f}}) {
    auto clip = std::make_shared<molga::AnimationClip2D>();
    clip->SetTextureGuid(TextureGuid);
    clip->SetLooping(loop);
    clip->SetFrames(std::move(frames));
    return clip;
}

std::shared_ptr<molga::AnimatorController2D> MakeSingleStateController(
    const std::string& clipGuid = IdleClipGuid) {
    auto controller = std::make_shared<molga::AnimatorController2D>();
    controller->SetStates({{"idle-state", "Idle", clipGuid, 1.0f}});
    controller->SetDefaultStateId("idle-state");
    return controller;
}

std::shared_ptr<molga::AnimatorController2D> MakeTransitionController() {
    using Type = molga::AnimatorParameterType2D;
    using Op = molga::AnimatorConditionOperator2D;
    auto controller = std::make_shared<molga::AnimatorController2D>();
    controller->SetParameters({
        {"goA", Type::Trigger, false},
        {"goB", Type::Trigger, false},
        {"ready", Type::Bool, false},
        {"count", Type::Int, 0},
        {"amount", Type::Float, 0.0f}
    });
    controller->SetStates({
        {"idle-state", "Idle", IdleClipGuid, 1.0f},
        {"run-state", "Run", RunClipGuid, 1.0f},
        {"jump-state", "Jump", JumpClipGuid, 1.0f}
    });
    controller->SetDefaultStateId("idle-state");
    controller->SetTransitions({
        // Both are eligible in the first evaluation. Array order is priority.
        {"idle-state", "run-state", false, 0.0f,
            {{"goA", Op::IsTrue, true}}},
        {"idle-state", "jump-state", false, 0.0f,
            {{"goB", Op::IsTrue, true}}},
        {"run-state", "jump-state", true, 0.5f,
            {{"goB", Op::IsTrue, true},
             {"ready", Op::Equals, true},
             {"count", Op::Greater, 1},
             {"amount", Op::GreaterOrEqual, 0.5f}}}
    });
    return controller;
}

void InstallClips(Animator2D& animator,
                  const std::shared_ptr<const molga::AnimationClip2D>& clip) {
    animator.SetClipAsset(IdleClipGuid, clip);
    animator.SetClipAsset(RunClipGuid, clip);
    animator.SetClipAsset(JumpClipGuid, clip);
}

class AnimationPhaseProbe final : public Script {
public:
    COMPONENT_TYPE(AnimationPhaseProbe)

    Animator2D* animator = nullptr;
    std::string stateDuringUpdate;
    std::string stateDuringLateUpdate;

    void Update(float) override {
        stateDuringUpdate = animator ? animator->GetCurrentStateId() : std::string{};
        if (animator) animator->SetTrigger("goA");
    }

    void LateUpdate(float) override {
        stateDuringLateUpdate = animator ? animator->GetCurrentStateId() : std::string{};
    }
};

} // namespace

TEST_CASE("AnimationClip2D JSON is variable-duration and transactional") {
    molga::AnimationClip2D clip;
    clip.SetTextureGuid(TextureGuid);
    clip.SetLooping(true);
    clip.SetFrames({{SliceA, 0.1f}, {SliceB, 0.2f}, {SliceC, 0.3f}});

    std::string error;
    REQUIRE(clip.Validate(&error));
    CHECK(clip.GetDurationSeconds() == doctest::Approx(0.6f));
    CHECK(clip.GetFrameIndexAt(0.0f) == 0);
    CHECK(clip.GetFrameIndexAt(0.1f) == 1);
    CHECK(clip.GetFrameIndexAt(0.31f) == 2);
    CHECK(clip.GetFrameIndexAt(0.61f) == 0);

    const nlohmann::json document = clip.ToJson();
    CHECK(document["schemaVersion"] == 1);
    CHECK(document["frames"][1]["durationSeconds"].get<float>() ==
          doctest::Approx(0.2f));

    molga::AnimationClip2D restored;
    REQUIRE(restored.FromJson(document, &error));
    CHECK(restored.GetTextureGuid() == TextureGuid);
    CHECK(restored.GetFrames().size() == 3);
    CHECK(restored.GetSpriteRef(2).sliceId == SliceC);

    nlohmann::json invalid = document;
    invalid["frames"][0]["durationSeconds"] = 0.0f;
    CHECK_FALSE(restored.FromJson(invalid, &error));
    CHECK_FALSE(error.empty());
    CHECK(restored.GetFrames().size() == 3); // failed load did not mutate it

    // Runtime helpers remain bounded even if an in-memory/generated clip has
    // invalid data that could not have passed the file loader.
    restored.GetFrames()[0].durationSeconds = 0.0f;
    CHECK(restored.GetFrameDuration(0) ==
          doctest::Approx(molga::AnimationClip2D::FallbackFrameDuration));
    CHECK(restored.GetFrameIndexAt(1.0e20f) < restored.GetFrames().size());
}

TEST_CASE("AnimatorController2D persists typed parameters and transition order") {
    const auto controller = MakeTransitionController();
    std::string error;
    REQUIRE(controller->Validate(&error));

    const nlohmann::json document = controller->ToJson();
    REQUIRE(document["parameters"].size() == 5);
    CHECK(document["parameters"][0]["type"] == "Trigger");
    REQUIRE(document["transitions"].size() == 3);
    CHECK(document["transitions"][0]["toStateId"] == "run-state");
    CHECK(document["transitions"][1]["toStateId"] == "jump-state");
    CHECK(document["transitions"][2]["hasExitTime"] == true);

    molga::AnimatorController2D restored;
    REQUIRE(restored.FromJson(document, &error));
    CHECK(restored.GetDefaultStateId() == "idle-state");
    REQUIRE(restored.FindParameter("count") != nullptr);
    CHECK(restored.FindParameter("count")->type ==
          molga::AnimatorParameterType2D::Int);
    CHECK(restored.GetTransitions()[0].toStateId == "run-state");
    CHECK(restored.GetTransitions()[2].conditions.size() == 4);

    nlohmann::json invalid = document;
    invalid["defaultStateId"] = "missing-state";
    CHECK_FALSE(restored.FromJson(invalid, &error));
    CHECK(restored.GetDefaultStateId() == "idle-state");
}

TEST_CASE("Animator2D advances large dt, pauses, and restores authored sprite") {
    auto object = std::make_shared<GameObject>("Animated");
    object->AddComponent<Transform>();
    auto* renderer = object->AddComponent<SpriteRenderer>();
    renderer->SetSpriteRef({TextureGuid, SliceC});
    auto* animator = object->AddComponent<Animator2D>();

    const auto controller = MakeSingleStateController();
    const auto clip = MakeClip();
    REQUIRE(animator->SetControllerAsset(controller));
    animator->SetClipAsset(IdleClipGuid, clip);
    REQUIRE(animator->Play());
    CHECK(renderer->GetEffectiveSpriteRef().sliceId == SliceA);

    animator->Evaluate(0.11f);
    CHECK(animator->GetCurrentFrameIndex() == 1);
    CHECK(renderer->GetEffectiveSpriteRef().sliceId == SliceB);

    const float beforeLargeStep = animator->GetNormalizedTime();
    animator->Evaluate(12345.678f);
    CHECK(std::isfinite(animator->GetNormalizedTime()));
    CHECK(animator->GetNormalizedTime() > beforeLargeStep);
    CHECK(animator->GetCurrentFrameIndex() < clip->GetFrames().size());

    animator->Pause();
    const float pausedTime = animator->GetNormalizedTime();
    animator->Evaluate(10.0f);
    CHECK(animator->GetNormalizedTime() == doctest::Approx(pausedTime));
    animator->Resume();
    animator->Evaluate(0.05f);
    CHECK(animator->GetNormalizedTime() > pausedTime);

    animator->Stop();
    CHECK(animator->IsStopped());
    CHECK_FALSE(renderer->HasRuntimeSpriteOverride());
    CHECK(renderer->GetEffectiveSpriteRef().sliceId == SliceC);

    REQUIRE(animator->Play());
    CHECK(renderer->HasRuntimeSpriteOverride());
    animator->SetEnabled(false);
    CHECK_FALSE(renderer->HasRuntimeSpriteOverride());
    CHECK(renderer->GetEffectiveSpriteRef().sliceId == SliceC);
}

TEST_CASE("non-loop clip clamps and missing renderer or invalid frame duration is safe") {
    auto object = std::make_shared<GameObject>("NoRenderer");
    auto* animator = object->AddComponent<Animator2D>();
    const auto controller = MakeSingleStateController();
    auto clip = MakeClip(false, {{SliceA, 0.0f}, {SliceB, 0.1f}});
    REQUIRE(animator->SetControllerAsset(controller));
    animator->SetClipAsset(IdleClipGuid, clip);
    REQUIRE(animator->Play());

    animator->Evaluate(1.0e20f);
    CHECK(animator->GetCurrentFrameIndex() == 1);
    CHECK(animator->GetNormalizedTime() == doctest::Approx(1.0f));
    animator->Evaluate(std::numeric_limits<float>::infinity());
    animator->Evaluate(-1.0f);
    CHECK(animator->GetNormalizedTime() == doctest::Approx(1.0f));
}

TEST_CASE("missing controller and clip fail safely without replacing authored sprite") {
    auto object = std::make_shared<GameObject>("MissingAssets");
    auto* renderer = object->AddComponent<SpriteRenderer>();
    renderer->SetSpriteRef({TextureGuid, SliceC});
    auto* animator = object->AddComponent<Animator2D>();

    CHECK_FALSE(animator->Play());
    animator->Evaluate(1000.0f);
    CHECK(renderer->GetEffectiveSpriteRef().sliceId == SliceC);

    REQUIRE(animator->SetControllerAsset(MakeSingleStateController()));
    REQUIRE(animator->Play()); // a valid state can play while its clip is unavailable
    animator->Evaluate(1000.0f);
    CHECK(animator->GetNormalizedTime() == doctest::Approx(0.0f));
    CHECK_FALSE(renderer->HasRuntimeSpriteOverride());
    CHECK(renderer->GetEffectiveSpriteRef().sliceId == SliceC);

    animator->ClearController();
    CHECK(animator->IsStopped());
    CHECK(renderer->GetEffectiveSpriteRef().sliceId == SliceC);
}

TEST_CASE("first eligible transition wins and only its triggers are consumed") {
    auto object = std::make_shared<GameObject>("FSM");
    object->AddComponent<Transform>();
    object->AddComponent<SpriteRenderer>()->SetSpriteRef({TextureGuid, SliceC});
    auto* animator = object->AddComponent<Animator2D>();
    const auto clip = MakeClip(true, {{SliceA, 1.0f}});
    REQUIRE(animator->SetControllerAsset(MakeTransitionController()));
    InstallClips(*animator, clip);

    REQUIRE(animator->SetTrigger("goA"));
    REQUIRE(animator->SetTrigger("goB"));
    REQUIRE(animator->SetBool("ready", true));
    REQUIRE(animator->SetInt("count", 2));
    REQUIRE(animator->SetFloat("amount", 0.5f));
    animator->Evaluate(0.0f);

    CHECK(animator->GetCurrentStateId() == "run-state");
    CHECK_FALSE(animator->IsTriggerSet("goA"));
    CHECK(animator->IsTriggerSet("goB")); // lower-priority transition did not consume it

    animator->Evaluate(0.49f);
    CHECK(animator->GetCurrentStateId() == "run-state");
    CHECK(animator->IsTriggerSet("goB")); // exit time blocked; still not consumed
    animator->Evaluate(0.02f);
    CHECK(animator->GetCurrentStateId() == "jump-state");
    CHECK_FALSE(animator->IsTriggerSet("goB"));
}

TEST_CASE("World evaluates Animator2D between Update and LateUpdate") {
    World world;
    auto object = std::make_shared<GameObject>("Phase");
    object->AddComponent<Transform>();
    object->AddComponent<SpriteRenderer>();
    auto* animator = object->AddComponent<Animator2D>();
    auto* probe = object->AddComponent<AnimationPhaseProbe>();
    probe->animator = animator;
    REQUIRE(animator->SetControllerAsset(MakeTransitionController()));
    InstallClips(*animator, MakeClip(true, {{SliceA, 1.0f}}));
    world.Add(object);

    world.Update(0.0f);
    CHECK(probe->stateDuringUpdate.empty());
    world.EvaluateAnimations(0.0f);
    CHECK(animator->GetCurrentStateId() == "run-state");
    world.LateUpdate(0.0f);
    CHECK(probe->stateDuringLateUpdate == "run-state");
}

TEST_CASE("Animator2D and authored SpriteRef round-trip through scene, prefab, and Play clone") {
    RegisterBuiltinComponents();
    World world;
    auto object = std::make_shared<GameObject>("RoundTrip");
    object->AddComponent<Transform>();
    auto* sprite = object->AddComponent<SpriteRenderer>();
    sprite->SetSpriteRef({TextureGuid, SliceB});
    sprite->SetCustomSize(19.0f, 23.0f);
    sprite->SetSizeMode(SpriteRenderer::SizeMode::Native);
    auto* animator = object->AddComponent<Animator2D>();
    animator->SetControllerGuid(ControllerGuid);
    animator->SetSpeed(1.75f);
    animator->SetAutoPlay(false);
    world.Add(object);

    const nlohmann::json scene =
        SceneSerializer::SerializeScene(world.Objects(), "Animation RoundTrip");
    std::vector<std::shared_ptr<GameObject>> restoredObjects;
    REQUIRE(SceneSerializer::DeserializeScene(scene, restoredObjects));
    REQUIRE(restoredObjects.size() == 1);
    auto* restoredAnimator = restoredObjects[0]->GetComponent<Animator2D>();
    auto* restoredSprite = restoredObjects[0]->GetComponent<SpriteRenderer>();
    REQUIRE(restoredAnimator != nullptr);
    REQUIRE(restoredSprite != nullptr);
    CHECK(restoredAnimator->GetControllerGuid() == ControllerGuid);
    CHECK(restoredAnimator->GetSpeed() == doctest::Approx(1.75f));
    CHECK_FALSE(restoredAnimator->GetAutoPlay());
    CHECK(restoredSprite->GetSpriteRef().sliceId == SliceB);
    CHECK(restoredSprite->GetSizeMode() == SpriteRenderer::SizeMode::Native);
    CHECK_FALSE(restoredSprite->HasRuntimeSpriteOverride());

    std::vector<std::shared_ptr<GameObject>> prefabObjects;
    std::unordered_map<unsigned int, unsigned int> idRemap;
    GameObject* prefabRoot = SceneSerializer::DeserializeSubtreeRemapped(
        SceneSerializer::SerializeSubtree(object.get()), prefabObjects, idRemap);
    REQUIRE(prefabRoot != nullptr);
    REQUIRE(prefabRoot->GetComponent<Animator2D>() != nullptr);
    CHECK(prefabRoot->GetComponent<Animator2D>()->GetControllerGuid() == ControllerGuid);
    CHECK(prefabRoot->GetComponent<SpriteRenderer>()->GetSpriteRef().sliceId == SliceB);

    std::unique_ptr<World> playClone = world.Clone();
    REQUIRE(playClone != nullptr);
    REQUIRE(playClone->Objects().size() == 1);
    REQUIRE(playClone->Objects()[0]->GetComponent<Animator2D>() != nullptr);
    CHECK(playClone->Objects()[0]->GetComponent<Animator2D>()->GetControllerGuid() ==
          ControllerGuid);
    CHECK(playClone->Objects()[0]->GetComponent<SpriteRenderer>()->GetSizeMode() ==
          SpriteRenderer::SizeMode::Native);
}

TEST_CASE("Animator2D resolves controller and clips by asset GUID") {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "molga-animation-assets";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);

    const auto clip = MakeClip(true, {{SliceA, 0.25f}});
    std::string error;
    REQUIRE(clip->SaveToFile(root / "idle.animclip", &error));
    molga::AssetDatabase::Get().ScanProject(root);
    const std::string clipGuid =
        molga::AssetDatabase::Get().GuidForSource("idle.animclip");
    REQUIRE(clipGuid.size() == 32);

    auto controller = MakeSingleStateController(clipGuid);
    REQUIRE(controller->SaveToFile(root / "hero.animator", &error));
    molga::AssetDatabase::Get().ScanProject(root);
    const std::string controllerGuid =
        molga::AssetDatabase::Get().GuidForSource("hero.animator");
    REQUIRE(controllerGuid.size() == 32);

    auto object = std::make_shared<GameObject>("AssetAnimator");
    object->AddComponent<SpriteRenderer>();
    auto* animator = object->AddComponent<Animator2D>();
    animator->SetControllerGuid(controllerGuid);
    animator->ResolveAssets();
    REQUIRE(animator->Play());
    CHECK(animator->GetCurrentStateId() == "idle-state");
    CHECK(object->GetComponent<SpriteRenderer>()->GetEffectiveSpriteRef().sliceId == SliceA);

    molga::AssetDatabase::Get().Clear();
    std::filesystem::remove_all(root);
}

TEST_CASE("legacy SpriteRenderer JSON remains Custom and top-left authored") {
    SpriteRenderer renderer;
    nlohmann::json legacy = {
        {"textureGuid", TextureGuid},
        {"size", {37.0f, 41.0f}},
        {"color", {1.0f, 1.0f, 1.0f, 1.0f}}
    };
    renderer.Deserialize(legacy);
    CHECK(renderer.GetSizeMode() == SpriteRenderer::SizeMode::Custom);
    CHECK(renderer.GetCustomSize().x == doctest::Approx(37.0f));
    CHECK(renderer.GetCustomSize().y == doctest::Approx(41.0f));

    nlohmann::json modern;
    renderer.Serialize(modern);
    CHECK(modern["sizeMode"] == "Custom");
    CHECK(modern["spriteRef"]["textureGuid"] == TextureGuid);
}
