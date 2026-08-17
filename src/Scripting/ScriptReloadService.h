#pragma once
#include <string>
#include <functional>

namespace molga {

enum class ReloadOutcome { Idle, Deferred, ValidationFailed, Reloaded };

// dlopen/필드 재바인딩을 추상화한 포트(테스트는 Fake로 주입).
class ILibraryPort {
public:
    virtual ~ILibraryPort() = default;
    virtual bool        Validate(const std::string& path, std::string& error) = 0;
    // 검증된 path를 활성으로 채택(직전 라이브러리 unload + 인스턴스 재바인딩).
    virtual void        Swap(const std::string& path) = 0;
    virtual std::string Active() const = 0;
};

// reload 정책 + safe-point 큐. UI/프레임 루프에서 호출한다.
class ScriptReloadService {
public:
    explicit ScriptReloadService(ILibraryPort* lib) : lib_(lib) {}

    // 즉시 검증→스왑 시도(이미 safe-point라고 가정). 테스트/동기 경로용.
    ReloadOutcome PerformReload(const std::string& path);

    // 다음 안전 지점에 수행하도록 큐잉(예: 컴파일 task 성공 콜백에서).
    void RequestReload(const std::string& path) { pending_ = path; hasPending_ = true; }

    // 프레임 끝 safe point에서 호출. Edit 모드가 아니면 Deferred를 돌려준다.
    ReloadOutcome PumpPendingReload(bool isEditMode);

    bool HasPending() const { return hasPending_; }

private:
    ILibraryPort* lib_;
    std::string   pending_;
    bool          hasPending_ = false;
};

} // namespace molga
