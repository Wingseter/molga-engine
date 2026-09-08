#pragma once

#include "Common/Fixed26_6.h"
#include "ECS/SceneObjectRef.h"
#include "Platform/Window.h"
#include "Text/TextDiagnostic.h"
#include "UI/UILayoutSnapshot.h"
#include "UI/UINavigationTypes.h"
#include "UI/UIRuntimeIdentity.h"

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

    UISnapshotPtr Build(World&, molga::WindowId surfaceWindowId,
                        molga::FixedSize logicalViewport,
                        molga::text::TextDiagnosticSink&);
    void OnWorldReleased(std::uint64_t worldGeneration);

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

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace molga::ui
