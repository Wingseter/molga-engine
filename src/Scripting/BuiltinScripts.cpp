#include "BuiltinScripts.h"
#include "ScriptManager.h"

void RegisterBuiltinScripts() {
    ScriptManager::Get().RegisterScript("PlayerController", []() -> Script* {
        return new PlayerController();
    });

    ScriptManager::Get().RegisterScript("Rotator", []() -> Script* {
        return new Rotator();
    });

    ScriptManager::Get().RegisterScript("Oscillator", []() -> Script* {
        return new Oscillator();
    });
}
