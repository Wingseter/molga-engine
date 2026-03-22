#ifndef MOLGA_VSCODE_INTEGRATION_H
#define MOLGA_VSCODE_INTEGRATION_H

#include <string>
#include <vector>

class VSCodeIntegration {
public:
    static VSCodeIntegration& Get();

    // Setup project for VSCode (creates .vscode folder with all configs)
    bool SetupVSCodeProject(const std::string& projectPath);

    // Generate individual config files
    bool GenerateTasksJson(const std::string& projectPath);
    bool GenerateCppPropertiesJson(const std::string& projectPath);
    bool GenerateLaunchJson(const std::string& projectPath);

    // Open VSCode
    bool OpenInVSCode(const std::string& projectPath);
    bool OpenFileInVSCode(const std::string& filePath);

    // Check if VSCode is available
    bool IsVSCodeAvailable() const;

    // Engine path for includes
    void SetEnginePath(const std::string& path) { enginePath = path; }
    const std::string& GetEnginePath() const { return enginePath; }

private:
    VSCodeIntegration() = default;

    std::string GetVSCodeCommand() const;
    std::vector<std::string> GetEngineIncludePaths() const;
    bool ExecuteCommand(const std::string& command) const;

    std::string enginePath;
};

#endif // MOLGA_VSCODE_INTEGRATION_H
