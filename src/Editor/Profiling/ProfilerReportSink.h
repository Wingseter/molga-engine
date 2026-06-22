#pragma once

#include "Common/Log.h"
#include <string>

namespace molga {

// 타이밍 리포트의 도착지. UX-2가 오면 Console/EditorTaskService 구현으로 교체한다.
class IProfilerReportSink {
public:
    virtual ~IProfilerReportSink() = default;
    // label = 단계/에셋 이름, ms = 소요 시간, detail = 선택 부가정보(서브시스템 등).
    virtual void ReportTiming(const std::string& label, double ms,
                              const std::string& detail) = 0;
};

// UX-2 부재 시 폴백: 표준 로그로 흘린다.
class LogProfilerReportSink : public IProfilerReportSink {
public:
    void ReportTiming(const std::string& label, double ms,
                      const std::string& detail) override {
        std::string msg = label + ": " + std::to_string(ms) + " ms";
        if (!detail.empty()) msg += " (" + detail + ")";
        Log::Info("Profiler", msg);
    }
};

// 현재 활성 sink. 기본은 Log 폴백. UX-2가 Console sink로 교체.
IProfilerReportSink& ActiveReportSink();
void SetReportSink(IProfilerReportSink* sink);   // nullptr = 폴백으로 복귀

namespace detail {
    inline LogProfilerReportSink& DefaultSink() { static LogProfilerReportSink s; return s; }
    inline IProfilerReportSink*&  CurrentSink() { static IProfilerReportSink* p = nullptr; return p; }
}

inline IProfilerReportSink& ActiveReportSink() {
    return detail::CurrentSink() ? *detail::CurrentSink() : detail::DefaultSink();
}

inline void SetReportSink(IProfilerReportSink* sink) {
    detail::CurrentSink() = sink;
}

} // namespace molga
