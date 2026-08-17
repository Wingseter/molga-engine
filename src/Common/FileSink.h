#pragma once

#include "Common/LogSink.h"
#include <fstream>
#include <mutex>
#include <string>

namespace Log {

// 세션 전체 로그를 한 줄/메시지로 파일에 기록한다(append). thread-safe.
class FileSink : public ILogSink {
public:
    explicit FileSink(const std::string& path);
    void Write(const LogMessage& m) override;
    void Flush();
private:
    std::mutex    mutex_;
    std::ofstream out_;
};

} // namespace Log
