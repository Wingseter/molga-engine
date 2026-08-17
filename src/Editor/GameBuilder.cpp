#include "GameBuilder.h"
#include "../Core/PathConstants.h"
#include "../Core/PathService.h"
#include "../Core/BuildManifest.h"
#include "../Core/PackageLayout.h"
#include "../Core/PackageFinalizer.h"
#include "../Core/GameConfig.h"
#include "../Common/Sha256.h"
#include "../Core/ProjectSettings.h"
#include "../Platform/Process.h"
#include "../Systems/Input.h"
#include "Project.h"
#include "Editor/Profiling/ProfilerReportSink.h"
#include "Core/Profiling/ScopedTimer.h"
#include <fstream>
#include <functional>
#include <iostream>
#include <filesystem>
#include <unordered_set>
#include <nlohmann/json.hpp>
#include "../ECS/GameObject.h"
#include "../ECS/ComponentFactory.h"
#include "../Scripting/ScriptManager.h"
#include "../Scripting/ScriptCompiler.h"
#include "../Scripting/ScriptApi.h"
#include "../Core/AssetDatabase.h"
#include "../Core/AssetDependencyValidator.h"

namespace fs = std::filesystem;

#ifndef MOLGA_ENGINE_SHADER_DESCRIPTOR_DIR
#define MOLGA_ENGINE_SHADER_DESCRIPTOR_DIR "src/Shaders"
#endif

static std::string ShellQuote(const fs::path& path) {
    const std::string value = path.string();
#ifdef _WIN32
    std::string quoted = "\"";
    for (const char character : value) {
        if (character == '"') quoted += '\\';
        quoted += character;
    }
    return quoted + "\"";
#else
    std::string quoted = "'";
    for (const char character : value) {
        if (character == '\'') quoted += "'\\''";
        else quoted += character;
    }
    return quoted + "'";
#endif
}

static fs::path ShaderCompilerPath() {
    fs::path path = PathService::Get().ExecutableDir() / "molga_shaderc";
#ifdef _WIN32
    path += ".exe";
#endif
    return path;
}

static bool ContainsShaderDescriptors(const fs::path& root) {
    std::error_code error;
    if (!fs::is_directory(root, error)) return false;
    for (fs::recursive_directory_iterator iterator(root, error), end;
         !error && iterator != end; iterator.increment(error)) {
        if (iterator->is_regular_file(error) &&
            iterator->path().extension() == ".json" &&
            iterator->path().filename().string().size() > 12U &&
            iterator->path().filename().string().compare(
                iterator->path().filename().string().size() - 12U, 12U,
                ".shader.json") == 0) {
            return true;
        }
    }
    return false;
}

static bool RunShaderTool(const std::string& command, std::string& output) {
    molga::SystemProcessRunner runner;
    output.clear();
    const auto result = runner.Run(
        command, PathService::Get().ExecutableDir().string(),
        [&](const std::string& line) { output += line; }, [] { return false; });
    return !result.cancelled && result.exitCode == 0;
}

static fs::path ResolveProjectPath(const fs::path& projectRoot, const std::string& stored) {
    fs::path p(stored);
    return p.is_absolute() ? p : projectRoot / p;
}

static bool CheckScenesForUserScripts(const BuildSettings& settings, bool hasUserLib, std::string& outMsg) {
    for (const auto& scene : settings.profile.scenes) {
        fs::path scenePath = ResolveProjectPath(settings.projectRoot, scene);
        if (!fs::exists(scenePath)) continue;

        std::ifstream file(scenePath);
        if (!file.is_open()) continue;

        try {
            nlohmann::json j;
            file >> j;

            if (j.contains("gameObjects")) {
                for (const auto& objJson : j["gameObjects"]) {
                    if (objJson.contains("components")) {
                        for (const auto& compJson : objJson["components"]) {
                            std::string type = compJson.value("type", "");
                            if (type.empty()) continue;

                            bool isBuiltinComp = ComponentFactory::Get().HasType(type);
                            bool isBuiltinScript = ScriptManager::Get().IsScriptRegistered(type) && !ScriptManager::Get().IsDynamicScript(type);

                            if (!isBuiltinComp && !isBuiltinScript) {
                                if (!hasUserLib) {
                                    outMsg = "Scene '" + scene + "' references user script component '" + type + 
                                             "' but no user script library is available. Please build the script library first.";
                                    return false;
                                }
                            }
                        }
                    }
                }
            }
        } catch (const std::exception&) {
            // Ignore parse errors
        }
    }
    return true;
}

static bool ValidateSceneFontReferences(const BuildPlan& plan,
                                        const fs::path& packagedRoot,
                                        std::string& outMsg) {
    std::unordered_set<std::string> fontGuids;
    std::unordered_set<std::string> visitedPrefabs;
    std::unordered_set<std::string> activePrefabs;

    std::function<bool(const nlohmann::json&, const std::string&)> scanDocument;
    std::function<bool(const std::string&, const std::string&)> scanPrefab;

    scanPrefab = [&](const std::string& guid, const std::string& referencedBy) {
        if (guid.empty()) {
            outMsg = "Prefab instance has no GUID while validating fonts: " +
                     referencedBy;
            return false;
        }
        if (visitedPrefabs.count(guid) != 0) return true;
        if (!activePrefabs.insert(guid).second) {
            outMsg = "Cyclic prefab reference while validating fonts ('" +
                     referencedBy + "'): " + guid;
            return false;
        }
        struct ActivePrefabGuard {
            std::unordered_set<std::string>& active;
            const std::string& guid;
            ~ActivePrefabGuard() { active.erase(guid); }
        } activeGuard{activePrefabs, guid};

        const molga::AssetRecord* record = molga::AssetDatabase::Get().Find(guid);
        if (!record || record->importer != "PrefabImporter" || record->importFailed) {
            outMsg = "Could not resolve prefab GUID while validating fonts ('" +
                     referencedBy + "'): " + guid;
            return false;
        }
        const fs::path source = molga::AssetDatabase::Get().AbsoluteSourcePath(guid);
        if (source.empty() || !fs::is_regular_file(source)) {
            outMsg = "Prefab source is missing while validating fonts: " + guid;
            return false;
        }
        try {
            std::ifstream file(source);
            if (!file) {
                outMsg = "Could not open prefab while validating fonts: " +
                         source.string();
                return false;
            }
            const nlohmann::json prefab = nlohmann::json::parse(file);
            if (!scanDocument(prefab, record->sourcePath)) return false;
            visitedPrefabs.insert(guid);
            return true;
        } catch (const nlohmann::json::exception& error) {
            outMsg = "Could not parse prefab while validating fonts ('" +
                     record->sourcePath + "'): " + error.what();
            return false;
        }
    };

    scanDocument = [&](const nlohmann::json& document, const std::string& label) {
        if (!document.contains("gameObjects") ||
            !document["gameObjects"].is_array()) {
            outMsg = "Scene or prefab has no valid gameObjects array: " + label;
            return false;
        }

        for (const auto& object : document["gameObjects"]) {
            if (object.contains("components") && object["components"].is_array()) {
                for (const auto& component : object["components"]) {
                    const std::string type = component.value("type", "");
                    if (type == "TextRenderer2D" || type == "UILabel") {
                        const std::string guid = component.value("fontGuid", "");
                        if (!guid.empty()) fontGuids.insert(guid);
                    } else if (type == "PrefabInstance") {
                        const std::string prefabGuid =
                            component.value("prefabGuid", "");
                        if (!prefabGuid.empty() && !scanPrefab(prefabGuid, label)) {
                            return false;
                        }
                    }
                }
            }

            if (!object.contains("prefabInstance") ||
                !object["prefabInstance"].is_object()) continue;
            const auto& instance = object["prefabInstance"];

            // Overrides live outside the prefab document. A font selected on
            // an instance must therefore be included as well as the template's
            // original reference.
            if (instance.contains("modifications") &&
                instance["modifications"].is_array()) {
                for (const auto& modification : instance["modifications"]) {
                    const std::string component =
                        modification.value("component", "");
                    if ((component == "TextRenderer2D" || component == "UILabel") &&
                        modification.value("key", "") == "fontGuid" &&
                        modification.contains("value") &&
                        modification["value"].is_string()) {
                        const std::string guid =
                            modification["value"].get<std::string>();
                        if (!guid.empty()) fontGuids.insert(guid);
                    }
                }
            }

            if (!scanPrefab(instance.value("guid", ""), label)) return false;
        }
        return true;
    };

    for (const auto& scene : plan.sceneEntries) {
        std::ifstream file(scene.sourceAbsolutePath);
        if (!file) {
            outMsg = "Could not open scene while validating fonts: " +
                     scene.sourceProfilePath;
            return false;
        }

        try {
            const nlohmann::json document = nlohmann::json::parse(file);
            if (!scanDocument(document, scene.sourceProfilePath)) return false;
        } catch (const nlohmann::json::exception& error) {
            outMsg = "Could not parse scene while validating fonts ('" +
                     scene.sourceProfilePath + "'): " + error.what();
            return false;
        }
    }

    for (const auto& guid : fontGuids) {
        const molga::AssetRecord* record = molga::AssetDatabase::Get().Find(guid);
        if (!record) {
            outMsg = "Scene references an unknown font GUID: " + guid;
            return false;
        }
        if (record->importer != "FontImporter" || record->importFailed) {
            outMsg = "Scene font GUID is not a valid imported TTF/OTF asset: " + guid;
            return false;
        }
        const fs::path source = molga::AssetDatabase::Get().AbsoluteSourcePath(guid);
        if (source.empty() || !fs::is_regular_file(source)) {
            outMsg = "Scene font source is missing for GUID: " + guid;
            return false;
        }
        if (!packagedRoot.empty()) {
            // sourcePath is already package-relative (for example,
            // "Assets/Fonts/NotoSansKR-Regular.ttf").
            const fs::path packaged = packagedRoot / fs::path(record->sourcePath);
            if (!fs::is_regular_file(packaged)) {
                outMsg = "Packaged font file is missing for GUID: " + guid;
                return false;
            }
        }
    }
    outMsg.clear();
    return true;
}

GameBuilder& GameBuilder::Get() {
    static GameBuilder instance;
    return instance;
}

bool GameBuilder::Build(const BuildSettings& settings) {
    long long totalStart = molga::NowNanos();

    progress = 0.0f;
    lastError.clear();

    // Verify user scripts
    std::string userLibPath = ScriptCompiler::Get().GetCompiledLibraryPath();
    bool hasUserLib = !userLibPath.empty() && fs::exists(userLibPath);

    std::string relativeScriptLibPath;
    if (hasUserLib) {
        fs::path srcFile(userLibPath);
        relativeScriptLibPath = (fs::path(Paths::Project::SCRIPTS) / srcFile.filename()).generic_string();
    }

    // Build plan first
    BuildPlan plan;
    std::string planError;
    if (!BuildPlanBuilder::Build(settings.profile, settings.projectRoot, settings.profile.target, relativeScriptLibPath, plan, planError)) {
        lastError = planError;
        return false;
    }

    if (Project::Get().IsOpen()) {
        molga::AssetDatabase::Get().ScanProject(Project::Get().GetAssetsPath());
    }
    {
        std::vector<fs::path> dependencyScenes;
        dependencyScenes.reserve(plan.sceneEntries.size());
        for (const auto& entry : plan.sceneEntries) {
            dependencyScenes.emplace_back(entry.sourceAbsolutePath);
        }
        const auto dependencies = molga::AssetDependencyValidator::ValidateScenes(
            dependencyScenes, molga::AssetDatabase::Get());
        if (!dependencies) {
            lastError = "Asset dependency validation failed:\n" + dependencies.Summary();
            return false;
        }
    }
    if (!ValidateSceneFontReferences(plan, {}, lastError)) {
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

    // Build required-file manifest from plan
    {
        long long t0 = molga::NowNanos();
        BuildManifest manifest;
        if (Project::Get().IsOpen()) {
            manifest.requiredFiles.push_back(Project::Get().GetAssetsPath());
        }
        manifest.requiredFiles.push_back(
            fs::path(MOLGA_ENGINE_SHADER_DESCRIPTOR_DIR).string());
        manifest.requiredFiles.push_back(ShaderCompilerPath().string());
        manifest.requiredFiles.push_back((PathService::Get().ExecutableDir() / "molga_runtime").string());
        for (const auto& entry : plan.sceneEntries) {
            manifest.requiredFiles.push_back(entry.sourceAbsolutePath);
        }

        // Require user script library if scenes reference user script types
        std::string checkError;
        if (!CheckScenesForUserScripts(settings, false, checkError)) {
            manifest.requiredFiles.push_back(userLibPath);
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
        if (!ValidateSceneFontReferences(plan, stagingOutput, lastError)) {
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
        if (!CopyScenes(plan, stagingPathStr)) {
            cleanupStaging();
            return false;
        }
        double ms = (molga::NowNanos() - t0) / 1.0e6;
        molga::ActiveReportSink().ReportTiming("Build: Copy scenes", ms, "");
    }

    // Verify scenes do not require missing user scripts
    std::string userScriptLibPath;
    std::string checkError;
    if (!CheckScenesForUserScripts(settings, hasUserLib, checkError)) {
        lastError = checkError;
        cleanupStaging();
        return false;
    }

    if (hasUserLib) {
        currentStep = "Copying user scripts...";
        if (!CopyUserScripts(stagingPathStr, userScriptLibPath)) {
            cleanupStaging();
            return false;
        }
    }

    // Step: Emit asset catalog
    currentStep = "Emitting asset catalog...";
    progress = 0.6f;
    {
        long long t0 = molga::NowNanos();
        if (!EmitAssetCatalog(stagingPathStr)) {
            cleanupStaging();
            return false;
        }
        double ms = (molga::NowNanos() - t0) / 1.0e6;
        molga::ActiveReportSink().ReportTiming("Build: Emit asset catalog", ms, "");
    }

    // Step: Copy placeholder resource
    {
        long long t0 = molga::NowNanos();
        if (!CopyPlaceholderResource(stagingPathStr)) {
            cleanupStaging();
            return false;
        }
        double ms = (molga::NowNanos() - t0) / 1.0e6;
        molga::ActiveReportSink().ReportTiming("Build: Copy placeholder resource", ms, "");
    }

    // Step 5: Generate game config
    currentStep = "Generating game configuration...";
    progress = 0.7f;
    {
        long long t0 = molga::NowNanos();
        if (!GenerateGameConfig(settings, plan, stagingPathStr)) {
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
        // Shader sources/descriptors are build-time authoring inputs. Only the
        // validated MSL bundle crosses the macOS package boundary.
        std::error_code shaderCleanupError;
        fs::remove_all(dest / "Shaders", shaderCleanupError);
        if (shaderCleanupError) {
            lastError = "Failed to remove shader authoring inputs: " +
                        shaderCleanupError.message();
            return false;
        }
        return true;
    } catch (const std::exception& e) {
        lastError = "Failed to copy assets: " + std::string(e.what());
        return false;
    }
}

bool GameBuilder::CopyShaders(const std::string& outputPath) {
    try {
        const fs::path compiler = ShaderCompilerPath();
        const fs::path engineDescriptors = MOLGA_ENGINE_SHADER_DESCRIPTOR_DIR;
        if (!fs::is_regular_file(compiler)) {
            lastError = "Shader compiler not found next to the editor: " +
                        compiler.string();
            return false;
        }
        if (!ContainsShaderDescriptors(engineDescriptors)) {
            lastError = "Engine shader descriptors not found: " +
                        engineDescriptors.string();
            return false;
        }

        const fs::path packageRoot = outputPath;
        const fs::path fullBundle = packageRoot / ".ShaderBundle.full";
        const fs::path packagedBundle =
            packageRoot / Paths::Build::SHADER_BUNDLE;
        std::string buildCommand = ShellQuote(compiler) + " build --descriptors " +
            ShellQuote(engineDescriptors);
        if (Project::Get().IsOpen()) {
            const fs::path projectShaders =
                fs::path(Project::Get().GetAssetsPath()) / "Shaders";
            if (ContainsShaderDescriptors(projectShaders)) {
                buildCommand += " --descriptors " + ShellQuote(projectShaders);
            }
        }
        buildCommand += " --output " + ShellQuote(fullBundle);

        std::string toolOutput;
        if (!RunShaderTool(buildCommand, toolOutput)) {
            lastError = "Shader bundle build failed:\n" + toolOutput;
            return false;
        }
        const std::string packageCommand = ShellQuote(compiler) +
            " package-msl --bundle " + ShellQuote(fullBundle) +
            " --output " + ShellQuote(packagedBundle);
        if (!RunShaderTool(packageCommand, toolOutput)) {
            lastError = "MSL shader packaging failed:\n" + toolOutput;
            std::error_code cleanupError;
            fs::remove_all(fullBundle, cleanupError);
            return false;
        }
        std::error_code cleanupError;
        fs::remove_all(fullBundle, cleanupError);
        if (cleanupError) {
            lastError = "Could not remove intermediate shader bundle: " +
                        cleanupError.message();
            return false;
        }
        return true;
    } catch (const std::exception& e) {
        lastError = "Failed to copy shaders: " + std::string(e.what());
        return false;
    }
}

bool GameBuilder::CopyScenes(const BuildPlan& plan, const std::string& outputPath) {
    try {
        for (const auto& entry : plan.sceneEntries) {
            const fs::path src(entry.sourceAbsolutePath);
            const fs::path dest = fs::path(outputPath) / entry.packagePath;
            fs::create_directories(dest.parent_path());
            fs::copy_file(src, dest, fs::copy_options::overwrite_existing);
        }
        return true;
    } catch (const std::exception& e) {
        lastError = "Failed to copy scenes: " + std::string(e.what());
        return false;
    }
}

bool GameBuilder::GenerateGameConfig(const BuildSettings& settings, const BuildPlan& plan, const std::string& outputPath) {
    try {
        nlohmann::json config;
        config["schemaVersion"] = GameConfig::CurrentSchemaVersion;
        config["gameName"] = settings.profile.gameName;
        config["productVersion"] = settings.profile.productVersion;
        config["companyName"] = settings.profile.companyName;
        config["mainScene"] = plan.startupScenePackagePath;
        config["startupSceneId"] = plan.startupSceneId;
        config["windowWidth"] = settings.profile.window.width;
        config["windowHeight"] = settings.profile.window.height;
        config["fullscreen"] = settings.profile.window.fullscreen;
        config["resizable"] = settings.profile.window.resizable;
        config["outputScaleMode"] = molga::GameOutputScaleModeName(
            settings.profile.window.outputScaleMode);
        config["developmentBuild"] = settings.profile.developmentBuild;
        config["projectSettings"] = ProjectSettings::Get().Serialize();

        const fs::path shaderManifest = fs::path(outputPath) /
            Paths::Build::SHADER_BUNDLE / "manifest.json";
        std::string hashError;
        const std::string shaderManifestSha256 =
            molga::Sha256File(shaderManifest, &hashError);
        if (shaderManifestSha256.empty()) {
            lastError = "Failed to hash packaged shader manifest: " + hashError;
            return false;
        }
        config["graphics"] = {
            {"api", "sdlgpu"},
            {"driver", "metal"},
            {"shaderFormat", "msl"},
            {"shaderManifest", "ShaderBundle/manifest.json"},
            {"shaderManifestSha256", shaderManifestSha256},
        };

        config["inputActions"] = Input::SerializeActions();

        // List all scenes from plan
        nlohmann::json scenesList = nlohmann::json::array();
        for (const auto& entry : plan.sceneEntries) {
            scenesList.push_back(entry.packagePath);
        }
        config["scenes"] = scenesList;

        nlohmann::json sceneCatalog = nlohmann::json::array();
        for (const auto& entry : plan.sceneEntries) {
            sceneCatalog.push_back({
                {"id", entry.sceneId},
                {"packagePath", entry.packagePath}
            });
        }
        config["sceneCatalog"] = std::move(sceneCatalog);

        // Scripts manifest
        if (!plan.optionalScriptLibrary.empty()) {
            nlohmann::json scriptsJson;
            scriptsJson["enabled"] = true;
            scriptsJson["library"] = plan.optionalScriptLibrary;
            scriptsJson["apiVersion"] = molga::ScriptApiVersion;
            scriptsJson["buildHash"] = "source-or-library-hash";
            config["scripts"] = scriptsJson;
        }

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

bool GameBuilder::CopyUserScripts(const std::string& outputPath, std::string& outLibraryPath) {
    outLibraryPath.clear();
    std::string userLibPath = ScriptCompiler::Get().GetCompiledLibraryPath();
    if (userLibPath.empty() || !fs::exists(userLibPath)) {
        return true;
    }

    try {
        fs::path destDir = fs::path(outputPath) / Paths::Project::SCRIPTS;
        fs::create_directories(destDir);

        fs::path srcFile(userLibPath);
        fs::path destFile = destDir / srcFile.filename();

        fs::copy_file(srcFile, destFile, fs::copy_options::overwrite_existing);

        outLibraryPath = (fs::path(Paths::Project::SCRIPTS) / srcFile.filename()).string();
        std::replace(outLibraryPath.begin(), outLibraryPath.end(), '\\', '/');

        return true;
    } catch (const std::exception& e) {
        lastError = "Failed to copy user scripts library: " + std::string(e.what());
        return false;
    }
}

bool GameBuilder::EmitAssetCatalog(const std::string& outputPath) {
    try {
        // Refresh AssetDatabase from the project's Assets/ so the catalog is current.
        if (Project::Get().IsOpen()) {
            molga::AssetDatabase::Get().ScanProject(Project::Get().GetAssetsPath());
        }

        fs::path catalogPath = fs::path(outputPath) / "asset_catalog.json";
        if (!molga::AssetDatabase::Get().SaveCatalog(
                catalogPath, "Assets/Shaders/")) {
            lastError = "Failed to write asset_catalog.json";
            return false;
        }
        return true;
    } catch (const std::exception& e) {
        lastError = "Failed to emit asset catalog: " + std::string(e.what());
        return false;
    }
}

bool GameBuilder::CopyPlaceholderResource(const std::string& outputPath) {
    try {
        fs::path destDir = fs::path(outputPath) / "Resources";
        fs::create_directories(destDir);

        fs::path src = molga::AssetDatabase::MissingTexturePath();
        if (fs::exists(src)) {
            fs::copy_file(src, destDir / "missing_texture.png",
                         fs::copy_options::overwrite_existing);
        } else {
            // Create a valid minimal 1x1 magenta PNG as placeholder.
            static constexpr unsigned char kMissingTexturePng[] = {
                0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a,
                0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52,
                0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
                0x08, 0x06, 0x00, 0x00, 0x00, 0x1f, 0x15, 0xc4,
                0x89, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x44, 0x41,
                0x54, 0x78, 0x9c, 0x63, 0xf8, 0xcf, 0xf0, 0xff,
                0x3f, 0x00, 0x06, 0xfe, 0x02, 0xfe, 0x0c, 0x75,
                0x89, 0xde, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45,
                0x4e, 0x44, 0xae, 0x42, 0x60, 0x82,
            };
            std::ofstream file(destDir / "missing_texture.png", std::ios::binary);
            if (!file.is_open()) {
                lastError = "Failed to create placeholder resource";
                return false;
            }
            file.write(reinterpret_cast<const char*>(kMissingTexturePng), sizeof(kMissingTexturePng));
            if (!file.good()) {
                lastError = "Failed to write placeholder resource";
                return false;
            }
        }
        return true;
    } catch (const std::exception& e) {
        lastError = "Failed to copy placeholder resource: " + std::string(e.what());
        return false;
    }
}
