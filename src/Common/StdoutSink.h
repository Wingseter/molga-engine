#pragma once

#include "Common/LogSink.h"
#include <mutex>

namespace Log {

// 기존 동작 보존: Info/Warning은 stdout, Error/Fatal은 stderr.
// 한 줄 형식: "[category] [SEV] message (path:line)" — CI/헤드리스에서도 동일.
class StdoutSink : public ILogSink {
public:
    void Write(const LogMessage& m) override;
private:
    std::mutex mutex_;  // 인터리브 방지
};

} // namespace Log
