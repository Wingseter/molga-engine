#include "ScriptWindow.h"
#include "../VSCodeIntegration.h"
#include "../../Scripting/ScriptCompiler.h"
#include "../../Scripting/ScriptManager.h"
#include "../../Core/Project.h"
#include <imgui.h>
#include <filesystem>

namespace fs = std::filesystem;

ScriptWindow::ScriptWindow()
    : EditorWindow("Scripts") {
    RefreshScriptList();
}

void ScriptWindow::OnGUI() {
    if (!isOpen) return;

    ImGui::SetNextWindowSize(ImVec2(350, 400), ImGuiCond_FirstUseEver);

    if (ImGui::Begin("Scripts", &isOpen)) {
        DrawToolbar();
        ImGui::Separator();
        DrawScriptList();
        ImGui::Separator();
        DrawCompileStatus();
    }
    ImGui::End();

    // Draw create dialog as a popup
    if (showCreateDialog) {
        DrawCreateScriptDialog();
    }
}

void ScriptWindow::DrawToolbar() {
    Project& project = Project::Get();
    bool hasProject = project.IsOpen();

    if (!hasProject) {
        ImGui::BeginDisabled();
    }

    // Open in VSCode button
    if (ImGui::Button("Open VSCode")) {
        VSCodeIntegration::Get().OpenInVSCode(project.GetPath());
    }

    ImGui::SameLine();

    // Create script button
    if (ImGui::Button("Create Script")) {
        showCreateDialog = true;
        memset(newScriptName, 0, sizeof(newScriptName));
        strcpy(newScriptName, "NewScript");
    }

    ImGui::SameLine();

    // Compile button
    if (ImGui::Button("Compile")) {
        ScriptCompiler& compiler = ScriptCompiler::Get();
        compiler.SetProjectPath(project.GetPath());

        isCompiling = true;
        if (compiler.Compile()) {
            compileStatus = "Compilation successful!";
            lastCompileSuccess = true;
        } else {
            compileStatus = "Compilation failed: " + compiler.GetLastError();
            lastCompileSuccess = false;
        }
        isCompiling = false;
        RefreshScriptList();
    }

    ImGui::SameLine();

    // Hot Reload button
    if (ImGui::Button("Hot Reload")) {
        ScriptCompiler& compiler = ScriptCompiler::Get();
        std::string libPath = compiler.GetCompiledLibraryPath();

        if (fs::exists(libPath)) {
            if (ScriptManager::Get().LoadScriptLibrary(libPath)) {
                compileStatus = "Scripts reloaded successfully!";
                lastCompileSuccess = true;
            } else {
                compileStatus = "Failed to reload scripts";
                lastCompileSuccess = false;
            }
        } else {
            compileStatus = "No compiled library found. Compile first!";
            lastCompileSuccess = false;
        }
    }

    if (!hasProject) {
        ImGui::EndDisabled();
    }

    // Refresh button
    ImGui::SameLine();
    if (ImGui::Button("Refresh")) {
        RefreshScriptList();
    }
}

void ScriptWindow::DrawScriptList() {
    Project& project = Project::Get();

    if (!project.IsOpen()) {
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "No project open");
        return;
    }

    ImGui::Text("Scripts/");
    ImGui::BeginChild("ScriptList", ImVec2(0, -50), true);

    if (scripts.empty()) {
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "(No scripts)");
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Click 'Create Script' to add one");
    } else {
        for (size_t i = 0; i < scripts.size(); ++i) {
            const auto& script = scripts[i];

            ImGui::PushID(static_cast<int>(i));

            // Script icon and name
            ImGui::TextColored(ImVec4(0.4f, 0.7f, 1.0f, 1.0f), "[S]");
            ImGui::SameLine();

            // Selectable script name
            if (ImGui::Selectable(script.name.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick)) {
                if (ImGui::IsMouseDoubleClicked(0)) {
                    // Open in VSCode on double-click
                    VSCodeIntegration::Get().OpenFileInVSCode(script.headerPath);
                }
            }

            // Tooltip with file paths
            if (ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                ImGui::Text("Header: %s", script.headerPath.c_str());
                ImGui::Text("Source: %s", script.sourcePath.c_str());
                ImGui::Text("Double-click to open in VSCode");
                ImGui::EndTooltip();
            }

            ImGui::PopID();
        }
    }

    ImGui::EndChild();
}

void ScriptWindow::DrawCreateScriptDialog() {
    ImGui::OpenPopup("Create New Script");

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(350, 150), ImGuiCond_Appearing);

    if (ImGui::BeginPopupModal("Create New Script", &showCreateDialog, ImGuiWindowFlags_NoResize)) {
        ImGui::Text("Script Name:");
        ImGui::SetNextItemWidth(-1);
        ImGui::InputText("##scriptname", newScriptName, sizeof(newScriptName));

        ImGui::Spacing();

        // Preview
        Project& project = Project::Get();
        std::string previewPath = project.GetScriptsPath() + "/" + std::string(newScriptName);
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Will create:");
        ImGui::TextWrapped("%s.h", previewPath.c_str());
        ImGui::TextWrapped("%s.cpp", previewPath.c_str());

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Buttons
        float buttonWidth = 100.0f;
        float spacing = 10.0f;
        ImGui::SetCursorPosX((ImGui::GetWindowWidth() - buttonWidth * 2 - spacing) * 0.5f);

        bool validName = strlen(newScriptName) > 0;
        if (!validName) {
            ImGui::BeginDisabled();
        }

        if (ImGui::Button("Create", ImVec2(buttonWidth, 0))) {
            ScriptCompiler& compiler = ScriptCompiler::Get();
            compiler.SetProjectPath(project.GetPath());

            if (compiler.CreateScriptTemplate(newScriptName)) {
                compileStatus = "Created script: " + std::string(newScriptName);
                lastCompileSuccess = true;
                RefreshScriptList();

                // Open in VSCode
                std::string headerPath = project.GetScriptsPath() + "/" + std::string(newScriptName) + ".h";
                VSCodeIntegration::Get().OpenFileInVSCode(headerPath);
            } else {
                compileStatus = "Failed: " + compiler.GetLastError();
                lastCompileSuccess = false;
            }
            showCreateDialog = false;
        }

        if (!validName) {
            ImGui::EndDisabled();
        }

        ImGui::SameLine();

        if (ImGui::Button("Cancel", ImVec2(buttonWidth, 0))) {
            showCreateDialog = false;
        }

        ImGui::EndPopup();
    }
}

void ScriptWindow::DrawCompileStatus() {
    // Status line
    if (isCompiling) {
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.3f, 1.0f), "Compiling...");
    } else if (!compileStatus.empty()) {
        ImVec4 color = lastCompileSuccess ?
            ImVec4(0.3f, 0.8f, 0.3f, 1.0f) :
            ImVec4(0.8f, 0.3f, 0.3f, 1.0f);
        ImGui::TextColored(color, "%s", compileStatus.c_str());
    } else {
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Ready");
    }

    // Show compile output button
    ScriptCompiler& compiler = ScriptCompiler::Get();
    if (!compiler.GetCompileOutput().empty()) {
        ImGui::SameLine();
        if (ImGui::SmallButton("Show Output")) {
            ImGui::OpenPopup("Compile Output");
        }

        // Compile output popup
        if (ImGui::BeginPopup("Compile Output")) {
            ImGui::Text("Compile Output:");
            ImGui::Separator();
            ImGui::BeginChild("output", ImVec2(500, 300), true);
            ImGui::TextWrapped("%s", compiler.GetCompileOutput().c_str());
            ImGui::EndChild();
            ImGui::EndPopup();
        }
    }
}

void ScriptWindow::RefreshScriptList() {
    scripts.clear();

    Project& project = Project::Get();
    if (!project.IsOpen()) return;

    std::string scriptsPath = project.GetScriptsPath();
    if (!fs::exists(scriptsPath)) return;

    // Update ScriptCompiler path
    ScriptCompiler::Get().SetProjectPath(project.GetPath());

    // Discover scripts
    auto discoveredScripts = ScriptCompiler::Get().DiscoverScripts();
    for (const auto& info : discoveredScripts) {
        ScriptFileInfo fileInfo;
        fileInfo.name = info.name;
        fileInfo.headerPath = info.headerPath;
        fileInfo.sourcePath = info.sourcePath;
        fileInfo.exists = true;
        scripts.push_back(fileInfo);
    }
}
