#include "UI/UIInteractionState.h"

#include "UI/UIRuntimeInvalidation.h"

#include <algorithm>
#include <utility>

namespace molga::ui {
namespace {

// 64비트 섞기(splitmix64의 마무리 단계). 필드를 하나씩 섞는다.
std::size_t Mix(std::size_t seed, std::uint64_t value) noexcept {
    std::uint64_t x = static_cast<std::uint64_t>(seed) ^
                      (value + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2));
    x ^= x >> 30;
    x *= 0xbf58476d1ce4e5b9ULL;
    x ^= x >> 27;
    x *= 0x94d049bb133111ebULL;
    x ^= x >> 31;
    return static_cast<std::size_t>(x);
}

bool VisibleEqual(const UIButtonPointerState& a,
                  const UIButtonPointerState& b) noexcept {
    return a.hovered == b.hovered && a.pressed == b.pressed;
}

// 빈 항목을 지운 정규형. 같은 의미의 두 상태가 같은 모양이 되어야 비교가
// 맞고, 빈 표면이 표에 남지 않는다.
void Normalize(UISurfaceInteractionState& state) {
    for (auto it = state.buttons.begin(); it != state.buttons.end();) {
        it = (it->second == UIButtonPointerState{}) ? state.buttons.erase(it)
                                                    : std::next(it);
    }
}

bool IsEmpty(const UISurfaceInteractionState& state) noexcept {
    return !state.focused && !state.pointerCapture && !state.hovered &&
           state.buttons.empty();
}

// 다음 스냅샷에 **보이는** 차이가 있는가. clickedThisFrame은 보이지 않는다.
bool VisiblyDiffers(const UISurfaceInteractionState& a,
                    const UISurfaceInteractionState& b) {
    if (a.focused != b.focused || a.pointerCapture != b.pointerCapture ||
        a.hovered != b.hovered || a.buttons.size() != b.buttons.size()) {
        return true;
    }
    for (const auto& entry : a.buttons) {
        const auto found = b.buttons.find(entry.first);
        if (found == b.buttons.end() ||
            !VisibleEqual(entry.second, found->second)) {
            return true;
        }
    }
    return false;
}

bool FullyEqual(const UISurfaceInteractionState& a,
                const UISurfaceInteractionState& b) {
    if (VisiblyDiffers(a, b)) return false;
    for (const auto& entry : a.buttons) {
        if (entry.second != b.buttons.at(entry.first)) return false;
    }
    return true;
}

}  // namespace

std::size_t UIRuntimeIdentityHash::operator()(
    const UIRuntimeTargetIdentity& id) const noexcept {
    std::size_t h = 0;
    h = Mix(h, id.worldGeneration);
    h = Mix(h, id.objectId);
    h = Mix(h, id.componentRuntimeTypeId);
    h = Mix(h, id.componentInstanceId);
    return h;
}

std::size_t UISurfaceKeyHash::operator()(const UISurfaceKey& key) const noexcept {
    std::size_t h = 0;
    h = Mix(h, key.windowId);
    h = Mix(h, key.worldGeneration);
    return h;
}

struct UIInteractionStateTable::Impl {
    std::unordered_map<UISurfaceKey, UISurfaceInteractionState, UISurfaceKeyHash>
        surfaces;
};

UIInteractionStateTable::UIInteractionStateTable()
    : impl_(std::make_unique<Impl>()) {}
UIInteractionStateTable::~UIInteractionStateTable() = default;

UIInteractionStateTable& UIInteractionStateTable::Get() {
    // 의도적으로 파괴하지 않는다 — UIScrollSystem::Get()과 같은 이유다. 정적
    // World 하나가 종료 시점에 소멸하면 그 소멸자가 NotifyUIWorldReleased ->
    // UISystem::OnWorldReleased -> 여기로 오는데, 평범한 Meyers 싱글턴이면 이쪽이
    // 먼저 죽어 이미 파괴된 unordered_map을 건드린다. 이 표에는 GPU 자원도 외부
    // 수명 토큰도 없다.
    static UIInteractionStateTable* table = new UIInteractionStateTable();
    return *table;
}

const UISurfaceInteractionState* UIInteractionStateTable::Surface(
    UISurfaceKey key) const noexcept {
    const auto found = impl_->surfaces.find(key);
    return found == impl_->surfaces.end() ? nullptr : &found->second;
}

UISurfaceInteractionState UIInteractionStateTable::SurfaceOrEmpty(
    UISurfaceKey key) const {
    const auto* found = Surface(key);
    return found ? *found : UISurfaceInteractionState{};
}

UIButtonPointerState UIInteractionStateTable::ButtonState(
    UISurfaceKey key, const UIRuntimeTargetIdentity& id) const noexcept {
    const auto* surface = Surface(key);
    if (!surface) return {};
    const auto found = surface->buttons.find(id);
    return found == surface->buttons.end() ? UIButtonPointerState{}
                                           : found->second;
}

UIButtonPointerState UIInteractionStateTable::ButtonStateAnySurface(
    const UIRuntimeTargetIdentity& id) const noexcept {
    UIButtonPointerState merged;
    if (!id) return merged;
    for (const auto& entry : impl_->surfaces) {
        if (entry.first.worldGeneration != id.worldGeneration) continue;
        const auto found = entry.second.buttons.find(id);
        if (found == entry.second.buttons.end()) continue;
        merged.hovered = merged.hovered || found->second.hovered;
        merged.pressed = merged.pressed || found->second.pressed;
        merged.clickedThisFrame =
            merged.clickedThisFrame || found->second.clickedThisFrame;
    }
    return merged;
}

bool UIInteractionStateTable::Apply(UISurfaceKey key,
                                    UISurfaceInteractionState next,
                                    molga::text::TextDiagnosticSink* sink,
                                    const char* subsystem,
                                    unsigned int sceneObjectId, bool* changed) {
    if (changed) *changed = false;
    Normalize(next);
    const UISurfaceInteractionState kEmpty;
    const auto found = impl_->surfaces.find(key);
    const UISurfaceInteractionState& current =
        found == impl_->surfaces.end() ? kEmpty : found->second;

    if (FullyEqual(current, next)) return true;

    const bool visible = VisiblyDiffers(current, next);
    if (visible) {
        // 보이는 상태를 게시하기 **전에** 의미 세대를 올린다. 올리지 않고 발행하면
        // warm 빠른 경로가 새 hover/focus에 옛 이름을 붙인다. 소진이면 이전 상태가
        // 그대로 보이고 캐시가 통째로 우회된다.
        if (!UIRuntimeInvalidationClock::Advance(
                UIRuntimeGenerationKind::SemanticDirty)) {
            if (sink) {
                molga::text::TextDiagnostic diagnostic;
                diagnostic.code = molga::text::TextDiagnosticCode::LayoutInvalid;
                diagnostic.severity = molga::text::TextSeverity::Blocker;
                diagnostic.subsystem = subsystem ? subsystem : "ui.interaction";
                diagnostic.message =
                    "the UI semantic generation is exhausted; this interaction "
                    "state is not published and snapshot caching is bypassed";
                diagnostic.remediation =
                    "restart the process to reset the runtime generation clocks";
                diagnostic.sceneObjectId = sceneObjectId;
                sink->Report(std::move(diagnostic));
            }
            return false;
        }
    }

    if (IsEmpty(next)) {
        if (found != impl_->surfaces.end()) impl_->surfaces.erase(found);
    } else if (found != impl_->surfaces.end()) {
        found->second = std::move(next);
    } else {
        impl_->surfaces.emplace(key, std::move(next));
    }
    if (changed) *changed = visible;
    return true;
}

bool UIInteractionStateTable::ClearIdentityEverywhere(
    const UIRuntimeTargetIdentity& id, molga::text::TextDiagnosticSink* sink) {
    if (!id) return true;
    // Apply가 표를 바꾸므로 키를 먼저 모은다.
    std::vector<UISurfaceKey> keys;
    for (const auto& entry : impl_->surfaces) {
        if (entry.first.worldGeneration == id.worldGeneration) {
            keys.push_back(entry.first);
        }
    }
    std::sort(keys.begin(), keys.end(),
              [](const UISurfaceKey& a, const UISurfaceKey& b) {
                  return a.windowId < b.windowId;
              });
    bool ok = true;
    for (const UISurfaceKey& key : keys) {
        UISurfaceInteractionState next = SurfaceOrEmpty(key);
        if (next.focused == id) next.focused.reset();
        if (next.pointerCapture == id) next.pointerCapture.reset();
        if (next.hovered == id) next.hovered.reset();
        next.buttons.erase(id);
        ok = Apply(key, std::move(next), sink, "ui.interaction", id.objectId) &&
             ok;
    }
    return ok;
}

void UIInteractionStateTable::OnWorldReleased(std::uint64_t worldGeneration) {
    if (worldGeneration == 0) return;
    for (auto it = impl_->surfaces.begin(); it != impl_->surfaces.end();) {
        it = it->first.worldGeneration == worldGeneration
                 ? impl_->surfaces.erase(it)
                 : std::next(it);
    }
}

std::size_t UIInteractionStateTable::SurfaceCountForWorld(
    std::uint64_t worldGeneration) const noexcept {
    std::size_t total = 0;
    for (const auto& entry : impl_->surfaces) {
        if (entry.first.worldGeneration == worldGeneration) ++total;
    }
    return total;
}

std::vector<UISurfaceKey> UIInteractionStateTable::SurfacesForWindow(
    molga::WindowId windowId) const {
    std::vector<UISurfaceKey> keys;
    for (const auto& entry : impl_->surfaces) {
        if (entry.first.windowId == windowId) keys.push_back(entry.first);
    }
    std::sort(keys.begin(), keys.end(),
              [](const UISurfaceKey& a, const UISurfaceKey& b) {
                  return a.worldGeneration < b.worldGeneration;
              });
    return keys;
}

}  // namespace molga::ui
