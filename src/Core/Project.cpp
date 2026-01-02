#include "Project.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;

Project& Project::Get() {
    static Project instance;
    return instance;
}

bool Project::Create(const std::string& parentPath, const std::string& name) {
    // Close any existing project
    Close();

    // Create project path
    fs::path basePath = fs::path(parentPath) / name;
    projectPath = basePath.string();
    projectName = name;

    // Check if directory already exists
    if (fs::exists(basePath)) {
        std::cerr << "[Project] Directory already exists: " << projectPath << std::endl;
        return false;
    }

    // Create directory structure
    if (!CreateDirectoryStructure()) {
        std::cerr << "[Project] Failed to create directory structure" << std::endl;
        return false;
    }

    // Save project file
    if (!SaveProjectFile()) {
        std::cerr << "[Project] Failed to save project file" << std::endl;
        return false;
    }

    isOpen = true;
    AddToRecentProjects(projectPath);

    std::cout << "[Project] Created project: " << projectName << " at " << projectPath << std::endl;
    return true;
}

bool Project::Open(const std::string& path) {
    // Close any existing project
    Close();

    fs::path projectDir = fs::path(path);

    // Check if it's a valid project
    if (!IsValidProject(path)) {
        std::cerr << "[Project] Not a valid Molga project: " << path << std::endl;
        return false;
    }

    projectPath = projectDir.string();

    // Load project file
    if (!LoadProjectFile()) {
        std::cerr << "[Project] Failed to load project file" << std::endl;
        return false;
    }

    isOpen = true;
    AddToRecentProjects(projectPath);

    std::cout << "[Project] Opened project: " << projectName << " at " << projectPath << std::endl;
    return true;
}

void Project::Close() {
    if (isOpen) {
        std::cout << "[Project] Closed project: " << projectName << std::endl;
    }
    projectPath.clear();
    projectName.clear();
    isOpen = false;
}

std::string Project::GetAssetsPath() const {
    if (!isOpen) return "";
    return (fs::path(projectPath) / "Assets").string();
}

std::string Project::GetScenesPath() const {
    if (!isOpen) return "";
    return (fs::path(projectPath) / "Scenes").string();
}

std::string Project::GetSettingsPath() const {
    if (!isOpen) return "";
    return (fs::path(projectPath) / "ProjectSettings").string();
}

std::string Project::GetScriptsPath() const {
    if (!isOpen) return "";
    return (fs::path(projectPath) / "Scripts").string();
}

std::string Project::GetScriptsBuildPath() const {
    if (!isOpen) return "";
    return (fs::path(projectPath) / "Scripts" / "build").string();
}

std::string Project::GetVSCodePath() const {
    if (!isOpen) return "";
    return (fs::path(projectPath) / ".vscode").string();
}

std::string Project::GetAbsolutePath(const std::string& relativePath) const {
    if (!isOpen || relativePath.empty()) return relativePath;

    // If already absolute, return as-is
    fs::path p(relativePath);
    if (p.is_absolute()) return relativePath;

    // Combine with project path
    return (fs::path(projectPath) / relativePath).string();
}

std::string Project::GetRelativePath(const std::string& absolutePath) const {
    if (!isOpen || absolutePath.empty()) return absolutePath;

    fs::path absPath(absolutePath);
    fs::path projPath(projectPath);

    // Check if the path is under project directory
    auto relPath = fs::relative(absPath, projPath);
    if (relPath.string().find("..") == 0) {
        // Path is outside project, return absolute
        return absolutePath;
    }

    return relPath.string();
}

bool Project::IsValidProject(const std::string& path) {
    fs::path projectDir(path);
    fs::path projectFile = projectDir / "project.molga";
    return fs::exists(projectFile);
}

bool Project::CreateDirectoryStructure() {
    try {
        fs::path basePath(projectPath);

        // Create main project directory
        fs::create_directories(basePath);

        // Create subdirectories
        fs::create_directories(basePath / "Assets");
        fs::create_directories(basePath / "Assets" / "Textures");
        fs::create_directories(basePath / "Assets" / "Audio");
        fs::create_directories(basePath / "Scenes");
        fs::create_directories(basePath / "ProjectSettings");

        // Create Scripts directory for user C++ scripts
        fs::create_directories(basePath / "Scripts");

        return true;
    } catch (const std::exception& e) {
        std::cerr << "[Project] Error creating directories: " << e.what() << std::endl;
        return false;
    }
}

bool Project::SaveProjectFile() {
    try {
        fs::path projectFile = fs::path(projectPath) / "project.molga";

        nlohmann::json j;
        j["name"] = projectName;
        j["version"] = "1.0";
        j["engine_version"] = "0.1.0";

        std::ofstream file(projectFile);
        if (!file.is_open()) {
            return false;
        }

        file << j.dump(2);
        file.close();

        return true;
    } catch (const std::exception& e) {
        std::cerr << "[Project] Error saving project file: " << e.what() << std::endl;
        return false;
    }
}

bool Project::LoadProjectFile() {
    try {
        fs::path projectFile = fs::path(projectPath) / "project.molga";

        std::ifstream file(projectFile);
        if (!file.is_open()) {
            return false;
        }

        nlohmann::json j;
        file >> j;
        file.close();

        if (j.contains("name")) {
            projectName = j["name"].get<std::string>();
        } else {
            // Fallback to directory name
            projectName = fs::path(projectPath).filename().string();
        }

        return true;
    } catch (const std::exception& e) {
        std::cerr << "[Project] Error loading project file: " << e.what() << std::endl;
        return false;
    }
}

std::vector<std::string> Project::GetRecentProjects() const {
    return recentProjects;
}

void Project::AddToRecentProjects(const std::string& path) {
    // Load existing recent projects
    LoadRecentProjects();

    // Remove if already exists
    auto it = std::find(recentProjects.begin(), recentProjects.end(), path);
    if (it != recentProjects.end()) {
        recentProjects.erase(it);
    }

    // Add to front
    recentProjects.insert(recentProjects.begin(), path);

    // Limit size
    if (recentProjects.size() > MAX_RECENT_PROJECTS) {
        recentProjects.resize(MAX_RECENT_PROJECTS);
    }

    SaveRecentProjects();
}

void Project::LoadRecentProjects() {
    recentProjects.clear();

    try {
        // Store in user's home directory
        const char* home = std::getenv("HOME");
        if (!home) return;

        fs::path configPath = fs::path(home) / ".molga" / "recent_projects.json";
        if (!fs::exists(configPath)) return;

        std::ifstream file(configPath);
        if (!file.is_open()) return;

        nlohmann::json j;
        file >> j;
        file.close();

        if (j.contains("recent") && j["recent"].is_array()) {
            for (const auto& path : j["recent"]) {
                std::string p = path.get<std::string>();
                // Only add if project still exists
                if (IsValidProject(p)) {
                    recentProjects.push_back(p);
                }
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "[Project] Error loading recent projects: " << e.what() << std::endl;
    }
}

void Project::SaveRecentProjects() {
    try {
        const char* home = std::getenv("HOME");
        if (!home) return;

        fs::path configDir = fs::path(home) / ".molga";
        fs::create_directories(configDir);

        fs::path configPath = configDir / "recent_projects.json";

        nlohmann::json j;
        j["recent"] = recentProjects;

        std::ofstream file(configPath);
        if (!file.is_open()) return;

        file << j.dump(2);
        file.close();
    } catch (const std::exception& e) {
        std::cerr << "[Project] Error saving recent projects: " << e.what() << std::endl;
    }
}
