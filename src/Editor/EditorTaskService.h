#pragma once

#include "Common/Log.h"
#include "Common/LogMessage.h"
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <vector>
#include <atomic>
#include <unordered_map>

namespace molga {

using TaskId = std::uint64_t;

enum class TaskCategory { Build, ScriptCompile, ScriptReload, Import, Package, ShaderReload };
enum class TaskState    { Queued, Running, Succeeded, Failed, Cancelled };

// 메인 스레드에 건네는 불변 스냅샷(직접 ImGui를 건드리지 않는다).
struct TaskInfo {
    TaskId       id = 0;
    std::string  name;
    TaskCategory category = TaskCategory::Build;
    TaskState    state    = TaskState::Queued;
    float        progress = 0.0f;     // 0..1, 불확정이면 -1
    std::string  log;                 // 누적 stdout/stderr
    std::string  result;              // 결과 payload(예: 라이브러리 경로)
};

// 스레드 안전 task 레지스트리. 워커 스레드가 상태/로그를 갱신하고,
// 메인 스레드(ImGui)는 Get()/Snapshot()으로 복사본만 읽는다.
class EditorTaskService {
public:
    using TaskView = TaskInfo;

    TaskId Begin(const std::string& name, TaskCategory category);
    void   MarkRunning(TaskId id);
    void   SetProgress(TaskId id, float p);
    void   AppendLog(TaskId id, const std::string& chunk);   // 워커 스레드 호출 OK
    void   SetResult(TaskId id, const std::string& payload);
    void   Complete(TaskId id, TaskState terminal);

    void   RequestCancel(TaskId id);
    bool   IsCancelRequested(TaskId id) const;

    TaskInfo               Get(TaskId id) const;
    std::vector<TaskInfo>  Snapshot() const;                 // UI 패널용

    // 하위 호환성용 메서드
    void      Update(TaskId id, float progress, const std::string& line);
    void      Finish(TaskId id, TaskState state);
    TaskState GetState(TaskId id) const;
    float     GetProgress(TaskId id) const;

    // 컴파일러 출력 한 줄에서 path:line 진단을 추출(gcc/clang/MSVC). 실패 시 false.
    static bool ParseDiagnostic(const std::string& line, Log::LogMessage& out);

private:
    static Log::LogContext ContextFor(TaskCategory c);

    struct Task {
        TaskId       id = 0;
        std::string  name;
        TaskCategory category;
        TaskState    state = TaskState::Queued;
        float        progress = 0.0f;
        std::string  log;
        std::string  result;
    };
    mutable std::mutex                  mutex_;
    std::map<TaskId, Task>              tasks_;
    std::unordered_map<TaskId, bool>    cancel_;
    TaskId                              nextId_ = 1;
};

} // namespace molga
