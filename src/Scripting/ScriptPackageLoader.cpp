#include "ScriptPackageLoader.h"
#include "Scripting/ScriptApi.h"
#include "Scripting/ScriptManager.h"
#include "Core/PathService.h"
#include "Core/SmokeReport.h"
#include "Platform/Platform.h"
#include "Common/Log.h"
#include <filesystem>
#include <iostream>

bool ScriptPackageLoader::Load(const GameConfig& config, bool smokeEnabled, const std::string& smokeReportPath, std::string& outError) {
    if (!config.scripts.enabled) {
        Log::Info("ScriptPackageLoader", "Scripts are disabled in game config.");
        return true;
    }

    if (config.scripts.apiVersion != molga::ScriptApiVersion) {
        outError = "Unsupported script API version: expected " +
            std::to_string(molga::ScriptApiVersion) + ", got " +
            std::to_string(config.scripts.apiVersion) +
            ". Recompile scripts for Script API v2.";
        Log::Error("ScriptPackageLoader", outError);
        return false;
    }

    if (config.scripts.library.empty()) {
        outError = "Script library path is empty but scripts are enabled.";
        Log::Error("ScriptPackageLoader", outError);
        if (smokeEnabled && !smokeReportPath.empty()) {
            SmokeReport report;
            report.executable = "molga_runtime";
            report.status = "error";
            report.message = outError;
            report.Save(smokeReportPath);
        }
        return false;
    }

    std::filesystem::path libPath(config.scripts.library);
    if (libPath.is_relative()) {
        libPath = PathService::Get().ExecutableDir() / libPath;
    }

    if (!std::filesystem::exists(libPath)) {
        outError = "Script library not found: " + libPath.string();
        Log::Error("ScriptPackageLoader", outError);
        if (smokeEnabled && !smokeReportPath.empty()) {
            SmokeReport report;
            report.executable = "molga_runtime";
            report.status = "error";
            report.message = outError;
            report.Save(smokeReportPath);
        }
        return false;
    }

    // Load dynamic library to perform verification checks
    void* handle = Platform::LoadDynamicLibrary(libPath.string().c_str());
    if (!handle) {
        outError = "Failed to load script library: " + libPath.string() + " (" + Platform::GetDynamicLibraryError() + ")";
        Log::Error("ScriptPackageLoader", outError);
        if (smokeEnabled && !smokeReportPath.empty()) {
            SmokeReport report;
            report.executable = "molga_runtime";
            report.status = "error";
            report.message = outError;
            report.Save(smokeReportPath);
        }
        return false;
    }

    // Check for RegisterScripts symbol
    using RegisterFunc = void(*)();
    RegisterFunc registerFunc = reinterpret_cast<RegisterFunc>(
        Platform::GetSymbol(handle, "RegisterScripts"));
    if (!registerFunc) {
        Platform::CloseDynamicLibrary(handle);
        outError = "RegisterScripts symbol not found in library: " + libPath.string();
        Log::Error("ScriptPackageLoader", outError);
        if (smokeEnabled && !smokeReportPath.empty()) {
            SmokeReport report;
            report.executable = "molga_runtime";
            report.status = "error";
            report.message = outError;
            report.Save(smokeReportPath);
        }
        return false;
    }

    // Check for GetScriptApiVersion symbol and validate version
    using GetApiVersionFunc = int(*)();
    GetApiVersionFunc getApiVersionFunc = reinterpret_cast<GetApiVersionFunc>(
        Platform::GetSymbol(handle, "GetScriptApiVersion"));
    if (!getApiVersionFunc) {
        Platform::CloseDynamicLibrary(handle);
        outError = "GetScriptApiVersion symbol not found in library: " + libPath.string();
        Log::Error("ScriptPackageLoader", outError);
        if (smokeEnabled && !smokeReportPath.empty()) {
            SmokeReport report;
            report.executable = "molga_runtime";
            report.status = "error";
            report.message = outError;
            report.Save(smokeReportPath);
        }
        return false;
    }

    int libApiVersion = getApiVersionFunc();
    if (libApiVersion != config.scripts.apiVersion) {
        Platform::CloseDynamicLibrary(handle);
        outError = "Script library API version mismatch: expected " + std::to_string(config.scripts.apiVersion) + ", got " + std::to_string(libApiVersion);
        Log::Error("ScriptPackageLoader", outError);
        if (smokeEnabled && !smokeReportPath.empty()) {
            SmokeReport report;
            report.executable = "molga_runtime";
            report.status = "error";
            report.message = outError;
            report.Save(smokeReportPath);
        }
        return false;
    }

    // Done verification. Close handle and delegate loading to ScriptManager so it maintains handles correctly.
    Platform::CloseDynamicLibrary(handle);

    if (!ScriptManager::Get().LoadScriptLibrary(libPath.string())) {
        outError = "ScriptManager failed to load library: " + libPath.string();
        Log::Error("ScriptPackageLoader", outError);
        if (smokeEnabled && !smokeReportPath.empty()) {
            SmokeReport report;
            report.executable = "molga_runtime";
            report.status = "error";
            report.message = outError;
            report.Save(smokeReportPath);
        }
        return false;
    }

    Log::Info("ScriptPackageLoader", "Successfully loaded and validated script library: " + libPath.string());
    return true;
}
