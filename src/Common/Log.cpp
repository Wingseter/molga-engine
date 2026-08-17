#include "Log.h"
#include "LogSink.h"
#include <atomic>
#include <chrono>
#include <mutex>
#include <vector>
#include <algorithm>

namespace Log {
namespace {
std::mutex                              g_mutex;
std::vector<std::shared_ptr<ILogSink>>  g_sinks;
std::atomic<std::uint64_t>              g_sequence{0};

std::int64_t NowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}
} // namespace

void Emit(const LogMessage& in) {
    LogMessage m = in;
    m.sequence    = ++g_sequence;
    if (m.timestampMs == 0) m.timestampMs = NowMs();
    if (m.threadId == std::thread::id{}) m.threadId = std::this_thread::get_id();

    // sink 목록을 lock 하에 복사한 뒤 lock 밖에서 Write(개별 sink가 자기 동기화 책임).
    std::vector<std::shared_ptr<ILogSink>> snapshot;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        snapshot = g_sinks;
    }
    for (auto& s : snapshot) {
        if (s) s->Write(m);
    }
}

void AddSink(std::shared_ptr<ILogSink> sink) {
    if (!sink) return;
    std::lock_guard<std::mutex> lock(g_mutex);
    g_sinks.push_back(std::move(sink));
}

void RemoveSink(const std::shared_ptr<ILogSink>& sink) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_sinks.erase(std::remove(g_sinks.begin(), g_sinks.end(), sink), g_sinks.end());
}

void ClearSinks() {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_sinks.clear();
}

static void EmitLegacy(Severity sev, const std::string& tag, const std::string& msg) {
    LogMessage m;
    m.severity = sev;
    m.category = tag;
    m.message  = msg;
    m.context  = LogContext::Editor;  // 기존 호출처는 대부분 에디터 측; 세분화는 점진적.
    Emit(m);
}

void Info (const std::string& tag, const std::string& msg) { EmitLegacy(Severity::Info,    tag, msg); }
void Warn (const std::string& tag, const std::string& msg) { EmitLegacy(Severity::Warning, tag, msg); }
void Error(const std::string& tag, const std::string& msg) { EmitLegacy(Severity::Error,   tag, msg); }

} // namespace Log
