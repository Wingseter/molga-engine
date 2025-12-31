#ifndef MOLGA_SCRIPT_MANAGER_H
#define MOLGA_SCRIPT_MANAGER_H

#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <memory>

class Script;
class GameObject;

// Function pointer type for script factory
using ScriptFactory = std::function<Script*()>;

class ScriptManager {
public:
    static ScriptManager& Get();

    // Register a script factory
    void RegisterScript(const std::string& name, ScriptFactory factory);

    // Create a script by name
    Script* CreateScript(const std::string& name);

    // Get all registered script names
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

    std::unordered_map<std::string, ScriptFactory> scriptFactories;
    std::vector<std::string> loadedLibraries;

    // Platform-specific library handles
    std::unordered_map<std::string, void*> libraryHandles;
};

// Macro for registering scripts
#define REGISTER_SCRIPT(ScriptClass) \
    namespace { \
        struct ScriptClass##Registrar { \
            ScriptClass##Registrar() { \
                ScriptManager::Get().RegisterScript(#ScriptClass, []() -> Script* { \
                    return new ScriptClass(); \
                }); \
            } \
        }; \
        static ScriptClass##Registrar g_##ScriptClass##Registrar; \
    }

#endif // MOLGA_SCRIPT_MANAGER_H
