#pragma once

#include <chrono>

namespace molga {

// 단조 ns 시계. 테스트는 외부 카운터(clock 포인터)를 주입해 실시간 의존을 없앤다.
inline long long NowNanos() {
    using namespace std::chrono;
    return duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
}

} // namespace molga
