#include "FileSink.h"

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

FileSink::FileSink(const std::string& path) : out_(path, std::ios::app) {}

void FileSink::Write(const LogMessage& m) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!out_.is_open()) return;
    out_ << m.sequence << "\t[" << SevTag(m.severity) << "]\t[" << m.category << "]\t"
         << m.message;
    if (!m.externalPath.empty()) {
        out_ << "\t(" << m.externalPath;
        if (m.externalLine > 0) out_ << ":" << m.externalLine;
        out_ << ")";
    }
    out_ << "\n";
}

void FileSink::Flush() {
    std::lock_guard<std::mutex> lock(mutex_);
    out_.flush();
}

} // namespace Log
