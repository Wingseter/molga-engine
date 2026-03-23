#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <memory>

class Script;
class GameObject;

// Function pointer type for script factory
using ScriptFactory = std::function<std::unique_ptr<Script>()>;

class ScriptManager {
public:
    static ScriptManager& Get();

    // Register a builtin script factory (survives hot-reload)
    void RegisterBuiltin(const std::string& name, ScriptFactory factory);

    // Register a dynamic script factory (cleared on hot-reload)
    void RegisterDynamic(const std::string& name, ScriptFactory factory);

    // Backward-compatible alias: routes to RegisterDynamic
    void RegisterScript(const std::string& name, ScriptFactory factory);

    // Create a script by name (searches dynamic first, then builtin)
    std::unique_ptr<Script> CreateScript(const std::string& name);

    // Get all registered script names (builtin + dynamic)
    std::vector<std::string> GetRegisteredScripts() const;

    // Check if a script is registered
    bool IsScriptRegistered(const std::string& name) const;

    // Dynamic library loading
    bool LoadScriptLibrary(const std::string& path);
    void UnloadScriptLibrary(const std::string& path);
    void ReloadScriptLibraries();

    // Get loaded library paths
    const std::vector<std::string>& GetLoadedLibraries() const { return loadedLibraries; }

private:
    ScriptManager() = default;
    ScriptManager(const ScriptManager&) = delete;
    ScriptManager& operator=(const ScriptManager&) = delete;

    std::unordered_map<std::string, ScriptFactory> builtinFactories;
    std::unordered_map<std::string, ScriptFactory> dynamicFactories;
    std::vector<std::string> loadedLibraries;

    // Platform-specific library handles
    std::unordered_map<std::string, void*> libraryHandles;
};

// Macro for registering scripts
#define REGISTER_SCRIPT(ScriptClass) \
    namespace { \
        struct ScriptClass##Registrar { \
            ScriptClass##Registrar() { \
                ScriptManager::Get().RegisterScript(#ScriptClass, []() -> std::unique_ptr<Script> { \
                    return std::make_unique<ScriptClass>(); \
                }); \
            } \
        }; \
        static ScriptClass##Registrar g_##ScriptClass##Registrar; \
    }
