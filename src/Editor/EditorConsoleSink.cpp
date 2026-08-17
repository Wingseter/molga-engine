#include "Editor/EditorConsoleSink.h"

namespace molga {

void EditorConsoleSink::Write(const Log::LogMessage& m) {
    std::lock_guard<std::mutex> lock(mutex_);
    pending_.push_back(m);
}

std::vector<Log::LogMessage> EditorConsoleSink::Drain() {
    std::vector<Log::LogMessage> out;
    std::lock_guard<std::mutex> lock(mutex_);
    out.swap(pending_);
    return out;
}

} // namespace molga
