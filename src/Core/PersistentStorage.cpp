#include "Core/PersistentStorage.h"

#include "Common/Sha256.h"

#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <system_error>

#include <fcntl.h>
#include <sys/stat.h>

#if defined(_WIN32)
#include <io.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace {
std::filesystem::path g_root;
std::filesystem::path g_overrideRoot;
bool g_configured = false;
bool g_failNextReplace = false;
std::atomic<unsigned long long> g_tempSequence{0};

void SetError(std::string* output, const std::string& message) {
    if (output) *output = message;
}

bool FlushFile(std::FILE* file) {
    if (std::fflush(file) != 0) return false;
#if defined(_WIN32)
    return _commit(_fileno(file)) == 0;
#else
    return ::fsync(fileno(file)) == 0;
#endif
}

#if defined(_WIN32)
bool ReplaceFileAtomically(const std::filesystem::path& from,
                           const std::filesystem::path& to) {
    const std::wstring source = from.wstring();
    const std::wstring destination = to.wstring();
    if (std::filesystem::exists(to)) {
        if (::ReplaceFileW(destination.c_str(), source.c_str(), nullptr,
                           REPLACEFILE_WRITE_THROUGH, nullptr, nullptr)) {
            return true;
        }
    }
    return ::MoveFileExW(source.c_str(), destination.c_str(),
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
}
#else
bool ReplaceFileAtomically(const std::filesystem::path& from,
                           const std::filesystem::path& to) {
    return ::rename(from.c_str(), to.c_str()) == 0;
}
#endif

// Creates a file only when it does not already exist. This is the primitive an
// immutable publish needs: "someone else got here first" has to be observable
// rather than silently clobbered.
int OpenCreateNew(const std::filesystem::path& path) {
#if defined(_WIN32)
    return ::_wopen(path.wstring().c_str(),
                    _O_CREAT | _O_EXCL | _O_WRONLY | _O_BINARY,
                    _S_IREAD | _S_IWRITE);
#else
    return ::open(path.c_str(), O_CREAT | O_EXCL | O_WRONLY, 0644);
#endif
}

bool WriteAllAndSync(int descriptor, const std::uint8_t* data,
                     std::size_t size) {
    std::size_t written = 0;
    while (written < size) {
#if defined(_WIN32)
        const int chunk = ::_write(descriptor, data + written,
                                   static_cast<unsigned int>(size - written));
#else
        const auto chunk = ::write(descriptor, data + written, size - written);
#endif
        if (chunk <= 0) return false;
        written += static_cast<std::size_t>(chunk);
    }
#if defined(_WIN32)
    return ::_commit(descriptor) == 0;
#else
    return ::fsync(descriptor) == 0;
#endif
}

void CloseDescriptor(int descriptor) {
#if defined(_WIN32)
    ::_close(descriptor);
#else
    ::close(descriptor);
#endif
}

bool ReadAllBytes(const std::filesystem::path& path,
                  std::vector<std::uint8_t>& out) {
    std::error_code error;
    const std::uintmax_t size = std::filesystem::file_size(path, error);
    if (error) return false;
    std::ifstream input(path, std::ios::binary);
    if (!input) return false;
    out.resize(static_cast<std::size_t>(size));
    if (!out.empty()) {
        input.read(reinterpret_cast<char*>(out.data()),
                   static_cast<std::streamsize>(out.size()));
        if (!input || static_cast<std::size_t>(input.gcount()) != out.size()) {
            return false;
        }
    }
    return true;
}

// An already-published destination is authoritative only when it is bit-for-bit
// the artifact we were about to write.
bool ExistingDestinationMatches(const std::filesystem::path& destination,
                                const std::vector<std::uint8_t>& bytes,
                                std::string_view expectedSha256,
                                std::string* errorOut) {
    std::vector<std::uint8_t> present;
    if (!ReadAllBytes(destination, present)) {
        SetError(errorOut, "Could not read the existing immutable artifact.");
        return false;
    }
    if (present.size() != bytes.size() || present != bytes ||
        molga::Sha256Bytes(present.data(), present.size()) != expectedSha256) {
        SetError(errorOut,
                 "An immutable artifact with different bytes already exists.");
        return false;
    }
    SetError(errorOut, {});
    return true;
}

// Removes the caller-owned sibling lock, and nothing else, on every exit path.
class ScopedPublishLock {
public:
    explicit ScopedPublishLock(std::filesystem::path path)
        : path_(std::move(path)) {}
    ~ScopedPublishLock() {
        if (!held_) return;
        std::error_code error;
        std::filesystem::remove(path_, error);
    }
    ScopedPublishLock(const ScopedPublishLock&) = delete;
    ScopedPublishLock& operator=(const ScopedPublishLock&) = delete;

    bool Acquire() {
        const int descriptor = OpenCreateNew(path_);
        if (descriptor < 0) return false;
        CloseDescriptor(descriptor);
        held_ = true;
        return true;
    }

private:
    std::filesystem::path path_;
    bool held_ = false;
};
} // namespace

bool PersistentStorage::ConfigureRuntime(const std::string& companyName,
                                         const std::string& gameName) {
    if (!IsSafePathSegment(companyName) || !IsSafePathSegment(gameName)) {
        return false;
    }
    g_root = g_overrideRoot.empty()
        ? PlatformDataRoot() / companyName / gameName
        : g_overrideRoot;
    g_configured = !g_root.empty();
    return g_configured;
}

bool PersistentStorage::ConfigureEditor(const std::filesystem::path& projectPath,
                                        const std::string& companyName,
                                        const std::string& gameName) {
    if (!IsSafePathSegment(companyName) || !IsSafePathSegment(gameName) ||
        projectPath.empty()) {
        return false;
    }

    if (!g_overrideRoot.empty()) {
        g_root = g_overrideRoot;
    } else {
        std::ostringstream hash;
        hash << std::hex << std::setfill('0') << std::setw(16)
             << StableProjectHash(projectPath);
        g_root = PlatformDataRoot() / "MolgaEditor" / hash.str() /
                 companyName / gameName;
    }
    g_configured = !g_root.empty();
    return g_configured;
}

const std::filesystem::path& PersistentStorage::Root() {
    return g_root;
}

bool PersistentStorage::IsConfigured() {
    return g_configured && !g_root.empty();
}

bool PersistentStorage::IsSafePathSegment(const std::string& value) {
    if (value.empty() || value == "." || value == "..") return false;
    for (unsigned char c : value) {
        if (c < 0x20 || c == 0x7f || c == '/' || c == '\\' || c == ':') {
            return false;
        }
    }
    return true;
}

std::uint64_t PersistentStorage::StableProjectHash(
    const std::filesystem::path& projectPath) {
    std::error_code error;
    std::filesystem::path normalized =
        std::filesystem::weakly_canonical(
            std::filesystem::absolute(projectPath, error), error);
    if (error || normalized.empty()) {
        normalized = projectPath.lexically_normal();
    }

    const std::string bytes = normalized.generic_string();
    std::uint64_t hash = 1469598103934665603ULL;
    for (unsigned char byte : bytes) {
        hash ^= byte;
        hash *= 1099511628211ULL;
    }
    return hash;
}

bool PersistentStorage::AtomicWriteText(
    const std::filesystem::path& destination,
    const std::string& contents,
    std::string* errorOut) {
    if (destination.empty() || destination.filename().empty()) {
        SetError(errorOut, "Destination path is empty.");
        return false;
    }

    std::error_code error;
    if (!destination.parent_path().empty()) {
        std::filesystem::create_directories(destination.parent_path(), error);
        if (error) {
            SetError(errorOut, "Could not create storage directory: " + error.message());
            return false;
        }
    }

    const auto sequence = ++g_tempSequence;
    std::filesystem::path temporary = destination;
    temporary += ".tmp." + std::to_string(sequence);

#if defined(_WIN32)
    std::FILE* file = _wfopen(temporary.wstring().c_str(), L"wb");
#else
    std::FILE* file = std::fopen(temporary.c_str(), "wb");
#endif
    if (!file) {
        SetError(errorOut, "Could not create temporary storage file: " +
                           std::string(std::strerror(errno)));
        return false;
    }

    const bool wrote = contents.empty() ||
        std::fwrite(contents.data(), 1, contents.size(), file) == contents.size();
    const bool flushed = wrote && FlushFile(file);
    const bool closed = std::fclose(file) == 0;
    if (!wrote || !flushed || !closed) {
        std::filesystem::remove(temporary, error);
        SetError(errorOut, "Could not fully write temporary storage file.");
        return false;
    }

    if (g_failNextReplace) {
        g_failNextReplace = false;
        std::filesystem::remove(temporary, error);
        SetError(errorOut, "Atomic replacement failed (injected test failure).");
        return false;
    }

    if (!ReplaceFileAtomically(temporary, destination)) {
        const int savedErrno = errno;
        std::filesystem::remove(temporary, error);
        SetError(errorOut, "Could not atomically replace storage file: " +
                           std::string(std::strerror(savedErrno)));
        return false;
    }

    SetError(errorOut, {});
    return true;
}

bool PersistentStorage::AtomicPublishImmutableBytes(
    const std::filesystem::path& destination,
    const std::vector<std::uint8_t>& bytes,
    std::string_view expectedSha256,
    std::string* errorOut) {
    if (destination.empty() || destination.filename().empty()) {
        SetError(errorOut, "Destination path is empty.");
        return false;
    }
    if (bytes.empty()) {
        SetError(errorOut, "An immutable artifact must not be empty.");
        return false;
    }
    const std::string actual = molga::Sha256Bytes(bytes.data(), bytes.size());
    if (actual.empty() || actual != expectedSha256) {
        SetError(errorOut,
                 "Immutable artifact bytes do not match the expected SHA-256.");
        return false;
    }

    std::error_code error;
    if (std::filesystem::exists(destination, error)) {
        return ExistingDestinationMatches(destination, bytes, expectedSha256,
                                          errorOut);
    }
    if (!destination.parent_path().empty()) {
        std::filesystem::create_directories(destination.parent_path(), error);
        if (error) {
            SetError(errorOut,
                     "Could not create artifact directory: " + error.message());
            return false;
        }
    }

    std::filesystem::path lockPath = destination;
    lockPath += ".lock";
    ScopedPublishLock lock(lockPath);
    if (!lock.Acquire()) {
        SetError(errorOut,
                 "Another publisher holds the immutable artifact lock: " +
                 std::string(std::strerror(errno)));
        return false;
    }
    // Another publisher may have completed between the first existence check
    // and the lock, so the decision is re-made while the lock is held.
    if (std::filesystem::exists(destination, error)) {
        return ExistingDestinationMatches(destination, bytes, expectedSha256,
                                          errorOut);
    }

    std::filesystem::path temporary = destination;
    temporary += ".tmp." + std::to_string(++g_tempSequence);
    const int descriptor = OpenCreateNew(temporary);
    if (descriptor < 0) {
        SetError(errorOut, "Could not create the artifact temporary file: " +
                           std::string(std::strerror(errno)));
        return false;
    }
    const bool wrote = WriteAllAndSync(descriptor, bytes.data(), bytes.size());
    CloseDescriptor(descriptor);
    if (!wrote) {
        std::filesystem::remove(temporary, error);
        SetError(errorOut, "Could not fully write the artifact temporary file.");
        return false;
    }

    std::vector<std::uint8_t> readBack;
    if (!ReadAllBytes(temporary, readBack) || readBack.size() != bytes.size() ||
        molga::Sha256Bytes(readBack.data(), readBack.size()) != expectedSha256) {
        std::filesystem::remove(temporary, error);
        SetError(errorOut,
                 "The published artifact did not verify after writing.");
        return false;
    }

    if (std::filesystem::exists(destination, error)) {
        std::filesystem::remove(temporary, error);
        return ExistingDestinationMatches(destination, bytes, expectedSha256,
                                          errorOut);
    }
    if (!ReplaceFileAtomically(temporary, destination)) {
        const int savedErrno = errno;
        std::filesystem::remove(temporary, error);
        SetError(errorOut, "Could not publish the immutable artifact: " +
                           std::string(std::strerror(savedErrno)));
        return false;
    }
    SetError(errorOut, {});
    return true;
}

void PersistentStorage::SetRootOverrideForTesting(
    const std::filesystem::path& root) {
    g_overrideRoot = root;
    g_root = root;
    g_configured = !root.empty();
}

void PersistentStorage::ClearRootOverrideForTesting() {
    g_overrideRoot.clear();
    g_root.clear();
    g_configured = false;
    g_failNextReplace = false;
}

void PersistentStorage::FailNextAtomicReplaceForTesting() {
    g_failNextReplace = true;
}

std::filesystem::path PersistentStorage::PlatformDataRoot() {
#if defined(_WIN32)
    if (const wchar_t* local = _wgetenv(L"LOCALAPPDATA")) {
        return std::filesystem::path(local);
    }
#elif defined(__APPLE__)
    if (const char* home = std::getenv("HOME")) {
        return std::filesystem::path(home) / "Library" / "Application Support";
    }
#else
    if (const char* xdg = std::getenv("XDG_DATA_HOME")) {
        if (*xdg != '\0') return std::filesystem::path(xdg);
    }
    if (const char* home = std::getenv("HOME")) {
        return std::filesystem::path(home) / ".local" / "share";
    }
#endif
    return std::filesystem::temp_directory_path() / "MolgaUserData";
}
