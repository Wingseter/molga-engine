#include "Scripting/Script.h"
#include "Scripting/ScriptManager.h"
#include "ECS/Component.h"
#include <memory>

class MyUserScript : public Script {
public:
    SCRIPT_CLASS(MyUserScript)

    void Update(float dt) override {
        // No-op
    }
};

#if defined(_WIN32)
#define EXPORT __declspec(dllexport)
#else
#define EXPORT __attribute__((visibility("default")))
#endif

extern "C" {
    EXPORT void RegisterScripts() {
        ScriptManager::Get().RegisterScript("MyUserScript", []() -> std::unique_ptr<Script> {
            return std::make_unique<MyUserScript>();
        });
    }
    EXPORT int GetScriptApiVersion() {
        return 1;
    }
}
