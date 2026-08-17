#pragma once

#include "Common/LogMessage.h"

namespace Log {

// 하나의 로그 목적지. 구현은 자기 내부 동기화를 책임지며,
// ImGui/Editor/Runtime 어떤 것도 접근하지 않는다. (thread-safe sink 계약)
class ILogSink {
public:
    virtual ~ILogSink() = default;
    virtual void Write(const LogMessage& m) = 0;
};

} // namespace Log
