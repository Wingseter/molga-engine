#include "BuiltinScripts.h"
#include "ScriptManager.h"

void RegisterBuiltinScripts() {
    ScriptManager::Get().RegisterBuiltin("PlayerController", []() -> std::unique_ptr<Script> {
        return std::make_unique<PlayerController>();
    });

    ScriptManager::Get().RegisterBuiltin("Rotator", []() -> std::unique_ptr<Script> {
        return std::make_unique<Rotator>();
    });

    ScriptManager::Get().RegisterBuiltin("Oscillator", []() -> std::unique_ptr<Script> {
        return std::make_unique<Oscillator>();
    });

    ScriptManager::Get().RegisterBuiltin("Spawner", []() -> std::unique_ptr<Script> {
        return std::make_unique<Spawner>();
    });

    ScriptManager::Get().RegisterBuiltin("SelfDestruct", []() -> std::unique_ptr<Script> {
        return std::make_unique<SelfDestruct>();
    });
}
