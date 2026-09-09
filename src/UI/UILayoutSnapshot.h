#pragma once

#include "Common/Fixed26_6.h"
#include "Common/Types.h"
#include "Platform/Window.h"
#include "Rendering/GraphicsDevice.h"
#include "Text/TextLayoutTypes.h"
#include "UI/UILayoutTypes.h"
#include "UI/UINavigationTypes.h"
#include "UI/UIRuntimeIdentity.h"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace molga {
// 게시된 바인딩 하나의 수명 토큰. 값은 TextureBindingRegistry가 만들고, 여기서는
// 지분만 든다 — 스냅샷이 살아 있는 동안 그 핸들을 파괴할 수 없게 하는 것이
// 이 포인터의 유일한 일이다.
class TextureBindingLifetime;
} // namespace molga

namespace molga::ui {

// 배치 결과 한 노드. 좌표는 전부 검증된 26.6 논리 단위이며 비교는 raw 정수로만
// 한다. frameIndex/타임스탬프/물리 픽셀 사각형/날 포인터/GPU 핸들은 여기에
// 들어오지 않는다 — 그런 필드가 하나라도 있으면 프레임마다 값이 달라져
// "변경 없는 프레임은 같은 스냅샷을 재사용한다"는 계약이 절대 성립하지 않는다.
// logicalClip은 이미 조상들과 교집합을 취한 결과이고, 비어 있는 교집합은 노드
// 자체를 제거하므로 여기에는 나타나지 않는다.
struct UILayoutNodeSnapshot {
    UIRuntimeTargetIdentity rectTransform;
    molga::FixedRect logicalRect;
    molga::FixedSize intrinsicSize;
    std::optional<molga::FixedRect> logicalClip;
    std::uint64_t layoutRevision = 0;
    bool interactionEligible = false;
};

// ── Step 3a: 프로세스를 건너뛰어도 같은 대상을 말하는 정규 키 ───────────────
// UIRuntimeTargetIdentity는 이 프로세스의 할당 순번이라 씬을 다시 열기만 해도
// 값이 달라진다. 정규 JSON과 parity 비교는 이 키만 본다: 씬에 저장되는
// objectId, 컴포넌트 타입 이름, 그리고 그 타입의 저작 스키마 버전.
struct UIStableComponentKey {
    unsigned int sceneObjectId = 0;
    std::string componentTypeName;
    std::uint32_t componentSchemaVersion = 0;
    bool operator==(const UIStableComponentKey&) const noexcept;
    bool operator!=(const UIStableComponentKey& other) const noexcept {
        return !(*this == other);
    }
};

// 스냅샷 N이 얼려 둔 대상 하나. 런타임 정체성과 정규 키를 함께 든다 —
// 라우팅은 런타임 쪽을, 직렬화와 parity는 정규 쪽을 본다. 나중 라우팅이 ECS
// 상태에서 이 둘을 다시 만들어 내는 일은 없다: 그 사이에 컴포넌트가 지워지고
// 같은 타입이 다시 붙으면 재구성된 대상은 다른 컴포넌트다.
struct UIFrozenTarget {
    UIRuntimeTargetIdentity runtimeTarget;
    UIStableComponentKey canonicalTarget;
    explicit operator bool() const noexcept {
        return static_cast<bool>(runtimeTarget);
    }
    bool operator==(const UIFrozenTarget&) const noexcept;
    bool operator!=(const UIFrozenTarget& other) const noexcept {
        return !(*this == other);
    }
};

// ── Step 3b: 정확한 런타임 바인딩 ───────────────────────────────────────────
// 전부 프로세스 지역 값이다. 정규 JSON에도 기하 키에도 들어가지 않고, 전체
// 스냅샷 충돌 키에만 들어간다. 핸들 비교는 resource index와 handle generation을
// 모두 본다(ResourceHandle::operator==) — 해시만 맞춰 보는 비교는 파괴 후
// 재사용된 슬롯을 같은 핸들로 읽는다.
struct TextureRuntimeBindingIdentity {
    std::uint64_t deviceGeneration = 0;
    std::uint64_t uploadGeneration = 0;
    molga::TextureHandle texture;
    molga::SamplerHandle sampler;
    std::uint64_t lifetimeIdentity = 0;
    bool operator==(const TextureRuntimeBindingIdentity&) const noexcept;
    bool operator!=(const TextureRuntimeBindingIdentity& other) const noexcept {
        return !(*this == other);
    }
};

struct UIRuntimeBindingCacheIdentity {
    unsigned int sceneObjectId = 0;
    std::string componentTypeName;
    std::uint32_t componentSchemaVersion = 0;
    TextureRuntimeBindingIdentity binding;
    bool operator==(const UIRuntimeBindingCacheIdentity&) const noexcept;
    bool operator!=(const UIRuntimeBindingCacheIdentity& other) const noexcept {
        return !(*this == other);
    }
};

// ── Task 11.3 Step 6b: 의미 있는 스크롤 변위 하나 ───────────────────────────
// 기하/전체 스냅샷 캐시의 입력이다. 원래 필드를 그대로 든다 — 해시 하나로
// 접으면 서로 다른 두 변위가 한 항목을 나눠 갖고, 그때 화면에 나오는 것은
// 다른 오프셋으로 배치된 내용이다.
//
// velocity는 여기 없다. 속도는 오프셋을 바꾸기 전까지 화면에 보이지 않으므로
// 그 자체로는 캐시 정체성을 움직이지 않는다. 넣으면 관성이 잦아드는 동안
// 매 tick이 기하 미스가 되어 배치가 통째로 다시 돈다.
//
// 런타임 값이므로 정규 JSON에도 씬/prefab 직렬화에도 들어가지 않는다.
struct UIScrollDisplacementCacheIdentity {
    UIRuntimeTargetIdentity scrollTarget;
    std::int32_t offsetXRaw = 0;
    std::int32_t offsetYRaw = 0;
    UIRuntimeTargetIdentity viewport;
    UIRuntimeTargetIdentity content;
    bool operator==(const UIScrollDisplacementCacheIdentity&) const noexcept;
    bool operator!=(
        const UIScrollDisplacementCacheIdentity& other) const noexcept {
        return !(*this == other);
    }
};

// ── Step 3d/3g: 구체 페이로드 ───────────────────────────────────────────────
// textureContentStableId는 검증된 GUID/SHA 쌍에서 결정적으로 유도된 값이고,
// 동등 비교는 언제나 원래의 두 문자열까지 다시 본다 — 유도값만 비교하면 서로
// 다른 두 내용이 한 항목을 나눠 갖는다.
struct UISpriteSnapshot {
    std::string textureGuid;
    std::string textureContentSha256;
    std::uint64_t textureContentStableId = 0;
    TextureRuntimeBindingIdentity binding;
    Color tint = Color::White();
    std::shared_ptr<const molga::TextureBindingLifetime> resourceLifetime;
};

struct UITextSnapshot {
    std::shared_ptr<const molga::text::TextLayout> layout;
    molga::FixedPoint origin;
    Color color = Color::White();
};

struct UISolidRectSnapshot {
    Color color = Color::White();
};

using UIRenderPayload =
    std::variant<UISpriteSnapshot, UITextSnapshot, UISolidRectSnapshot>;

// 렌더 레코드 하나. 소비자가 이 값을 그리기 위해 살아 있는 컴포넌트를 다시
// 들여다볼 일이 없어야 한다 — 그 순간 게시된 스냅샷과 화면이 갈린다.
//
// reservedCommandSpan은 이 레코드가 실제로 낼 명령 수다. 스프라이트/솔리드는
// 1이고, 텍스트는 배치된 glyph/tofu 기록 수 전부다. 다음 레코드의
// stableSubmissionIndex는 이 값을 검증된 덧셈으로 더한 뒤에만 배정된다.
struct UIRenderItemSnapshot {
    UIRuntimeTargetIdentity source;
    UIStableComponentKey canonicalSource;
    UIDrawOrderKey order;
    molga::FixedRect logicalRect;
    std::optional<molga::FixedRect> logicalClip;
    std::uint64_t reservedCommandSpan = 1;
    UIRenderPayload payload;
};

// ── Step 3h: 얼어붙은 다단계 hit 레코드 ─────────────────────────────────────
struct UINavigationSnapshot {
    UINavigationMode mode = UINavigationMode::None;
    std::array<std::optional<UIRuntimeTargetIdentity>, 4> explicitTargets;
    std::array<std::optional<UIStableComponentKey>, 4> canonicalTargets;
};

struct UIHitTargetSnapshot {
    UIRuntimeTargetIdentity target;
    UIStableComponentKey canonicalTarget;
    std::optional<UIFrozenTarget> focusTarget;
    std::optional<UIFrozenTarget> textInputTarget;
    std::vector<UIFrozenTarget> scrollTargets;
    UIDrawOrderKey order;
    molga::FixedRect logicalRect;
    std::optional<molga::FixedRect> logicalClip;
    bool interactable = false;
    bool focusable = false;
    bool acceptsTextInput = false;
    UINavigationSnapshot navigation;
};

// ── Step 3f: 입력창이 소유한 라벨과 불변 IME 기하 ───────────────────────────
//
// enabledAndVisible은 **런타임 값**이다: placeholder는 "비어 있고 포커스가
// 없을 때만" 보이므로 그 두 사실이 그대로 들어 있다. 그래서 정규 JSON에는
// 들어가지 않는다 — 같은 저작 씬에서 사용자가 입력창을 한 번 누르기만 해도
// 정규 바이트가 달라지고, Exit Contract의 "편집 이력이 다른 빌드가 바이트
// 동일한 의미 스냅샷을 낸다"가 그 자리에서 깨진다. UISnapshot::
// textInputImeGeometry가 focused를 담고 있다는 이유로 통째로 제외된 것과
// 같은 이유이고, 같은 규칙이어야 한다.
struct UIFrozenInputLabelVisual {
    UIFrozenTarget label;
    molga::text::TextLayoutRequest requestTemplate;
    Color color = Color::White();
    bool enabledAndVisible = false;
};

struct UITextInputLabelSnapshot {
    UIFrozenTarget input;
    UIFrozenInputLabelVisual renderedLabel;
    std::optional<UIFrozenInputLabelVisual> placeholderLabel;
    molga::text::TextLayoutRequest effectiveInputRequestTemplate;
    molga::FixedRect logicalViewport;
    std::optional<molga::FixedRect> logicalClip;
    UIDrawOrderKey baseOrder;
    // 이 입력창의 텍스트 단계가 예약한 위치 기록 수. baseOrder부터 이 수만큼이
    // 이 묶음의 것이고, 그 다음 오브젝트는 그 뒤에서 번호를 받는다. 0이면
    // 예약 없이 baseOrder 한 자리만 이 묶음의 시작을 가리킨다.
    std::uint64_t reservedCommandSpan = 0;
};

// IME 기하는 게시된 스냅샷의 값이고 caret이 지금 그려지는가와 무관하다.
// caretVisible에 묶으면 깜빡임이 꺼진 프레임에 OS 후보창이 화면 구석으로
// 튄다.
struct UITextInputImeGeometrySnapshot {
    UIFrozenTarget input;
    molga::FixedRect logicalInputArea;
    molga::FixedPoint logicalCursor;
    std::optional<molga::FixedRect> logicalClip;
    bool focused = false;
    bool operator==(const UITextInputImeGeometrySnapshot&) const noexcept;
    bool operator!=(const UITextInputImeGeometrySnapshot& other) const noexcept {
        return !(*this == other);
    }
};

// 한 UI 표면의 의미(semantic) 스냅샷. 게시된 뒤에는 불변이며, 같은 프레임의
// 렌더와 hit-test가 같은 인스턴스를 공유한다.
//
// 게시 후 변경 금지. 불변성을 강제하는 것은 아래 UISnapshotPtr의 const뿐이고
// 구조체 자체는 평범한 집합체이므로, 게시하기 전에 shared_ptr<const>로 넘겨
// 어떤 소유자도 나중에 수정할 수 없게 해야 한다. 게시된 스냅샷을 고쳐 쓰면
// 이미 그 인스턴스를 들고 있는 이번 프레임의 렌더/hit-test가 서로 다른 값을
// 보게 된다.
//
// nodes는 게시하는 쪽이 UIDrawOrderKey 오름차순으로 정렬해서 넣는다.
// UILayoutNodeSnapshot이 draw order key를 담지 않는 것은 의도된 설계라서
// (키는 렌더/hit 페이로드가 따로 들고 다닌다) 직렬화기는 스스로 그 순서를
// 만들 수 없고 주어진 순서를 그대로 내보낸다. renderItems/hitTargets는 자기
// order를 들고 있고, 게시하는 쪽이 그 키로 정렬한다.
struct UISnapshot {
    molga::WindowId surfaceWindowId = 0;
    std::uint64_t worldGeneration = 0;
    molga::FixedSize logicalViewport;
    std::vector<UILayoutNodeSnapshot> nodes;
    std::vector<UIRenderItemSnapshot> renderItems;
    std::vector<UIHitTargetSnapshot> hitTargets;
    std::vector<UITextInputLabelSnapshot> textInputLabels;
    std::vector<UITextInputImeGeometrySnapshot> textInputImeGeometry;
};

using UISnapshotPtr = std::shared_ptr<const UISnapshot>;

// 진단/parity 비교용 정규 JSON. raw 26.6 정수만 내보내며(부동소수 표기는
// 반올림 때문에 바이트 동일성을 깨뜨린다) 프로세스나 편집 이력에 따라 달라지는
// 값은 전부 뺀다: surfaceWindowId는 에디터/런타임의 창 할당이고,
// worldGeneration과 UIRuntimeTargetIdentity의 componentInstanceId/
// componentRuntimeTypeId는 이 프로세스의 할당 순번이며, layoutRevision은
// 재계산/편집 횟수에 달린 순번이다. 하나라도 넣으면 같은 authored 입력이
// 실행마다, 또는 cold 빌드와 편집을 거친 warm 빌드 사이에서 다른 바이트를
// 내서 parity 비교가 무너진다. 노드에서 내보내는 식별자는 씬에 저장되는
// objectId 하나뿐이고, 렌더/hit 레코드는 UIStableComponentKey를 쓴다.
//
// TextureRuntimeBindingIdentity는 통째로 빠진다. 그래야 같은 내용을 다시
// 올리거나 장치를 다시 만든 뒤의 스냅샷이 바이트 동일한 정규 JSON을 낸다.
//
// ── 문서 형태 (Task 11.2에서 바뀐 곳) ───────────────────────────────────────
// 색 배열(`tint`, `color`)은 이제 `array<number | string>`이다. 유한한 성분은
// 수로, 유한하지 않은 성분은 문자열 `"non-finite"`로 나간다 — `null`을 쓰면
// 이 문서에서 이미 "값이 없다"를 뜻하는 표기(예: `clip`)와 별칭이 되어 거절이
// 부재와 구별되지 않기 때문이다. 이 배열을 `array<float>`로 읽는 소비자는
// 거절된 성분에서 깨진다. 사본 하나가 아니라 이 문장이 그 형태의 정의다.
//
// 사전조건: snapshot.nodes는 이미 UIDrawOrderKey 오름차순으로 정렬되어
// 있어야 한다. 노드가 draw order key를 담지 않으므로 이 함수는 그 순서를
// 만들 수도, 검사할 수도 없다 — 정렬되지 않은 목록을 넘기면 진단 없이 다른
// 바이트가 나온다. 정렬은 스냅샷을 게시하는 쪽의 의무다.
std::string StableLayoutSnapshotJson(const UISnapshot&);

} // namespace molga::ui
