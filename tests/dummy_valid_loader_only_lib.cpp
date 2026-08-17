#include "Scripting/ScriptApi.h"

#if defined(_WIN32)
#define MOLGA_TEST_EXPORT __declspec(dllexport)
#else
#define MOLGA_TEST_EXPORT __attribute__((visibility("default")))
#endif

extern "C" {
MOLGA_TEST_EXPORT void RegisterScripts() {
    // Loader-only fixture: symbol discovery is the contract under test.
}

MOLGA_TEST_EXPORT int GetScriptApiVersion() {
    return molga::ScriptApiVersion;
}
}
