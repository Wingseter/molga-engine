#include "StdoutSink.h"
#include <iostream>

namespace Log {

static const char* SevTag(Severity s) {
    switch (s) {
        case Severity::Trace:   return "TRACE";
        case Severity::Info:    return "INFO";
        case Severity::Warning: return "WARN";
        case Severity::Error:   return "ERROR";
        case Severity::Fatal:   return "FATAL";
    }
    return "INFO";
}

void StdoutSink::Write(const LogMessage& m) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::ostream& os = (m.severity >= Severity::Error) ? std::cerr : std::cout;
    os << "[" << m.category << "] [" << SevTag(m.severity) << "] " << m.message;
    if (!m.externalPath.empty()) {
        os << " (" << m.externalPath;
        if (m.externalLine > 0) os << ":" << m.externalLine;
        os << ")";
    }
    os << std::endl;
}

} // namespace Log
