// molga_text_runtime_probe — one ICU lifetime observation per process.
//
// A successful ICU lifetime is terminal: hb_icu_get_unicode_funcs() caches ICU
// normalizer pointers process-statically, so nothing can restart one. Every
// not-ready, post-cleanup and tamper observation therefore needs a fresh
// process, and this executable is it. It performs at most one successful
// lifetime, writes its schema-1 report, and returns immediately after terminal
// cleanup.
//
// It is handed everything it needs as literal argv. It has no compiled source,
// fixture or development root, and it never derives one from the working
// directory or from its own executable path.

#include "TextRuntimeDependenciesTestAccess.h"

#include "Text/TextDiagnostic.h"
#include "Text/TextRuntimeDependencies.h"

#include <nlohmann/json.hpp>

#include <hb.h>
#include <hb-icu.h>

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <system_error>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;

namespace {

// ── Command line ─────────────────────────────────────────────────────────────

const std::vector<std::string>& AllowedModes() {
    static const std::vector<std::string> modes = {
        "tampered-data", "nonportable-contract", "terminal-nonrestart",
        "staged-valid",  "dev-missing-contract", "dev-tampered-data"};
    return modes;
}

struct Arguments {
    std::string mode;
    fs::path    fixtureRoot;
    fs::path    developmentRoot;
    fs::path    report;
};

void Fail(const std::string& message) {
    std::cerr << "molga_text_runtime_probe: " << message << '\n';
}

// An existing, absolute, symlink-free directory whose own canonical form is
// itself. The caller is the only party allowed to choose these paths, so a
// noncanonical one means the caller did not do what the contract says.
bool IsCanonicalExistingDirectory(const fs::path& candidate) {
    if (!candidate.is_absolute()) return false;
    std::error_code error;
    if (!fs::is_directory(candidate, error) || error) return false;
    const fs::path resolved = fs::canonical(candidate, error);
    return !error && resolved == candidate;
}

bool ParseArguments(int argc, char** argv, Arguments& out) {
    std::string mode;
    std::string fixtureRoot;
    std::string developmentRoot;
    std::string report;
    struct Slot {
        const char*  name;
        std::string* value;
        bool         seen = false;
    };
    Slot slots[] = {{"--mode", &mode},
                    {"--fixture-root", &fixtureRoot},
                    {"--development-root", &developmentRoot},
                    {"--report", &report}};

    for (int index = 1; index < argc; ++index) {
        const std::string switchName = argv[index];
        Slot*             matched    = nullptr;
        for (Slot& slot : slots) {
            if (switchName == slot.name) matched = &slot;
        }
        if (matched == nullptr) {
            Fail("unknown switch: " + switchName);
            return false;
        }
        if (matched->seen) {
            Fail("duplicate switch: " + switchName);
            return false;
        }
        if (index + 1 >= argc) {
            Fail("missing value for " + switchName);
            return false;
        }
        matched->seen   = true;
        *matched->value = argv[++index];
    }
    for (const Slot& slot : slots) {
        if (!slot.seen) {
            Fail(std::string("missing switch: ") + slot.name);
            return false;
        }
    }

    bool known = false;
    for (const std::string& allowed : AllowedModes()) {
        if (mode == allowed) known = true;
    }
    if (!known) {
        Fail("unknown mode: " + mode);
        return false;
    }
    if (!IsCanonicalExistingDirectory(fixtureRoot)) {
        Fail("fixture root is not an existing canonical directory: " + fixtureRoot);
        return false;
    }
    if (!IsCanonicalExistingDirectory(developmentRoot)) {
        Fail("development root is not an existing canonical directory: " +
             developmentRoot);
        return false;
    }

    const fs::path reportPath(report);
    if (!reportPath.is_absolute() || reportPath.lexically_normal() != reportPath) {
        Fail("report is not an absolute normalized path: " + report);
        return false;
    }
    const fs::path parent = reportPath.parent_path();
    if (!IsCanonicalExistingDirectory(parent)) {
        Fail("report parent is not an existing canonical directory: " +
             parent.string());
        return false;
    }
    const fs::path leaf = reportPath.filename();
    if (leaf.empty() || leaf == "." || leaf == ".." || parent / leaf != reportPath) {
        Fail("report is not a direct child of its parent: " + report);
        return false;
    }
    // symlink_status, not exists(): a dangling symlink at the report path is a
    // pre-existing entry too, and the caller promised a brand new child.
    std::error_code error;
    if (fs::exists(fs::symlink_status(reportPath, error))) {
        Fail("report already exists: " + report);
        return false;
    }

    out.mode            = mode;
    out.fixtureRoot     = fixtureRoot;
    out.developmentRoot = developmentRoot;
    out.report          = reportPath;
    return true;
}

// ── Isolated dependency trees ────────────────────────────────────────────────

// A uniquely named canonical child of the caller's validated temp root, removed
// with everything under it when the mode finishes. Nothing outside this
// directory is ever written, so committed data and the two immutable roots
// stay exactly as the build produced them.
class ScopedDirectory {
public:
    ScopedDirectory() = default;

    ScopedDirectory(const fs::path& parent, const std::string& prefix) {
        static unsigned counter = 0;
        fs::path        candidate;
        do {
            candidate = parent / (prefix + "-" +
                                  std::to_string(static_cast<long>(::getpid())) +
                                  "-" + std::to_string(counter++));
        } while (fs::exists(candidate));
        if (!fs::create_directory(candidate)) {
            throw std::runtime_error("could not create " + candidate.string());
        }
        path_ = fs::canonical(candidate);
    }

    ScopedDirectory(const ScopedDirectory&)            = delete;
    ScopedDirectory& operator=(const ScopedDirectory&) = delete;

    ScopedDirectory(ScopedDirectory&& other) noexcept
        : path_(std::move(other.path_)) {
        other.path_.clear();
    }

    ScopedDirectory& operator=(ScopedDirectory&& other) noexcept {
        if (this == &other) return *this;
        // Discharge this object's own directory before taking the source's,
        // so assigning over a live ScopedDirectory cannot abandon a tree.
        Remove();
        path_ = std::move(other.path_);
        other.path_.clear();
        return *this;
    }

    ~ScopedDirectory() { Remove(); }

    const fs::path& Path() const noexcept { return path_; }

private:
    void Remove() noexcept {
        if (path_.empty()) return;
        std::error_code error;
        fs::remove_all(path_, error);
        path_.clear();
    }

    fs::path path_;
};

struct CopiedTextRoot {
    ScopedDirectory directory;
    fs::path        contract;
    fs::path        icuData;

    const fs::path& Root() const noexcept { return directory.Path(); }
};

std::string ReadBinary(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input.good()) throw std::runtime_error("cannot read " + path.string());
    return std::string(std::istreambuf_iterator<char>(input),
                       std::istreambuf_iterator<char>());
}

void WriteBinary(const fs::path& path, const std::string& bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output.good()) throw std::runtime_error("cannot write " + path.string());
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    output.flush();
    if (!output.good()) throw std::runtime_error("short write " + path.string());
}

CopiedTextRoot CopyPair(const fs::path& sourceRoot, const fs::path& callerTempRoot,
                        const std::string& prefix) {
    CopiedTextRoot copied;
    copied.directory = ScopedDirectory(callerTempRoot, prefix);
    copied.contract  = copied.Root() / "text_dependency_contract.json";
    copied.icuData   = copied.Root() / "icudt78l.dat";
    WriteBinary(copied.contract,
                ReadBinary(sourceRoot / "text_dependency_contract.json"));
    WriteBinary(copied.icuData, ReadBinary(sourceRoot / "icudt78l.dat"));
    return copied;
}

// The verified fixture pair, copied so a mode may corrupt it.
CopiedTextRoot CopyValidTextDependencyTree(const fs::path& fixtureRoot,
                                           const fs::path& callerTempRoot) {
    return CopyPair(fixtureRoot, callerTempRoot, "valid-tree");
}

// The built development pair, copied so a mode may corrupt it. Only these two
// files are copied: whatever else may sit in a development root is not part of
// what the runtime consumes.
CopiedTextRoot CopyStagedEngineTextRoot(const fs::path& developmentRoot,
                                        const fs::path& callerTempRoot) {
    return CopyPair(developmentRoot, callerTempRoot, "staged-root");
}

// Flips the high bit of one byte in place and requires the file's size to be
// unchanged, so the tamper is content-only and a size check alone cannot see it.
void FlipOneByte(const fs::path& path, std::streamoff offset) {
    const std::uintmax_t before = fs::file_size(path);
    std::fstream file(path, std::ios::binary | std::ios::in | std::ios::out);
    if (!file.good()) throw std::runtime_error("cannot open " + path.string());
    file.seekg(offset);
    char original = 0;
    if (!file.read(&original, 1)) {
        throw std::runtime_error("cannot read byte in " + path.string());
    }
    const char flipped = static_cast<char>(static_cast<unsigned char>(original) ^
                                           0x80u);
    file.seekp(offset);
    if (!file.write(&flipped, 1)) {
        throw std::runtime_error("cannot write byte in " + path.string());
    }
    file.flush();
    file.close();
    if (fs::file_size(path) != before) {
        throw std::runtime_error("tamper changed the size of " + path.string());
    }
}

void RewriteContractField(const fs::path& contract, const std::string& section,
                          const std::string& field, const std::string& value) {
    nlohmann::json document = nlohmann::json::parse(ReadBinary(contract));
    document.at(section).at(field) = value;
    WriteBinary(contract, document.dump(2) + "\n");
}

enum class DevRootMutation { RemoveContract, TamperIcuData };

void ApplyDevRootMutation(const CopiedTextRoot& copied, DevRootMutation mutation) {
    switch (mutation) {
        case DevRootMutation::RemoveContract:
            if (!fs::remove(copied.contract)) {
                throw std::runtime_error("could not remove " +
                                         copied.contract.string());
            }
            return;
        case DevRootMutation::TamperIcuData:
            FlipOneByte(copied.icuData, 4096);
            return;
    }
}

// ── The report ───────────────────────────────────────────────────────────────

struct ProbeReport {
    std::string mode;
    bool        firstInitialize           = false;
    bool        readyBeforeShutdown       = false;
    bool        harfbuzzIcuProbe          = false;
    bool        secondInitializeAttempted = false;
    bool        secondInitialize          = false;
    bool        terminallyCleaned         = false;
};

void WriteReport(const fs::path& path, const ProbeReport& report,
                 const molga::text::VectorTextDiagnosticSink& sink) {
    std::vector<std::string> codes;
    for (const molga::text::TextDiagnostic& diagnostic : sink.Diagnostics()) {
        codes.push_back(molga::text::StableTextDiagnosticCode(diagnostic.code));
    }
    nlohmann::json document;
    document["mode"]                      = report.mode;
    document["firstInitialize"]           = report.firstInitialize;
    document["readyBeforeShutdown"]       = report.readyBeforeShutdown;
    document["harfbuzzIcuProbe"]          = report.harfbuzzIcuProbe;
    document["secondInitializeAttempted"] = report.secondInitializeAttempted;
    document["secondInitialize"]          = report.secondInitialize;
    document["icuCallsBeforePublish"] =
        molga::text_test::IcuCallsBeforePublish();
    document["icuCallsAfterTerminal"] =
        molga::text_test::IcuCallsAfterTerminal();
    document["terminallyCleaned"] = report.terminallyCleaned;
    document["diagnosticCodes"]   = codes;

    // Same-directory sibling then rename: a reader either sees no report at all
    // or the complete one, never a half-written object.
    const fs::path partial = path.parent_path() /
                             (path.filename().string() + ".partial");
    WriteBinary(partial, document.dump(2) + "\n");
    fs::rename(partial, path);
}

// ── Modes ────────────────────────────────────────────────────────────────────

// Exercised only while the runtime is ready. These funcs cache ICU normalizer
// pointers process-statically, which is exactly why the lifetime below is
// terminal and why nothing touches them again after u_cleanup.
bool ProbeHarfBuzzIcuFuncs() {
    hb_unicode_funcs_t* funcs = hb_icu_get_unicode_funcs();
    if (funcs == nullptr) return false;
    const bool category = hb_unicode_general_category(funcs, 0x0041u) ==
                          HB_UNICODE_GENERAL_CATEGORY_UPPERCASE_LETTER;
    const bool combining = hb_unicode_combining_class(funcs, 0x0301u) ==
                           HB_UNICODE_COMBINING_CLASS_ABOVE;
    return category && combining;
}

using molga::text::TextDependencyConfig;
using molga::text::TextRuntimeDependencies;

// One initialization attempt against a root that is expected to be rejected
// before any ICU call.
void RunFailClosed(const fs::path& root, molga::text::TextDiagnosticSink& sink,
                   ProbeReport& report) {
    TextRuntimeDependencies& runtime = TextRuntimeDependencies::Get();
    report.firstInitialize =
        runtime.Initialize(TextDependencyConfig::FromEngineTextRoot(root, false),
                           sink);
    report.readyBeforeShutdown = runtime.IsReady();
    if (report.firstInitialize) molga::text_test::MarkReadyPublished();
}

// One successful lifetime, ended by the single terminal cleanup this process
// is allowed. It returns to main immediately afterwards.
void RunOneLifetime(const fs::path& root, molga::text::TextDiagnosticSink& sink,
                    ProbeReport& report, bool probeHarfBuzz) {
    TextRuntimeDependencies& runtime = TextRuntimeDependencies::Get();
    report.firstInitialize =
        runtime.Initialize(TextDependencyConfig::FromEngineTextRoot(root, false),
                           sink);
    if (report.firstInitialize) molga::text_test::MarkReadyPublished();
    report.readyBeforeShutdown = runtime.IsReady();
    if (probeHarfBuzz && report.readyBeforeShutdown) {
        report.harfbuzzIcuProbe = ProbeHarfBuzzIcuFuncs();
    }
    runtime.Shutdown();
    report.terminallyCleaned = runtime.WasTerminallyCleaned();
}

int Run(const Arguments& arguments) {
    const fs::path callerTempRoot = arguments.report.parent_path();
    molga::text::VectorTextDiagnosticSink sink;
    ProbeReport                           report;
    report.mode = arguments.mode;

    if (arguments.mode == "tampered-data") {
        CopiedTextRoot copied =
            CopyValidTextDependencyTree(arguments.fixtureRoot, callerTempRoot);
        FlipOneByte(copied.icuData, 4096);
        RunFailClosed(copied.Root(), sink, report);
    } else if (arguments.mode == "nonportable-contract") {
        CopiedTextRoot copied =
            CopyValidTextDependencyTree(arguments.fixtureRoot, callerTempRoot);
        RewriteContractField(copied.contract, "harfbuzz", "sourcePath",
                             "/checkout/external/harfbuzz");
        RunFailClosed(copied.Root(), sink, report);
    } else if (arguments.mode == "dev-missing-contract") {
        CopiedTextRoot copied = CopyStagedEngineTextRoot(
            arguments.developmentRoot, callerTempRoot);
        ApplyDevRootMutation(copied, DevRootMutation::RemoveContract);
        RunFailClosed(copied.Root(), sink, report);
    } else if (arguments.mode == "dev-tampered-data") {
        CopiedTextRoot copied = CopyStagedEngineTextRoot(
            arguments.developmentRoot, callerTempRoot);
        ApplyDevRootMutation(copied, DevRootMutation::TamperIcuData);
        RunFailClosed(copied.Root(), sink, report);
    } else if (arguments.mode == "staged-valid") {
        RunOneLifetime(arguments.developmentRoot, sink, report,
                       /*probeHarfBuzz=*/false);
    } else if (arguments.mode == "terminal-nonrestart") {
        TextRuntimeDependencies& runtime = TextRuntimeDependencies::Get();
        RunOneLifetime(arguments.fixtureRoot, sink, report,
                       /*probeHarfBuzz=*/true);
        // The lifecycle has transitioned, so the private hook must now refuse
        // to replace the table. Nothing may swap a counting or forwarding
        // table under a runtime that has already entered — or left — ICU.
        if (molga::text_test::InstallCountingIcuRuntimeApi()) {
            Fail("the ICU runtime table was replaced after a lifecycle "
                 "transition");
            return 3;
        }
        // An extra Shutdown from the terminal state is a no-op that must reach
        // no ICU entry point, and a second Initialize must be rejected before
        // one. Neither may touch the HarfBuzz funcs cached above.
        runtime.Shutdown();
        report.secondInitializeAttempted = true;
        report.secondInitialize          = runtime.Initialize(
            TextDependencyConfig::FromEngineTextRoot(arguments.fixtureRoot, false),
            sink);
    } else {
        Fail("unhandled mode: " + arguments.mode);
        return 3;
    }

    WriteReport(arguments.report, report, sink);
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    Arguments arguments;
    if (!ParseArguments(argc, argv, arguments)) return 2;
    // Installed before the first initialization, which the private hook
    // enforces by refusing any replacement after a lifecycle transition.
    if (!molga::text_test::InstallCountingIcuRuntimeApi()) {
        Fail("could not install the counting ICU runtime table");
        return 2;
    }
    try {
        return Run(arguments);
    } catch (const std::exception& error) {
        Fail(error.what());
        return 3;
    }
}
