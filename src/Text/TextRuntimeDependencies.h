#pragma once

#include "Text/TextDiagnostic.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace molga::text {

// The pinned packaged ICU data. Every consumer that judges an icudt78l.dat —
// the runtime before udata_setCommonData, the editor's packager before it
// copies one into a game — answers to exactly these two values, stated once.
inline constexpr std::uintmax_t kPackagedIcuDataBytes = 33107232;
inline constexpr const char*    kPackagedIcuDataSha256 =
    "d5cf2a40dccbe471781ec7af85693bff542ff12f0b670c9630c4e72d60714b8b";

// Verifies an icudt78l.dat on disk against the two values above without
// modifying it, reporting one DependencyInvalid with remediation on failure.
//
// TextRuntimeDependencies::Initialize deliberately does not call this. It
// hashes the aligned buffer it is about to hand ICU, so nothing can substitute
// bytes between the check and the call; re-reading the file here would be both
// a second 33 MB read and a weaker claim. Use this where only the file on disk
// matters, as package staging does.
bool VerifyPackagedIcuDataFile(const std::filesystem::path& path,
                               TextDiagnosticSink&          sink);

// One Engine/Text root determines both verified files. Nothing here consults a
// source tree or the current working directory: an executable that cannot find
// its staged pair beside itself fails closed rather than reaching for one.
struct TextDependencyConfig {
    std::filesystem::path dependencyContract;
    std::filesystem::path icuData;
    bool                  packagedRuntime = false;
    static TextDependencyConfig FromEngineTextRoot(
        const std::filesystem::path& root, bool packagedRuntime);
};

// The one ICU lifetime a process gets.
//
// It is one-way. Once Initialize publishes ready state, Shutdown is the only
// remaining transition and it ends in TerminallyCleaned forever, because
// hb_icu_get_unicode_funcs() caches ICU normalizer pointers process-statically
// and nothing can make those valid again. There is deliberately no in-process
// stop/restore seam in any target: not-ready and post-cleanup behaviour is
// observed by fresh child processes instead.
class TextRuntimeDependencies {
public:
    static TextRuntimeDependencies& Get();
    bool                            Initialize(const TextDependencyConfig&,
                                               TextDiagnosticSink&);
    void                            Shutdown();
    bool                            IsReady() const noexcept;
    const std::string&              DependencyContractSha256() const noexcept;
    std::size_t                     OutstandingClientHandleCount() const noexcept;

private:
    enum class Lifecycle { NeverInitialized, Ready, TerminallyCleaned };
    struct State;
    Lifecycle              lifecycle_ = Lifecycle::NeverInitialized;
    std::unique_ptr<State> state_;

    // State is incomplete here, so the destructor is out of line. Get() owns
    // the only instance and its destructor runs after main has returned, which
    // is why that destructor never calls an ICU API: by then Shutdown has
    // already run u_cleanup and released state_.
    TextRuntimeDependencies();
    ~TextRuntimeDependencies();
    TextRuntimeDependencies(const TextRuntimeDependencies&)            = delete;
    TextRuntimeDependencies& operator=(const TextRuntimeDependencies&) = delete;

    friend class TextRuntimeClientHandle;
};

// Weak client-lifetime token. Every ICU/HarfBuzz-backed service, face,
// analyzer or shaper an application owns holds one for exactly as long as it
// can still touch ICU. Shutdown requires all of them to be gone first. The
// runtime's own common-data and internal ICU handles are deliberately not
// counted here: they belong to State, not to a client.
class TextRuntimeClientHandle {
public:
    // nullopt unless the runtime is ready right now.
    static std::optional<TextRuntimeClientHandle> Acquire();
    TextRuntimeClientHandle(TextRuntimeClientHandle&&) noexcept;
    TextRuntimeClientHandle& operator=(TextRuntimeClientHandle&&) noexcept;
    TextRuntimeClientHandle(const TextRuntimeClientHandle&)            = delete;
    TextRuntimeClientHandle& operator=(const TextRuntimeClientHandle&) = delete;
    ~TextRuntimeClientHandle();

private:
    explicit TextRuntimeClientHandle(std::uint64_t token) noexcept;
    std::uint64_t token_ = 0;  // 0 means moved-from and owns nothing.
};

// Application-scoped owner of the lifetime above. Declare it before every text
// service so ordinary stack unwinding destroys all of them first and leaves
// u_cleanup last, immediately before the process returns.
class TextRuntimeLifetimeGuard {
public:
    static std::optional<TextRuntimeLifetimeGuard> Create(
        const TextDependencyConfig&, TextDiagnosticSink&);
    TextRuntimeLifetimeGuard(TextRuntimeLifetimeGuard&&) noexcept;
    TextRuntimeLifetimeGuard& operator=(TextRuntimeLifetimeGuard&&) noexcept;
    TextRuntimeLifetimeGuard(const TextRuntimeLifetimeGuard&)            = delete;
    TextRuntimeLifetimeGuard& operator=(const TextRuntimeLifetimeGuard&) = delete;
    ~TextRuntimeLifetimeGuard();

private:
    explicit TextRuntimeLifetimeGuard(bool active) noexcept;
    bool active_ = false;
};

namespace detail {

// The four ICU entry points the runtime is allowed to call, behind one table
// so a test can count them. Production installs nothing: the default table
// forwards straight to ICU.
//
// Test-only seam. The counting companion translation unit links into
// molga_text_runtime_probe alone; the editor, the development runtime and
// molga_core keep the default table. int stands in for UErrorCode and
// UDataFileAccess so this header stays free of ICU includes, which matters
// because molga_core links ICU privately and its consumers have no ICU include
// path.
struct IcuRuntimeApi {
    void (*setCommonData)(const void* data, int* status) = nullptr;
    void (*setFileAccess)(int access, int* status)       = nullptr;
    void (*init)(int* status)                            = nullptr;
    void (*cleanup)()                                    = nullptr;
};

const IcuRuntimeApi& DefaultIcuRuntimeApi() noexcept;

// Refuses replacement once any lifecycle transition has happened, so a table
// can never be swapped underneath a live or already terminal ICU runtime.
bool InstallIcuRuntimeApi(const IcuRuntimeApi& api) noexcept;

}  // namespace detail

}  // namespace molga::text
