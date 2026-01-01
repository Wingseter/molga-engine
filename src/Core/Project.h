#ifndef MOLGA_PROJECT_H
#define MOLGA_PROJECT_H

#include <string>
#include <vector>

class Project {
public:
    static Project& Get();

    // Project lifecycle
    bool Create(const std::string& parentPath, const std::string& name);
    bool Open(const std::string& path);
    void Close();

    // State queries
    bool IsOpen() const { return isOpen; }
    const std::string& GetPath() const { return projectPath; }
    const std::string& GetName() const { return projectName; }

    // Standard paths
    std::string GetAssetsPath() const;
    std::string GetScenesPath() const;
    std::string GetSettingsPath() const;

    // Path conversion
    std::string GetAbsolutePath(const std::string& relativePath) const;
    std::string GetRelativePath(const std::string& absolutePath) const;

    // Recent projects
    std::vector<std::string> GetRecentProjects() const;
    void AddToRecentProjects(const std::string& path);

    // Validate if a path is a valid project
    static bool IsValidProject(const std::string& path);

private:
    Project() = default;
    Project(const Project&) = delete;
    Project& operator=(const Project&) = delete;

    bool CreateDirectoryStructure();
    bool SaveProjectFile();
    bool LoadProjectFile();
    void LoadRecentProjects();
    void SaveRecentProjects();

    std::string projectPath;
    std::string projectName;
    bool isOpen = false;

    std::vector<std::string> recentProjects;
    static constexpr int MAX_RECENT_PROJECTS = 10;
};

#endif // MOLGA_PROJECT_H
