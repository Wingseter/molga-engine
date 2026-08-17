#pragma once

#include "Common/LogSink.h"
#include <cstddef>
#include <deque>
#include <mutex>
#include <vector>

namespace Log {

// 고정 용량의 thread-safe 링버퍼. 용량 초과 시 가장 오래된 메시지를 버린다.
// 메모리 상한을 보장한다(완료 기준: 100k 입력에도 상한 유지).
class RingBufferSink : public ILogSink {
public:
    explicit RingBufferSink(std::size_t capacity) : capacity_(capacity == 0 ? 1 : capacity) {}

    void Write(const LogMessage& m) override {
        std::lock_guard<std::mutex> lock(mutex_);
        buffer_.push_back(m);
        if (buffer_.size() > capacity_) buffer_.pop_front();
    }

    std::vector<LogMessage> Snapshot() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return std::vector<LogMessage>(buffer_.begin(), buffer_.end());
    }

    void Clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        buffer_.clear();
    }

private:
    mutable std::mutex     mutex_;
    std::deque<LogMessage> buffer_;
    std::size_t            capacity_;
};

} // namespace Log
