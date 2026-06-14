#include "GameBuilder.h"
#include "../Core/PathConstants.h"
#include "../Core/PathService.h"
#include "../Core/BuildManifest.h"
#include "../Core/PackageLayout.h"
#include "Project.h"
#include <fstream>
#include <iostream>
#include <filesystem>
#include <nlohmann/json.hpp>

namespace {
std::string RuntimeOutputName(const std::string& gameName) {
#ifdef _WIN32
    return gameName + ".exe";
#else
    return gameName;
#endif
}
}

namespace fs = std::filesystem;

GameBuilder& GameBuilder::Get() {
    static GameBuilder instance;
    return instance;
}

bool GameBuilder::Build(const BuildSettings& settings) {
    progress = 0.0f;
    lastError.clear();

    // 필수 입력 검증(절대 경로)
    {
        BuildManifest manifest;
        if (Project::Get().IsOpen()) {
            manifest.requiredFiles.push_back(Project::Get().GetAssetsPath());
        }
        manifest.requiredFiles.push_back(PathService::Get().EngineResource("Shaders").string());
        manifest.requiredFiles.push_back(settings.mainScene);
        manifest.requiredFiles.push_back((PathService::Get().ExecutableDir() / "molga_runtime").string());

        std::string err;
        if (!manifest.Validate(err)) {
            lastError = err;
            return false;
        }
    }

    // Step 1: Create output directory
    currentStep = "Creating output directory...";
    progress = 0.1f;
    if (!CreateOutputDirectory(settings.outputPath)) {
        return false;
    }

    // Step 2: Copy assets
    currentStep = "Copying assets...";
    progress = 0.2f;
    if (!CopyAssets(settings.outputPath)) {
        return false;
    }

    // Step 3: Copy shaders
    currentStep = "Copying shaders...";
    progress = 0.4f;
    if (!CopyShaders(settings.outputPath)) {
        return false;
    }

    // Step 4: Copy scenes
    currentStep = "Copying scenes...";
    progress = 0.5f;
    if (!CopyScenes(settings, settings.outputPath)) {
        return false;
    }

    // Step 5: Generate game config
    currentStep = "Generating game configuration...";
    progress = 0.7f;
    if (!GenerateGameConfig(settings, settings.outputPath)) {
        return false;
    }

    // Step 6: Copy executable
    currentStep = "Copying executable...";
    progress = 0.9f;
    if (!CopyExecutable(settings.outputPath, settings.gameName)) {
        return false;
    }

    // Validate output package layout
    std::string packageError;
    if (!PackageLayout::Validate(
            settings.outputPath,
            RuntimeOutputName(settings.gameName),
            packageError)) {
        lastError = packageError;
        return false;
    }

    currentStep = "Build complete!";
    progress = 1.0f;

    std::cout << "[GameBuilder] Build successful: " << settings.outputPath << "/" << settings.gameName << std::endl;
    return true;
}

bool GameBuilder::CreateOutputDirectory(const std::string& path) {
    try {
        std::string reason;
        if (!PathService::IsSafeOutputPath(path, reason)) {
            lastError = "Refusing to use output path '" + path + "': " + reason;
            return false;
        }
        if (fs::exists(path)) {
            fs::remove_all(path);
        }
        fs::create_directories(path);
        return true;
    } catch (const std::exception& e) {
        lastError = "Failed to create output directory: " + std::string(e.what());
        return false;
    }
}

bool GameBuilder::CopyAssets(const std::string& outputPath) {
    try {
        if (!Project::Get().IsOpen()) {
            lastError = "No project open; cannot locate Assets to build.";
            return false;
        }
        fs::path src = Project::Get().GetAssetsPath();
        if (!fs::exists(src)) {
            lastError = "Project Assets folder not found: " + src.string();
            return false;
        }
        fs::path dest = fs::path(outputPath) / "Assets"; // casing preserved
        fs::create_directories(dest);
        fs::copy(src, dest, fs::copy_options::recursive | fs::copy_options::overwrite_existing);
        return true;
    } catch (const std::exception& e) {
        lastError = "Failed to copy assets: " + std::string(e.what());
        return false;
    }
}

bool GameBuilder::CopyShaders(const std::string& outputPath) {
    try {
        fs::path src = PathService::Get().EngineResource("Shaders");
        if (!fs::exists(src)) {
            lastError = "Engine Shaders folder not found next to the editor: " + src.string();
            return false;
        }
        fs::path dest = fs::path(outputPath) / "Shaders";
        fs::create_directories(dest);
        fs::copy(src, dest, fs::copy_options::recursive | fs::copy_options::overwrite_existing);
        return true;
    } catch (const std::exception& e) {
        lastError = "Failed to copy shaders: " + std::string(e.what());
        return false;
    }
}

bool GameBuilder::CopyScenes(const BuildSettings& settings, const std::string& outputPath) {
    try {
        std::string scenesPath = outputPath + "/" + Paths::Build::SCENES;
        fs::create_directories(scenesPath);

        // Copy main scene
        if (!fs::exists(settings.mainScene)) {
            lastError = "Main scene not found: " + settings.mainScene;
            return false;
        }
        fs::copy_file(settings.mainScene, scenesPath + "/main.json",
                     fs::copy_options::overwrite_existing);

        // Copy additional scenes
        for (const auto& scene : settings.scenes) {
            if (fs::exists(scene)) {
                std::string filename = fs::path(scene).filename().string();
                fs::copy_file(scene, scenesPath + "/" + filename,
                             fs::copy_options::overwrite_existing);
            }
        }

        return true;
    } catch (const std::exception& e) {
        lastError = "Failed to copy scenes: " + std::string(e.what());
        return false;
    }
}

bool GameBuilder::GenerateGameConfig(const BuildSettings& settings, const std::string& outputPath) {
    try {
        nlohmann::json config;
        config["gameName"] = settings.gameName;
        config["mainScene"] = std::string(Paths::Build::SCENES) + "/main.json";
        config["windowWidth"] = settings.windowWidth;
        config["windowHeight"] = settings.windowHeight;
        config["fullscreen"] = settings.fullscreen;

        // List all scenes
        nlohmann::json scenesList = nlohmann::json::array();
        scenesList.push_back(std::string(Paths::Build::SCENES) + "/main.json");
        for (const auto& scene : settings.scenes) {
            std::string filename = fs::path(scene).filename().string();
            scenesList.push_back(std::string(Paths::Build::SCENES) + "/" + filename);
        }
        config["scenes"] = scenesList;

        std::ofstream file(outputPath + "/game.json");
        if (!file.is_open()) {
            lastError = "Failed to create game.json";
            return false;
        }
        file << config.dump(2);
        file.close();

        return true;
    } catch (const std::exception& e) {
        lastError = "Failed to generate game config: " + std::string(e.what());
        return false;
    }
}

bool GameBuilder::CopyExecutable(const std::string& outputPath, const std::string& gameName) {
    try {
        fs::path runtimePath = PathService::Get().ExecutableDir() / "molga_runtime";
        if (!fs::exists(runtimePath)) {
            lastError = "Runtime executable not found next to the editor: " + runtimePath.string()
                      + ". Build the molga_runtime target first.";
            return false;
        }

#ifdef __APPLE__
        std::string execName = gameName;
#else
        std::string execName = gameName + ".exe";
#endif

        fs::copy_file(runtimePath, outputPath + "/" + execName,
                     fs::copy_options::overwrite_existing);

        // Make executable on Unix
#ifndef _WIN32
        fs::permissions(outputPath + "/" + execName,
                       fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec,
                       fs::perm_options::add);
#endif

        return true;
    } catch (const std::exception& e) {
        lastError = "Failed to copy executable: " + std::string(e.what());
        return false;
    }
}
