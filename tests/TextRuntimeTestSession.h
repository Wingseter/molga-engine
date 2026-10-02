#pragma once

#include <filesystem>

// The one text runtime lifetime a session-backed test executable gets.
//
// It deliberately has no stop/restore seam. A successful ICU lifetime is
// terminal — hb_icu_get_unicode_funcs() caches ICU normalizer pointers
// process-statically — so no test case may shut the runtime down, restart it,
// or build a second session. Not-ready and post-cleanup behaviour belongs to
// fresh child executables such as molga_text_runtime_probe.
class TextRuntimeTestSession {
public:
    explicit TextRuntimeTestSession(std::filesystem::path engineTextRoot);
    ~TextRuntimeTestSession();
    bool Initialize();
    bool ShutdownAfterTests();
    const std::filesystem::path& EngineTextRoot() const noexcept;
    bool ShutdownWasRequested() const noexcept;
    static TextRuntimeTestSession& Current();
    static void Install(TextRuntimeTestSession*) noexcept;
private:
    std::filesystem::path engineTextRoot_;
    bool installed_ = false;
    bool shutdownRequested_ = false;
};
