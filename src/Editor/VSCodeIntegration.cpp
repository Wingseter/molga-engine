#include "VSCodeIntegration.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <iostream>
#include <cstdlib>

namespace fs = std::filesystem;

VSCodeIntegration& VSCodeIntegration::Get() {
    static VSCodeIntegration instance;
    return instance;
}

bool VSCodeIntegration::SetupVSCodeProject(const std::string& projectPath) {
    // Create .vscode directory
    fs::path vscodePath = fs::path(projectPath) / ".vscode";
    try {
        fs::create_directories(vscodePath);
    } catch (const std::exception& e) {
        std::cerr << "[VSCode] Failed to create .vscode directory: " << e.what() << std::endl;
        return false;
    }

    // Generate all config files
    bool success = true;
    success &= GenerateTasksJson(projectPath);
    success &= GenerateCppPropertiesJson(projectPath);
    success &= GenerateLaunchJson(projectPath);

    if (success) {
        std::cout << "[VSCode] Project setup complete" << std::endl;
    }

    return success;
}

bool VSCodeIntegration::GenerateTasksJson(const std::string& projectPath) {
    fs::path tasksPath = fs::path(projectPath) / ".vscode" / "tasks.json";

    std::stringstream ss;
    ss << "{\n";
    ss << "    \"version\": \"2.0.0\",\n";
    ss << "    \"tasks\": [\n";

    // Configure task
    ss << "        {\n";
    ss << "            \"label\": \"Configure Scripts\",\n";
    ss << "            \"type\": \"shell\",\n";
    ss << "            \"command\": \"cmake\",\n";
    ss << "            \"args\": [\n";
    ss << "                \"-S\", \"${workspaceFolder}/Scripts\",\n";
    ss << "                \"-B\", \"${workspaceFolder}/Scripts/build\",\n";
    ss << "                \"-DCMAKE_BUILD_TYPE=Debug\"\n";
    ss << "            ],\n";
    ss << "            \"problemMatcher\": [],\n";
    ss << "            \"detail\": \"Configure CMake for scripts\"\n";
    ss << "        },\n";

    // Build task
    ss << "        {\n";
    ss << "            \"label\": \"Build Scripts\",\n";
    ss << "            \"type\": \"shell\",\n";
    ss << "            \"command\": \"cmake\",\n";
    ss << "            \"args\": [\n";
    ss << "                \"--build\",\n";
    ss << "                \"${workspaceFolder}/Scripts/build\",\n";
    ss << "                \"--config\",\n";
    ss << "                \"Debug\"\n";
    ss << "            ],\n";
    ss << "            \"group\": {\n";
    ss << "                \"kind\": \"build\",\n";
    ss << "                \"isDefault\": true\n";
    ss << "            },\n";
    ss << "            \"problemMatcher\": [\"$gcc\"],\n";
    ss << "            \"detail\": \"Build user scripts to dynamic library\",\n";
    ss << "            \"dependsOn\": \"Configure Scripts\"\n";
    ss << "        },\n";

    // Clean task
    ss << "        {\n";
    ss << "            \"label\": \"Clean Scripts\",\n";
    ss << "            \"type\": \"shell\",\n";
    ss << "            \"command\": \"cmake\",\n";
    ss << "            \"args\": [\n";
    ss << "                \"--build\",\n";
    ss << "                \"${workspaceFolder}/Scripts/build\",\n";
    ss << "                \"--target\", \"clean\"\n";
    ss << "            ],\n";
    ss << "            \"problemMatcher\": [],\n";
    ss << "            \"detail\": \"Clean build files\"\n";
    ss << "        }\n";

    ss << "    ]\n";
    ss << "}\n";

    std::ofstream file(tasksPath);
    if (!file.is_open()) {
        std::cerr << "[VSCode] Failed to create tasks.json" << std::endl;
        return false;
    }
    file << ss.str();
    file.close();

    std::cout << "[VSCode] Generated tasks.json" << std::endl;
    return true;
}

bool VSCodeIntegration::GenerateCppPropertiesJson(const std::string& projectPath) {
    fs::path propsPath = fs::path(projectPath) / ".vscode" / "c_cpp_properties.json";

    auto includePaths = GetEngineIncludePaths();

    std::stringstream ss;
    ss << "{\n";
    ss << "    \"configurations\": [\n";

    // macOS configuration
    ss << "        {\n";
    ss << "            \"name\": \"Mac\",\n";
    ss << "            \"includePath\": [\n";
    ss << "                \"${workspaceFolder}/Scripts/**\"";

    for (const auto& path : includePaths) {
        ss << ",\n                \"" << path << "/**\"";
    }

    ss << "\n            ],\n";
    ss << "            \"defines\": [\"MOLGA_EDITOR\"],\n";
    ss << "            \"macFrameworkPath\": [\n";
    ss << "                \"/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk/System/Library/Frameworks\"\n";
    ss << "            ],\n";
    ss << "            \"compilerPath\": \"/usr/bin/clang++\",\n";
    ss << "            \"cStandard\": \"c17\",\n";
    ss << "            \"cppStandard\": \"c++17\",\n";
    ss << "            \"intelliSenseMode\": \"macos-clang-x64\"\n";
    ss << "        },\n";

    // Windows configuration
    ss << "        {\n";
    ss << "            \"name\": \"Win32\",\n";
    ss << "            \"includePath\": [\n";
    ss << "                \"${workspaceFolder}/Scripts/**\"";

    for (const auto& path : includePaths) {
        ss << ",\n                \"" << path << "/**\"";
    }

    ss << "\n            ],\n";
    ss << "            \"defines\": [\"_WIN32\", \"MOLGA_EDITOR\"],\n";
    ss << "            \"cStandard\": \"c17\",\n";
    ss << "            \"cppStandard\": \"c++17\",\n";
    ss << "            \"intelliSenseMode\": \"windows-msvc-x64\"\n";
    ss << "        },\n";

    // Linux configuration
    ss << "        {\n";
    ss << "            \"name\": \"Linux\",\n";
    ss << "            \"includePath\": [\n";
    ss << "                \"${workspaceFolder}/Scripts/**\"";

    for (const auto& path : includePaths) {
        ss << ",\n                \"" << path << "/**\"";
    }

    ss << "\n            ],\n";
    ss << "            \"defines\": [\"MOLGA_EDITOR\"],\n";
    ss << "            \"compilerPath\": \"/usr/bin/g++\",\n";
    ss << "            \"cStandard\": \"c17\",\n";
    ss << "            \"cppStandard\": \"c++17\",\n";
    ss << "            \"intelliSenseMode\": \"linux-gcc-x64\"\n";
    ss << "        }\n";

    ss << "    ],\n";
    ss << "    \"version\": 4\n";
    ss << "}\n";

    std::ofstream file(propsPath);
    if (!file.is_open()) {
        std::cerr << "[VSCode] Failed to create c_cpp_properties.json" << std::endl;
        return false;
    }
    file << ss.str();
    file.close();

    std::cout << "[VSCode] Generated c_cpp_properties.json" << std::endl;
    return true;
}

bool VSCodeIntegration::GenerateLaunchJson(const std::string& projectPath) {
    fs::path launchPath = fs::path(projectPath) / ".vscode" / "launch.json";

    std::stringstream ss;
    ss << "{\n";
    ss << "    \"version\": \"0.2.0\",\n";
    ss << "    \"configurations\": [\n";

    // LLDB configuration for macOS
    ss << "        {\n";
    ss << "            \"name\": \"Debug Scripts (LLDB)\",\n";
    ss << "            \"type\": \"lldb\",\n";
    ss << "            \"request\": \"attach\",\n";
    ss << "            \"program\": \"${workspaceFolder}/Scripts/build/libUserScripts.dylib\",\n";
    ss << "            \"pid\": \"${command:pickProcess}\",\n";
    ss << "            \"stopOnEntry\": false\n";
    ss << "        }\n";

    ss << "    ]\n";
    ss << "}\n";

    std::ofstream file(launchPath);
    if (!file.is_open()) {
        std::cerr << "[VSCode] Failed to create launch.json" << std::endl;
        return false;
    }
    file << ss.str();
    file.close();

    std::cout << "[VSCode] Generated launch.json" << std::endl;
    return true;
}

bool VSCodeIntegration::OpenInVSCode(const std::string& projectPath) {
    if (!IsVSCodeAvailable()) {
        std::cerr << "[VSCode] Visual Studio Code is not available" << std::endl;
        return false;
    }

    // Setup VSCode project first
    SetupVSCodeProject(projectPath);

    // Open VSCode
    std::string cmd = GetVSCodeCommand() + " \"" + projectPath + "\"";
    return ExecuteCommand(cmd);
}

bool VSCodeIntegration::OpenFileInVSCode(const std::string& filePath) {
    if (!IsVSCodeAvailable()) {
        std::cerr << "[VSCode] Visual Studio Code is not available" << std::endl;
        return false;
    }

    std::string cmd = GetVSCodeCommand() + " \"" + filePath + "\"";
    return ExecuteCommand(cmd);
}

bool VSCodeIntegration::IsVSCodeAvailable() const {
    std::string cmd = GetVSCodeCommand() + " --version";

#ifdef _WIN32
    cmd += " > NUL 2>&1";
#else
    cmd += " > /dev/null 2>&1";
#endif

    return system(cmd.c_str()) == 0;
}

std::string VSCodeIntegration::GetVSCodeCommand() const {
#ifdef _WIN32
    // Windows - try code.cmd first, then full path
    return "code";
#elif defined(__APPLE__)
    // macOS - assumes 'code' is in PATH (installed via "Install 'code' command in PATH")
    return "code";
#else
    // Linux
    return "code";
#endif
}

std::vector<std::string> VSCodeIntegration::GetEngineIncludePaths() const {
    std::vector<std::string> paths;

    if (!enginePath.empty()) {
        paths.push_back((fs::path(enginePath) / "src").string());
        paths.push_back((fs::path(enginePath) / "external" / "glm").string());
        paths.push_back((fs::path(enginePath) / "external" / "imgui").string());
        paths.push_back((fs::path(enginePath) / "external" / "json" / "include").string());
    }

    return paths;
}

bool VSCodeIntegration::ExecuteCommand(const std::string& command) const {
#ifdef _WIN32
    std::string cmd = "start \"\" " + command;
#elif defined(__APPLE__)
    std::string cmd = command + " &";
#else
    std::string cmd = command + " &";
#endif

    int result = system(cmd.c_str());
    return result == 0;
}
