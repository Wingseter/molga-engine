#pragma once

#include "Common/LogSink.h"
#include "Common/LogMessage.h"
#include <mutex>
#include <vector>

namespace molga {

// thread-safe 큐 sink. Write는 어느 thread에서든 메시지를 큐에 복사 push만 한다.
// ImGui/Editor 컨테이너에 절대 접근하지 않는다(thread-safe sink 계약).
// 표시 모델은 ConsoleWindow가 소유하며, main thread에서 Drain()으로 가져간다.
class EditorConsoleSink : public Log::ILogSink {
public:
    void Write(const Log::LogMessage& m) override;
    std::vector<Log::LogMessage> Drain();   // main thread 전용. 큐를 비우고 반환.
private:
    std::mutex                    mutex_;
    std::vector<Log::LogMessage>  pending_;
};

} // namespace molga
