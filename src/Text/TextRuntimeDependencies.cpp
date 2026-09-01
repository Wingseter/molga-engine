#include "Text/TextRuntimeDependencies.h"

#include "Common/Sha256.h"

#include <nlohmann/json.hpp>

#include <unicode/uclean.h>
#include <unicode/udata.h>
#include <unicode/utypes.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <set>
#include <string>
#include <unordered_set>
#include <utility>

namespace fs = std::filesystem;

namespace molga::text {
namespace {

// ICU reads the common data in place for the whole lifetime and wants it
// aligned; the copy therefore has to outlive every ICU call, u_cleanup last.
constexpr std::size_t kIcuDataAlignment = 16;

constexpr const char* kSubsystem = "text-runtime";
constexpr const char* kRestoreIcuDataRemediation =
    "restore the verified icudt78l.dat by rebuilding the Engine/Text staging "
    "target";
constexpr const char* kFreshProcessRemediation =
    "start a fresh process: a text runtime lifetime is terminal and cannot be "
    "restarted";

// The portable contract's complete locked field sets. An unknown key is as bad
// as a missing one: it means the record was produced by something other than
// the verification barrier.
const std::set<std::string>& RootKeys() {
    static const std::set<std::string> keys = {"harfbuzz", "icu", "schema"};
    return keys;
}
const std::set<std::string>& HarfBuzzKeys() {
    static const std::set<std::string> keys = {
        "commit", "compositeSha256", "libraries", "options", "sourcePath",
        "version"};
    return keys;
}
const std::set<std::string>& IcuKeys() {
    static const std::set<std::string> keys = {
        "archiveSha256", "commit", "libraries", "options", "sourcePath",
        "version"};
    return keys;
}

void ReportDependencyInvalid(TextDiagnosticSink& sink, std::string message,
                             std::string remediation) {
    TextDiagnostic diagnostic;
    diagnostic.code        = TextDiagnosticCode::DependencyInvalid;
    diagnostic.severity    = TextSeverity::Blocker;
    diagnostic.subsystem   = kSubsystem;
    diagnostic.message     = std::move(message);
    diagnostic.remediation = std::move(remediation);
    sink.Report(std::move(diagnostic));
}

bool KeysAre(const nlohmann::json& object, const std::set<std::string>& expected) {
    if (!object.is_object() || object.size() != expected.size()) return false;
    for (const std::string& key : expected) {
        if (!object.contains(key)) return false;
    }
    return true;
}

// A portable record may not carry a checkout or build path. Both POSIX and
// Windows spellings are rejected so a record produced on either host is judged
// by the same rule.
bool LooksAbsolute(const std::string& value) {
    if (value.empty()) return false;
    if (value[0] == '/' || value[0] == '\\') return true;
    return value.size() >= 3 && value[1] == ':' &&
           (value[2] == '/' || value[2] == '\\');
}

bool ContainsAbsolutePath(const nlohmann::json& node) {
    if (node.is_string()) return LooksAbsolute(node.get<std::string>());
    if (node.is_object() || node.is_array()) {
        for (const auto& child : node) {
            if (ContainsAbsolutePath(child)) return true;
        }
    }
    return false;
}

std::string ReadWholeFile(const fs::path& path, bool& ok) {
    std::ifstream input(path, std::ios::binary);
    if (!input.good()) {
        ok = false;
        return {};
    }
    std::string bytes((std::istreambuf_iterator<char>(input)),
                      std::istreambuf_iterator<char>());
    ok = input.good() || input.eof();
    return bytes;
}

// Parses schema 1, requires every locked field and no absolute path, and hands
// back the SHA-256 of the exact bytes on disk.
bool VerifyPortableContract(const fs::path& path, std::string& sha256Out,
                            std::string& errorOut) {
    std::error_code error;
    if (!fs::is_regular_file(path, error)) {
        errorOut = "dependency contract is not a regular file";
        return false;
    }
    bool              readable = false;
    const std::string bytes    = ReadWholeFile(path, readable);
    if (!readable) {
        errorOut = "dependency contract could not be read";
        return false;
    }
    const nlohmann::json document = nlohmann::json::parse(bytes, nullptr, false);
    if (document.is_discarded()) {
        errorOut = "dependency contract is not valid JSON";
        return false;
    }
    if (!KeysAre(document, RootKeys())) {
        errorOut = "dependency contract has unknown or missing top-level fields";
        return false;
    }
    if (!document.at("schema").is_number_unsigned() ||
        document.at("schema").get<unsigned>() != 1u) {
        errorOut = "dependency contract schema is not 1";
        return false;
    }
    if (!KeysAre(document.at("harfbuzz"), HarfBuzzKeys())) {
        errorOut = "dependency contract has unknown or missing harfbuzz fields";
        return false;
    }
    if (!KeysAre(document.at("icu"), IcuKeys())) {
        errorOut = "dependency contract has unknown or missing icu fields";
        return false;
    }
    if (ContainsAbsolutePath(document)) {
        errorOut = "dependency contract carries an absolute path";
        return false;
    }
    sha256Out = molga::Sha256Bytes(bytes.data(), bytes.size());
    return true;
}

// ── The routed ICU entry points ──────────────────────────────────────────────

void DefaultSetCommonData(const void* data, int* status) {
    UErrorCode code = static_cast<UErrorCode>(*status);
    udata_setCommonData(data, &code);
    *status = static_cast<int>(code);
}

void DefaultSetFileAccess(int access, int* status) {
    UErrorCode code = static_cast<UErrorCode>(*status);
    udata_setFileAccess(static_cast<UDataFileAccess>(access), &code);
    *status = static_cast<int>(code);
}

void DefaultInit(int* status) {
    UErrorCode code = static_cast<UErrorCode>(*status);
    u_init(&code);
    *status = static_cast<int>(code);
}

void DefaultCleanup() { u_cleanup(); }

detail::IcuRuntimeApi MakeDefaultApi() noexcept {
    detail::IcuRuntimeApi api;
    api.setCommonData = &DefaultSetCommonData;
    api.setFileAccess = &DefaultSetFileAccess;
    api.init          = &DefaultInit;
    api.cleanup       = &DefaultCleanup;
    return api;
}

detail::IcuRuntimeApi& ActiveApi() noexcept {
    static detail::IcuRuntimeApi api = MakeDefaultApi();
    return api;
}

// Set the first time the lifecycle leaves NeverInitialized. After that the
// table is frozen: swapping it under a live or already terminal ICU runtime
// would make the recorded call counts meaningless.
bool g_lifecycleTransitioned = false;

}  // namespace

namespace detail {

const IcuRuntimeApi& DefaultIcuRuntimeApi() noexcept {
    static const IcuRuntimeApi api = MakeDefaultApi();
    return api;
}

bool InstallIcuRuntimeApi(const IcuRuntimeApi& api) noexcept {
    if (g_lifecycleTransitioned) return false;
    if (api.setCommonData == nullptr || api.setFileAccess == nullptr ||
        api.init == nullptr || api.cleanup == nullptr) {
        return false;
    }
    ActiveApi() = api;
    return true;
}

}  // namespace detail

// ── Shared packaged-data verification ────────────────────────────────────────

bool VerifyPackagedIcuDataFile(const fs::path& path, TextDiagnosticSink& sink) {
    std::error_code error;
    if (!fs::is_regular_file(path, error) || error) {
        ReportDependencyInvalid(sink,
                                "ICU data is not a regular file: " + path.string(),
                                kRestoreIcuDataRemediation);
        return false;
    }
    const std::uintmax_t actualBytes = fs::file_size(path, error);
    if (error || actualBytes != kPackagedIcuDataBytes) {
        ReportDependencyInvalid(
            sink,
            "ICU data is " +
                (error ? std::string("unreadable")
                       : std::to_string(actualBytes) + " bytes, expected " +
                             std::to_string(kPackagedIcuDataBytes)) +
                ": " + path.string(),
            kRestoreIcuDataRemediation);
        return false;
    }
    std::string       hashError;
    const std::string actualSha = molga::Sha256File(path, &hashError);
    if (!hashError.empty() || actualSha != kPackagedIcuDataSha256) {
        ReportDependencyInvalid(
            sink,
            hashError.empty()
                ? "ICU data SHA-256 is " + actualSha + ", expected " +
                      std::string(kPackagedIcuDataSha256) + ": " + path.string()
                : "ICU data could not be hashed: " + path.string() + " (" +
                      hashError + ")",
            kRestoreIcuDataRemediation);
        return false;
    }
    return true;
}

// ── Configuration ────────────────────────────────────────────────────────────

TextDependencyConfig TextDependencyConfig::FromEngineTextRoot(
    const fs::path& root, bool packagedRuntime) {
    TextDependencyConfig config;
    config.packagedRuntime = packagedRuntime;
    if (root.empty()) return config;
    config.dependencyContract = root / "text_dependency_contract.json";
    config.icuData            = root / "icudt78l.dat";
    return config;
}

// ── Runtime state ────────────────────────────────────────────────────────────

struct TextRuntimeDependencies::State {
    // The 16-byte aligned immutable copy ICU reads for its whole lifetime. It
    // is released only after the terminal u_cleanup has returned.
    void*                            alignedData = nullptr;
    std::string                      dependencyContractSha256;
    std::unordered_set<std::uint64_t> clientTokens;
    std::uint64_t                    nextToken = 1;

    ~State() {
        if (alignedData != nullptr) std::free(alignedData);
    }
};

TextRuntimeDependencies::TextRuntimeDependencies()  = default;
TextRuntimeDependencies::~TextRuntimeDependencies() = default;

TextRuntimeDependencies& TextRuntimeDependencies::Get() {
    static TextRuntimeDependencies instance;
    return instance;
}

bool TextRuntimeDependencies::IsReady() const noexcept {
    return lifecycle_ == Lifecycle::Ready;
}

const std::string& TextRuntimeDependencies::DependencyContractSha256() const noexcept {
    static const std::string empty;
    return state_ ? state_->dependencyContractSha256 : empty;
}

std::size_t TextRuntimeDependencies::OutstandingClientHandleCount() const noexcept {
    return state_ ? state_->clientTokens.size() : 0;
}

bool TextRuntimeDependencies::Initialize(const TextDependencyConfig& config,
                                         TextDiagnosticSink&         sink) {
    if (lifecycle_ != Lifecycle::NeverInitialized) {
        ReportDependencyInvalid(
            sink,
            lifecycle_ == Lifecycle::Ready
                ? "the text runtime is already initialized in this process"
                : "the text runtime was terminally cleaned up in this process",
            kFreshProcessRemediation);
        return false;
    }
    if (config.dependencyContract.empty() || config.icuData.empty()) {
        ReportDependencyInvalid(
            sink, "text dependency configuration has an empty path",
            "point the runtime at a built Engine/Text root containing "
            "text_dependency_contract.json and icudt78l.dat");
        return false;
    }

    // Everything below this point up to the first ICU call is pure validation,
    // so a rejected input leaves the process able to try again with a good one.
    // The candidate holds the verified SHA and, later, the aligned data; it
    // becomes state_ only once every ICU call has succeeded.
    auto        candidate = std::make_unique<State>();
    std::string error;
    if (!VerifyPortableContract(config.dependencyContract,
                                candidate->dependencyContractSha256, error)) {
        ReportDependencyInvalid(
            sink, error + ": " + config.dependencyContract.string(),
            "rebuild the text dependencies so the verification barrier "
            "republishes a portable contract");
        return false;
    }

    std::error_code sizeError;
    const std::uintmax_t actualBytes = fs::file_size(config.icuData, sizeError);
    if (sizeError || actualBytes != kPackagedIcuDataBytes) {
        ReportDependencyInvalid(
            sink,
            "ICU data is " +
                (sizeError ? std::string("unreadable")
                           : std::to_string(actualBytes) + " bytes, expected " +
                                 std::to_string(kPackagedIcuDataBytes)) +
                ": " + config.icuData.string(),
            kRestoreIcuDataRemediation);
        return false;
    }

    const std::size_t byteCount = static_cast<std::size_t>(kPackagedIcuDataBytes);
    static_assert(kPackagedIcuDataBytes % kIcuDataAlignment == 0,
                  "the pinned ICU data size must be a multiple of the alignment");
    candidate->alignedData = std::aligned_alloc(kIcuDataAlignment, byteCount);
    if (candidate->alignedData == nullptr) {
        ReportDependencyInvalid(
            sink, "could not allocate aligned ICU common data storage",
            "free memory and retry; no ICU state was created");
        return false;
    }
    void* const aligned = candidate->alignedData;

    // Read straight into the aligned storage and judge those exact bytes: the
    // configured file itself is never written to, and nothing between the hash
    // and udata_setCommonData can substitute different content.
    {
        std::ifstream input(config.icuData, std::ios::binary);
        if (!input.good() ||
            !input.read(static_cast<char*>(aligned),
                        static_cast<std::streamsize>(byteCount))) {
            ReportDependencyInvalid(
                sink, "could not read ICU data: " + config.icuData.string(),
                kRestoreIcuDataRemediation);
            return false;
        }
    }

    const std::string actualSha = molga::Sha256Bytes(aligned, byteCount);
    if (actualSha != kPackagedIcuDataSha256) {
        ReportDependencyInvalid(
            sink,
            "ICU data SHA-256 is " + actualSha + ", expected " +
                std::string(kPackagedIcuDataSha256) + ": " + config.icuData.string(),
            kRestoreIcuDataRemediation);
        return false;
    }

    // First ICU call. From here a failure is terminal, because ICU may already
    // hold registrations that no second attempt in this process can replace.
    // Freezing the table here rather than at the transition itself means no
    // routed call can ever land on a table a later Install swapped in.
    g_lifecycleTransitioned = true;

    const detail::IcuRuntimeApi& api                = ActiveApi();
    int                          status             = static_cast<int>(U_ZERO_ERROR);
    bool                         commonDataAccepted = false;
    std::string                  failedCall;

    api.setCommonData(aligned, &status);
    if (U_SUCCESS(static_cast<UErrorCode>(status))) {
        commonDataAccepted = true;
        api.setFileAccess(static_cast<int>(UDATA_ONLY_PACKAGES), &status);
        if (U_SUCCESS(static_cast<UErrorCode>(status))) {
            api.init(&status);
            if (!U_SUCCESS(static_cast<UErrorCode>(status))) failedCall = "u_init";
        } else {
            failedCall = "udata_setFileAccess";
        }
    } else {
        failedCall = "udata_setCommonData";
    }

    if (!failedCall.empty()) {
        api.cleanup();
        // The aligned storage outlived every ICU call, u_cleanup included, and
        // only now goes away. state_ was never published, so it stays null.
        candidate.reset();
        lifecycle_ = Lifecycle::TerminallyCleaned;
        ReportDependencyInvalid(
            sink,
            failedCall + " failed with ICU status " + std::to_string(status) +
                (commonDataAccepted ? " after common data was accepted"
                                    : " before common data was accepted"),
            kFreshProcessRemediation);
        return false;
    }

    state_     = std::move(candidate);
    lifecycle_ = Lifecycle::Ready;
    return true;
}

void TextRuntimeDependencies::Shutdown() {
    if (lifecycle_ != Lifecycle::Ready) return;
    if (!state_->clientTokens.empty()) {
        std::fprintf(stderr,
                     "molga text runtime: %zu client handles outlived the "
                     "runtime; refusing to run u_cleanup underneath them\n",
                     state_->clientTokens.size());
        std::abort();
    }

    // Runtime-owned ICU/HarfBuzz handles are released here, while the common
    // data buffer and the ICU runtime are both still alive. This runtime owns
    // none yet; the faces, break iterators and shapers that Milestones 4-8 add
    // to State are cleared at this point, before the terminal cleanup below.

    g_lifecycleTransitioned = true;
    ActiveApi().cleanup();
    // Only now may the aligned common data go away, and only now is the
    // lifetime terminal. No ICU API and no HarfBuzz object backed by
    // hb_icu_get_unicode_funcs may be touched in this process afterwards.
    state_.reset();
    lifecycle_ = Lifecycle::TerminallyCleaned;
}

// ── Client handles ───────────────────────────────────────────────────────────

TextRuntimeClientHandle::TextRuntimeClientHandle(std::uint64_t token) noexcept
    : token_(token) {}

std::optional<TextRuntimeClientHandle> TextRuntimeClientHandle::Acquire() {
    TextRuntimeDependencies& runtime = TextRuntimeDependencies::Get();
    if (!runtime.IsReady()) return std::nullopt;
    const std::uint64_t token = runtime.state_->nextToken++;
    runtime.state_->clientTokens.insert(token);
    return TextRuntimeClientHandle(token);
}

TextRuntimeClientHandle::TextRuntimeClientHandle(
    TextRuntimeClientHandle&& other) noexcept
    : token_(other.token_) {
    other.token_ = 0;
}

TextRuntimeClientHandle& TextRuntimeClientHandle::operator=(
    TextRuntimeClientHandle&& other) noexcept {
    if (this != &other) std::swap(token_, other.token_);
    return *this;
}

TextRuntimeClientHandle::~TextRuntimeClientHandle() {
    if (token_ == 0) return;
    TextRuntimeDependencies& runtime = TextRuntimeDependencies::Get();
    if (runtime.state_) runtime.state_->clientTokens.erase(token_);
    token_ = 0;
}

// ── Application lifetime guard ───────────────────────────────────────────────

TextRuntimeLifetimeGuard::TextRuntimeLifetimeGuard(bool active) noexcept
    : active_(active) {}

std::optional<TextRuntimeLifetimeGuard> TextRuntimeLifetimeGuard::Create(
    const TextDependencyConfig& config, TextDiagnosticSink& sink) {
    if (!TextRuntimeDependencies::Get().Initialize(config, sink)) {
        return std::nullopt;
    }
    return TextRuntimeLifetimeGuard(true);
}

TextRuntimeLifetimeGuard::TextRuntimeLifetimeGuard(
    TextRuntimeLifetimeGuard&& other) noexcept
    : active_(other.active_) {
    other.active_ = false;
}

TextRuntimeLifetimeGuard& TextRuntimeLifetimeGuard::operator=(
    TextRuntimeLifetimeGuard&& other) noexcept {
    // Swap rather than drop: assigning over a live guard must neither run
    // u_cleanup in the middle of an assignment nor silently lose the
    // obligation. The source carries the old state out and performs exactly
    // the cleanup this guard would have, so still only one guard ever does.
    if (this != &other) std::swap(active_, other.active_);
    return *this;
}

TextRuntimeLifetimeGuard::~TextRuntimeLifetimeGuard() {
    if (!active_) return;
    active_                          = false;
    TextRuntimeDependencies& runtime = TextRuntimeDependencies::Get();
    if (runtime.OutstandingClientHandleCount() != 0) {
        std::fprintf(stderr,
                     "molga text runtime: %zu client handles outlived the "
                     "lifetime guard\n",
                     runtime.OutstandingClientHandleCount());
        std::abort();
    }
    runtime.Shutdown();
}

}  // namespace molga::text
