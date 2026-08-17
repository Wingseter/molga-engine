#include "Editor/Selection/SelectionService.h"
#include <algorithm>

namespace molga {

namespace {

std::vector<unsigned int> OrderedUnique(const std::vector<unsigned int>& ids) {
    std::vector<unsigned int> result;
    result.reserve(ids.size());
    for (unsigned int id : ids) {
        if (id != 0 && std::find(result.begin(), result.end(), id) == result.end()) {
            result.push_back(id);
        }
    }
    return result;
}

} // namespace

void SelectionService::Select(unsigned int id, SelectionSource source) {
    if (id == 0) { Clear(source); return; }
    if (ids_.size() == 1 && ids_[0] == id && primary_ == id &&
        rangeAnchor_ == id) {
        return;  // 같은 단일 선택 — 재알림 없음
    }
    ids_.assign(1, id);
    primary_ = id;
    rangeAnchor_ = id;
    lastSource_ = source;
    Notify(source);
}

void SelectionService::SelectMany(const std::vector<unsigned int>& ids,
                                  unsigned int primary,
                                  SelectionSource source) {
    std::vector<unsigned int> next = OrderedUnique(ids);
    if (next.empty()) { Clear(source); return; }
    if (std::find(next.begin(), next.end(), primary) == next.end()) {
        primary = next.back();
    }
    const unsigned int nextAnchor = primary;
    if (ids_ == next && primary_ == primary && rangeAnchor_ == nextAnchor) return;
    ids_ = std::move(next);
    primary_ = primary;
    rangeAnchor_ = nextAnchor;
    lastSource_ = source;
    Notify(source);
}

void SelectionService::RestoreState(const SelectionState& state,
                                    SelectionSource source) {
    SelectionState next = state;
    next.selectedIds = OrderedUnique(next.selectedIds);
    if (std::find(next.selectedIds.begin(), next.selectedIds.end(), next.primaryId) ==
        next.selectedIds.end()) {
        next.primaryId = next.selectedIds.empty() ? 0u : next.selectedIds.back();
    }
    if (next.rangeAnchor == 0 && !next.selectedIds.empty()) {
        next.rangeAnchor = next.primaryId;
    }
    next.inspectorTargetIds = OrderedUnique(next.inspectorTargetIds);
    if (!next.inspectorLocked || next.inspectorTargetIds.empty()) {
        next.inspectorLocked = false;
        next.inspectorTargetIds.clear();
        next.inspectorPrimaryId = 0;
    } else if (std::find(next.inspectorTargetIds.begin(), next.inspectorTargetIds.end(),
                         next.inspectorPrimaryId) == next.inspectorTargetIds.end()) {
        next.inspectorPrimaryId = next.inspectorTargetIds.back();
    }
    if (State() == next) return;
    ids_ = std::move(next.selectedIds);
    primary_ = next.primaryId;
    rangeAnchor_ = next.rangeAnchor;
    locked_ = next.inspectorLocked;
    lockedIds_ = std::move(next.inspectorTargetIds);
    lockedId_ = next.inspectorPrimaryId;
    lastSource_ = source;
    Notify(source);
}

void SelectionService::Add(unsigned int id, SelectionSource source) {
    if (id == 0) return;
    const auto found = std::find(ids_.begin(), ids_.end(), id);
    bool changed = found == ids_.end();
    if (changed) ids_.push_back(id);
    changed = changed || primary_ != id || rangeAnchor_ != id;
    if (!changed) return;
    primary_ = id;
    rangeAnchor_ = id;
    lastSource_ = source;
    Notify(source);
}

void SelectionService::Toggle(unsigned int id, SelectionSource source) {
    if (id == 0) return;
    const auto found = std::find(ids_.begin(), ids_.end(), id);
    if (found == ids_.end()) {
        Add(id, source);
        return;
    }
    ids_.erase(found);
    if (primary_ == id) primary_ = ids_.empty() ? 0u : ids_.back();
    rangeAnchor_ = id;
    lastSource_ = source;
    Notify(source);
}

void SelectionService::SelectRange(const std::vector<unsigned int>& visibleOrder,
                                   unsigned int id, bool additive,
                                   SelectionSource source) {
    if (id == 0) return;
    auto target = std::find(visibleOrder.begin(), visibleOrder.end(), id);
    if (target == visibleOrder.end()) {
        additive ? Add(id, source) : Select(id, source);
        return;
    }
    auto anchor = std::find(visibleOrder.begin(), visibleOrder.end(), rangeAnchor_);
    if (anchor == visibleOrder.end()) anchor = target;
    auto first = anchor < target ? anchor : target;
    auto last = anchor < target ? target : anchor;

    std::vector<unsigned int> next = additive ? ids_ : std::vector<unsigned int>{};
    for (auto it = first; it != last + 1; ++it) {
        if (*it != 0 && std::find(next.begin(), next.end(), *it) == next.end()) {
            next.push_back(*it);
        }
    }
    if (next.empty()) { Clear(source); return; }
    const bool changed = ids_ != next || primary_ != id;
    ids_ = std::move(next);
    primary_ = id;
    // Shift range extension keeps the original anchor stable.
    if (rangeAnchor_ == 0) rangeAnchor_ = id;
    if (!changed) return;
    lastSource_ = source;
    Notify(source);
}

void SelectionService::Clear(SelectionSource source) {
    if (ids_.empty() && primary_ == 0) return;
    ids_.clear();
    primary_ = 0;
    rangeAnchor_ = 0;
    lastSource_ = source;
    Notify(source);
}

bool SelectionService::IsSelected(unsigned int id) const {
    return std::find(ids_.begin(), ids_.end(), id) != ids_.end();
}

void SelectionService::Rebind(const std::function<bool(unsigned int)>& alive) {
    const SelectionState before = State();
    ids_.erase(std::remove_if(ids_.begin(), ids_.end(),
                              [&](unsigned int id) { return !alive(id); }),
               ids_.end());
    if (primary_ != 0 && !alive(primary_)) {
        primary_ = ids_.empty() ? 0u : ids_.back();
    }
    if (rangeAnchor_ != 0 && !alive(rangeAnchor_)) {
        rangeAnchor_ = primary_;
    }
    if (locked_) {
        lockedIds_.erase(std::remove_if(lockedIds_.begin(), lockedIds_.end(),
                                        [&](unsigned int id) { return !alive(id); }),
                         lockedIds_.end());
        if (lockedId_ != 0 && !alive(lockedId_)) {
            lockedId_ = lockedIds_.empty() ? 0u : lockedIds_.back();
        }
        if (lockedIds_.empty()) {
            locked_ = false;
            lockedId_ = 0;
        }
    }
    if (before != State()) {
        lastSource_ = SelectionSource::Code;
        Notify(SelectionSource::Code);
    }
}

void SelectionService::LockInspector() {
    if (ids_.empty()) return;
    if (locked_ && lockedIds_ == ids_ && lockedId_ == primary_) return;
    locked_ = true;
    lockedIds_ = ids_;
    lockedId_ = primary_;
    lastSource_ = SelectionSource::Inspector;
    Notify(SelectionSource::Inspector);
}

void SelectionService::LockInspector(unsigned int id) {
    if (id == 0) { UnlockInspector(); return; }
    if (IsSelected(id)) {
        LockInspector();
        return;
    }
    if (locked_ && lockedIds_.size() == 1 && lockedId_ == id) return;
    locked_ = true;
    lockedIds_.assign(1, id);
    lockedId_ = id;
    lastSource_ = SelectionSource::Inspector;
    Notify(SelectionSource::Inspector);
}

void SelectionService::UnlockInspector() {
    if (!locked_) return;
    locked_ = false;
    lockedId_ = 0;
    lockedIds_.clear();
    lastSource_ = SelectionSource::Inspector;
    Notify(SelectionSource::Inspector);
}

void SelectionService::Notify(SelectionSource source) {
    for (auto& l : listeners_) l(*this, source);
}

} // namespace molga
