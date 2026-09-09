#pragma once

#include "Common/Fixed26_6.h"
#include "ECS/SceneObjectRef.h"
#include "Platform/Window.h"
#include "Rendering/TextRenderer.h"
#include "Text/TextDiagnostic.h"
#include "UI/UILayoutSnapshot.h"
#include "UI/UINavigationTypes.h"
#include "UI/UIRuntimeIdentity.h"
#include "UI/UITextInputVisualState.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <list>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

class World;

namespace molga::text {
class TextLayoutService;
struct TextLayoutRequest;
} // namespace molga::text

namespace molga::ui {

// ── Step 5c: 한 축의 크기를 실제로 정하는 주인 ──────────────────────────────
// 우선순위는 Canvas > 부모 그룹 > 자기 fitter > 저작 RectTransform 하나뿐이고,
// 그 순서 자체가 계약이다. UILayoutElement는 여기 나타나지 않는다 — 그것은
// 부모에게 내미는 요구값이지 주인이 아니다.
enum class UILayoutDriver : std::uint8_t {
    Canvas, ParentGroup, SelfFitter, AuthoredRect
};
UILayoutDriver ResolveDriver(bool canvasRoot, bool parentDrives,
                             bool fitterDrives) noexcept;

// ── 내용에서 유도된 불변 고유 크기 ──────────────────────────────────────────
// 배치는 텍스트 서비스를 스스로 찾지 않는다(Build의 인자에 없다). 확정된
// 불변 배치를 가진 쪽이 그 내용 정체성과 고유 크기를 여기에 게시하고, 배치는
// 읽기만 한다. 그래서 같은 내용은 언제나 같은 정체성을 갖고, 같은 정체성은
// 재측정 없이 같은 기하를 재사용한다.
struct UIIntrinsicLayoutRecord {
    // 내용에서 유도된 정체성. 프로세스 순번이 아니라 내용 자체에서 나와야
    // cold 빌드와 warm 빌드가 같은 바이트를 낸다.
    std::string contentIdentity;
    molga::FixedSize intrinsicSize;
    // 프로세스 지역 세대. 기하 캐시 키에만 들어가고 정규 JSON에는 들어가지
    // 않는다 — 같은 씬을 다시 열기만 해도 값이 달라지기 때문이다.
    std::uint64_t generation = 0;
};

// ── 인계받은 결함 6 (Task 11.1 -> 11.2): 이 파일의 세 등록부는 동기화되지 ──
// 않는다. 검토했고, 지금은 그것이 맞다고 판단한다. 근거는 관례가 아니라
// 호출 그래프다:
//
//  - Publish/Find/Retire/Clear를 부르는 프로덕션 경로는 UILayoutSystem::Build,
//    UISystem::CollectRender, TextureManager의 로드/리로드/언로드, 그리고
//    Task 11.2의 EngineShutdown뿐이다. 넷 다 결정적 CPU 단계에서 프레임
//    스레드 하나만 지난다.
//  - Task 11.2가 더한 GPU 제출은 이 등록부를 읽지 않는다. UIRenderCollector가
//    읽는 것은 **이미 게시된 스냅샷이 값으로 복사해 간 바인딩**이고, 그
//    복사는 Build 안에서 같은 스레드가 한다.
//
// 그러므로 오늘 데이터 경합은 없다. 무엇이 이 판단을 뒤집는가도 함께 적는다:
// 워커 스레드에서 텍스처를 로드하거나(TextureManager::Load), 두 표면을 서로
// 다른 스레드에서 Build하는 순간 세 등록부 전부가 뮤텍스를 필요로 한다.
// 그때는 unordered_map을 그대로 두고 잠그는 것으로는 부족하다 —
// UITextureBindingRegistry는 수명 토큰의 강한 소유자라, 잠금 밖에서 지분을
// 놓으면 그 파괴가 다른 스레드의 조회와 겹친다.
class UIIntrinsicLayoutRegistry {
public:
    static UIIntrinsicLayoutRegistry& Get();

    // 정체성이나 크기가 실제로 달라졌을 때만 참을 돌려주고 의미 세대를 올린다.
    // 같은 값을 다시 게시하는 것은 변경이 아니다 — 변경으로 세면 매 프레임
    // 재게시하는 소비자가 캐시를 통째로 무력화한다.
    bool Publish(const UIRuntimeTargetIdentity&, std::string contentIdentity,
                 molga::FixedSize intrinsicSize);
    std::optional<UIIntrinsicLayoutRecord> Find(
        const UIRuntimeTargetIdentity&) const;
    void ReleaseWorld(std::uint64_t worldGeneration);

private:
    struct Key {
        UIRuntimeTargetIdentity identity;
    };
    // 네 필드를 개별로 섞는다. 구조체에 패딩이 있어 오브젝트 표현을 통째로
    // 해시하면 같은 식별자가 다른 해시를 낸다.
    std::unordered_map<std::uint64_t, std::vector<
        std::pair<UIRuntimeTargetIdentity, UIIntrinsicLayoutRecord>>> byWorld_;
    std::uint64_t nextGeneration_ = 1;
};

// ── 텍스처 내용 정체성 ──────────────────────────────────────────────────────
// 저작된 GUID 하나에 대해 "지금 그 GUID가 실제로 담고 있는 바이트"를 말한다.
// 프로세스 지역 asset/upload 순번이 아니라 검증된 내용 SHA와 내용에서 유도된
// 안정 ID다: 같은 내용을 다시 업로드해도 정체성은 그대로여야 하고, 다른
// 내용이 같은 GUID로 들어오면 반드시 달라져야 한다.
struct UITextureContentIdentity {
    std::string contentSha256;
    std::uint64_t contentStableId = 0;
    friend bool operator==(const UITextureContentIdentity& a,
                           const UITextureContentIdentity& b) {
        return a.contentSha256 == b.contentSha256 &&
               a.contentStableId == b.contentStableId;
    }
    friend bool operator!=(const UITextureContentIdentity& a,
                           const UITextureContentIdentity& b) {
        return !(a == b);
    }
};

class UITextureContentRegistry {
public:
    static UITextureContentRegistry& Get();
    // 검증된 내용이 실제로 달라졌을 때만 참이다.
    bool Publish(const std::string& textureGuid, UITextureContentIdentity);
    std::optional<UITextureContentIdentity> Find(
        const std::string& textureGuid) const;

private:
    std::unordered_map<std::string, UITextureContentIdentity> byGuid_;
};

// ── Step 5c: GUID 하나가 지금 묶여 있는 런타임 바인딩 ───────────────────────
// 내용 정체성과 나란히 서지만 성격이 정반대다: 이쪽은 전부 프로세스 지역
// 값이고 정규 JSON에도 기하 키에도 절대 들어가지 않는다. 수명 토큰을 함께
// 드는 이유는 하나다 — 스냅샷이 이 값을 복사해 가고, 그 지분이 살아 있는
// 동안에는 핸들이 파괴될 수 없어야 한다.
struct UITextureRuntimeBinding {
    TextureRuntimeBindingIdentity binding;
    std::shared_ptr<const molga::TextureBindingLifetime> lifetime;
};

enum class UITextureBindingPublishResult : std::uint8_t {
    // 같은 값을 다시 게시했다. 변경이 아니므로 세대는 움직이지 않는다 —
    // 변경으로 세면 매 프레임 재게시하는 소비자가 캐시를 통째로 무력화한다.
    Unchanged,
    Published,
    // 집계 세대가 소진되었다. 옛 바인딩을 그대로 두고 아무것도 게시하지 않는다.
    // 감아서 재사용하면 옛 세대가 새 바인딩을 가리키게 된다.
    Exhausted
};

class UITextureBindingRegistry {
public:
    static UITextureBindingRegistry& Get();
    // 수명 토큰이 담은 정체성이 실제로 달라졌을 때만 세대를 올린다.
    UITextureBindingPublishResult Publish(
        const std::string& textureGuid,
        std::shared_ptr<const molga::TextureBindingLifetime> lifetime);
    std::optional<UITextureRuntimeBinding> Find(
        const std::string& textureGuid) const;
    // GUID 하나의 기록을 놓는다. 텍스처가 언로드되면 반드시 여기를 지나야
    // 한다: 이 등록부는 수명 토큰의 *강한* 소유자라, 기록이 남아 있는 한
    // TextureBindingRegistry는 그 핸들을 영원히 반납하지 못한다.
    // 실제로 무언가를 놓았을 때만 참이다.
    bool Retire(const std::string& textureGuid);
    void Clear();

private:
    std::unordered_map<std::string, UITextureRuntimeBinding> byGuid_;
};

// ── Step 4b: 할당 없는 빠른 경로 도장 ───────────────────────────────────────
// 전부 스칼라/값 필드다. 벡터나 문자열이 하나라도 들어오면 변경 없는 프레임이
// 도장을 만드는 것만으로 할당을 하게 되고, "600 프레임 동안 키 할당 0"이라는
// 계약이 그 자리에서 깨진다.
struct UILayoutFastPathStamp {
    molga::WindowId surfaceWindowId = 0;
    std::uint64_t worldGeneration = 0;
    molga::FixedSize logicalViewport;
    std::uint64_t viewportGeneration = 0;
    std::uint64_t semanticDirtyGeneration = 0;
    std::uint64_t scrollDisplacementGeneration = 0;
    std::uint64_t textureBindingGeneration = 0;
    std::uint64_t deviceGeneration = 0;
    bool operator==(const UILayoutFastPathStamp&) const noexcept;
    bool operator!=(const UILayoutFastPathStamp& other) const noexcept {
        return !(*this == other);
    }
};

// ── Step 4c: 충돌 검사되는 기하 캐시 키 ─────────────────────────────────────
// 원래의 순서 있는 벡터를 그대로 보관하고, 해시가 맞은 뒤에 전 필드를 다시
// 비교한다. 해시만 믿으면 서로 다른 두 씬이 한 항목을 공유할 수 있고, 그때
// 화면에 나오는 것은 다른 씬의 기하다.
//
// 이 키는 tint/포커스처럼 페이로드만 바뀐 편집 뒤에도 측정·배치 결과를 그대로
// 재사용한다. 그래서 여기에는 기하에 영향을 주는 값만 들어간다.
struct UILayoutGeometryCacheKey {
    std::uint64_t worldGeneration = 0;
    molga::FixedSize viewport;
    std::uint64_t viewportGeneration = 0;
    std::vector<std::uint64_t> canvasScaleRevisions;
    std::vector<std::uint64_t> hierarchyAndSiblingRevisions;
    std::vector<std::uint64_t> rectAndLayoutRevisions;
    std::vector<std::uint64_t> intrinsicGenerations;
    // ── Step 3e: 입력창의 보이는 글과 유효 요청 ─────────────────────────────
    // 안정된 Canvas DFS 순서다. selection 끝점/caret/affinity/focus/blink/
    // surface revision은 여기 없다 — 그것들은 전체 키에만 있고, 그래서 caret이
    // 깜빡이는 프레임은 셰이핑과 배치를 그대로 재사용한다. 반대로 보이는
    // UTF-8과 유효 요청이 빠지면 편집 이전의 고유/확정 기하가 재사용되어
    // 화면에 옛 글이 남는다.
    std::vector<UITextInputGeometryCacheIdentity> inputGeometry;
    // ── Task 11.3 Step 6b: 의미 있는 스크롤 변위 ────────────────────────────
    // 완전한 식별자 순서로 정렬된 원본 필드 벡터다(해시 하나가 아니다).
    // 오프셋이 달라지면 배치된 노드/렌더/hit 사각형이 달라지므로 이것은
    // **기하** 키에 있다 — 전체 키는 이 키를 통째로 담으므로 두 캐시가 같은
    // 구분을 얻는다. 속도는 여기 없다: 오프셋을 바꾸기 전까지 보이지 않으므로
    // 넣으면 관성이 잦아드는 동안 매 tick이 배치 전체를 다시 돌린다.
    std::vector<UIScrollDisplacementCacheIdentity> scrollDisplacements;
    bool operator==(const UILayoutGeometryCacheKey&) const;
    bool operator!=(const UILayoutGeometryCacheKey& other) const {
        return !(*this == other);
    }
};

// ── Step 4d: 시각/내용 정체성 ───────────────────────────────────────────────
// canonicalAuthoredPayload는 그 컴포넌트가 실제로 디스크에 쓰는 정규 저작
// 바이트 전체다. 해시가 아니라 값이므로, 저작 필드가 하나 늘어도 키가 자동으로
// 그 필드를 덮는다 — 필드마다 손으로 옮겨 적는 키는 언젠가 하나를 빠뜨린다.
struct UIVisualCacheIdentity {
    unsigned int sceneObjectId = 0;
    std::string componentTypeName;
    std::uint32_t componentSchemaVersion = 0;
    std::uint64_t authoredRevision = 0;
    std::string canonicalAuthoredPayload;
    std::string immutableTextLayoutIdentity;
    std::string textureGuid;
    std::string textureContentSha256;
    std::uint64_t textureContentStableId = 0;
    bool operator==(const UIVisualCacheIdentity&) const;
    bool operator!=(const UIVisualCacheIdentity& other) const {
        return !(*this == other);
    }
};

// ── Step 4e: 상호작용 정체성과 완전한 스냅샷 키 ─────────────────────────────
struct UIInteractionCacheIdentity {
    unsigned int sceneObjectId = 0;
    std::string componentTypeName;
    std::uint32_t componentSchemaVersion = 0;
    std::uint64_t authoredRevision = 0;
    bool active = false;
    bool interactable = false;
    bool focusable = false;
    bool acceptsTextInput = false;
    bool maskEnabled = false;
    UINavigationMode navigationMode = UINavigationMode::None;
    std::array<SceneObjectRef, 4> explicitNavigation;
    bool operator==(const UIInteractionCacheIdentity&) const;
    bool operator!=(const UIInteractionCacheIdentity& other) const {
        return !(*this == other);
    }
};

struct UISnapshotCacheKey {
    // 0이 아닌 런타임 라우팅 정체성. 하나뿐인 world/device 슬롯을 돌려주기
    // 전에 비교되지만, 기하 재사용에도 정규 JSON에도 들어가지 않는다.
    molga::WindowId surfaceWindowId = 0;
    UILayoutGeometryCacheKey geometry;
    std::uint64_t semanticDirtyGeneration = 0;
    std::uint64_t scrollDisplacementGeneration = 0;
    std::uint64_t textureBindingGeneration = 0;
    std::uint64_t deviceGeneration = 0;
    std::vector<UIVisualCacheIdentity> visualContent;
    std::vector<UIInteractionCacheIdentity> interaction;
    // ── Step 3b: 안정된 Canvas DFS 순서의 런타임 바인딩 ─────────────────────
    // 위의 넓은 textureBindingGeneration/deviceGeneration 스칼라는 할당 없는
    // 빠른 경로만 무효화한다. 원래의 출처 필드와 바인딩의 모든 필드는 여기
    // 남아 전체 키에서 충돌 비교된다 — 같은 내용을 다시 올렸을 때 기하는
    // 재사용하면서도 낡은 GPU 바인딩은 절대 재사용하지 않게 하는 것이 그
    // 구분의 전부다. 기하 키에도 정규 JSON에도 들어가지 않는다.
    std::vector<UIRuntimeBindingCacheIdentity> runtimeBindings;
    // 입력창의 런타임 편집 상태 전부(선택 끝점/caret/포커스/깜빡임/표면
    // revision 포함). 기하 키에는 그 부분집합만 들어간다.
    std::vector<UITextInputVisualState> inputVisualStates;
    bool operator==(const UISnapshotCacheKey&) const;
    bool operator!=(const UISnapshotCacheKey& other) const {
        return !(*this == other);
    }
};

// 기하 키의 해시. 후보를 좁히기만 하고 값 비교를 대신하지 않는다.
//
// 접기 한 걸음과 씨앗을 함께 내놓는 이유는 하나뿐이다: "해시가 같아도 원래
// 필드를 전부 다시 본다"는 계약은 실제로 충돌하는 두 키를 만들어 보이지
// 않으면 시험할 수 없고, 서로 다른 키만 넣어 본 시험은 해시만 믿는 구현에서도
// 전부 통과한다. FNV-1a의 한 걸음은 (state ^ value) * prime이라 가역이므로,
// 이 두 함수만 있으면 충돌 쌍을 만들 수 있다.
inline constexpr std::size_t kUILayoutCacheHashSeed = 1469598103934665603ULL;
std::size_t FoldUILayoutCacheHash(std::size_t state,
                                  std::uint64_t value) noexcept;
std::size_t HashUILayoutGeometryCacheKey(
    const UILayoutGeometryCacheKey&) noexcept;

inline constexpr std::size_t kUILayoutGeometryEntriesPerWorld = 256;

// ── Step 4a: 검증된 26.6 사각형 교집합 ──────────────────────────────────────
// 최소 변끼리는 최댓값, 최대 변끼리는 최솟값을 취한다. 비어 있거나 검증된
// 산술이 실패하면 nullopt다 — 빈 교집합을 폭 0짜리 사각형으로 돌려주면
// "빈 교집합은 레코드를 제거한다"는 계약이 호출부마다 다시 구현되고, 언젠가
// 한 곳이 그것을 잊는다.
std::optional<molga::FixedRect> IntersectFixedRects(
    const molga::FixedRect&, const molga::FixedRect&) noexcept;

// ── Step 4e: 하나의 확정된 배치가 실제로 낼 명령 수 ─────────────────────────
// 배치된 glyph/tofu 기록의 합이다. 줄 수도 grapheme 수도 아니다 — 그 둘로
// 세면 합자와 대체 glyph에서 예약 구간이 실제 명령 수와 어긋나고, 그 어긋남은
// 다음 항목의 정렬 키가 이미 쓰인 뒤에야 드러난다.
//
// Task 11.2: 이 규칙은 이제 molga::text::TextRenderCommandSpan 한 곳에만 있다
// (Rendering/TextRenderer.h). 예약하는 쪽(여기)과 소비하는 쪽(CollectLayout)이
// 각자 세면 두 수가 어긋날 수 있고, 그 어긋남은 다음 항목의 정렬 키가 이미
// 배정된 뒤에야 드러나기 때문이다. 여기서는 그 이름을 다시 선언하지 않는다 —
// 같은 이름이 두 네임스페이스에 있으면 ADL이 호출을 모호하게 만든다.

// ── 고유 크기를 결정하는 입력을 그대로 이어 붙인 정체성 ─────────────────────
// 해시가 아니라 바이트다 — 이 문자열은 UIVisualCacheIdentity::
// immutableTextLayoutIdentity로 들어가 스냅샷 캐시의 정체성이 되므로, 두 다른
// 내용이 같은 값을 가지면 캐시가 남의 기하를 재사용한다. 제약은 일부러 넣지
// 않는다: 제약이 들어가면 fitter가 rect를 바꿀 때마다 정체성이 달라져 값이
// 영원히 흔들린다.
//
// 규칙이 한 벌인 것이 요점이다. Build와 UISystem::CollectRender가 둘 다 이
// 고유 크기를 게시하는데, 두 곳이 각자 정체성을 계산하면 같은 라벨에 서로
// 다른 바이트를 붙이게 되고 그때 두 생산자가 매 프레임 서로를 덮어써 의미
// 세대가 멈추지 않는다.
std::string UILabelIntrinsicContentIdentity(const molga::text::TextLayoutRequest&);

// ── Step 6: 논리 -> 물리 변환의 유일한 자리 ─────────────────────────────────
// 이 변환은 파이프라인 끝의 뷰포트 가장자리에서 정확히 한 번 일어난다.
// 스냅샷 안에는 물리 픽셀이 하나도 없다: 있으면 backing scale이 바뀌는 순간
// 의미가 같은 스냅샷이 다른 값을 갖게 되어 캐시가 무너진다.
//
// ToPhysicalOutward는 바깥쪽으로 연다 — 최소 변은 floor, 최대 변은 ceil.
// 반올림이면 1픽셀 폭 클립이 통째로 사라지고, 안쪽으로 닫으면 가장자리 픽셀이
// 잘려 나간다.
struct UIPhysicalTransform {
    molga::FixedRect logicalViewport;
    molga::PixelRectU32 physicalViewport;
    // 지금 살아 있는 GraphicsDevice::Generation() 그 값이다. 수집 전에 0이
    // 아니어야 한다 — 0은 "장치 없음"이고, 그 상태로 만든 물리 사각형은 어느
    // 장치의 것도 아니다.
    std::uint64_t deviceGeneration = 0;

    std::optional<molga::PixelRectU32> ToPhysicalOutward(
        const molga::FixedRect&) const noexcept;
    // ── Task 11.2 close-out: 잘린 사각형은 잘린 그림을 가리켜야 한다 ────────
    // ToPhysicalOutward는 뷰포트로 **자른** 사각형을 돌려준다. scissor에는
    // 그것이 맞지만, 그 사각형을 스프라이트의 quad로 쓰면서 UV를 0,0->1,1로
    // 두면 텍스처 전체가 살아남은 폭에 눌려 들어간다 — 화면 밖으로 걸친
    // 패널이 잘리는 대신 **찌그러진다**. 같은 요소의 글자는 자르지 않는
    // LayoutToOutputAffine을 지나므로, 그 순간 글자와 그림이 눈에 보이게
    // 어긋난다.
    //
    // 그래서 자름 자체를 UV로 옮긴다. u/v는 자르기 **전** 사각형(같은
    // floor/ceil 규칙으로 연 것) 위에서 살아남은 구간이 차지하는 비율이고,
    // 그래서 잘린 quad는 잘리지 않은 quad가 그 자리에 그렸을 바로 그 픽셀을
    // 그린다.
    struct SpriteQuad {
        molga::PixelRectU32 rect;
        float u0 = 0.0f;
        float v0 = 0.0f;
        float u1 = 1.0f;
        float v1 = 1.0f;
    };
    std::optional<SpriteQuad> ToPhysicalSpriteOutward(
        const molga::FixedRect&) const noexcept;
    // 역방향 점 변환의 유일한 자리. 반열린 물리 뷰포트 안의 유한한 점만
    // 받는다 — 오른쪽/아래 가장자리는 뷰포트 밖이다.
    std::optional<molga::FixedPoint> ToLogicalPoint(
        double outputPixelX, double outputPixelY) const noexcept;
    std::optional<TextAffine2D> LayoutToOutputAffine(
        molga::FixedPoint logicalOrigin) const noexcept;
    // Task 8의 검증된 정수 규칙 그대로다. affine의 float에서 배율을 되짚지
    // 않는다 — 그 값은 폰트와 배치에 따라 달라져 글자마다 다른 래스터 높이를
    // 만든다.
    std::optional<TextRasterPolicy> RasterPolicy(
        molga::text::TextDiagnosticSink&) const;
};

struct UISnapshotWorldDeviceSlotKey {
    std::uint64_t worldGeneration = 0;
    std::uint64_t deviceGeneration = 0;
    bool operator==(const UISnapshotWorldDeviceSlotKey&) const noexcept;
    bool operator!=(const UISnapshotWorldDeviceSlotKey& other) const noexcept {
        return !(*this == other);
    }
};

// ── 결정적 배치와 그 두 캐시 ────────────────────────────────────────────────
// 살아 있는 월드마다 최대 256개짜리 최근성 갱신 기하 LRU 하나, 살아 있는
// world/device 조합마다 정확히 하나의 {완전한 키, 스냅샷} 항목을 갖는다.
// 전체 미스는 그 슬롯을 교체할 뿐, 포커스/스크롤/깜빡임/업로드 키의 역사를
// 쌓지 않는다.
class UILayoutSystem {
public:
    UILayoutSystem();
    ~UILayoutSystem();
    UILayoutSystem(const UILayoutSystem&) = delete;
    UILayoutSystem& operator=(const UILayoutSystem&) = delete;

    // ── Step 3i: 배치의 유일한 입구 ────────────────────────────────────────
    // 제공자와 텍스트 서비스를 명시적으로 받는다. 기본 인자를 가진 세 인자
    // 오버로드를 남기지 않는 이유는 하나다 — 그런 오버로드가 있으면 진짜
    // 제공자를 넘기는 것을 한 곳에서 빠뜨려도 컴파일이 통과하고, 그 표면만
    // 조용히 편집 상태 없이 그려진다.
    UISnapshotPtr Build(World&, molga::WindowId surfaceWindowId,
                        molga::FixedSize logicalViewport,
                        const UITextInputVisualStateProvider&,
                        molga::text::TextLayoutService&,
                        molga::text::TextDiagnosticSink&);
    void OnWorldReleased(std::uint64_t worldGeneration);

    // ── Step 7d: 장치가 바뀌면 장치에 묶인 스냅샷은 전부 버린다 ────────────
    // newGeneration은 GraphicsDevice가 **이미 취득해 게시한** 그 값이다. 여기서
    // 축을 한 번 더 올리지 않는다 — 올리면 방금 게시된 장치 세대가 그 자리에서
    // 낡은 값이 되고, 새 장치로 지은 첫 스냅샷조차 캐시에 들어가지 못한다.
    //
    // 지우는 것: lastSnapshot_ 빠른 경로 포인터, 새 세대의 것이 아닌 모든
    // world/device 슬롯, 옛 장치의 런타임 바인딩을 담은 마지막 키.
    // 남기는 것: 상한 있는 기하 LRU와 정규 의미 스크래치 — 기하는 장치와
    // 무관하므로 버리면 장치 재생성마다 전 UI를 다시 배치하게 된다.
    void OnDeviceGenerationChanged(std::uint64_t oldGeneration,
                                   std::uint64_t newGeneration);
    // ── Step 7g: teardown이 부르는 자리 ────────────────────────────────────
    // 그 장치 세대에 묶인 전체 스냅샷 슬롯과 빠른 경로 포인터를 놓는다.
    // OnDeviceGenerationChanged와 달리 "남길 세대"가 없다: 장치가 사라지는
    // 중이므로 그 세대의 것은 전부 놓아야 엔진이 마지막 강한 소유자를
    // 내려놓는다.
    void ClearFullSnapshotBindingCache(std::uint64_t deviceGeneration);

    // ── 관찰 seam ──────────────────────────────────────────────────────────
    // 텍스트 하위 시스템의 detail:: 계수기와 같은 성격이고 같은 이유로 출하되는
    // 빌드에 들어 있다. 시험할 가치가 있는 주장 — "페이로드만 바뀐 편집은 기하를
    // 다시 만들지 않는다", "변경 없는 600 프레임은 키를 만들지 않는다" — 이
    // 전부 프로덕션 경로에 대한 것이라, 테스트에만 컴파일되는 계수기는 다른
    // 프로그램을 재게 된다.
    std::uint64_t GeometryBuildCount() const noexcept;
    std::uint64_t SnapshotKeyBuildCount() const noexcept;
    std::uint64_t SnapshotKeyAllocationCount() const noexcept;
    // 마지막 빌드가 만든 시각 키 항목 수. 클립으로 떨어진 노드가 키에 남는지를
    // 밖에서 볼 수 있는 유일한 자리다 — 스냅샷 내용은 캐시된 노드 벡터에서
    // 오므로 오염된 키와 올바른 키가 같은 스냅샷을 낸다. 전역 의미 세대가
    // 모든 편집에서 오르므로 포인터 동일성으로는 이 차이를 잡을 수 없다.
    std::size_t LastVisualKeyEntryCount() const noexcept;
    std::size_t GeometryCacheEntryCountForWorld(
        std::uint64_t worldGeneration) const noexcept;
    std::size_t FullSnapshotCacheEntryCountForWorldDevice(
        UISnapshotWorldDeviceSlotKey) const noexcept;
    // 프로덕션이 쓰는 그 조회 경로를 그대로 탄다: 해시로 후보를 좁힌 뒤
    // 원래의 순서 있는 필드를 전부 비교한다. 값 비교를 건너뛴 구현은 해시가
    // 같은 다른 키에 대해 여기서 참을 돌려주고 죽는다.
    bool GeometryCacheContains(const UILayoutGeometryCacheKey&) const;
    std::optional<UILayoutGeometryCacheKey> LastGeometryKey() const;
    // 마지막 빌드가 만든 완전한 충돌 키. LastGeometryKey와 같은 성격의 seam
    // 이고 같은 이유로 출하되는 빌드에 있다.
    //
    // 이것이 없으면 키의 *내용*은 사실상 시험할 수 없다: 넓은
    // semanticDirty/textureBinding 스칼라가 같은 키 안에서 모든 편집에 대해
    // 함께 움직이므로, runtimeBindings나 inputVisualStates를 통째로 빼도
    // 포인터 동일성으로는 아무 차이가 보이지 않는다. 그 상태로 두면 Task 11.2가
    // 렌더 페이로드를 이 키에 의존하게 만드는 순간, 검증된 적 없는 필드 위에
    // 서게 된다.
    std::optional<UISnapshotCacheKey> LastSnapshotKey() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace molga::ui
