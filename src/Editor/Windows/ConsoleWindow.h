#pragma once

#include "Editor/Windows/EditorWindow.h"
#include "Editor/EditorConsoleSink.h"
#include "Common/LogMessage.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

class ConsoleWindow : public EditorWindow {
public:
    ConsoleWindow();

    // 표시 모델만 렌더한다. sink drain은 Editor::Update의 PumpPending 한 곳에서 한다.
    void OnGUI() override;

    // Visibility와 무관하게 main thread에서 호출한다. Error Pause는 콘솔
    // 패널이 닫혀 있어도 같은 의미를 가져야 한다.
    void PumpPending();

    // 등록된 콘솔 sink(부트스트랩이 Log::AddSink로 등록한 동일 인스턴스).
    std::shared_ptr<molga::EditorConsoleSink> Sink() const { return sink_; }

    // source 링크 더블클릭 시 호출. 기본은 VSCodeIntegration; 주입 가능(테스트/대체).
    void SetOpenFileHandler(std::function<void(const std::string&, int)> fn) {
        openFile_ = std::move(fn);
    }

    void RequestClear() { rows_.clear(); selectedRow_ = -1; }

    bool IsClearOnPlay() const { return clearOnPlay_; }
    bool IsClearOnBuild() const { return clearOnBuild_; }
    bool IsClearOnRecompile() const { return clearOnRecompile_; }
    bool IsErrorPause() const { return errorPause_; }
    void SetErrorPause(bool enabled) { errorPause_ = enabled; }

private:
    struct Row {
        Log::LogMessage msg;
        std::uint32_t   repeat = 1;   // collapse 시 동일 메시지 누적 횟수
    };

    bool PassesFilter(const Log::LogMessage& m) const;

    std::shared_ptr<molga::EditorConsoleSink> sink_;
    std::vector<Row> rows_;           // 표시 모델(이 패널이 단독 소유; main thread)

    // 툴바 상태
    bool clearOnPlay_      = false;
    bool clearOnBuild_     = false;
    bool clearOnRecompile_ = false;
    bool errorPause_       = false;   // Error 발생 시 Play 일시정지(UX-1 PlayMode 연동 지점)
    bool collapse_         = true;

    // 필터 상태
    char searchBuf_[128] = {0};
    bool showInfo_ = true, showWarn_ = true, showError_ = true;
    int  contextMask_ = -1;           // -1 = 전부; LogContext 비트마스크
    int  selectedRow_ = -1;           // detail pane 대상

    std::function<void(const std::string&, int)> openFile_;
};
