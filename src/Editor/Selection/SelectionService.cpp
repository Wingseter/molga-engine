#include "Editor/Selection/SelectionService.h"
#include <algorithm>

namespace molga {

void SelectionService::Select(unsigned int id, SelectionSource source) {
    if (id == 0) { Clear(source); return; }
    if (ids_.size() == 1 && ids_[0] == id && primary_ == id) {
        return;  // 같은 단일 선택 — 재알림 없음
    }
    ids_.assign(1, id);
    primary_ = id;
    lastSource_ = source;
    Notify(source);
}

void SelectionService::Clear(SelectionSource source) {
    if (ids_.empty() && primary_ == 0) return;
    ids_.clear();
    primary_ = 0;
    lastSource_ = source;
    Notify(source);
}

bool SelectionService::IsSelected(unsigned int id) const {
    return std::find(ids_.begin(), ids_.end(), id) != ids_.end();
}

void SelectionService::Rebind(const std::function<bool(unsigned int)>& alive) {
    ids_.erase(std::remove_if(ids_.begin(), ids_.end(),
                              [&](unsigned int id) { return !alive(id); }),
               ids_.end());
    if (primary_ != 0 && !alive(primary_)) {
        primary_ = ids_.empty() ? 0u : ids_.front();
    }
    if (locked_ && !alive(lockedId_)) UnlockInspector();
}

void SelectionService::LockInspector(unsigned int id) {
    locked_ = (id != 0);
    lockedId_ = id;
}

void SelectionService::UnlockInspector() {
    locked_ = false;
    lockedId_ = 0;
}

void SelectionService::Notify(SelectionSource source) {
    for (auto& l : listeners_) l(*this, source);
}

} // namespace molga
