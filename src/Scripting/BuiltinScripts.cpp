#include "BuiltinScripts.h"
#include "ScriptManager.h"

void RegisterBuiltinScripts() {
    ScriptManager::Get().RegisterScript("PlayerController", []() -> std::unique_ptr<Script> {
        return std::make_unique<PlayerController>();
    });

    ScriptManager::Get().RegisterScript("Rotator", []() -> std::unique_ptr<Script> {
        return std::make_unique<Rotator>();
    });

    ScriptManager::Get().RegisterScript("Oscillator", []() -> std::unique_ptr<Script> {
        return std::make_unique<Oscillator>();
    });
}
