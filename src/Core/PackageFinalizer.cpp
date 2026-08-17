#include "PackageFinalizer.h"

#include <system_error>

namespace PackageFinalizer {
namespace fs = std::filesystem;

namespace {

bool g_failNextStagingRename = false;

Result Fail(const std::string& error) {
    return Result{false, error};
}

std::string QuotePath(const fs::path& path) {
    return "'" + path.string() + "'";
}

}  // namespace

Result FinalizeStagedPackage(const fs::path& stagingOutput,
                             const fs::path& finalOutput) {
    if (stagingOutput.empty()) {
        return Fail("staging output path is empty");
    }
    if (finalOutput.empty()) {
        return Fail("final output path is empty");
    }

    std::error_code ec;
    const bool stagingExists = fs::exists(stagingOutput, ec);
    if (ec) {
        return Fail("could not inspect staging output " + QuotePath(stagingOutput) +
                    ": " + ec.message());
    }
    const bool stagingIsDirectory = fs::is_directory(stagingOutput, ec);
    if (ec) {
        return Fail("could not inspect staging output " + QuotePath(stagingOutput) +
                    ": " + ec.message());
    }
    if (!stagingExists || !stagingIsDirectory) {
        return Fail("staging output does not exist or is not a directory: " +
                    QuotePath(stagingOutput));
    }

    const fs::path backupOutput(finalOutput.string() + ".previous");

    fs::remove_all(backupOutput, ec);
    if (ec) {
        return Fail("could not remove stale backup " + QuotePath(backupOutput) +
                    ": " + ec.message());
    }

    const bool hadFinal = fs::exists(finalOutput, ec);
    if (ec) {
        return Fail("could not inspect final output " + QuotePath(finalOutput) +
                    ": " + ec.message());
    }

    if (hadFinal) {
        fs::rename(finalOutput, backupOutput, ec);
        if (ec) {
            return Fail("could not move current output " + QuotePath(finalOutput) +
                        " to backup " + QuotePath(backupOutput) + ": " +
                        ec.message());
        }
    }

    if (g_failNextStagingRename) {
        g_failNextStagingRename = false;
        ec = std::make_error_code(std::errc::permission_denied);
    } else {
        fs::rename(stagingOutput, finalOutput, ec);
    }
    if (ec) {
        const std::string renameError = ec.message();

        std::error_code cleanupEc;
        if (fs::exists(finalOutput, cleanupEc)) {
            fs::remove_all(finalOutput, cleanupEc);
        }

        std::error_code restoreEc;
        if (hadFinal && fs::exists(backupOutput, restoreEc)) {
            fs::rename(backupOutput, finalOutput, restoreEc);
        }

        if (restoreEc) {
            return Fail("could not move staged output into place: " + renameError +
                        "; previous output restore failed: " +
                        restoreEc.message());
        }

        return Fail("could not move staged output into place; previous output "
                    "was restored: " +
                    renameError);
    }

    if (hadFinal) {
        fs::remove_all(backupOutput, ec);
        if (ec) {
            return Fail("built package is in place, but backup cleanup failed for " +
                        QuotePath(backupOutput) + ": " + ec.message());
        }
    }

    return Result{true, ""};
}

void FailNextStagingRenameForTesting() {
    g_failNextStagingRename = true;
}

}  // namespace PackageFinalizer
