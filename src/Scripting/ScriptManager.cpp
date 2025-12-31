#include "ScriptManager.h"
#include "Script.h"
#include "../Platform/Platform.h"
#include <iostream>
#include <algorithm>

ScriptManager& ScriptManager::Get() {
    static ScriptManager instance;
    return instance;
}

void ScriptManager::RegisterScript(const std::string& name, ScriptFactory factory) {
    scriptFactories[name] = factory;
    std::cout << "[ScriptManager] Registered script: " << name << std::endl;
}

Script* ScriptManager::CreateScript(const std::string& name) {
    auto it = scriptFactories.find(name);
    if (it != scriptFactories.end()) {
        return it->second();
    }
    std::cerr << "[ScriptManager] Script not found: " << name << std::endl;
    return nullptr;
}

std::vector<std::string> ScriptManager::GetRegisteredScripts() const {
    std::vector<std::string> names;
    names.reserve(scriptFactories.size());
    for (const auto& pair : scriptFactories) {
        names.push_back(pair.first);
    }
    return names;
}

bool ScriptManager::IsScriptRegistered(const std::string& name) const {
    return scriptFactories.find(name) != scriptFactories.end();
}

bool ScriptManager::LoadScriptLibrary(const std::string& path) {
    // Check if already loaded
    if (libraryHandles.find(path) != libraryHandles.end()) {
        std::cout << "[ScriptManager] Library already loaded: " << path << std::endl;
        return true;
    }

    void* handle = Platform::LoadDynamicLibrary(path.c_str());
    if (!handle) {
        std::cerr << "[ScriptManager] Failed to load library: " << path
                  << " (" << Platform::GetDynamicLibraryError() << ")" << std::endl;
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
        std::cout << "[ScriptManager] Loaded and registered scripts from: " << path << std::endl;
    } else {
        std::cout << "[ScriptManager] Loaded library (no RegisterScripts): " << path << std::endl;
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

        std::cout << "[ScriptManager] Unloaded library: " << path << std::endl;
    }
}

void ScriptManager::ReloadScriptLibraries() {
    // Store paths to reload
    std::vector<std::string> pathsToReload = loadedLibraries;

    // Unload all
    for (const auto& path : pathsToReload) {
        UnloadScriptLibrary(path);
    }

    // Clear script factories (they will be re-registered)
    scriptFactories.clear();

    // Reload all
    for (const auto& path : pathsToReload) {
        LoadScriptLibrary(path);
    }

    std::cout << "[ScriptManager] Reloaded all script libraries" << std::endl;
}
