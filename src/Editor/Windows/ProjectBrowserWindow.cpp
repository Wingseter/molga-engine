#include "ProjectBrowserWindow.h"
#include "../../Core/Project.h"
#include <imgui.h>
#include <filesystem>
#include <algorithm>

namespace fs = std::filesystem;

ProjectBrowserWindow::ProjectBrowserWindow()
    : EditorWindow("Project") {
}

void ProjectBrowserWindow::OnGUI() {
    if (!Project::Get().IsOpen()) {
        ImGui::Begin("Project", nullptr);
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "No project open");
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

    if (ImGui::Begin("Project", nullptr)) {
        // Toolbar
        if (ImGui::Button("Refresh")) {
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

    bool opened = ImGui::TreeNodeEx(node.name.c_str(), flags);

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
    } catch (const std::exception&) {
        // Permission denied or other errors
    }
}

void ProjectBrowserWindow::DrawFileGrid() {
    float windowWidth = ImGui::GetContentRegionAvail().x;
    float cellSize = iconSize + padding * 2;
    int columns = std::max(1, static_cast<int>(windowWidth / cellSize));

    int itemIndex = 0;

    for (size_t i = 0; i < currentEntries.size(); i++) {
        FileEntry& entry = currentEntries[i];

        if (itemIndex > 0 && itemIndex % columns != 0) {
            ImGui::SameLine();
        }

        ImGui::PushID(static_cast<int>(i));

        // Item button
        ImVec2 pos = ImGui::GetCursorPos();
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
        ImVec4 iconColor;
        if (entry.isDirectory) {
            iconColor = ImVec4(0.9f, 0.7f, 0.2f, 1.0f);  // Yellow for folders
        } else if (entry.extension == ".png" || entry.extension == ".jpg" || entry.extension == ".jpeg") {
            iconColor = ImVec4(0.2f, 0.7f, 0.9f, 1.0f);  // Blue for images
        } else if (entry.extension == ".json") {
            iconColor = ImVec4(0.2f, 0.9f, 0.4f, 1.0f);  // Green for JSON
        } else if (entry.extension == ".wav" || entry.extension == ".mp3" || entry.extension == ".ogg") {
            iconColor = ImVec4(0.9f, 0.4f, 0.6f, 1.0f);  // Pink for audio
        } else {
            iconColor = ImVec4(0.6f, 0.6f, 0.6f, 1.0f);  // Gray for others
        }

        ImGui::PushStyleColor(ImGuiCol_Button, iconColor);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(iconColor.x + 0.1f, iconColor.y + 0.1f, iconColor.z + 0.1f, 1.0f));

        if (ImGui::Button(GetFileIcon(entry), ImVec2(iconSize, iconSize))) {
            selectedIndex = static_cast<int>(i);
            selectedPath = entry.path;
            if (onFileSelected) {
                onFileSelected(entry.path);
            }
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

        // Double click
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
            if (entry.isDirectory) {
                NavigateTo(entry.path);
            } else if (onFileDoubleClicked) {
                onFileDoubleClicked(entry.path);
            }
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

    // Context menu
    DrawContextMenu();
}

void ProjectBrowserWindow::DrawContextMenu() {
    if (ImGui::BeginPopupContextWindow("ProjectBrowserContext")) {
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
    if (entry.isDirectory) {
        return "[DIR]";
    } else if (entry.extension == ".png" || entry.extension == ".jpg" || entry.extension == ".jpeg") {
        return "[IMG]";
    } else if (entry.extension == ".json") {
        return "[SCN]";
    } else if (entry.extension == ".wav" || entry.extension == ".mp3" || entry.extension == ".ogg") {
        return "[SFX]";
    } else {
        return "[FILE]";
    }
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
    } catch (const std::exception&) {
        // Permission denied or other errors
    }
}
