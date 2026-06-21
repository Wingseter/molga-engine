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
    tasks_[id] = Task{name, category, TaskState::Running, 0.0f};
    return id;
}

void EditorTaskService::Update(TaskId id, float progress, const std::string& line) {
    TaskCategory cat;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = tasks_.find(id);
        if (it == tasks_.end()) return;
        it->second.progress = progress;
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
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = tasks_.find(id);
    if (it == tasks_.end()) return;
    it->second.state = state;
    if (state == TaskState::Succeeded) it->second.progress = 1.0f;
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

std::vector<EditorTaskService::TaskView> EditorTaskService::Snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<TaskView> out;
    for (auto& [id, t] : tasks_)
        out.push_back({id, t.name, t.category, t.state, t.progress});
    return out;
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
