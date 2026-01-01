#ifndef MOLGA_PROJECT_WINDOW_H
#define MOLGA_PROJECT_WINDOW_H

#include "EditorWindow.h"
#include <string>
#include <vector>
#include <functional>

class ProjectWindow : public EditorWindow {
public:
    ProjectWindow();
    ~ProjectWindow() override = default;

    void OnGUI() override;

    // Callback when project is opened/created
    using ProjectCallback = std::function<void(const std::string&)>;
    void SetOnProjectOpened(ProjectCallback callback) { onProjectOpened = callback; }

    // Check if a project was selected
    bool HasProjectSelected() const { return projectSelected; }
    const std::string& GetSelectedProjectPath() const { return selectedProjectPath; }

    // Reset state for re-showing
    void Reset();

private:
    void DrawNewProjectPanel();
    void DrawOpenProjectPanel();
    void DrawRecentProjectsList();
    void DrawFolderBrowser();
    void DrawFolderBrowserForNewProject();

    // Refresh recent projects list
    void RefreshRecentProjects();

    // Simple folder browser
    void NavigateTo(const std::string& path);
    void NavigateUp();
    std::vector<std::string> GetDirectories(const std::string& path);

    // State
    enum class Tab { Recent, New, Open, BrowseForNew };
    Tab currentTab = Tab::Recent;
    Tab previousTab = Tab::Recent;

    // New project
    char newProjectName[256] = "MyProject";
    char newProjectPath[512] = "";

    // Open project / folder browser
    char openProjectPath[512] = "";
    std::string currentBrowsePath;
    std::vector<std::string> currentDirectories;
    int selectedDirectoryIndex = -1;

    // Recent projects
    std::vector<std::string> recentProjects;

    // Result
    bool projectSelected = false;
    std::string selectedProjectPath;

    ProjectCallback onProjectOpened;
};

#endif // MOLGA_PROJECT_WINDOW_H
