#include "ProjectBrowserWindow.h"
#include "../../Common/Log.h"
#include "../Project.h"
#include "../EditorConstants.h"
#include "../Editor.h"
#include "../EditorTheme.h"
#include "../FontManager.h"
#include "../UIRegistry.h"
#include "../../Scripting/ScriptCompiler.h"
#include "../../Core/SceneSerializer.h"
#include "../../ECS/GameObject.h"
#include "Core/PrefabRegistry.h"
#include "Editor/Commands/PrefabCommands.h"
#include "Core/AssetDatabase.h"
#include "Editor/AssetReferenceScan.h"
#include "Editor/Commands/ProjectFileCommands.h"
#include <imgui.h>
#include <filesystem>
#include <algorithm>
#include <fstream>

namespace fs = std::filesystem;

ProjectBrowserWindow::ProjectBrowserWindow()
    : EditorWindow(EditorConstants::WIN_PROJECT_BROWSER) {
}

void ProjectBrowserWindow::OnGUI() {
    if (!Project::Get().IsOpen()) {
        ImGui::Begin(title.c_str(), nullptr);
        ImGui::TextColored(EditorTheme::DISABLED_TEXT, "No project open");
        ImGui::End();
        return;
    }

    // Initialize current path if empty
    if (currentPath.empty()) {
        currentPath = Project::Get().GetAssetsPath();
        ScanDirectory(currentPath);
        rootFolder.name = "Assets";
        rootFolder.path = Project::Get().GetAssetsPath();
        rootFolder.expanded = true;
        BuildFolderTree(rootFolder);
    }

    if (ImGui::Begin(title.c_str(), nullptr)) {
        // Toolbar
        if (ImGui::Button((std::string(Icons::SyncAlt) + " Refresh").c_str())) {
            Refresh();
        }
        ImGui::SameLine();

        // Breadcrumb path
        DrawBreadcrumb();

        ImGui::Separator();

        // Split view: folder tree on left, file grid on right
        ImGui::BeginChild("FolderTree", ImVec2(folderTreeWidth, 0), true);
        DrawFolderTree();
        ImGui::EndChild();

        ImGui::SameLine();

        // Splitter
        ImGui::Button("##splitter", ImVec2(4, -1));
        if (ImGui::IsItemActive()) {
            folderTreeWidth += ImGui::GetIO().MouseDelta.x;
            folderTreeWidth = std::max(100.0f, std::min(400.0f, folderTreeWidth));
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        }

        ImGui::SameLine();

        ImGui::BeginChild("FileGrid", ImVec2(0, 0), true);
        DrawFileGrid();
        ImGui::EndChild();
    }
    ImGui::End();
}

void ProjectBrowserWindow::DrawBreadcrumb() {
    // Show path as clickable breadcrumb
    std::string assetsPath = Project::Get().GetAssetsPath();
    std::string relativePath = currentPath;

    // Make path relative to Assets folder
    if (relativePath.find(assetsPath) == 0) {
        relativePath = relativePath.substr(assetsPath.length());
        if (!relativePath.empty() && relativePath[0] == '/') {
            relativePath = relativePath.substr(1);
        }
    }

    // Assets root
    if (ImGui::SmallButton("Assets")) {
        NavigateTo(assetsPath);
    }

    // Split path and create buttons
    if (!relativePath.empty()) {
        std::string accumulated = assetsPath;
        size_t start = 0;
        size_t end;

        while ((end = relativePath.find('/', start)) != std::string::npos) {
            std::string part = relativePath.substr(start, end - start);
            accumulated += "/" + part;

            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "/");
            ImGui::SameLine();

            if (ImGui::SmallButton(part.c_str())) {
                NavigateTo(accumulated);
            }

            start = end + 1;
        }

        // Last part
        if (start < relativePath.length()) {
            std::string part = relativePath.substr(start);
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "/");
            ImGui::SameLine();
            ImGui::Text("%s", part.c_str());
        }
    }
}

void ProjectBrowserWindow::DrawFolderTree() {
    DrawFolderNode(rootFolder);
}

void ProjectBrowserWindow::DrawFolderNode(FolderNode& node) {
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;

    if (node.children.empty()) {
        flags |= ImGuiTreeNodeFlags_Leaf;
    }

    if (node.path == currentPath) {
        flags |= ImGuiTreeNodeFlags_Selected;
    }

    if (node.expanded) {
        ImGui::SetNextItemOpen(true, ImGuiCond_Once);
    }

    // Use folder icon
    std::string label = std::string(Icons::Folder) + " " + node.name;
    bool opened = ImGui::TreeNodeEx(node.name.c_str(), flags, "%s", label.c_str());

    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
        NavigateTo(node.path);
    }

    if (opened) {
        for (auto& child : node.children) {
            DrawFolderNode(child);
        }
        ImGui::TreePop();
    }
}

void ProjectBrowserWindow::BuildFolderTree(FolderNode& node) {
    node.children.clear();

    try {
        for (const auto& entry : fs::directory_iterator(node.path)) {
            if (entry.is_directory()) {
                std::string name = entry.path().filename().string();
                if (!showHiddenFiles && !name.empty() && name[0] == '.') {
                    continue;
                }

                FolderNode child;
                child.name = name;
                child.path = entry.path().string();
                BuildFolderTree(child);
                node.children.push_back(child);
            }
        }

        // Sort alphabetically
        std::sort(node.children.begin(), node.children.end(),
            [](const FolderNode& a, const FolderNode& b) {
                return a.name < b.name;
            });
    } catch (const std::exception& e) {
        Log::Error("ProjectBrowser", "Error building folder tree: " + std::string(e.what()));
    }
}

static int AssetTypeIndex(const std::string& ext) {
    if (ext == ".png" || ext == ".jpg" || ext == ".jpeg") return 1; // Texture
    if (ext == ".wav" || ext == ".mp3" || ext == ".ogg")  return 2; // Audio
    if (ext == ".prefab")                                  return 3; // Prefab
    if (ext == ".cpp" || ext == ".h" || ext == ".lua")     return 4; // Script
    if (ext == ".json" || ext == ".scene")                 return 5; // Scene
    return 0; // All or Other
}

static void DrawBadge(ImVec2 minPos, ImU32 color, const char* text) {
    ImVec2 badgePos = ImVec2(minPos.x + 8.0f, minPos.y + 8.0f);
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddCircleFilled(badgePos, 8.0f, color);
    ImVec2 textSize = ImGui::CalcTextSize(text);
    drawList->AddText(ImVec2(badgePos.x - textSize.x * 0.5f, badgePos.y - textSize.y * 0.5f), IM_COL32(255, 255, 255, 255), text);
}

void ProjectBrowserWindow::DrawFileGrid() {
    // 검색 바 + 타입 필터(그리드 위)
    ImGui::InputTextWithHint("##search", "Search assets...", searchBuffer_, sizeof(searchBuffer_));
    ImGui::SameLine();
    const char* kFilters[] = {"All","Texture","Audio","Prefab","Script","Scene"};
    ImGui::SetNextItemWidth(120);
    ImGui::Combo("##typeFilter", &typeFilter_, kFilters, IM_ARRAYSIZE(kFilters));

    float windowWidth = ImGui::GetContentRegionAvail().x;
    float cellSize = iconSize + padding * 2;
    int columns = std::max(1, static_cast<int>(windowWidth / cellSize));

    int itemIndex = 0;

    for (size_t i = 0; i < currentEntries.size(); i++) {
        FileEntry& entry = currentEntries[i];

        // 검색 필터 + 타입 필터 적용
        std::string needle = searchBuffer_;
        if (!needle.empty()) {
            std::string nameLower = entry.name;
            std::string needleLower = needle;
            std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), ::tolower);
            std::transform(needleLower.begin(), needleLower.end(), needleLower.begin(), ::tolower);
            if (nameLower.find(needleLower) == std::string::npos) continue;
        }
        if (typeFilter_ != 0 && AssetTypeIndex(entry.extension) != typeFilter_) continue;

        if (itemIndex > 0 && itemIndex % columns != 0) {
            ImGui::SameLine();
        }
        itemIndex++;

        ImGui::PushID(static_cast<int>(i));

        // Item button
        bool selected = (static_cast<int>(i) == selectedIndex);

        ImGui::BeginGroup();

        // Selection background
        if (selected) {
            ImVec2 screenPos = ImGui::GetCursorScreenPos();
            ImGui::GetWindowDrawList()->AddRectFilled(
                screenPos,
                ImVec2(screenPos.x + cellSize, screenPos.y + cellSize + 20),
                IM_COL32(60, 100, 150, 255),
                4.0f
            );
        }

        // Icon placeholder (colored box based on type)
        const auto& fileInfo = UIRegistry::GetFileTypeInfo(entry.extension, entry.isDirectory);
        ImVec4 iconColor = fileInfo.color;

        ImGui::PushStyleColor(ImGuiCol_Button, iconColor);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(iconColor.x + 0.1f, iconColor.y + 0.1f, iconColor.z + 0.1f, 1.0f));

        if (ImGui::Button(GetFileIcon(entry), ImVec2(iconSize, iconSize))) {
            selectedIndex = static_cast<int>(i);
            selectedPath = entry.path;
            if (onFileSelected) {
                onFileSelected(entry.path);
            }
        }

        // badge 오버레이
        ImVec2 buttonMinPos = ImGui::GetItemRectMin();
        std::string guid = molga::AssetDatabase::Get().GuidForSource(
            Project::Get().GetRelativePath(entry.path));
        const molga::AssetRecord* rec = molga::AssetDatabase::Get().Find(guid);
        if (guid.empty() && !entry.isDirectory) {
            DrawBadge(buttonMinPos, IM_COL32(255,80,80,255), "?");      // missing/미인덱스
        } else if (rec && rec->importFailed) {
            DrawBadge(buttonMinPos, IM_COL32(255,160,0,255), "!");      // import-failed
        } else if (rec && rec->generated) {
            DrawBadge(buttonMinPos, IM_COL32(120,120,255,255), "G");    // generated
        }

        // Drag source for image files
        if (!entry.isDirectory && (entry.extension == ".png" || entry.extension == ".jpg" || entry.extension == ".jpeg")) {
            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
                // Store the path in drag data
                ImGui::SetDragDropPayload("TEXTURE_PATH", entry.path.c_str(), entry.path.size() + 1);
                ImGui::Text("Texture: %s", entry.name.c_str());
                ImGui::EndDragDropSource();
            }
        }

        // Drag source for audio files
        if (!entry.isDirectory && (entry.extension == ".wav" || entry.extension == ".mp3" || entry.extension == ".ogg")) {
            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
                ImGui::SetDragDropPayload("AUDIO_PATH", entry.path.c_str(), entry.path.size() + 1);
                ImGui::Text("Audio: %s", entry.name.c_str());
                ImGui::EndDragDropSource();
            }
        }

        // Double click
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
            if (entry.isDirectory) {
                NavigateTo(entry.path);
            } else if (onFileDoubleClicked) {
                onFileDoubleClicked(entry.path);
            }
        }

        // Right-click context menu for this file/folder
        if (ImGui::BeginPopupContextItem("FileContextMenu")) {
            selectedIndex = static_cast<int>(i);
            selectedPath = entry.path;

            if (entry.extension == ".prefab") {
                if (ImGui::MenuItem((std::string(Icons::Sitemap) + " Instantiate into Scene").c_str())) {
                    std::filesystem::path relPath = std::filesystem::relative(entry.path, Project::Get().GetAssetsPath());
                    std::string guid = PrefabRegistry::Get().GetPrefabGuid(relPath);
                    if (!guid.empty()) {
                        Editor::Get().GetCommandHistory().Execute(
                            std::make_unique<molga::InstantiatePrefabCommand>(guid));
                    }
                }
            }

            if (ImGui::MenuItem((std::string(Icons::Trash) + " Delete").c_str())) {
                std::string guid = molga::AssetDatabase::Get().GuidForSource(
                    Project::Get().GetRelativePath(entry.path));
                auto refs = molga::AssetReferenceScan::FindReferencers(
                    Project::Get().GetAssetsPath(), guid);
                if (!refs.empty()) {
                    Log::Warn("ProjectBrowser",
                        "Deleting '" + entry.name + "' still referenced by " +
                        std::to_string(refs.size()) + " document(s). Moved to trash (recoverable).");
                }
                std::filesystem::path trash =
                    std::filesystem::path(Project::Get().GetAssetsPath()) / ".trash";
                Editor::Get().GetCommandHistory().Execute(
                    std::make_unique<molga::ProjectFileDeleteCommand>(entry.path, trash));
                Refresh();
            }
            ImGui::EndPopup();
        }

        ImGui::PopStyleColor(2);

        // File name (truncated if too long)
        std::string displayName = entry.name;
        float textWidth = ImGui::CalcTextSize(displayName.c_str()).x;
        if (textWidth > cellSize - 4) {
            // Truncate with ellipsis
            while (textWidth > cellSize - 20 && displayName.length() > 3) {
                displayName = displayName.substr(0, displayName.length() - 1);
                textWidth = ImGui::CalcTextSize((displayName + "...").c_str()).x;
            }
            displayName += "...";
        }

        // Center the text
        float textOffset = (cellSize - ImGui::CalcTextSize(displayName.c_str()).x) * 0.5f;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + textOffset - padding);
        ImGui::TextWrapped("%s", displayName.c_str());

        ImGui::EndGroup();

        // Tooltip with full name
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", entry.name.c_str());
        }

        ImGui::PopID();

        itemIndex++;
    }

    // Inline create input widget (이름 입력 중일 때 표시)
    DrawCreateInlineInput();

    // Context menu
    DrawContextMenu();
}

void ProjectBrowserWindow::DrawContextMenu() {
    if (ImGui::BeginPopupContextWindow("ProjectBrowserContext", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
        // Create 서브메뉴
        if (ImGui::BeginMenu((std::string(Icons::Plus) + " Create").c_str())) {
            if (ImGui::MenuItem((std::string(Icons::Code) + " C++ Script").c_str())) {
                ImGui::CloseCurrentPopup();
                StartCreateScript();
            }
            ImGui::Separator();
            if (ImGui::MenuItem((std::string(Icons::Folder) + " Folder").c_str())) {
                ImGui::CloseCurrentPopup();
                StartCreateFolder();
            }
            if (ImGui::MenuItem((std::string(Icons::File) + " Scene").c_str())) {
                ImGui::CloseCurrentPopup();
                StartCreateScene();
            }
            ImGui::EndMenu();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Refresh")) {
            Refresh();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Show Hidden Files", nullptr, showHiddenFiles)) {
            showHiddenFiles = !showHiddenFiles;
            Refresh();
        }
        ImGui::EndPopup();
    }
}

const char* ProjectBrowserWindow::GetFileIcon(const FileEntry& entry) {
    return UIRegistry::GetFileTypeInfo(entry.extension, entry.isDirectory).icon;
}

void ProjectBrowserWindow::Refresh() {
    ScanDirectory(currentPath);
    rootFolder.path = Project::Get().GetAssetsPath();
    BuildFolderTree(rootFolder);
}

void ProjectBrowserWindow::NavigateTo(const std::string& path) {
    if (fs::exists(path) && fs::is_directory(path)) {
        currentPath = fs::canonical(path).string();
        ScanDirectory(currentPath);
        selectedIndex = -1;
    }
}

void ProjectBrowserWindow::NavigateUp() {
    fs::path current(currentPath);
    fs::path assetsPath(Project::Get().GetAssetsPath());

    // Don't navigate above Assets folder
    if (current != assetsPath && current.has_parent_path()) {
        fs::path parent = current.parent_path();
        if (parent.string().find(assetsPath.string()) == 0 || parent == assetsPath) {
            NavigateTo(parent.string());
        }
    }
}

void ProjectBrowserWindow::ScanDirectory(const std::string& path) {
    currentEntries.clear();

    try {
        for (const auto& entry : fs::directory_iterator(path)) {
            std::string name = entry.path().filename().string();

            // Skip hidden files unless showing them
            if (!showHiddenFiles && !name.empty() && name[0] == '.') {
                continue;
            }

            FileEntry fe;
            fe.name = name;
            fe.path = entry.path().string();
            fe.isDirectory = entry.is_directory();
            fe.isSelected = false;

            if (!fe.isDirectory) {
                fe.extension = entry.path().extension().string();
                std::transform(fe.extension.begin(), fe.extension.end(), fe.extension.begin(), ::tolower);
            }

            currentEntries.push_back(fe);
        }

        // Sort: directories first, then alphabetically
        std::sort(currentEntries.begin(), currentEntries.end(),
            [](const FileEntry& a, const FileEntry& b) {
                if (a.isDirectory != b.isDirectory) {
                    return a.isDirectory > b.isDirectory;
                }
                return a.name < b.name;
            });
    } catch (const std::exception& e) {
        Log::Error("ProjectBrowser", "Error scanning directory: " + std::string(e.what()));
    }
}

// ── Create 헬퍼 ──────────────────────────────────────────────────────────────

void ProjectBrowserWindow::DrawCreateInlineInput() {
    if (createMode == CreateMode::None) return;

    const char* label = "";
    switch (createMode) {
        case CreateMode::Script: label = "New Script Name:"; break;
        case CreateMode::Folder: label = "New Folder Name:"; break;
        case CreateMode::Scene:  label = "New Scene Name:";  break;
        default: break;
    }

    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.3f, 1.0f), "%s", label);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);

    if (createFocusNextFrame) {
        ImGui::SetKeyboardFocusHere();
        createFocusNextFrame = false;
    }

    bool confirmed = ImGui::InputText("##CreateName", createNameBuffer, sizeof(createNameBuffer),
        ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);

    if (confirmed) {
        FinishCreate();
    } else if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        CancelCreate();
    }

    ImGui::SameLine();
    if (ImGui::SmallButton("OK")) {
        FinishCreate();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Cancel")) {
        CancelCreate();
    }
}

void ProjectBrowserWindow::StartCreateScript() {
    createMode = CreateMode::Script;
    strncpy(createNameBuffer, "NewScript", sizeof(createNameBuffer) - 1);
    createNameBuffer[sizeof(createNameBuffer) - 1] = '\0';
    createFocusNextFrame = true;
}

void ProjectBrowserWindow::StartCreateFolder() {
    createMode = CreateMode::Folder;
    strncpy(createNameBuffer, "NewFolder", sizeof(createNameBuffer) - 1);
    createNameBuffer[sizeof(createNameBuffer) - 1] = '\0';
    createFocusNextFrame = true;
}

void ProjectBrowserWindow::StartCreateScene() {
    createMode = CreateMode::Scene;
    strncpy(createNameBuffer, "NewScene", sizeof(createNameBuffer) - 1);
    createNameBuffer[sizeof(createNameBuffer) - 1] = '\0';
    createFocusNextFrame = true;
}

void ProjectBrowserWindow::FinishCreate() {
    std::string name(createNameBuffer);
    if (name.empty()) {
        CancelCreate();
        return;
    }

    namespace fs = std::filesystem;

    switch (createMode) {
        case CreateMode::Script: {
            if (!Project::Get().IsOpen()) {
                Log::Error("ProjectBrowser", "No project open — cannot create script");
                break;
            }

            ScriptCompiler& compiler = ScriptCompiler::Get();
            compiler.SetProjectPath(Project::Get().GetPath());

            // currentPath가 Scripts 하위인지 판단
            std::string scriptsPath = Project::Get().GetScriptsPath();
            fs::path currentFsPath(currentPath);
            fs::path scriptsFsPath(scriptsPath);
            bool inScriptsDir = false;
            try {
                fs::path canonCurrent = fs::weakly_canonical(currentFsPath);
                fs::path canonScripts = fs::weakly_canonical(scriptsFsPath);
                
                std::string strCurrent = canonCurrent.string();
                std::string strScripts = canonScripts.string();
                
#if defined(__APPLE__) || defined(_WIN32)
                std::transform(strCurrent.begin(), strCurrent.end(), strCurrent.begin(), ::tolower);
                std::transform(strScripts.begin(), strScripts.end(), strScripts.begin(), ::tolower);
#endif
                
                if (strCurrent == strScripts) {
                    inScriptsDir = true;
                } else if (strCurrent.size() > strScripts.size() && strCurrent.substr(0, strScripts.size()) == strScripts) {
                    char nextChar = strCurrent[strScripts.size()];
                    if (nextChar == '/' || nextChar == '\\') {
                        inScriptsDir = true;
                    }
                }
            } catch (...) {
                inScriptsDir = false;
            }

            // 생성 대상 디렉터리: Scripts 하위면 현재 폴더, 아니면 Scripts 루트
            std::string destDir = inScriptsDir ? currentPath : scriptsPath;

            // 버그2 수정: 파일 생성 먼저 → 그다음 NavigateTo
            // (CreateScriptTemplate이 Scripts 폴더 자체를 만들므로 순서가 중요)
            if (!inScriptsDir) {
                Log::Warn("ProjectBrowser",
                    "Scripts must be in the Scripts folder. Creating '" + name + "' in Scripts/ instead.");
            }

            // 버그1+2 수정: targetDir 전달, 먼저 생성
            bool ok = compiler.CreateScriptTemplate(name, destDir);
            if (!ok) {
                Log::Error("ProjectBrowser", "Failed to create script: " + compiler.GetLastError());
            } else {
                Log::Info("ProjectBrowser", "Script created: " + name);
                // 생성 후 해당 폴더로 이동 (이미 폴더가 존재하므로 NavigateTo 성공)
                NavigateTo(destDir);
            }
            break;
        }
        case CreateMode::Folder: {
            fs::path newDir = fs::path(currentPath) / name;
            if (fs::exists(newDir)) {
                Log::Warn("ProjectBrowser", "Folder already exists: " + name);
                break;
            }
            try {
                fs::create_directory(newDir);
                Log::Info("ProjectBrowser", "Folder created: " + name);
            } catch (const std::exception& e) {
                Log::Error("ProjectBrowser", "Failed to create folder: " + std::string(e.what()));
            }
            break;
        }
        case CreateMode::Scene: {
            fs::path scenePath = fs::path(currentPath) / (name + ".json");
            if (fs::exists(scenePath)) {
                Log::Warn("ProjectBrowser", "Scene already exists: " + name + ".json");
                break;
            }
            // 빈 오브젝트 목록을 SceneSerializer로 직렬화 → 올바른 스키마 보장
            std::vector<std::shared_ptr<GameObject>> emptyObjects;
            if (SceneSerializer::SaveScene(scenePath.string(), emptyObjects)) {
                Log::Info("ProjectBrowser", "Scene created: " + name + ".json");
            } else {
                Log::Error("ProjectBrowser", "Failed to create scene: " + name);
            }
            break;
        }
        default:
            break;
    }

    createMode = CreateMode::None;
    memset(createNameBuffer, 0, sizeof(createNameBuffer));
    Refresh();
}

void ProjectBrowserWindow::CancelCreate() {
    createMode = CreateMode::None;
    memset(createNameBuffer, 0, sizeof(createNameBuffer));
}
