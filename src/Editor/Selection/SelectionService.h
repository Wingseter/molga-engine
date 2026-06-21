#pragma once

#include <cstdint>
#include <functional>
#include <vector>

namespace molga {

// 선택 변경을 일으킨 주체. 패널이 자기 자신 이벤트를 무시할 때 사용.
enum class SelectionSource { Code, SceneView, Hierarchy, Inspector };

// 에디터 전역 선택 모델. raw pointer가 아니라 GameObject ID(0 = 없음)를 소유한다.
// 단일 선택으로 시작하되 SelectedIds()는 다중 선택을 받을 수 있는 형태로 노출한다.
class SelectionService {
public:
    using Listener = std::function<void(const SelectionService&, SelectionSource)>;

    // 단일 선택으로 교체. id == 0 이면 Clear와 동일.
    void Select(unsigned int id, SelectionSource source);
    void Clear(SelectionSource source);

    bool HasSelection() const { return !ids_.empty(); }
    unsigned int PrimaryId() const { return primary_; }
    bool IsSelected(unsigned int id) const;
    const std::vector<unsigned int>& SelectedIds() const { return ids_; }
    SelectionSource LastSource() const { return lastSource_; }

    // World 교체(Play/Stop) 후 호출: alive(id)가 false인 선택을 떨군다.
    void Rebind(const std::function<bool(unsigned int)>& alive);

    // 잠긴 Inspector 대상. 잠금 동안 InspectorTargetId는 primary 대신 잠금 id를 반환.
    void LockInspector(unsigned int id);
    void UnlockInspector();
    unsigned int InspectorTargetId() const { return locked_ ? lockedId_ : primary_; }
    bool IsInspectorLocked() const { return locked_; }

    void AddListener(Listener l) { listeners_.push_back(std::move(l)); }

private:
    void Notify(SelectionSource source);

    std::vector<unsigned int> ids_;          // 선택된 ID들(현재는 0..1개)
    unsigned int primary_ = 0;               // 0 = 없음
    SelectionSource lastSource_ = SelectionSource::Code;
    bool locked_ = false;
    unsigned int lockedId_ = 0;
    std::vector<Listener> listeners_;
};

} // namespace molga
