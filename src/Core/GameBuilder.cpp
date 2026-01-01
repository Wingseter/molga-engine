#include "GameBuilder.h"
#include <fstream>
#include <iostream>
#include <filesystem>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;

GameBuilder& GameBuilder::Get() {
    static GameBuilder instance;
    return instance;
}

bool GameBuilder::Build(const BuildSettings& settings) {
    progress = 0.0f;
    lastError.clear();

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

    currentStep = "Build complete!";
    progress = 1.0f;

    std::cout << "[GameBuilder] Build successful: " << settings.outputPath << "/" << settings.gameName << std::endl;
    return true;
}

bool GameBuilder::CreateOutputDirectory(const std::string& path) {
    try {
        if (fs::exists(path)) {
            // Clean existing directory
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
        std::string assetsPath = "assets";
        std::string destPath = outputPath + "/assets";

        if (fs::exists(assetsPath)) {
            fs::create_directories(destPath);
            fs::copy(assetsPath, destPath, fs::copy_options::recursive | fs::copy_options::overwrite_existing);
        }
        return true;
    } catch (const std::exception& e) {
        lastError = "Failed to copy assets: " + std::string(e.what());
        return false;
    }
}

bool GameBuilder::CopyShaders(const std::string& outputPath) {
    try {
        std::string shadersPath = "src/Shaders";
        std::string destPath = outputPath + "/Shaders";

        if (fs::exists(shadersPath)) {
            fs::create_directories(destPath);
            fs::copy(shadersPath, destPath, fs::copy_options::recursive | fs::copy_options::overwrite_existing);
        }
        return true;
    } catch (const std::exception& e) {
        lastError = "Failed to copy shaders: " + std::string(e.what());
        return false;
    }
}

bool GameBuilder::CopyScenes(const BuildSettings& settings, const std::string& outputPath) {
    try {
        std::string scenesPath = outputPath + "/scenes";
        fs::create_directories(scenesPath);

        // Copy main scene
        if (fs::exists(settings.mainScene)) {
            fs::copy_file(settings.mainScene, scenesPath + "/main.json",
                         fs::copy_options::overwrite_existing);
        }

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
        config["mainScene"] = "scenes/main.json";
        config["windowWidth"] = settings.windowWidth;
        config["windowHeight"] = settings.windowHeight;
        config["fullscreen"] = settings.fullscreen;

        // List all scenes
        nlohmann::json scenesList = nlohmann::json::array();
        scenesList.push_back("scenes/main.json");
        for (const auto& scene : settings.scenes) {
            std::string filename = fs::path(scene).filename().string();
            scenesList.push_back("scenes/" + filename);
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
        // Find the runtime executable
        std::string runtimePath = "build/molga_runtime";

        // Check if runtime exists, if not use the main engine
        if (!fs::exists(runtimePath)) {
            runtimePath = "build/molga_engine";
        }

        if (!fs::exists(runtimePath)) {
            lastError = "Runtime executable not found. Please build the runtime first.";
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
