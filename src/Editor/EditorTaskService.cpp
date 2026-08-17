#include "Editor/EditorTaskService.h"
#include <regex>

namespace molga {

Log::LogContext EditorTaskService::ContextFor(TaskCategory c) {
    switch (c) {
        case TaskCategory::Build:
        case TaskCategory::Package:      return Log::LogContext::Build;
        case TaskCategory::ScriptCompile:
        case TaskCategory::ScriptReload: return Log::LogContext::ScriptCompiler;
        case TaskCategory::Import:
        case TaskCategory::ShaderReload: return Log::LogContext::Importer;
    }
    return Log::LogContext::Editor;
}

TaskId EditorTaskService::Begin(const std::string& name, TaskCategory category) {
    std::lock_guard<std::mutex> lock(mutex_);
    TaskId id = nextId_++;
    tasks_[id] = Task{id, name, category, TaskState::Queued, 0.0f, "", ""};
    return id;
}

void EditorTaskService::MarkRunning(TaskId id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = tasks_.find(id);
    if (it != tasks_.end()) {
        it->second.state = TaskState::Running;
    }
}

void EditorTaskService::SetProgress(TaskId id, float p) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = tasks_.find(id);
    if (it != tasks_.end()) {
        it->second.progress = p;
    }
}

void EditorTaskService::AppendLog(TaskId id, const std::string& chunk) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = tasks_.find(id);
    if (it != tasks_.end()) {
        it->second.log += chunk;
    }
}

void EditorTaskService::SetResult(TaskId id, const std::string& payload) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = tasks_.find(id);
    if (it != tasks_.end()) {
        it->second.result = payload;
    }
}

void EditorTaskService::Complete(TaskId id, TaskState terminal) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = tasks_.find(id);
    if (it != tasks_.end()) {
        it->second.state = terminal;
        if (terminal == TaskState::Succeeded) {
            it->second.progress = 1.0f;
        }
    }
}

void EditorTaskService::RequestCancel(TaskId id) {
    std::lock_guard<std::mutex> lock(mutex_);
    cancel_[id] = true;
    auto it = tasks_.find(id);
    if (it != tasks_.end() && it->second.state == TaskState::Queued) {
        it->second.state = TaskState::Cancelled;
    }
}

bool EditorTaskService::IsCancelRequested(TaskId id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = cancel_.find(id);
    return it != cancel_.end() && it->second;
}

TaskInfo EditorTaskService::Get(TaskId id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = tasks_.find(id);
    if (it == tasks_.end()) {
        return TaskInfo{};
    }
    const auto& t = it->second;
    return TaskInfo{t.id, t.name, t.category, t.state, t.progress, t.log, t.result};
}

std::vector<TaskInfo> EditorTaskService::Snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<TaskInfo> out;
    for (auto& [id, t] : tasks_) {
        out.push_back({t.id, t.name, t.category, t.state, t.progress, t.log, t.result});
    }
    return out;
}

void EditorTaskService::Update(TaskId id, float progress, const std::string& line) {
    TaskCategory cat;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = tasks_.find(id);
        if (it == tasks_.end()) return;
        it->second.progress = progress;
        if (!line.empty()) {
            it->second.log += line;
            if (line.back() != '\n') {
                it->second.log += "\n";
            }
        }
        cat = it->second.category;
    }
    if (line.empty()) return;

    // 진단 줄이면 구조화(path:line, severity), 아니면 일반 진행 메시지.
    Log::LogMessage m;
    if (!ParseDiagnostic(line, m)) {
        m.severity = Log::Severity::Info;
        m.message  = line;
    }
    m.context  = ContextFor(cat);
    m.category = (cat == TaskCategory::Build || cat == TaskCategory::Package)
                     ? "Build" : "ScriptCompiler";
    Log::Emit(m);
}

void EditorTaskService::Finish(TaskId id, TaskState state) {
    Complete(id, state);
}

TaskState EditorTaskService::GetState(TaskId id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = tasks_.find(id);
    return it == tasks_.end() ? TaskState::Cancelled : it->second.state;
}

float EditorTaskService::GetProgress(TaskId id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = tasks_.find(id);
    return it == tasks_.end() ? 0.0f : it->second.progress;
}

bool EditorTaskService::ParseDiagnostic(const std::string& line, Log::LogMessage& out) {
    // gcc/clang: path:line:col: error|warning: msg   /   MSVC: path(line): error Cxxxx: msg
    static const std::regex kGcc(R"(^(.*?):(\d+):(?:\d+:)?\s*(error|warning|note):\s*(.*)$)");
    static const std::regex kMsvc(R"(^(.*?)\((\d+)\):\s*(error|warning)\s+\w+:\s*(.*)$)");
    std::smatch mt;
    const std::regex* used = nullptr;
    if (std::regex_match(line, mt, kGcc))  used = &kGcc;
    else if (std::regex_match(line, mt, kMsvc)) used = &kMsvc;
    if (!used) return false;

    out.externalPath = mt[1].str();
    out.externalLine = std::stoi(mt[2].str());
    const std::string kind = mt[3].str();
    out.severity = (kind == "error") ? Log::Severity::Error
                 : (kind == "warning") ? Log::Severity::Warning
                 : Log::Severity::Info;
    out.message = mt[4].str();
    return true;
}

} // namespace molga
