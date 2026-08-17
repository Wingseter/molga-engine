#include "ScriptReloadService.h"

namespace molga {

ReloadOutcome ScriptReloadService::PerformReload(const std::string& path) {
    std::string err;
    if (!lib_->Validate(path, err)) {
        return ReloadOutcome::ValidationFailed;   // last-good 유지(Swap 호출 안 함)
    }
    lib_->Swap(path);
    return ReloadOutcome::Reloaded;
}

ReloadOutcome ScriptReloadService::PumpPendingReload(bool isEditMode) {
    if (!hasPending_)  return ReloadOutcome::Idle;
    if (!isEditMode)   return ReloadOutcome::Deferred;   // Play 중이면 미룬다
    std::string path = pending_;
    hasPending_ = false;
    pending_.clear();
    return PerformReload(path);
}

} // namespace molga
