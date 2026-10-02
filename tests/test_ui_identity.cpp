#include "doctest.h"
#include "Core/World.h"
#include "ECS/SceneObjectRef.h"
#include "ECS/Components/RectTransform.h"
#include "ECS/Components/UIButton.h"
#include "ECS/GameObject.h"
#include "Scripting/ScriptField.h"
#include "UI/UIRuntimeIdentity.h"
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <utility>

static_assert(std::is_same<ObjectRef, SceneObjectRef>::value,
              "ObjectRef must remain a source-compatible alias");

TEST_CASE("SceneObjectRef preserves targetId and Clear") {
    SceneObjectRef ref;
    ref.targetId = 41;
    CHECK(ref.IsSet());
    CHECK(ref.ObjectId() == 41);
    ref.Clear();
    CHECK(ref.targetId == 0);
}

TEST_CASE("component replacement never inherits a captured identity") {
    World world;
    auto object = std::make_shared<GameObject>("Button");
    auto* first = object->AddComponent<UIButton>();
    world.Add(object);
    const auto captured = molga::ui::CaptureTarget(world, *first);
    object->RemoveComponent<UIButton>();
    object->AddComponent<UIButton>();
    CHECK(molga::ui::ResolveTarget(world, captured) == nullptr);

    // 위 nullptr이 "이 식별자만 정확히 거절됐다"임을 보이는 성공 증인. 이 줄이
    // 없으면 오브젝트가 통째로 망가지거나 CaptureTarget이 애초에 빈 식별자를
    // 돌려줬어도 같은 결과가 나온다. 옛 포인터(first)는 이미 파괴됐으므로
    // 비교하지 않고, 교체된 컴포넌트의 식별자로 확인한다.
    UIButton* replacement = object->GetComponent<UIButton>();
    REQUIRE(replacement != nullptr);
    const auto replacementIdentity = molga::ui::CaptureTarget(world, *replacement);
    REQUIRE(static_cast<bool>(replacementIdentity));
    CHECK(replacementIdentity != captured);
    CHECK(replacementIdentity.objectId == captured.objectId);
    CHECK(replacementIdentity.componentRuntimeTypeId ==
          captured.componentRuntimeTypeId);
    CHECK(replacementIdentity.worldGeneration == captured.worldGeneration);
    CHECK(molga::ui::ResolveTarget(world, replacementIdentity) == replacement);
}

// ── Step 5b: Clear publishes the new generation BEFORE shutdown callbacks ────

namespace {

// OnDestroy는 죽어 가는 World를 관찰한다. Clear가 세대를 콜백 뒤에 발행하면
// 이 콜백은 이미 무효가 된 세대를 살아 있는 것으로 읽는다.
class GenerationObserverOnDestroy final : public Component {
public:
    COMPONENT_TYPE(GenerationObserverOnDestroy)

    int destroyCalls = 0;
    std::uint64_t observedGeneration = 0;

    void OnDestroy() override {
        ++destroyCalls;
        GameObject* owner = GetGameObject();
        World* world = owner ? owner->GetWorld() : nullptr;
        if (!world) return;
        observedGeneration = world->Generation();
    }
};

} // namespace

TEST_CASE("Clear publishes the new generation before shutdown callbacks run") {
    World world;
    auto object = std::make_shared<GameObject>("Observes Clear");
    auto* observer = object->AddComponent<GenerationObserverOnDestroy>();
    world.Add(object);

    const std::uint64_t beforeClear = world.Generation();
    REQUIRE(beforeClear != 0);

    world.Clear();

    // 콜백이 실제로 돌았다는 증인이 없으면 아래 두 줄은 "OnDestroy가 아예
    // 호출되지 않았다"와 구별되지 않는다.
    REQUIRE(observer->destroyCalls == 1);
    CHECK(observer->observedGeneration != 0);
    // 발행이 Shutdown() 뒤로 밀리면 이 값이 옛 세대와 같아진다.
    CHECK(observer->observedGeneration != beforeClear);
    CHECK(observer->observedGeneration == world.Generation());
    CHECK(world.Generation() > beforeClear);
}

TEST_CASE("world generations are process-global and replacement-safe") {
    World a;
    World b;
    CHECK(a.Generation() != 0);
    CHECK(b.Generation() > a.Generation());
    const auto beforeClear = a.Generation();
    a.Clear();
    CHECK(a.Generation() > b.Generation());
    CHECK(a.Generation() != beforeClear);
}

TEST_CASE("SceneObjectRef mutable and const Resolve link and agree") {
    World world;
    auto object = std::make_shared<GameObject>("Referenced");
    world.Add(object);
    SceneObjectRef ref{object->GetID()};
    CHECK(ref.Resolve(world) == object.get());
    const World& constWorld = world;
    CHECK(ref.Resolve(constWorld) == object.get());
    ref.targetId = object->GetID() + 1000;
    CHECK(ref.Resolve(world) == nullptr);
    CHECK(ref.Resolve(constWorld) == nullptr);

    // 0은 "참조 없음"이라는 센티널이다. id가 0인 오브젝트가 어쩌다 월드에
    // 들어와도 미설정 참조가 그것을 집어서는 안 된다.
    auto zeroIdObject = std::make_shared<GameObject>("Id zero");
    zeroIdObject->SetID(0);
    world.Add(zeroIdObject);
    REQUIRE(world.FindById(0) == zeroIdObject.get());
    SceneObjectRef unset;
    REQUIRE_FALSE(unset.IsSet());
    CHECK(unset.Resolve(world) == nullptr);
    CHECK(unset.Resolve(constWorld) == nullptr);

    // 같은 이유로 id 0짜리 오브젝트로는 런타임 식별자도 만들어지지 않는다.
    // 거절은 "빈 식별자"여야 한다 — 세대/타입/인스턴스만 채운 반쪽짜리 값은
    // operator bool로는 false지만 빈 식별자와 같지 않아 캐시 키를 오염시킨다.
    auto* zeroIdButton = zeroIdObject->AddComponent<UIButton>();
    const molga::ui::UIRuntimeTargetIdentity empty;
    CHECK(molga::ui::CaptureTarget(world, *zeroIdButton) == empty);

    // 해석기 쪽 0 검사도 실제로 도달 가능하다. 세대와 인스턴스 id가 모두
    // 진짜인 식별자를 손으로 만들어 objectId만 0으로 두면, objectId != 0 검사가
    // 없을 때 FindById(0)이 위 오브젝트를 집어 해석에 성공해 버린다.
    molga::ui::UIRuntimeTargetIdentity zeroObjectIdentity;
    zeroObjectIdentity.worldGeneration = world.Generation();
    zeroObjectIdentity.objectId = 0;
    zeroObjectIdentity.componentRuntimeTypeId = UIButton::StaticRuntimeTypeID();
    zeroObjectIdentity.componentInstanceId = zeroIdButton->GetInstanceID();
    REQUIRE(zeroObjectIdentity.worldGeneration != 0);
    REQUIRE(zeroObjectIdentity.componentInstanceId != 0);
    CHECK(molga::ui::ResolveTarget(world, zeroObjectIdentity) == nullptr);
    CHECK(molga::ui::ResolveTarget(constWorld, zeroObjectIdentity) == nullptr);

    // 성공 증인: 같은 컴포넌트라도 objectId가 0이 아닌 오브젝트에 달려 있으면
    // 똑같은 모양의 식별자가 해석된다 — 거절 이유가 "0"임을 못 박는다.
    auto realIdObject = std::make_shared<GameObject>("Id nonzero");
    auto* realIdButton = realIdObject->AddComponent<UIButton>();
    world.Add(realIdObject);
    REQUIRE(realIdObject->GetID() != 0);
    molga::ui::UIRuntimeTargetIdentity realIdentity = zeroObjectIdentity;
    realIdentity.objectId = realIdObject->GetID();
    realIdentity.componentInstanceId = realIdButton->GetInstanceID();
    CHECK(molga::ui::ResolveTarget(world, realIdentity) == realIdButton);
    CHECK(molga::ui::ResolveTarget(constWorld, realIdentity) == realIdButton);
}

TEST_CASE("component instance allocation never wraps or publishes zero") {
    GameObject object("Owner");
    const auto countBefore = object.GetComponents().size();
    ScopedComponentInstanceIdForTesting exhausted(UINT64_MAX);
    CHECK_THROWS_AS(object.AddComponent<UIButton>(), std::overflow_error);
    CHECK(object.GetComponents().size() == countBefore);
    CHECK(ComponentInstanceIdForTesting() == UINT64_MAX);
    CHECK(object.GetComponent<UIButton>() == nullptr);
}

namespace {

// `CHECK_THROWS_AS(UIButton(source), ...)` would parse as a declaration of
// `source`, not as a construction, so each row under test lives behind a call.
void CopyConstructButton(const UIButton& source) {
    UIButton copy(source);
    (void)copy;
}

void MoveConstructButton(UIButton& source) {
    UIButton moved(std::move(source));
    (void)moved;
}

// 복사 생성이 "게시 전에" 던지는지 보려면 실제 게시 경로가 필요하다. 위
// CopyConstructButton은 스택 위에만 만들므로 어느 GameObject의 목록도 건드리지
// 않아, 개수 검사가 깨질 수 없는 vacuous 단언이 된다.
void PublishCopyInto(GameObject& destination, const UIButton& source) {
    destination.AddComponentRaw(new UIButton(source));
}

} // namespace

TEST_CASE("component copy construction never wraps or publishes zero") {
    GameObject owner("Copy source owner");
    UIButton* source = owner.AddComponent<UIButton>();
    REQUIRE(source != nullptr);
    const std::uint64_t sourceInstanceId = source->GetInstanceID();
    REQUIRE(sourceInstanceId != 0);
    const auto countBefore = owner.GetComponents().size();
    GameObject destination("Copy destination owner");
    REQUIRE(destination.GetComponents().empty());

    // 훅을 세우기 직전의 다음 발급 후보. 이 두 줄이 없으면
    // ComponentInstanceIdForTesting()을 UINT64_MAX 상수로 바꿔치기해도 아래
    // 단언들이 그대로 통과한다 — 접근자에 성공 증인이 없어지는 것이다.
    const std::uint64_t nextBefore = ComponentInstanceIdForTesting();
    CHECK(nextBefore != 0);
    CHECK(nextBefore != UINT64_MAX);

    {
        ScopedComponentInstanceIdForTesting exhausted(UINT64_MAX);
        CHECK_THROWS_AS(CopyConstructButton(*source), std::overflow_error);
        // 게시 경로로도 던진다: 복사 생성이 AddComponentRaw에 닿기 전에 실패해야
        // 목적지 GameObject가 반쪽짜리 컴포넌트를 갖지 않는다.
        CHECK_THROWS_AS(PublishCopyInto(destination, *source),
                        std::overflow_error);
        CHECK(destination.GetComponents().empty());
        CHECK(destination.GetComponent<UIButton>() == nullptr);
        CHECK(ComponentInstanceIdForTesting() == UINT64_MAX);
    }

    CHECK(ComponentInstanceIdForTesting() == nextBefore);
    CHECK(owner.GetComponents().size() == countBefore);
    CHECK(source->GetInstanceID() == sourceInstanceId);

    // Success witness: the same construction outside exhaustion publishes a
    // fresh nonzero identity instead of reusing the source's.
    UIButton copy(*source);
    CHECK(copy.GetInstanceID() != 0);
    CHECK(copy.GetInstanceID() != sourceInstanceId);
    CHECK(source->GetInstanceID() == sourceInstanceId);

    // 게시 경로의 성공 증인. 위 CHECK(destination…empty())가 "그 경로가 아무것도
    // 게시하지 못한다"가 아니라 "소진 때만 못 게시한다"임을 보인다.
    PublishCopyInto(destination, *source);
    UIButton* published = destination.GetComponent<UIButton>();
    REQUIRE(published != nullptr);
    CHECK(published->GetInstanceID() != 0);
    CHECK(published->GetInstanceID() != sourceInstanceId);
    CHECK(published->GetInstanceID() != copy.GetInstanceID());

    // Step 7d: 대입은 목적지의 인스턴스 id를 그대로 둔다. 이 두 행이 없으면
    // 대입 연산자에 instanceId_ = other.instanceId_ 한 줄을 끼워 넣어도
    // 아무 테스트도 깨지지 않고, 살아 있는 두 컴포넌트가 같은 id를 갖게 된다.
    const std::uint64_t publishedInstanceId = published->GetInstanceID();
    *published = *source;
    CHECK(published->GetInstanceID() == publishedInstanceId);
    CHECK(published->GetInstanceID() != source->GetInstanceID());
    CHECK(source->GetInstanceID() == sourceInstanceId);

    UIButton movable;
    const std::uint64_t movableInstanceId = movable.GetInstanceID();
    REQUIRE(movableInstanceId != 0);
    REQUIRE(movableInstanceId != publishedInstanceId);
    *published = std::move(movable);
    CHECK(published->GetInstanceID() == publishedInstanceId);
    CHECK(published->GetInstanceID() != movableInstanceId);

    // 소진 검사의 나머지 절반. 0은 "인스턴스 없음" 센티널이므로 후보가 0이면
    // 발급이 아니라 예외여야 한다. 이 행이 없으면 `candidate == 0 ||` 를 지워도
    // 프로덕션에서는 원자값이 0에 닿을 수 없어 아무 테스트도 깨지지 않는다.
    GameObject zeroSeeded("Zero seeded owner");
    const std::uint64_t beforeZeroHook = ComponentInstanceIdForTesting();
    {
        ScopedComponentInstanceIdForTesting zeroed(0);
        CHECK(ComponentInstanceIdForTesting() == 0);
        CHECK_THROWS_AS(zeroSeeded.AddComponent<UIButton>(),
                        std::overflow_error);
        CHECK(ComponentInstanceIdForTesting() == 0);
    }
    CHECK(zeroSeeded.GetComponents().empty());
    CHECK(ComponentInstanceIdForTesting() == beforeZeroHook);
}

TEST_CASE("the identity test hooks never rewind past a value they issued") {
    // 소진 경계값을 세운 훅은 아무것도 발급하지 못하므로 직전 후보를 그대로
    // 돌려놓는 것이 옳다. 하지만 훅이 무언가를 발급했다면 그 값은 살아 있는
    // 식별자다 — 거기서 시퀀스를 뒤로 되감으면 프로덕션이 같은 값을 한 번 더
    // 발급해, 이 태스크가 세운 "재사용 없음"이 테스트 훅 하나로 깨진다.
    constexpr std::uint64_t kFarAhead = 1ull << 40;

    const std::uint64_t worldNextBefore = WorldGenerationForTesting();
    REQUIRE(worldNextBefore != 0);
    REQUIRE(worldNextBefore < kFarAhead);
    std::uint64_t issuedGeneration = 0;
    {
        ScopedWorldGenerationForTesting ahead(kFarAhead);
        World issued;
        issuedGeneration = issued.Generation();
        CHECK(issuedGeneration == kFarAhead);
    }
    CHECK(WorldGenerationForTesting() > issuedGeneration);
    World afterWorldHook;
    CHECK(afterWorldHook.Generation() > issuedGeneration);

    const std::uint64_t componentNextBefore = ComponentInstanceIdForTesting();
    REQUIRE(componentNextBefore != 0);
    REQUIRE(componentNextBefore < kFarAhead);
    std::uint64_t issuedInstanceId = 0;
    {
        ScopedComponentInstanceIdForTesting ahead(kFarAhead);
        UIButton issued;
        issuedInstanceId = issued.GetInstanceID();
        CHECK(issuedInstanceId == kFarAhead);
    }
    CHECK(ComponentInstanceIdForTesting() > issuedInstanceId);
    UIButton afterComponentHook;
    CHECK(afterComponentHook.GetInstanceID() > issuedInstanceId);
}

TEST_CASE("component move construction never wraps or publishes zero") {
    GameObject owner("Move source owner");
    UIButton* source = owner.AddComponent<UIButton>();
    REQUIRE(source != nullptr);
    const std::uint64_t sourceInstanceId = source->GetInstanceID();
    REQUIRE(sourceInstanceId != 0);
    const auto countBefore = owner.GetComponents().size();

    {
        ScopedComponentInstanceIdForTesting exhausted(UINT64_MAX);
        CHECK_THROWS_AS(MoveConstructButton(*source), std::overflow_error);
        CHECK(ComponentInstanceIdForTesting() == UINT64_MAX);
    }

    // A failed move must not have stolen the owner pointer: the source is the
    // component the GameObject still publishes, under its original identity.
    CHECK(owner.GetComponents().size() == countBefore);
    CHECK(source->GetInstanceID() == sourceInstanceId);
    CHECK(source->GetGameObject() == &owner);
    CHECK(owner.GetComponent<UIButton>() == source);

    UIButton standalone;
    const std::uint64_t standaloneInstanceId = standalone.GetInstanceID();
    CHECK(standaloneInstanceId != 0);
    UIButton moved(std::move(standalone));
    CHECK(moved.GetInstanceID() != 0);
    CHECK(moved.GetInstanceID() != standaloneInstanceId);

    // 성공한 이동은 원본에서 소유자를 실제로 떼어 낸다. 위 실패 행의
    // `source->GetGameObject() == &owner`는 "소유자를 아예 훔치지 않는" 이동에도
    // 만족되므로, 그 문장이 지워졌는지는 여기서만 드러난다. 떼지 않으면 옮겨진
    // 원본이 자기를 더 이상 소유하지 않는 GameObject를 계속 가리킨다.
    GameObject detachOwner("Move detach owner");
    UIButton* attached = detachOwner.AddComponent<UIButton>();
    REQUIRE(attached != nullptr);
    REQUIRE(attached->GetGameObject() == &detachOwner);
    const std::uint64_t attachedInstanceId = attached->GetInstanceID();
    {
        UIButton stolen(std::move(*attached));
        CHECK(attached->GetGameObject() == nullptr);
        CHECK(stolen.GetGameObject() == &detachOwner);
        CHECK(stolen.GetInstanceID() != 0);
        CHECK(stolen.GetInstanceID() != attachedInstanceId);
        CHECK(attached->GetInstanceID() == attachedInstanceId);
    }
    // 소유권은 여전히 GameObject에 있다: 훔쳐 간 쪽은 값일 뿐이다.
    CHECK(detachOwner.GetComponent<UIButton>() == attached);
}

// ── Step 6b/6c/6d: two-sided coverage for the runtime target helpers ─────────

TEST_CASE("CaptureTarget refuses a component that does not belong to the world") {
    World world;
    // 거절된 캡처는 반쪽짜리 값이 아니라 빈 식별자를 돌려주어야 한다.
    const molga::ui::UIRuntimeTargetIdentity empty;

    UIButton detached;
    CHECK(molga::ui::CaptureTarget(world, detached) == empty);
    CHECK_FALSE(static_cast<bool>(molga::ui::CaptureTarget(world, detached)));

    auto orphan = std::make_shared<GameObject>("Never added");
    auto* orphanButton = orphan->AddComponent<UIButton>();
    CHECK(molga::ui::CaptureTarget(world, *orphanButton) == empty);

    // 소유 포인터만 이 월드를 가리키고 실제로는 편입되지 않은 오브젝트.
    // 여기서 캡처를 허용하면 ResolveTarget이 쓰는 FindById로는 절대 해석되지
    // 않는 식별자, 즉 처음부터 죽어 있는 식별자가 만들어진다.
    auto claimant = std::make_shared<GameObject>("Claims the world");
    auto* claimantButton = claimant->AddComponent<UIButton>();
    claimant->SetWorld(&world);
    REQUIRE(world.FindById(claimant->GetID()) == nullptr);
    CHECK(molga::ui::CaptureTarget(world, *claimantButton) == empty);
    claimant->SetWorld(nullptr);

    World other;
    auto object = std::make_shared<GameObject>("Elsewhere");
    auto* button = object->AddComponent<UIButton>();
    other.Add(object);
    CHECK(molga::ui::CaptureTarget(world, *button) == empty);

    // 위 행들이 전부 UIButton이라는 것이 함정이다. ComponentTypeID는 처음
    // 등록된 타입에 0을 준다. 거절 경로가 빈 식별자 대신 componentRuntimeTypeId
    // 만 채운 반쪽짜리 값을 돌려줘도, 그 타입 id가 0이면 빈 식별자와 값이 같아
    // == empty가 통과한다. 두 타입의 id는 서로 다르므로 적어도 하나는 0이
    // 아니다 — 같은 거절을 두 타입 모두로 관찰해 그 사각을 없앤다.
    REQUIRE(UIButton::StaticRuntimeTypeID() !=
            RectTransform::StaticRuntimeTypeID());

    RectTransform detachedRect;
    CHECK(molga::ui::CaptureTarget(world, detachedRect) == empty);

    auto rectOrphan = std::make_shared<GameObject>("Rect never added");
    auto* orphanRect = rectOrphan->AddComponent<RectTransform>();
    CHECK(molga::ui::CaptureTarget(world, *orphanRect) == empty);

    auto rectClaimant = std::make_shared<GameObject>("Rect claims the world");
    auto* claimantRect = rectClaimant->AddComponent<RectTransform>();
    rectClaimant->SetWorld(&world);
    REQUIRE(world.FindById(rectClaimant->GetID()) == nullptr);
    CHECK(molga::ui::CaptureTarget(world, *claimantRect) == empty);
    rectClaimant->SetWorld(nullptr);

    auto* elsewhereRect = object->AddComponent<RectTransform>();
    CHECK(molga::ui::CaptureTarget(world, *elsewhereRect) == empty);
    // 성공 증인: 같은 RectTransform도 소유 월드로는 타입 id가 채워진 채 잡힌다.
    const auto capturedRect = molga::ui::CaptureTarget(other, *elsewhereRect);
    REQUIRE(static_cast<bool>(capturedRect));
    CHECK(capturedRect.componentRuntimeTypeId ==
          RectTransform::StaticRuntimeTypeID());
    CHECK(molga::ui::ResolveTarget(other, capturedRect) == elsewhereRect);

    // 성공 증인: 같은 컴포넌트를 실제 소유 월드로 캡처하면 네 필드가 다 찬다.
    const auto captured = molga::ui::CaptureTarget(other, *button);
    REQUIRE(static_cast<bool>(captured));
    CHECK(captured.worldGeneration == other.Generation());
    CHECK(captured.objectId == object->GetID());
    CHECK(captured.componentRuntimeTypeId == UIButton::StaticRuntimeTypeID());
    CHECK(captured.componentInstanceId == button->GetInstanceID());
    CHECK(molga::ui::ResolveTarget(other, captured) == button);
}

TEST_CASE("ResolveTarget rejects a mismatch in any single identity field") {
    World world;
    auto first = std::make_shared<GameObject>("First");
    auto* firstButton = first->AddComponent<UIButton>();
    auto* firstRect = first->AddComponent<RectTransform>();
    world.Add(first);
    auto second = std::make_shared<GameObject>("Second");
    auto* secondButton = second->AddComponent<UIButton>();
    world.Add(second);

    const auto identity = molga::ui::CaptureTarget(world, *firstButton);
    REQUIRE(static_cast<bool>(identity));
    REQUIRE(molga::ui::ResolveTarget(world, identity) == firstButton);

    auto wrongGeneration = identity;
    wrongGeneration.worldGeneration += 1;
    auto wrongObject = identity;
    wrongObject.objectId = second->GetID();
    // 같은 오브젝트에 실제로 붙어 있는 다른 타입. 해석기가 타입 id를 무시하고
    // 인스턴스 id만 훑는다면 이 행이 firstButton을 돌려주며 통과해 버린다.
    auto wrongType = identity;
    wrongType.componentRuntimeTypeId = RectTransform::StaticRuntimeTypeID();
    auto wrongInstance = identity;
    wrongInstance.componentInstanceId = secondButton->GetInstanceID();
    const molga::ui::UIRuntimeTargetIdentity empty;

    CHECK(molga::ui::ResolveTarget(world, wrongGeneration) == nullptr);
    CHECK(molga::ui::ResolveTarget(world, wrongObject) == nullptr);
    CHECK(molga::ui::ResolveTarget(world, wrongType) == nullptr);
    CHECK(molga::ui::ResolveTarget(world, wrongInstance) == nullptr);
    CHECK(molga::ui::ResolveTarget(world, empty) == nullptr);

    // 타입 id가 진짜로 대상을 고른다: 같은 오브젝트의 RectTransform 식별자는
    // RectTransform을 돌려준다.
    const auto rectIdentity = molga::ui::CaptureTarget(world, *firstRect);
    REQUIRE(static_cast<bool>(rectIdentity));
    CHECK(rectIdentity.componentRuntimeTypeId != identity.componentRuntimeTypeId);
    CHECK(molga::ui::ResolveTarget(world, rectIdentity) == firstRect);

    // Step 6d: const 오버로드는 모든 성공/실패 행에서 mutable과 같은 판정이다.
    const World& constWorld = world;
    CHECK(molga::ui::ResolveTarget(constWorld, identity) == firstButton);
    CHECK(molga::ui::ResolveTarget(constWorld, rectIdentity) == firstRect);
    CHECK(molga::ui::ResolveTarget(constWorld, wrongGeneration) == nullptr);
    CHECK(molga::ui::ResolveTarget(constWorld, wrongObject) == nullptr);
    CHECK(molga::ui::ResolveTarget(constWorld, wrongType) == nullptr);
    CHECK(molga::ui::ResolveTarget(constWorld, wrongInstance) == nullptr);
    CHECK(molga::ui::ResolveTarget(constWorld, empty) == nullptr);
}

// ── Step 7a: move-based replacement and successful load invalidate ───────────

TEST_CASE("world replacement and successful load invalidate old identities") {
    namespace fs = std::filesystem;

    World world;
    auto original = std::make_shared<GameObject>("Original button");
    auto* originalButton = original->AddComponent<UIButton>();
    world.Add(original);
    const auto originalIdentity = molga::ui::CaptureTarget(world, *originalButton);
    REQUIRE(static_cast<bool>(originalIdentity));
    REQUIRE(molga::ui::ResolveTarget(world, originalIdentity) == originalButton);

    World replacement;
    auto incoming = std::make_shared<GameObject>("Incoming button");
    auto* incomingButton = incoming->AddComponent<UIButton>();
    incoming->AddComponent<RectTransform>();
    replacement.Add(incoming);
    const auto incomingIdentity =
        molga::ui::CaptureTarget(replacement, *incomingButton);
    REQUIRE(static_cast<bool>(incomingIdentity));

    const std::uint64_t beforeMove = world.Generation();
    world = std::move(replacement);

    // 내용은 그대로 옮겨 왔는데도 양쪽 식별자가 모두 죽는다: 교체는 세대를
    // 새로 발행하고, 옮겨진 쪽에도 별도의 세대를 발행한다.
    CHECK(world.FindById(incoming->GetID()) == incoming.get());
    CHECK(world.Generation() != beforeMove);
    CHECK(molga::ui::ResolveTarget(world, originalIdentity) == nullptr);
    CHECK(molga::ui::ResolveTarget(world, incomingIdentity) == nullptr);
    CHECK(molga::ui::ResolveTarget(replacement, incomingIdentity) == nullptr);
    CHECK(replacement.Generation() != 0);
    CHECK(replacement.Generation() != incomingIdentity.worldGeneration);
    // 두 World가 같은 세대를 발행하면 한쪽에서 잡은 식별자가 다른 쪽에서도
    // 검증에 성공한다 — 이 태스크가 막으려는 바로 그 혼동이다.
    CHECK(replacement.Generation() != world.Generation());

    // 무효화가 "언제나 nullptr"이 아님을 보이는 성공 증인.
    const auto afterMove = molga::ui::CaptureTarget(world, *incomingButton);
    REQUIRE(static_cast<bool>(afterMove));
    CHECK(afterMove != incomingIdentity);
    CHECK(afterMove.objectId == incomingIdentity.objectId);
    CHECK(afterMove.componentInstanceId == incomingIdentity.componentInstanceId);
    CHECK(molga::ui::ResolveTarget(world, afterMove) == incomingButton);

    // Step 7b를 이동 쌍에 대해서도: 비워진 원본에 같은 오브젝트 id와 같은
    // 컴포넌트 타입을 다시 심어도, 목적지에서 잡은 식별자는 그쪽에서 해석되지
    // 않는다. 두 세대가 서로 다르다는 것이 값 하나가 아니라 동작으로 드러난다.
    auto reusedInMovedFrom = std::make_shared<GameObject>("Reused id");
    reusedInMovedFrom->SetID(incoming->GetID());
    auto* reusedButton = reusedInMovedFrom->AddComponent<UIButton>();
    replacement.Add(reusedInMovedFrom);
    REQUIRE(replacement.FindById(incoming->GetID()) == reusedInMovedFrom.get());
    CHECK(molga::ui::ResolveTarget(replacement, afterMove) == nullptr);
    CHECK(molga::ui::ResolveTarget(replacement, incomingIdentity) == nullptr);
    const auto reusedIdentity =
        molga::ui::CaptureTarget(replacement, *reusedButton);
    REQUIRE(static_cast<bool>(reusedIdentity));
    CHECK(reusedIdentity.objectId == afterMove.objectId);
    CHECK(reusedIdentity.worldGeneration != afterMove.worldGeneration);
    CHECK(molga::ui::ResolveTarget(replacement, reusedIdentity) == reusedButton);
    CHECK(molga::ui::ResolveTarget(world, reusedIdentity) == nullptr);

    const fs::path scenePath =
        fs::temp_directory_path() / "molga_test_ui_identity_scene.json";
    REQUIRE(world.SaveToFile(scenePath.string()));

    // 실패한 로드는 내용도 세대도 건드리지 않는다. 없는 파일은 열기 단계에서
    // 끝나므로 약한 행이고, 진짜 위험한 것은 열리기는 하지만 씬이 아닌
    // 파일이다: DeserializeScene은 실패를 알리기 전에 목적지 벡터를 먼저 비운다.
    const std::uint64_t beforeFailedLoad = world.Generation();
    CHECK_FALSE(world.LoadFromFile(
        (fs::temp_directory_path() / "molga_test_ui_identity_missing.json").string()));
    CHECK(world.Generation() == beforeFailedLoad);
    CHECK(molga::ui::ResolveTarget(world, afterMove) == incomingButton);

    const fs::path notAScenePath =
        fs::temp_directory_path() / "molga_test_ui_identity_not_a_scene.json";
    {
        std::ofstream notAScene(notAScenePath);
        REQUIRE(notAScene.is_open());
        notAScene << "{\"notAScene\": true}";
    }
    CHECK_FALSE(world.LoadFromFile(notAScenePath.string()));
    CHECK(world.Generation() == beforeFailedLoad);
    CHECK(world.FindById(incoming->GetID()) == incoming.get());
    CHECK(molga::ui::ResolveTarget(world, afterMove) == incomingButton);

    // 성공한 로드는 새 세대를 발행하므로 이전 식별자가 전부 죽는다.
    REQUIRE(world.LoadFromFile(scenePath.string()));
    CHECK(world.Generation() != beforeFailedLoad);
    CHECK(molga::ui::ResolveTarget(world, afterMove) == nullptr);
    CHECK(molga::ui::ResolveTarget(world, originalIdentity) == nullptr);
    CHECK(molga::ui::ResolveTarget(world, incomingIdentity) == nullptr);

    // 로드된 씬은 같은 오브젝트 id를 그대로 되살린다 — 죽은 이유가 "대상이
    // 사라져서"가 아니라 "세대가 달라서"임을 못 박는다.
    GameObject* reloaded = world.FindById(afterMove.objectId);
    REQUIRE(reloaded != nullptr);
    // 로드된 오브젝트는 이 World에 다시 묶여야 한다. LoadFromFile이 임시 벡터로
    // 옮겨졌으므로 SetWorld 재바인딩은 새로 배치된 코드다 — 이 줄이 없으면 그
    // 루프를 통째로 지워도 CaptureTarget이 포인터로만 소속을 보기 때문에 아무
    // 테스트도 깨지지 않는다.
    CHECK(reloaded->GetWorld() == &world);
    UIButton* reloadedButton = reloaded->GetComponent<UIButton>();
    REQUIRE(reloadedButton != nullptr);
    const auto reloadedIdentity = molga::ui::CaptureTarget(world, *reloadedButton);
    REQUIRE(static_cast<bool>(reloadedIdentity));
    CHECK(reloadedIdentity.objectId == afterMove.objectId);
    CHECK(molga::ui::ResolveTarget(world, reloadedIdentity) == reloadedButton);

    std::error_code removeError;
    fs::remove(scenePath, removeError);
    fs::remove(notAScenePath, removeError);
}

TEST_CASE("move construction publishes fresh generations on both worlds") {
    World source;
    auto object = std::make_shared<GameObject>("Move constructed");
    auto* button = object->AddComponent<UIButton>();
    source.Add(object);
    const auto identity = molga::ui::CaptureTarget(source, *button);
    REQUIRE(static_cast<bool>(identity));
    REQUIRE(molga::ui::ResolveTarget(source, identity) == button);
    const std::uint64_t sourceGenerationBefore = source.Generation();

    World moved(std::move(source));

    // 내용은 넘어왔다.
    CHECK(moved.FindById(object->GetID()) == object.get());
    CHECK(object->GetWorld() == &moved);
    // 목적지는 이전 세대를 물려받지 않는다. 이 줄이 없으면 이동 생성이 원본의
    // 세대를 그대로 복원해도 아무 게이트가 깨지지 않고, 옛 식별자가 조용히
    // 살아남는다.
    CHECK(moved.Generation() != 0);
    CHECK(moved.Generation() != sourceGenerationBefore);
    CHECK(source.Generation() != 0);
    CHECK(source.Generation() != sourceGenerationBefore);
    CHECK(moved.Generation() != source.Generation());

    CHECK(molga::ui::ResolveTarget(moved, identity) == nullptr);
    CHECK(molga::ui::ResolveTarget(source, identity) == nullptr);

    // 성공 증인: 새 세대로 다시 잡으면 같은 컴포넌트가 해석된다.
    const auto afterMove = molga::ui::CaptureTarget(moved, *button);
    REQUIRE(static_cast<bool>(afterMove));
    CHECK(afterMove.worldGeneration == moved.Generation());
    CHECK(afterMove.objectId == identity.objectId);
    CHECK(afterMove.componentInstanceId == identity.componentInstanceId);
    CHECK(molga::ui::ResolveTarget(moved, afterMove) == button);
}

// ── Step 7b: cross-world object-ID reuse rejection ───────────────────────────

TEST_CASE("a reused object ID in another world never resolves an old identity") {
    constexpr unsigned int kSharedObjectId = 424242;

    World first;
    auto firstObject = std::make_shared<GameObject>("Shared id");
    firstObject->SetID(kSharedObjectId);
    auto* firstButton = firstObject->AddComponent<UIButton>();
    first.Add(firstObject);
    const auto identity = molga::ui::CaptureTarget(first, *firstButton);
    REQUIRE(static_cast<bool>(identity));
    REQUIRE(molga::ui::ResolveTarget(first, identity) == firstButton);

    World second;
    auto secondObject = std::make_shared<GameObject>("Shared id");
    secondObject->SetID(kSharedObjectId);
    auto* secondButton = secondObject->AddComponent<UIButton>();
    second.Add(secondObject);

    // 오브젝트 id와 컴포넌트 타입이 완전히 같아도 세대가 다르면 남이다.
    REQUIRE(second.FindById(kSharedObjectId) == secondObject.get());
    CHECK(molga::ui::ResolveTarget(second, identity) == nullptr);
    // 원래 월드에서는 여전히 살아 있다.
    CHECK(molga::ui::ResolveTarget(first, identity) == firstButton);

    const auto secondIdentity = molga::ui::CaptureTarget(second, *secondButton);
    REQUIRE(static_cast<bool>(secondIdentity));
    CHECK(secondIdentity.objectId == identity.objectId);
    CHECK(secondIdentity.componentRuntimeTypeId == identity.componentRuntimeTypeId);
    CHECK(secondIdentity.worldGeneration != identity.worldGeneration);
    CHECK(secondIdentity != identity);
    CHECK(molga::ui::ResolveTarget(second, secondIdentity) == secondButton);
    CHECK(molga::ui::ResolveTarget(first, secondIdentity) == nullptr);
}

// ── Step 7c: non-wrapping world generation exhaustion ────────────────────────

namespace {

// 함수형 캐스트를 doctest 매크로 인자에 그대로 두면 선언으로 파싱된다.
void ConstructWorld() {
    World world;
    (void)world;
}

void MoveAssignWorld(World& destination, World& source) {
    destination = std::move(source);
}

void MoveConstructWorld(World& source) {
    World moved(std::move(source));
    (void)moved;
}

} // namespace

TEST_CASE("self move assignment changes neither content nor generation") {
    World world;
    auto object = std::make_shared<GameObject>("Self moved");
    auto* button = object->AddComponent<UIButton>();
    world.Add(object);
    const auto identity = molga::ui::CaptureTarget(world, *button);
    REQUIRE(static_cast<bool>(identity));
    const std::uint64_t before = world.Generation();

    // 자기 대입 가드가 사라지면 Shutdown()이 돌아 내용이 조용히 사라지고,
    // 아무것도 하지 않은 연산이 세대를 두 개 태운다. 별칭 대신 두 참조를 받는
    // 헬퍼를 쓰는 것은 -Wself-move를 피하기 위해서다.
    MoveAssignWorld(world, world);

    CHECK(world.Generation() == before);
    CHECK(world.FindById(object->GetID()) == object.get());
    CHECK(object->GetWorld() == &world);
    CHECK(molga::ui::ResolveTarget(world, identity) == button);
}

TEST_CASE("world generation allocation never wraps or publishes zero") {
    World survivor;
    auto survivorObject = std::make_shared<GameObject>("Survivor");
    auto* survivorButton = survivorObject->AddComponent<UIButton>();
    survivor.Add(survivorObject);
    const auto survivorIdentity =
        molga::ui::CaptureTarget(survivor, *survivorButton);
    REQUIRE(static_cast<bool>(survivorIdentity));
    const std::uint64_t survivorGeneration = survivor.Generation();

    World donor;
    const std::uint64_t donorGeneration = donor.Generation();

    // 훅을 세우기 직전의 다음 발급 후보. 소진 구간에서 아무것도 발급되지
    // 않았음을 스코프 종료 후에 이 값으로 확인한다.
    const std::uint64_t nextBefore = WorldGenerationForTesting();
    CHECK(nextBefore != 0);
    CHECK(nextBefore != UINT64_MAX);

    {
        ScopedWorldGenerationForTesting exhausted(UINT64_MAX);
        CHECK(WorldGenerationForTesting() == UINT64_MAX);

        CHECK_THROWS_AS(ConstructWorld(), std::overflow_error);
        CHECK_THROWS_AS(survivor.Clear(), std::overflow_error);
        CHECK_THROWS_AS(MoveAssignWorld(donor, survivor), std::overflow_error);
        CHECK_THROWS_AS(MoveConstructWorld(survivor), std::overflow_error);

        // 감기지 않는다: 원자값은 UINT64_MAX 그대로고 0은 관찰되지 않는다.
        CHECK(WorldGenerationForTesting() == UINT64_MAX);
        // 실패한 연산은 세대도 내용도 바꾸지 않았다.
        CHECK(survivor.Generation() == survivorGeneration);
        CHECK(donor.Generation() == donorGeneration);
        CHECK(survivor.Generation() != 0);
        CHECK(donor.Generation() != 0);
        CHECK(survivor.FindById(survivorObject->GetID()) == survivorObject.get());
        CHECK(molga::ui::ResolveTarget(survivor, survivorIdentity) == survivorButton);
    }

    CHECK(WorldGenerationForTesting() == nextBefore);

    // 소진 검사의 나머지 절반. 0은 "식별자 없음" 센티널이므로 후보가 0이면
    // 발급이 아니라 예외여야 한다. 이 행이 없으면 `candidate == 0 ||` 를 지워도
    // 프로덕션에서는 원자값이 0에 닿을 수 없어 아무 테스트도 깨지지 않는다.
    {
        ScopedWorldGenerationForTesting zeroed(0);
        CHECK(WorldGenerationForTesting() == 0);
        CHECK_THROWS_AS(ConstructWorld(), std::overflow_error);
        CHECK(WorldGenerationForTesting() == 0);
    }
    // 훅은 시퀀스를 뒤로 되돌리지 않는다: 0을 세웠다 걷어도 다음 후보는 그대로다.
    CHECK(WorldGenerationForTesting() == nextBefore);

    // 성공 증인: 훅이 걷힌 뒤에는 다시 정상적으로 발급된다.
    World afterRestore;
    CHECK(afterRestore.Generation() == nextBefore);
    CHECK(afterRestore.Generation() != 0);
    CHECK(afterRestore.Generation() > survivorGeneration);
}

// ── Step 7e: explicit C++17 equality coverage for the identity values ────────

TEST_CASE("identity values compare field by field under C++17 rules") {
    SceneObjectRef left;
    SceneObjectRef right;
    CHECK(left == right);
    CHECK_FALSE(left != right);
    right.targetId = 7;
    CHECK(left != right);
    CHECK_FALSE(left == right);
    left.targetId = 7;
    CHECK(left == right);
    CHECK_FALSE(left != right);

    molga::ui::UIRuntimeTargetIdentity base;
    base.worldGeneration = 9;
    base.objectId = 3;
    base.componentRuntimeTypeId = 5;
    base.componentInstanceId = 11;

    molga::ui::UIRuntimeTargetIdentity same = base;
    CHECK(base == same);
    CHECK_FALSE(base != same);

    // 네 필드 각각이 하나만 달라도 다른 식별자다. 이 네 행이 없으면 비교에서
    // 필드 하나를 빼먹어도 모든 픽스처가 그대로 통과한다.
    molga::ui::UIRuntimeTargetIdentity generationDiffers = base;
    generationDiffers.worldGeneration = 10;
    CHECK(base != generationDiffers);
    CHECK_FALSE(base == generationDiffers);

    molga::ui::UIRuntimeTargetIdentity objectDiffers = base;
    objectDiffers.objectId = 4;
    CHECK(base != objectDiffers);
    CHECK_FALSE(base == objectDiffers);

    molga::ui::UIRuntimeTargetIdentity typeDiffers = base;
    typeDiffers.componentRuntimeTypeId = 6;
    CHECK(base != typeDiffers);
    CHECK_FALSE(base == typeDiffers);

    molga::ui::UIRuntimeTargetIdentity instanceDiffers = base;
    instanceDiffers.componentInstanceId = 12;
    CHECK(base != instanceDiffers);
    CHECK_FALSE(base == instanceDiffers);

    // operator bool: 세 필드가 0이 아니어야 한다. 타입 id는 첫 등록 타입이 0을
    // 받으므로 0이어도 유효하다.
    CHECK(static_cast<bool>(base));
    molga::ui::UIRuntimeTargetIdentity zeroGeneration = base;
    zeroGeneration.worldGeneration = 0;
    CHECK_FALSE(static_cast<bool>(zeroGeneration));
    molga::ui::UIRuntimeTargetIdentity zeroObject = base;
    zeroObject.objectId = 0;
    CHECK_FALSE(static_cast<bool>(zeroObject));
    molga::ui::UIRuntimeTargetIdentity zeroInstance = base;
    zeroInstance.componentInstanceId = 0;
    CHECK_FALSE(static_cast<bool>(zeroInstance));
    molga::ui::UIRuntimeTargetIdentity zeroType = base;
    zeroType.componentRuntimeTypeId = 0;
    CHECK(static_cast<bool>(zeroType));

    const molga::ui::UIRuntimeTargetIdentity empty;
    CHECK_FALSE(static_cast<bool>(empty));
    const molga::ui::UIRuntimeTargetIdentity alsoEmpty;
    CHECK(empty == alsoEmpty);
    CHECK_FALSE(empty != alsoEmpty);
    CHECK(empty != base);
}
