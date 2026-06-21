#pragma once

#include "Common/Log.h"
#include "Common/LogMessage.h"
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace molga {

using TaskId = std::uint64_t;

enum class TaskCategory { Build, ScriptCompile, ScriptReload, Import, Package, ShaderReload };
enum class TaskState    { Queued, Running, Succeeded, Failed, Cancelled };

// 느린 작업의 상태/진행/로그 표면. 출력 라인은 Log::Emit으로 Console에 라우팅한다.
// (전체 비동기 수명주기·취소·last-good 라이브러리는 UX-4가 확장)
class EditorTaskService {
public:
    TaskId Begin(const std::string& name, TaskCategory category);
    void   Update(TaskId id, float progress, const std::string& line);
    void   Finish(TaskId id, TaskState state);

    TaskState GetState(TaskId id) const;
    float     GetProgress(TaskId id) const;

    struct TaskView {
        TaskId       id;
        std::string  name;
        TaskCategory category;
        TaskState    state;
        float        progress;
    };
    std::vector<TaskView> Snapshot() const;   // 작업 상태 UI가 읽음

    // 컴파일러 출력 한 줄에서 path:line 진단을 추출(gcc/clang/MSVC). 실패 시 false.
    static bool ParseDiagnostic(const std::string& line, Log::LogMessage& out);

private:
    static Log::LogContext ContextFor(TaskCategory c);

    struct Task {
        std::string  name;
        TaskCategory category;
        TaskState    state = TaskState::Queued;
        float        progress = 0.0f;
    };
    mutable std::mutex      mutex_;
    std::map<TaskId, Task>  tasks_;
    TaskId                  nextId_ = 1;
};

} // namespace molga
