#include "GameBuilder.h"
#include "../Core/PathConstants.h"
#include "../Core/PathService.h"
#include "../Core/BuildManifest.h"
#include "../Core/PackageLayout.h"
#include "../Core/PackageFinalizer.h"
#include "../Core/ProjectSettings.h"
#include "../Systems/Input.h"
#include "Project.h"
#include "Editor/Profiling/ProfilerReportSink.h"
#include "Core/Profiling/ScopedTimer.h"
#include <fstream>
#include <iostream>
#include <filesystem>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;

static fs::path ResolveProjectPath(const fs::path& projectRoot, const std::string& stored) {
    fs::path p(stored);
    return p.is_absolute() ? p : projectRoot / p;
}

GameBuilder& GameBuilder::Get() {
    static GameBuilder instance;
    return instance;
}

bool GameBuilder::Build(const BuildSettings& settings) {
    long long totalStart = molga::NowNanos();

    progress = 0.0f;
    lastError.clear();

    // Validate the profile
    std::string profileError;
    if (!settings.profile.Validate(profileError)) {
        lastError = profileError;
        return false;
    }

    // Resolve output paths
    const fs::path finalOutput = settings.projectRoot.empty()
        ? fs::path(settings.profile.outputPath)
        : ResolveProjectPath(settings.projectRoot, settings.profile.outputPath);
    const fs::path stagingOutput = fs::path(finalOutput.string() + ".staging");

    // Safety check (rejects project root and engine root)
    {
        std::string reason;
        if (!PathService::IsSafeOutputPath(finalOutput, reason,
                settings.projectRoot, PathService::Get().ExecutableDir())) {
            lastError = "Refusing to use output path '" + finalOutput.string() + "': " + reason;
            return false;
        }
    }

    // Build required-file manifest
    {
        long long t0 = molga::NowNanos();
        BuildManifest manifest;
        if (Project::Get().IsOpen()) {
            manifest.requiredFiles.push_back(Project::Get().GetAssetsPath());
        }
        manifest.requiredFiles.push_back(PathService::Get().EngineResource("Shaders").string());
        manifest.requiredFiles.push_back((PathService::Get().ExecutableDir() / "molga_runtime").string());
        for (const auto& scene : settings.profile.scenes) {
            manifest.requiredFiles.push_back(ResolveProjectPath(settings.projectRoot, scene).string());
        }

        std::string err;
        if (!manifest.Validate(err)) {
            lastError = err;
            return false;
        }
        double ms = (molga::NowNanos() - t0) / 1.0e6;
        molga::ActiveReportSink().ReportTiming("Build: Validate manifest", ms, "");
    }

    // Prepare staging directory
    {
        long long t0 = molga::NowNanos();
        try {
            if (fs::exists(stagingOutput)) {
                fs::remove_all(stagingOutput);
            }
            fs::create_directories(stagingOutput);
        } catch (const std::exception& e) {
            lastError = "Failed to create staging directory: " + std::string(e.what());
            return false;
        }
        double ms = (molga::NowNanos() - t0) / 1.0e6;
        molga::ActiveReportSink().ReportTiming("Build: Prepare staging", ms, "");
    }

    auto cleanupStaging = [&]() {
        std::error_code ec;
        fs::remove_all(stagingOutput, ec);
    };

    const std::string stagingPathStr = stagingOutput.string();

    // Step 2: Copy assets
    currentStep = "Copying assets...";
    progress = 0.2f;
    {
        long long t0 = molga::NowNanos();
        if (!CopyAssets(stagingPathStr)) {
            cleanupStaging();
            return false;
        }
        double ms = (molga::NowNanos() - t0) / 1.0e6;
        molga::ActiveReportSink().ReportTiming("Build: Copy assets", ms, "");
    }

    // Step 3: Copy shaders
    currentStep = "Copying shaders...";
    progress = 0.4f;
    {
        long long t0 = molga::NowNanos();
        if (!CopyShaders(stagingPathStr)) {
            cleanupStaging();
            return false;
        }
        double ms = (molga::NowNanos() - t0) / 1.0e6;
        molga::ActiveReportSink().ReportTiming("Build: Copy shaders", ms, "");
    }

    // Step 4: Copy scenes
    currentStep = "Copying scenes...";
    progress = 0.5f;
    {
        long long t0 = molga::NowNanos();
        if (!CopyScenes(settings, stagingPathStr)) {
            cleanupStaging();
            return false;
        }
        double ms = (molga::NowNanos() - t0) / 1.0e6;
        molga::ActiveReportSink().ReportTiming("Build: Copy scenes", ms, "");
    }

    // Step 5: Generate game config
    currentStep = "Generating game configuration...";
    progress = 0.7f;
    {
        long long t0 = molga::NowNanos();
        if (!GenerateGameConfig(settings, stagingPathStr)) {
            cleanupStaging();
            return false;
        }
        double ms = (molga::NowNanos() - t0) / 1.0e6;
        molga::ActiveReportSink().ReportTiming("Build: Generate game config", ms, "");
    }

    // Step 6: Copy executable
    currentStep = "Copying executable...";
    progress = 0.9f;
    {
        long long t0 = molga::NowNanos();
        if (!CopyExecutable(stagingPathStr, settings.profile.gameName)) {
            cleanupStaging();
            return false;
        }
        double ms = (molga::NowNanos() - t0) / 1.0e6;
        molga::ActiveReportSink().ReportTiming("Build: Copy executable", ms, "");
    }

    // Validate output package layout and finalize
    {
        long long t0 = molga::NowNanos();
        std::string packageError;
        if (!PackageLayout::Validate(
                stagingOutput,
                PackageLayout::ExecutableNameFor(settings.profile.gameName),
                packageError)) {
            lastError = packageError;
            cleanupStaging();
            return false;
        }

        // Atomic swap: staging -> final
        const auto finalizeResult =
            PackageFinalizer::FinalizeStagedPackage(stagingOutput, finalOutput);
        if (!finalizeResult) {
            lastError = "Failed to finalize output: " + finalizeResult.error;
            cleanupStaging();
            return false;
        }
        double ms = (molga::NowNanos() - t0) / 1.0e6;
        molga::ActiveReportSink().ReportTiming("Build: Finalize package", ms, "");
    }

    currentStep = "Build complete!";
    progress = 1.0f;

    double totalMs = (molga::NowNanos() - totalStart) / 1.0e6;
    molga::ActiveReportSink().ReportTiming("Build: Total", totalMs, settings.profile.gameName);

    std::cout << "[GameBuilder] Build successful: " << finalOutput.string() << " (" << totalMs << " ms)" << std::endl;
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
        fs::path dest = fs::path(outputPath) / Paths::Build::ASSETS;
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
        fs::path dest = fs::path(outputPath) / Paths::Build::SHADERS;
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
        const fs::path scenesDir = fs::path(outputPath) / Paths::Build::SCENES;
        fs::create_directories(scenesDir);

        for (const auto& sceneRel : settings.profile.scenes) {
            const fs::path src = ResolveProjectPath(settings.projectRoot, sceneRel);
            if (!fs::exists(src)) {
                lastError = "Scene listed in build profile is missing: " + src.string();
                return false;
            }
            const fs::path dest = scenesDir / fs::path(sceneRel).filename();
            fs::copy_file(src, dest, fs::copy_options::overwrite_existing);
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
        config["schemaVersion"] = 1;
        config["gameName"] = settings.profile.gameName;
        config["productVersion"] = settings.profile.productVersion;
        config["companyName"] = settings.profile.companyName;
        config["mainScene"] = std::string(Paths::Build::SCENES) + "/" +
                              fs::path(settings.profile.startupScene).filename().string();
        config["windowWidth"] = settings.profile.window.width;
        config["windowHeight"] = settings.profile.window.height;
        config["fullscreen"] = settings.profile.window.fullscreen;
        config["resizable"] = settings.profile.window.resizable;
        config["developmentBuild"] = settings.profile.developmentBuild;
        config["projectSettings"] = ProjectSettings::Get().Serialize();

        // Bundle input actions
        nlohmann::json inputActionsJson = nlohmann::json::array();
        for (const auto& action : Input::GetActions()) {
            nlohmann::json actionJson;
            actionJson["name"] = action.name;
            actionJson["isAxis"] = action.isAxis;

            nlohmann::json bindingsJson = nlohmann::json::array();
            for (const auto& binding : action.bindings) {
                nlohmann::json bindingJson;
                std::string devStr = "Keyboard";
                switch (binding.device) {
                    case Input::DeviceType::Keyboard: devStr = "Keyboard"; break;
                    case Input::DeviceType::Mouse: devStr = "Mouse"; break;
                    case Input::DeviceType::GamepadButton: devStr = "GamepadButton"; break;
                    case Input::DeviceType::GamepadAxis: devStr = "GamepadAxis"; break;
                }
                bindingJson["device"] = devStr;
                bindingJson["code"] = binding.code;
                bindingJson["multiplier"] = binding.multiplier;
                bindingsJson.push_back(bindingJson);
            }
            actionJson["bindings"] = bindingsJson;
            inputActionsJson.push_back(actionJson);
        }
        config["inputActions"] = inputActionsJson;

        // List all scenes
        nlohmann::json scenesList = nlohmann::json::array();
        for (const auto& scene : settings.profile.scenes) {
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

        std::string execName = PackageLayout::ExecutableNameFor(gameName);

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
