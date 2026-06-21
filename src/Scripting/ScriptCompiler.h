#pragma once

#include <string>
#include <vector>
#include <ctime>

struct ScriptInfo {
    std::string name;           // e.g., "PlayerController"
    std::string headerPath;     // e.g., "Scripts/PlayerController.h"
    std::string sourcePath;     // e.g., "Scripts/PlayerController.cpp"
    std::time_t lastModified;
};

class ScriptCompiler {
public:
    static ScriptCompiler& Get();

    // Initialize with project path
    void SetProjectPath(const std::string& projectPath);

    // Script discovery
    std::vector<ScriptInfo> DiscoverScripts() const;
    bool HasScriptsChanged() const;

    // CMakeLists.txt generation
    bool GenerateCMakeLists();

    // Compilation
    bool Compile();
    bool IsCompiling() const { return isCompiling; }
    const std::string& GetCompileOutput() const { return compileOutput; }
    const std::string& GetLastError() const { return lastError; }

    // Script template generation
    // targetDir: 비어있으면 scriptsPath(루트)에 생성. 하위 폴더 지원.
    bool CreateScriptTemplate(const std::string& scriptName,
                              const std::string& targetDir = "");

    // Get compiled library path
    std::string GetCompiledLibraryPath() const;

    // Engine paths
    std::string GetEngineIncludePath() const;
    std::string GetEnginePath() const { return enginePath; }
    void SetEnginePath(const std::string& path) { enginePath = path; }

    std::string ConfigureCommand() const;
    std::string BuildCommand() const;
    const std::string& ScriptsDir() const { return scriptsPath; }

private:
    ScriptCompiler() = default;

    std::string GenerateCMakeContent(const std::vector<ScriptInfo>& scripts);
    std::string GenerateScriptExportsContent(const std::vector<ScriptInfo>& scripts);
    std::string GenerateHeaderTemplate(const std::string& className);
    std::string GenerateSourceTemplate(const std::string& className);

    bool ExecuteCommand(const std::string& command, std::string& output);

    std::string projectPath;
    std::string scriptsPath;
    std::string buildPath;
    std::string enginePath;

    bool isCompiling = false;
    std::string compileOutput;
    std::string lastError;

    // For change detection
    mutable std::vector<ScriptInfo> cachedScripts;
    mutable std::time_t lastCheckTime = 0;
};
