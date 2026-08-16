#if defined(_WIN32)
#define EXPORT __declspec(dllexport)
#else
#define EXPORT __attribute__((visibility("default")))
#endif

#include "Scripting/ScriptApi.h"

extern "C" {
    EXPORT int GetScriptApiVersion() {
        return molga::ScriptApiVersion;
    }
}
