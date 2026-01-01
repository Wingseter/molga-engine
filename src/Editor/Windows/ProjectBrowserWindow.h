#ifndef MOLGA_PROJECT_BROWSER_WINDOW_H
#define MOLGA_PROJECT_BROWSER_WINDOW_H

#include "EditorWindow.h"
#include <string>
#include <vector>
#include <functional>

struct FileEntry {
    std::string name;
    std::string path;
    std::string extension;
    bool isDirectory;
    bool isSelected;
};

class ProjectBrowserWindow : public EditorWindow {
public:
    ProjectBrowserWindow();
    ~ProjectBrowserWindow() override = default;

    void OnGUI() override;

    // File selection callback
    using FileCallback = std::function<void(const std::string&)>;
    void SetOnFileSelected(FileCallback callback) { onFileSelected = callback; }
    void SetOnFileDoubleClicked(FileCallback callback) { onFileDoubleClicked = callback; }

    // Refresh current directory
    void Refresh();

    // Navigate to path
    void NavigateTo(const std::string& path);

private:
    void DrawFolderTree();
    void DrawFileGrid();
    void DrawBreadcrumb();
    void DrawContextMenu();

    void ScanDirectory(const std::string& path);
    void NavigateUp();

    // Get icon based on file type
    const char* GetFileIcon(const FileEntry& entry);

    // Current state
    std::string currentPath;
    std::string selectedPath;
    std::vector<FileEntry> currentEntries;
    int selectedIndex = -1;

    // Tree state
    struct FolderNode {
        std::string name;
        std::string path;
        std::vector<FolderNode> children;
        bool expanded = false;
    };
    FolderNode rootFolder;
    void BuildFolderTree(FolderNode& node);
    void DrawFolderNode(FolderNode& node);

    // Callbacks
    FileCallback onFileSelected;
    FileCallback onFileDoubleClicked;

    // UI state
    float folderTreeWidth = 200.0f;
    bool showHiddenFiles = false;

    // Grid settings
    float iconSize = 64.0f;
    float padding = 10.0f;
};

#endif // MOLGA_PROJECT_BROWSER_WINDOW_H
