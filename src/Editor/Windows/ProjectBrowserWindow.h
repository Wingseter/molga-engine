#pragma once

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
    void DrawCreateInlineInput();  // 인라인 이름 입력 위젯

    void ScanDirectory(const std::string& path);
    void NavigateUp();

    // Create helpers
    void StartCreateScript();
    void StartCreateFolder();
    void StartCreateScene();
    void FinishCreate();   // 이름 입력 확정
    void CancelCreate();   // 취소

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

    // Inline create state
    enum class CreateMode { None, Script, Folder, Scene };
    CreateMode createMode = CreateMode::None;
    char createNameBuffer[256] = "";
    bool createFocusNextFrame = false;

    char searchBuffer_[128] = "";
    int typeFilter_ = 0;   // 0=All,1=Texture,2=Audio,3=Prefab,4=Script,5=Scene
};
