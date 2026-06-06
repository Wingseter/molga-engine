#include "ScriptManager.h"
#include "Script.h"
#include "../Common/Log.h"
#include "../Platform/Platform.h"
#include <iostream>
#include <algorithm>

ScriptManager& ScriptManager::Get() {
    static ScriptManager instance;
    return instance;
}

void ScriptManager::RegisterBuiltin(const std::string& name, ScriptFactory factory) {
    builtinFactories[name] = factory;
    Log::Info("ScriptManager", "Registered builtin script: " + name);
}

void ScriptManager::RegisterDynamic(const std::string& name, ScriptFactory factory) {
    dynamicFactories[name] = factory;
    Log::Info("ScriptManager", "Registered dynamic script: " + name);
}

void ScriptManager::RegisterScript(const std::string& name, ScriptFactory factory) {
    RegisterDynamic(name, factory);
}

std::unique_ptr<Script> ScriptManager::CreateScript(const std::string& name) {
    // Search dynamic first (user scripts take priority)
    auto it = dynamicFactories.find(name);
    if (it != dynamicFactories.end()) {
        return it->second();
    }
    // Then search builtin
    it = builtinFactories.find(name);
    if (it != builtinFactories.end()) {
        return it->second();
    }
    Log::Error("ScriptManager", "Script not found: " + name);
    return nullptr;
}

std::vector<std::string> ScriptManager::GetRegisteredScripts() const {
    std::vector<std::string> names;
    names.reserve(builtinFactories.size() + dynamicFactories.size());
    for (const auto& pair : builtinFactories) {
        names.push_back(pair.first);
    }
    for (const auto& pair : dynamicFactories) {
        // Avoid duplicates if a dynamic script overrides a builtin
        if (builtinFactories.find(pair.first) == builtinFactories.end()) {
            names.push_back(pair.first);
        }
    }
    return names;
}

bool ScriptManager::IsScriptRegistered(const std::string& name) const {
    return dynamicFactories.find(name) != dynamicFactories.end() ||
           builtinFactories.find(name) != builtinFactories.end();
}

bool ScriptManager::LoadScriptLibrary(const std::string& path) {
    // Check if already loaded
    if (libraryHandles.find(path) != libraryHandles.end()) {
        Log::Info("ScriptManager", "Library already loaded: " + path);
        return true;
    }

    void* handle = Platform::LoadDynamicLibrary(path.c_str());
    if (!handle) {
        Log::Error("ScriptManager", "Failed to load library: " + path + " (" + Platform::GetDynamicLibraryError() + ")");
        return false;
    }

    libraryHandles[path] = handle;
    loadedLibraries.push_back(path);

    // Look for RegisterScripts function
    using RegisterFunc = void(*)();
    RegisterFunc registerFunc = reinterpret_cast<RegisterFunc>(
        Platform::GetSymbol(handle, "RegisterScripts"));

    if (registerFunc) {
        registerFunc();
        Log::Info("ScriptManager", "Loaded and registered scripts from: " + path);
    } else {
        Log::Info("ScriptManager", "Loaded library (no RegisterScripts): " + path);
    }

    return true;
}

void ScriptManager::UnloadScriptLibrary(const std::string& path) {
    auto it = libraryHandles.find(path);
    if (it != libraryHandles.end()) {
        Platform::CloseDynamicLibrary(it->second);
        libraryHandles.erase(it);

        // Remove from loaded libraries list
        auto libIt = std::find(loadedLibraries.begin(), loadedLibraries.end(), path);
        if (libIt != loadedLibraries.end()) {
            loadedLibraries.erase(libIt);
        }

        Log::Info("ScriptManager", "Unloaded library: " + path);
    }
}

void ScriptManager::ReloadScriptLibraries() {
    // Store paths to reload
    std::vector<std::string> pathsToReload = loadedLibraries;

    // Unload all
    for (const auto& path : pathsToReload) {
        UnloadScriptLibrary(path);
    }

    // Clear only dynamic factories (builtins are preserved)
    dynamicFactories.clear();

    // Reload all
    for (const auto& path : pathsToReload) {
        LoadScriptLibrary(path);
    }

    Log::Info("ScriptManager", "Reloaded all script libraries");
}
