#pragma once

#include <cstdint>
#include <functional>
#include <vector>

namespace molga {

// 선택 변경을 일으킨 주체. 패널이 자기 자신 이벤트를 무시할 때 사용.
enum class SelectionSource { Code, SceneView, Hierarchy, Inspector };

struct SelectionState {
    std::vector<unsigned int> selectedIds;
    unsigned int primaryId = 0;
    unsigned int rangeAnchor = 0;
    bool inspectorLocked = false;
    std::vector<unsigned int> inspectorTargetIds;
    unsigned int inspectorPrimaryId = 0;

    bool operator==(const SelectionState& other) const {
        return selectedIds == other.selectedIds && primaryId == other.primaryId &&
               rangeAnchor == other.rangeAnchor &&
               inspectorLocked == other.inspectorLocked &&
               inspectorTargetIds == other.inspectorTargetIds &&
               inspectorPrimaryId == other.inspectorPrimaryId;
    }
    bool operator!=(const SelectionState& other) const { return !(*this == other); }
};

// 에디터 전역 선택 모델. raw pointer가 아니라 GameObject ID(0 = 없음)를 소유한다.
// 단일 선택으로 시작하되 SelectedIds()는 다중 선택을 받을 수 있는 형태로 노출한다.
class SelectionService {
public:
    using Listener = std::function<void(const SelectionService&, SelectionSource)>;

    // 단일 선택으로 교체. id == 0 이면 Clear와 동일.
    void Select(unsigned int id, SelectionSource source);
    // ordered unique 집합으로 교체. primary == 0이면 마지막 유효 ID가 primary.
    void SelectMany(const std::vector<unsigned int>& ids, unsigned int primary,
                    SelectionSource source);
    // 기존 선택에 추가하거나 제거한다. 명시적으로 선택된 ID가 primary/anchor가 된다.
    void Add(unsigned int id, SelectionSource source);
    void Toggle(unsigned int id, SelectionSource source);
    // visibleOrder 안의 anchor~id 범위를 교체하거나 기존 선택에 합친다.
    void SelectRange(const std::vector<unsigned int>& visibleOrder, unsigned int id,
                     bool additive, SelectionSource source);
    void Clear(SelectionSource source);

    bool HasSelection() const { return !ids_.empty(); }
    unsigned int PrimaryId() const { return primary_; }
    unsigned int RangeAnchor() const { return rangeAnchor_; }
    bool IsSelected(unsigned int id) const;
    const std::vector<unsigned int>& SelectedIds() const { return ids_; }
    SelectionState State() const {
        return {ids_, primary_, rangeAnchor_, locked_, lockedIds_, lockedId_};
    }
    void RestoreState(const SelectionState& state, SelectionSource source);
    SelectionSource LastSource() const { return lastSource_; }

    // World 교체(Play/Stop) 후 호출: alive(id)가 false인 선택을 떨군다.
    void Rebind(const std::function<bool(unsigned int)>& alive);

    // 잠긴 Inspector 대상 집합. LockInspector()는 현재 선택 전체를 스냅샷한다.
    void LockInspector();
    void LockInspector(unsigned int id);
    void UnlockInspector();
    unsigned int InspectorTargetId() const { return locked_ ? lockedId_ : primary_; }
    const std::vector<unsigned int>& InspectorTargetIds() const {
        return locked_ ? lockedIds_ : ids_;
    }
    bool IsInspectorLocked() const { return locked_; }

    void AddListener(Listener l) { listeners_.push_back(std::move(l)); }

private:
    void Notify(SelectionSource source);

    std::vector<unsigned int> ids_;          // 선택된 ID들(현재는 0..1개)
    unsigned int primary_ = 0;               // 0 = 없음
    unsigned int rangeAnchor_ = 0;
    SelectionSource lastSource_ = SelectionSource::Code;
    bool locked_ = false;
    unsigned int lockedId_ = 0;
    std::vector<unsigned int> lockedIds_;
    std::vector<Listener> listeners_;
};

} // namespace molga
