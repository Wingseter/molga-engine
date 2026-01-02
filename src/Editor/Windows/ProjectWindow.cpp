#include "ProjectWindow.h"
#include "../../Core/Project.h"
#include <imgui.h>
#include <filesystem>
#include <algorithm>

namespace fs = std::filesystem;

ProjectWindow::ProjectWindow()
    : EditorWindow("Project") {

    // Set default path to home directory
    const char* home = std::getenv("HOME");
    if (home) {
        strncpy(newProjectPath, home, sizeof(newProjectPath) - 1);
        currentBrowsePath = home;
    } else {
        currentBrowsePath = "/";
    }

    RefreshRecentProjects();
    NavigateTo(currentBrowsePath);
}

void ProjectWindow::Reset() {
    projectSelected = false;
    selectedProjectPath.clear();
    currentTab = Tab::Recent;
    RefreshRecentProjects();
}

void ProjectWindow::RefreshRecentProjects() {
    recentProjects = Project::Get().GetRecentProjects();
}

void ProjectWindow::OnGUI() {
    // Center the window
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(700, 500), ImGuiCond_Appearing);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;

    if (ImGui::Begin("Molga Engine - Project", nullptr, flags)) {
        // Title
        ImGui::PushFont(nullptr);
        ImGui::SetCursorPosX((ImGui::GetWindowWidth() - ImGui::CalcTextSize("MOLGA ENGINE").x) * 0.5f);
        ImGui::TextColored(ImVec4(0.4f, 0.7f, 1.0f, 1.0f), "MOLGA ENGINE");
        ImGui::PopFont();

        ImGui::Separator();
        ImGui::Spacing();

        // Tab buttons
        float tabWidth = 100.0f;
        float totalWidth = tabWidth * 3 + ImGui::GetStyle().ItemSpacing.x * 2;
        ImGui::SetCursorPosX((ImGui::GetWindowWidth() - totalWidth) * 0.5f);

        if (ImGui::Selectable("Recent", currentTab == Tab::Recent, 0, ImVec2(tabWidth, 0))) {
            currentTab = Tab::Recent;
        }
        ImGui::SameLine();
        if (ImGui::Selectable("New Project", currentTab == Tab::New, 0, ImVec2(tabWidth, 0))) {
            currentTab = Tab::New;
        }
        ImGui::SameLine();
        if (ImGui::Selectable("Open Project", currentTab == Tab::Open, 0, ImVec2(tabWidth, 0))) {
            currentTab = Tab::Open;
            NavigateTo(currentBrowsePath);
        }

        ImGui::Separator();
        ImGui::Spacing();

        // Content based on tab
        switch (currentTab) {
            case Tab::Recent:
                DrawRecentProjectsList();
                break;
            case Tab::New:
                DrawNewProjectPanel();
                break;
            case Tab::Open:
                DrawOpenProjectPanel();
                break;
            case Tab::BrowseForNew:
                DrawFolderBrowserForNewProject();
                break;
        }
    }
    ImGui::End();
}

void ProjectWindow::DrawRecentProjectsList() {
    ImGui::Text("Recent Projects");
    ImGui::Separator();

    if (recentProjects.empty()) {
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "No recent projects");
        ImGui::Spacing();
        ImGui::Text("Create a new project or open an existing one.");
    } else {
        ImGui::BeginChild("RecentList", ImVec2(0, -40), true);

        for (size_t i = 0; i < recentProjects.size(); i++) {
            const std::string& path = recentProjects[i];
            fs::path p(path);
            std::string name = p.filename().string();

            ImGui::PushID(static_cast<int>(i));

            // Project item
            bool selected = false;
            if (ImGui::Selectable("##item", &selected, ImGuiSelectableFlags_AllowDoubleClick, ImVec2(0, 40))) {
                if (ImGui::IsMouseDoubleClicked(0)) {
                    // Open project on double click
                    if (Project::Get().Open(path)) {
                        projectSelected = true;
                        selectedProjectPath = path;
                        if (onProjectOpened) {
                            onProjectOpened(path);
                        }
                    }
                }
            }

            // Draw project info on top of selectable
            ImGui::SameLine(10);
            ImGui::BeginGroup();
            ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.9f, 1.0f), "%s", name.c_str());
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "%s", path.c_str());
            ImGui::EndGroup();

            ImGui::PopID();
        }

        ImGui::EndChild();
    }

    // Open button for selected recent project
    ImGui::Spacing();
    if (ImGui::Button("Refresh", ImVec2(100, 0))) {
        RefreshRecentProjects();
    }
}

void ProjectWindow::DrawNewProjectPanel() {
    ImGui::Text("Create New Project");
    ImGui::Separator();
    ImGui::Spacing();

    // Project name
    ImGui::Text("Project Name:");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##name", newProjectName, sizeof(newProjectName));

    ImGui::Spacing();

    // Project location
    ImGui::Text("Location:");
    ImGui::SetNextItemWidth(-80);
    ImGui::InputText("##path", newProjectPath, sizeof(newProjectPath));
    ImGui::SameLine();
    if (ImGui::Button("Browse...")) {
        previousTab = Tab::New;
        currentTab = Tab::BrowseForNew;
        NavigateTo(strlen(newProjectPath) > 0 ? newProjectPath : currentBrowsePath);
    }

    ImGui::Spacing();

    // Preview full path
    fs::path fullPath = fs::path(newProjectPath) / newProjectName;
    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Project will be created at:");
    ImGui::TextWrapped("%s", fullPath.string().c_str());

    ImGui::Spacing();
    ImGui::Spacing();

    // Create button
    bool canCreate = strlen(newProjectName) > 0 && strlen(newProjectPath) > 0;
    if (!canCreate) {
        ImGui::BeginDisabled();
    }

    float buttonWidth = 150.0f;
    ImGui::SetCursorPosX((ImGui::GetWindowWidth() - buttonWidth) * 0.5f);
    if (ImGui::Button("Create Project", ImVec2(buttonWidth, 35))) {
        if (Project::Get().Create(newProjectPath, newProjectName)) {
            projectSelected = true;
            selectedProjectPath = fullPath.string();
            if (onProjectOpened) {
                onProjectOpened(selectedProjectPath);
            }
        }
    }

    if (!canCreate) {
        ImGui::EndDisabled();
    }
}

void ProjectWindow::DrawOpenProjectPanel() {
    ImGui::PushID("OpenProjectPanel");
    ImGui::Text("Open Project");
    ImGui::Separator();
    ImGui::Spacing();

    // Current path
    ImGui::Text("Current Path:");
    ImGui::SetNextItemWidth(-80);
    if (ImGui::InputText("##openpath", openProjectPath, sizeof(openProjectPath), ImGuiInputTextFlags_EnterReturnsTrue)) {
        NavigateTo(openProjectPath);
    }
    ImGui::SameLine();
    if (ImGui::Button("Go##open")) {
        NavigateTo(openProjectPath);
    }

    // Navigation buttons
    if (ImGui::Button("^ Up##open")) {
        NavigateUp();
    }
    ImGui::SameLine();
    if (ImGui::Button("Home##open")) {
        const char* home = std::getenv("HOME");
        if (home) {
            NavigateTo(home);
        }
    }

    ImGui::Spacing();

    // Folder browser
    DrawFolderBrowser();

    ImGui::Spacing();

    // Check if current path is a valid project
    bool isValidProject = Project::IsValidProject(currentBrowsePath);
    if (isValidProject) {
        ImGui::TextColored(ImVec4(0.3f, 0.8f, 0.3f, 1.0f), "Valid Molga project found!");
    }

    // Open button
    float buttonWidth = 150.0f;
    ImGui::SetCursorPosX((ImGui::GetWindowWidth() - buttonWidth) * 0.5f);

    if (!isValidProject) {
        ImGui::BeginDisabled();
    }

    if (ImGui::Button("Open Project", ImVec2(buttonWidth, 35))) {
        if (Project::Get().Open(currentBrowsePath)) {
            projectSelected = true;
            selectedProjectPath = currentBrowsePath;
            if (onProjectOpened) {
                onProjectOpened(selectedProjectPath);
            }
        }
    }

    if (!isValidProject) {
        ImGui::EndDisabled();
        ImGui::SetCursorPosX((ImGui::GetWindowWidth() - 300) * 0.5f);
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Navigate to a folder with project.molga file");
    }
    ImGui::PopID();
}

void ProjectWindow::DrawFolderBrowser() {
    ImGui::BeginChild("FolderBrowser", ImVec2(0, -80), true);

    for (size_t i = 0; i < currentDirectories.size(); i++) {
        const std::string& dir = currentDirectories[i];

        ImGui::PushID(static_cast<int>(i));

        // Check if this folder is a valid project
        fs::path fullPath = fs::path(currentBrowsePath) / dir;
        bool isProject = Project::IsValidProject(fullPath.string());

        // Icon and name
        if (isProject) {
            ImGui::TextColored(ImVec4(0.4f, 0.7f, 1.0f, 1.0f), "[P]");
        } else {
            ImGui::TextColored(ImVec4(0.8f, 0.7f, 0.2f, 1.0f), "[D]");
        }
        ImGui::SameLine();

        bool selected = (static_cast<int>(i) == selectedDirectoryIndex);
        if (ImGui::Selectable(dir.c_str(), selected, ImGuiSelectableFlags_AllowDoubleClick)) {
            selectedDirectoryIndex = static_cast<int>(i);

            if (ImGui::IsMouseDoubleClicked(0)) {
                NavigateTo(fullPath.string());
            }
        }

        ImGui::PopID();
    }

    if (currentDirectories.empty()) {
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "(Empty folder)");
    }

    ImGui::EndChild();
}

void ProjectWindow::NavigateTo(const std::string& path) {
    if (!fs::exists(path) || !fs::is_directory(path)) {
        return;
    }

    currentBrowsePath = fs::canonical(path).string();
    strncpy(openProjectPath, currentBrowsePath.c_str(), sizeof(openProjectPath) - 1);
    selectedDirectoryIndex = -1;

    currentDirectories = GetDirectories(currentBrowsePath);
}

void ProjectWindow::NavigateUp() {
    fs::path current(currentBrowsePath);
    if (current.has_parent_path()) {
        NavigateTo(current.parent_path().string());
    }
}

std::vector<std::string> ProjectWindow::GetDirectories(const std::string& path) {
    std::vector<std::string> dirs;

    try {
        for (const auto& entry : fs::directory_iterator(path)) {
            if (entry.is_directory()) {
                std::string name = entry.path().filename().string();
                // Skip hidden directories
                if (!name.empty() && name[0] != '.') {
                    dirs.push_back(name);
                }
            }
        }

        // Sort alphabetically
        std::sort(dirs.begin(), dirs.end());
    } catch (const std::exception& e) {
        // Permission denied or other errors
    }

    return dirs;
}

void ProjectWindow::DrawFolderBrowserForNewProject() {
    ImGui::PushID("BrowseForNewPanel");
    ImGui::Text("Select Location for New Project");
    ImGui::Separator();
    ImGui::Spacing();

    // Current path
    ImGui::Text("Current Path:");
    ImGui::SetNextItemWidth(-80);
    if (ImGui::InputText("##browsepath", openProjectPath, sizeof(openProjectPath), ImGuiInputTextFlags_EnterReturnsTrue)) {
        NavigateTo(openProjectPath);
    }
    ImGui::SameLine();
    if (ImGui::Button("Go##browse")) {
        NavigateTo(openProjectPath);
    }

    // Navigation buttons
    if (ImGui::Button("^ Up##browse")) {
        NavigateUp();
    }
    ImGui::SameLine();
    if (ImGui::Button("Home##browse")) {
        const char* home = std::getenv("HOME");
        if (home) {
            NavigateTo(home);
        }
    }

    ImGui::Spacing();

    // Folder browser
    ImGui::BeginChild("FolderBrowserNew", ImVec2(0, -60), true);

    for (size_t i = 0; i < currentDirectories.size(); i++) {
        const std::string& dir = currentDirectories[i];

        ImGui::PushID(static_cast<int>(i));

        // Folder icon
        ImGui::TextColored(ImVec4(0.8f, 0.7f, 0.2f, 1.0f), "[D]");
        ImGui::SameLine();

        bool selected = (static_cast<int>(i) == selectedDirectoryIndex);
        if (ImGui::Selectable(dir.c_str(), selected, ImGuiSelectableFlags_AllowDoubleClick)) {
            selectedDirectoryIndex = static_cast<int>(i);

            if (ImGui::IsMouseDoubleClicked(0)) {
                fs::path fullPath = fs::path(currentBrowsePath) / dir;
                NavigateTo(fullPath.string());
            }
        }

        ImGui::PopID();
    }

    if (currentDirectories.empty()) {
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "(Empty folder)");
    }

    ImGui::EndChild();

    ImGui::Spacing();

    // Select and Cancel buttons
    float buttonWidth = 120.0f;
    float spacing = 10.0f;
    float totalWidth = buttonWidth * 2 + spacing;
    ImGui::SetCursorPosX((ImGui::GetWindowWidth() - totalWidth) * 0.5f);

    if (ImGui::Button("Select This Folder", ImVec2(buttonWidth, 30))) {
        // Set the selected path and go back to New Project tab
        strncpy(newProjectPath, currentBrowsePath.c_str(), sizeof(newProjectPath) - 1);
        currentTab = Tab::New;
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(buttonWidth, 30))) {
        currentTab = Tab::New;
    }
    ImGui::PopID();
}
