#include "ProfilerWindow.h"
#include "../EditorConstants.h"
#include "Core/Profiling/ProfilerService.h"
#include "Core/Profiling/ProfilerCategory.h"
#include <imgui.h>
#include <vector>
#include <cfloat>

using molga::ProfilerService;
using molga::FrameProfile;
using molga::ProfileCategory;

ProfilerWindow::ProfilerWindow()
    : EditorWindow(EditorConstants::WIN_PROFILER) {}

static float MsOf(long long nanos) { return static_cast<float>(nanos) / 1.0e6f; }

void ProfilerWindow::OnGUI() {
    if (!isOpen) return;
    ImGui::Begin(title.c_str(), &isOpen);

    ProfilerService& svc = ProfilerService::Get();
    bool enabled = svc.IsEnabled();
    if (ImGui::Checkbox("Enabled", &enabled)) svc.SetEnabled(enabled);
    ImGui::SameLine();
    if (ImGui::Button("Clear")) { svc.Clear(); selectedFrameIndex_ = -1; }
    ImGui::SameLine();
    if (ImGui::Button("Jump to slowest")) {
        if (const FrameProfile* s = svc.SlowestFrame())
            selectedFrameIndex_ = static_cast<long long>(s->frameIndex);
    }
    ImGui::Separator();

    DrawTimeline();
    ImGui::Separator();
    DrawSelectedFrame();

    ImGui::End();
}

void ProfilerWindow::DrawTimeline() {
    ProfilerService& svc = ProfilerService::Get();
    size_t n = svc.Size();
    if (n == 0) { ImGui::TextUnformatted("No frames captured."); return; }

    // dt(ms) 막대 그래프. 클릭으로 프레임을 선택한다.
    std::vector<float> dtMs(n);
    for (size_t i = 0; i < n; ++i) dtMs[i] = svc.At(i)->dt * 1000.0f;

    ImGui::PlotHistogram("##frametimes", dtMs.data(), static_cast<int>(n),
                         0, "frame time (ms)", 0.0f, FLT_MAX, ImVec2(0, 80));

    // Auto-track latest checkbox
    bool autoTrack = (selectedFrameIndex_ == -1);
    if (ImGui::Checkbox("Auto-track Latest", &autoTrack)) {
        if (autoTrack) {
            selectedFrameIndex_ = -1;
        } else {
            selectedFrameIndex_ = static_cast<long long>(svc.Latest()->frameIndex);
        }
    }

    // 슬라이더로 프레임 선택
    int sel = static_cast<int>(n - 1);
    if (selectedFrameIndex_ >= 0) {
        for (size_t i = 0; i < n; ++i) {
            if (static_cast<long long>(svc.At(i)->frameIndex) == selectedFrameIndex_) {
                sel = static_cast<int>(i);
                break;
            }
        }
    }

    if (ImGui::SliderInt("Frame Select", &sel, 0, static_cast<int>(n - 1))) {
        selectedFrameIndex_ = static_cast<long long>(svc.At(static_cast<size_t>(sel))->frameIndex);
    }
}

void ProfilerWindow::DrawSelectedFrame() {
    ProfilerService& svc = ProfilerService::Get();
    size_t n = svc.Size();
    if (n == 0) return;

    const FrameProfile* f = nullptr;
    if (selectedFrameIndex_ == -1) {
        f = svc.Latest();
    } else {
        for (size_t i = 0; i < n; ++i) {
            if (static_cast<long long>(svc.At(i)->frameIndex) == selectedFrameIndex_) {
                f = svc.At(i);
                break;
            }
        }
        if (!f) {
            // 선택한 프레임이 더 이상 버퍼에 존재하지 않으면 최신 프레임이나 마지막으로 폴백
            f = svc.Latest();
        }
    }
    if (!f) return;

    ImGui::Text("Frame %llu   dt = %.3f ms (%.1f FPS)", f->frameIndex, f->dt * 1000.0f, f->dt > 0.0f ? 1.0f / f->dt : 0.0f);

    // 카테고리 합계 — exit scenario의 핵심: 시간이 어디로 갔는가.
    if (ImGui::CollapsingHeader("By category", ImGuiTreeNodeFlags_DefaultOpen)) {
        for (size_t i = 0; i < static_cast<size_t>(ProfileCategory::Count); ++i) {
            auto c = static_cast<ProfileCategory>(i);
            ImGui::Text("%-12s %8.3f ms", molga::ProfileCategoryName(c),
                        MsOf(f->CategoryNanos(c)));
        }
    }

    // named 스코프 — 깊이 들여쓰기로 중첩 표현.
    if (ImGui::CollapsingHeader("CPU sections", ImGuiTreeNodeFlags_DefaultOpen)) {
        for (const auto& s : f->scopes) {
            ImGui::Text("%*s%-25s %8.3f ms  [%s]", s.depth * 2, "",
                        s.name.c_str(), MsOf(s.nanos),
                        molga::ProfileCategoryName(s.category));
        }
    }

    // 렌더러 통계 + 카운터.
    if (ImGui::CollapsingHeader("Renderer stats", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text("Draw calls: %d", f->render.drawCalls);
        ImGui::Text("Batches: %d", f->render.batches);
        
        float efficiency = f->render.submittedSprites > 0 
            ? (1.0f - (float)f->render.batches / f->render.submittedSprites) * 100.0f 
            : 0.0f;
        ImGui::Text("Batch efficiency: %.1f%%", efficiency);
        ImGui::Text("Submitted sprites: %d", f->render.submittedSprites);
        ImGui::Text("Submitted commands: %d", f->render.submittedCommands);
        ImGui::Text("Batch flushes: %d", f->render.batchFlushes);
        ImGui::Text("Batch breaks: %d", f->render.batchBreaks);
        ImGui::Text("Max sprites/batch: %d", f->render.maxSpritesPerBatch);
        ImGui::Text("Vertices uploaded: %.1f KB", f->render.verticesUploadedBytes / 1024.0f);
        ImGui::Text("Queue sort: %.3f ms", MsOf(f->render.queueSortNanos));
        ImGui::Text("Texture binds: %d", f->render.textureBinds);
        ImGui::Text("Shader switches: %d", f->render.shaderSwitches);
        ImGui::Text("FBO resizes: %d", f->render.fboResizes);
    }
    if (ImGui::CollapsingHeader("Counters", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text("Sprites: %d", f->counters.sprites);
        ImGui::Text("Particles: %d", f->counters.particles);
        ImGui::Text("Text: %d", f->counters.text);
        ImGui::Text("Tile chunks: %d", f->counters.tileChunks);
        ImGui::Text("Asset loads: %d", f->counters.assetLoads);
        ImGui::Text("Scripts: %d", f->counters.scripts);
        ImGui::Text("Physics: %d", f->counters.physics);
    }
}
