#include "Editor/Windows/ConsoleWindow.h"
#include "Editor/EditorConstants.h"
#include "Editor/EditorState.h"
#include "imgui.h"
#include <memory>

ConsoleWindow::ConsoleWindow()
    : EditorWindow(EditorConstants::WIN_CONSOLE),
      sink_(std::make_shared<molga::EditorConsoleSink>()) {}

void ConsoleWindow::PullPending() {
    bool hasError = false;
    for (auto& m : sink_->Drain()) {
        if (m.severity >= Log::Severity::Error) {
            hasError = true;
        }
        if (collapse_ && !rows_.empty()) {
            Row& last = rows_.back();
            if (last.msg.severity == m.severity &&
                last.msg.category == m.category &&
                last.msg.message  == m.message) {
                last.repeat++;                 // 반복 메시지 카운트만 증가
                continue;
            }
        }
        rows_.push_back(Row{m, 1});
    }

    if (hasError && errorPause_ && EditorState::Get().IsPlayMode()) {
        EditorState::Get().Pause();
    }
}

bool ConsoleWindow::PassesFilter(const Log::LogMessage& m) const {
    using Log::Severity;
    if (m.severity == Severity::Info    && !showInfo_)  return false;
    if (m.severity == Severity::Warning && !showWarn_)  return false;
    if (m.severity >= Severity::Error   && !showError_) return false;
    if (contextMask_ != -1 &&
        !(contextMask_ & (1 << static_cast<int>(m.context)))) return false;
    if (searchBuf_[0] != '\0' &&
        m.message.find(searchBuf_) == std::string::npos &&
        m.category.find(searchBuf_) == std::string::npos) return false;
    return true;
}

void ConsoleWindow::OnGUI() {
    if (!IsOpen()) return;
    PullPending();                    // ← main thread에서만 표시 모델 변경

    ImGui::Begin(title.c_str(), nullptr);

    // ── 툴바 ──
    if (ImGui::Button("Clear")) { RequestClear(); }
    ImGui::SameLine(); ImGui::Checkbox("Collapse", &collapse_);
    ImGui::SameLine(); ImGui::Checkbox("On Play", &clearOnPlay_);
    ImGui::SameLine(); ImGui::Checkbox("On Build", &clearOnBuild_);
    ImGui::SameLine(); ImGui::Checkbox("On Recompile", &clearOnRecompile_);
    ImGui::SameLine(); ImGui::Checkbox("Error Pause", &errorPause_);
    ImGui::SameLine(); ImGui::SetNextItemWidth(180);
    ImGui::InputTextWithHint("##search", "Search", searchBuf_, sizeof(searchBuf_));
    ImGui::SameLine(); ImGui::Checkbox("Info", &showInfo_);
    ImGui::SameLine(); ImGui::Checkbox("Warn", &showWarn_);
    ImGui::SameLine(); ImGui::Checkbox("Error", &showError_);
    ImGui::Separator();

    // ── 가상화 row 목록 ──
    ImGui::BeginChild("##rows", ImVec2(0, -120), true);
    // 필터 통과 인덱스를 먼저 모아 ImGuiListClipper로 가상화.
    static std::vector<int> visible; visible.clear();
    for (int i = 0; i < (int)rows_.size(); ++i)
        if (PassesFilter(rows_[i].msg)) visible.push_back(i);

    ImGuiListClipper clipper;
    clipper.Begin((int)visible.size());
    while (clipper.Step()) {
        for (int vi = clipper.DisplayStart; vi < clipper.DisplayEnd; ++vi) {
            int i = visible[vi];
            const Row& r = rows_[i];
            std::string label = "[" + r.msg.category + "] " + r.msg.message;
            if (r.repeat > 1) label += "  (" + std::to_string(r.repeat) + ")";
            if (ImGui::Selectable(label.c_str(), selectedRow_ == i))
                selectedRow_ = i;
            // 더블클릭으로 source 열기(externalPath:externalLine 우선)
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
                const auto& m = r.msg;
                if (openFile_ && !m.externalPath.empty())
                    openFile_(m.externalPath, m.externalLine);
                else if (openFile_ && !m.sourceFile.empty())
                    openFile_(m.sourceFile, m.sourceLine);
            }
        }
    }
    ImGui::EndChild();

    // ── detail pane ──
    ImGui::BeginChild("##detail", ImVec2(0, 0), true);
    if (selectedRow_ >= 0 && selectedRow_ < (int)rows_.size()) {
        const auto& m = rows_[selectedRow_].msg;
        ImGui::TextWrapped("%s", m.message.c_str());
        if (!m.stack.empty()) ImGui::TextWrapped("%s", m.stack.c_str());
        if (!m.externalPath.empty()) {
            ImGui::Text("%s:%d", m.externalPath.c_str(), m.externalLine);
            ImGui::SameLine();
            if (ImGui::SmallButton("Open") && openFile_)
                openFile_(m.externalPath, m.externalLine);
            ImGui::SameLine();
            if (ImGui::SmallButton("Copy"))
                ImGui::SetClipboardText(m.message.c_str());
        }
    }
    ImGui::EndChild();
    ImGui::End();
}
