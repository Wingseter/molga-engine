#include "SmokeReportSink.h"
#include <fstream>

namespace Log {

void SmokeReportSink::Write(const LogMessage& m) {
    if (m.severity < Severity::Error) return;     // 실패만 수집
    std::lock_guard<std::mutex> lock(mutex_);
    failures_.push_back(m);
}

void SmokeReportSink::Flush() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::ofstream out(path_, std::ios::app);
    if (!out.is_open()) return;
    for (const auto& m : failures_) {
        out << "[" << m.category << "] " << m.message;
        if (!m.externalPath.empty()) {
            out << " (" << m.externalPath;
            if (m.externalLine > 0) out << ":" << m.externalLine;
            out << ")";
        }
        out << "\n";
    }
}

bool SmokeReportSink::HasFailures() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return !failures_.empty();
}

} // namespace Log
