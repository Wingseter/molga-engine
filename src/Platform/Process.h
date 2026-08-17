#pragma once
#include <string>
#include <functional>

namespace molga {

struct ProcessResult {
    int  exitCode  = 0;
    bool cancelled = false;
};

// 외부 프로세스를 줄단위로 실행하는 심. 테스트는 Fake로 주입한다.
class IProcessRunner {
public:
    virtual ~IProcessRunner() = default;
    // onLine: stdout/stderr 한 줄마다 호출(워커 스레드에서). isCancelled가
    // true를 반환하면 가능한 한 빨리 중단한다.
    virtual ProcessResult Run(const std::string& command,
                              const std::string& workdir,
                              const std::function<void(const std::string&)>& onLine,
                              const std::function<bool()>& isCancelled) = 0;
};

// 실제 popen/_popen 기반 러너(EDITOR_SOURCES).
class SystemProcessRunner : public IProcessRunner {
public:
    ProcessResult Run(const std::string& command,
                      const std::string& workdir,
                      const std::function<void(const std::string&)>& onLine,
                      const std::function<bool()>& isCancelled) override;
};

} // namespace molga
