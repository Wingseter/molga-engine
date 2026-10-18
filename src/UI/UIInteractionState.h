#pragma once

#include "Platform/Window.h"
#include "Text/TextDiagnostic.h"
#include "UI/UIRuntimeIdentity.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

namespace molga::ui {

// 한 UI 표면의 키. 창만으로 잡으면 씬 교체 뒤 옛 포커스가 살아남고, 세대만으로
// 잡으면 분리된 두 창이 서로의 포커스를 나눠 갖는다.
struct UISurfaceKey {
    molga::WindowId windowId = 0;
    std::uint64_t worldGeneration = 0;
    bool operator==(const UISurfaceKey& other) const noexcept {
        return windowId == other.windowId &&
               worldGeneration == other.worldGeneration;
    }
    bool operator!=(const UISurfaceKey& other) const noexcept {
        return !(*this == other);
    }
};

// 레거시 UISystem::ProcessInput 경로가 쓰는 예약 표면. SDL 창 id는 0이 아니므로
// 실제 표면과 겹치지 않는다. Task 12.3이 레거시 경로와 함께 지운다.
inline constexpr molga::WindowId kLegacyPointerSurfaceWindow = 0;

// 필드별로 섞는다(UIRuntimeIdentity.h의 패딩 경고 그대로).
struct UIRuntimeIdentityHash {
    std::size_t operator()(const UIRuntimeTargetIdentity& id) const noexcept;
};
struct UISurfaceKeyHash {
    std::size_t operator()(const UISurfaceKey& key) const noexcept;
};

struct UIButtonPointerState {
    bool hovered = false;
    bool pressed = false;
    // 보이는 상태가 아니다: 세대를 올리지 않는다.
    bool clickedThisFrame = false;
    bool operator==(const UIButtonPointerState& other) const noexcept {
        return hovered == other.hovered && pressed == other.pressed &&
               clickedThisFrame == other.clickedThisFrame;
    }
    bool operator!=(const UIButtonPointerState& other) const noexcept {
        return !(*this == other);
    }
};

struct UISurfaceInteractionState {
    std::optional<UIRuntimeTargetIdentity> focused;
    std::optional<UIRuntimeTargetIdentity> pointerCapture;
    std::optional<UIRuntimeTargetIdentity> hovered;
    std::unordered_map<UIRuntimeTargetIdentity, UIButtonPointerState,
                       UIRuntimeIdentityHash>
        buttons;
};

// 프로세스에 하나. UIScrollSystem::Get()과 같은 이유로 new 하고 파괴하지 않는다
// (정적 World의 소멸자가 OnWorldReleased로 여기까지 온다).
//
// 쓰는 사람은 셋뿐이다: 입력 라우터(포인터 동작 상태), UIFocusSystem(포커스와
// 정리), 레거시 UIButton::ApplyPointerState 어댑터. 읽는 쪽은 UIButton getter.
class UIInteractionStateTable {
public:
    static UIInteractionStateTable& Get();

    UIInteractionStateTable(const UIInteractionStateTable&) = delete;
    UIInteractionStateTable& operator=(const UIInteractionStateTable&) = delete;

    // 표면이 없으면 nullptr. 정규화로 빈 표면은 지워지므로 호출부가 널을 직접
    // 다루지 않게 SurfaceOrEmpty를 쓴다.
    const UISurfaceInteractionState* Surface(UISurfaceKey) const noexcept;
    UISurfaceInteractionState SurfaceOrEmpty(UISurfaceKey) const;
    UIButtonPointerState ButtonState(
        UISurfaceKey, const UIRuntimeTargetIdentity&) const noexcept;
    // 같은 월드 세대의 모든 표면을 OR 한다. 창을 모르는 레거시 getter 전용.
    UIButtonPointerState ButtonStateAnySurface(
        const UIRuntimeTargetIdentity&) const noexcept;

    // 보이는 상태를 바꾸는 유일한 입구. next를 정규화해 현재와 비교하고, 보이는
    // 차이(hovered/pressed/focused/capture/hovered-id)가 있을 때만
    // Advance(SemanticDirty)를 **한 번** 얻은 뒤 저장한다. 소진이면 저장하지
    // 않고(이전 상태 보존) sink가 있으면 LayoutInvalid/Blocker를 보고한다.
    // clickedThisFrame만 다른 변경은 세대를 올리지 않고 저장한다.
    // 반환: 저장됐는가(변경 없음도 true). *changed는 **보이는** 상태가 달라졌는가.
    bool Apply(UISurfaceKey, UISurfaceInteractionState next,
               molga::text::TextDiagnosticSink* sink, const char* subsystem,
               unsigned int sceneObjectId, bool* changed = nullptr);

    // 이 식별자의 버튼 항목·hover·capture·focus를 그 세대의 모든 표면에서
    // 지운다(비활성/상호작용 불가 정리). 표면마다 Apply를 지난다.
    bool ClearIdentityEverywhere(const UIRuntimeTargetIdentity&,
                                 molga::text::TextDiagnosticSink* sink);
    // 세대를 올리지 않는다: 죽은 월드의 표는 어떤 스냅샷에도 보이지 않는다.
    void OnWorldReleased(std::uint64_t worldGeneration);
    std::size_t SurfaceCountForWorld(std::uint64_t worldGeneration) const noexcept;
    std::vector<UISurfaceKey> SurfacesForWindow(molga::WindowId) const;

    struct Impl;

private:
    UIInteractionStateTable();
    ~UIInteractionStateTable();
    std::unique_ptr<Impl> impl_;
};

}  // namespace molga::ui
