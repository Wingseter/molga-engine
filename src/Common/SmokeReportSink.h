#pragma once

#include "Common/LogSink.h"
#include <mutex>
#include <string>
#include <vector>

namespace Log {

// Error/Fatal 메시지만 모아 smoke report 파일에 기록한다.
// StdoutSink가 동일 메시지를 stdout에 내보내므로 stdout↔report 파리티가 성립한다.
class SmokeReportSink : public ILogSink {
public:
    explicit SmokeReportSink(std::string reportPath) : path_(std::move(reportPath)) {}
    void Write(const LogMessage& m) override;
    void Flush();   // 누적된 실패를 report 파일에 기록
    bool HasFailures() const;
private:
    mutable std::mutex     mutex_;
    std::string            path_;
    std::vector<LogMessage> failures_;
};

} // namespace Log
