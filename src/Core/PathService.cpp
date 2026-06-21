#include "Core/PathService.h"
#include <cstdlib>
#include <system_error>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#include <vector>
#elif defined(__linux__)
#include <unistd.h>
#elif defined(_WIN32)
#include <windows.h>
#endif

namespace fs = std::filesystem;

PathService& PathService::Get() {
    static PathService instance;
    return instance;
}

void PathService::InitFromExecutable(const char* argv0) {
    fs::path exePath;

#if defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::vector<char> buf(size + 1, '\0');
    if (_NSGetExecutablePath(buf.data(), &size) == 0) {
        exePath = fs::path(buf.data());
    }
#elif defined(__linux__)
    char buf[4096];
    ssize_t n = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n > 0) { buf[n] = '\0'; exePath = fs::path(buf); }
#elif defined(_WIN32)
    wchar_t buf[MAX_PATH];
    DWORD n = ::GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (n > 0) exePath = fs::path(std::wstring(buf, n));
#endif

    if (exePath.empty() && argv0) {
        exePath = fs::path(argv0);          // 폴백
    }

    std::error_code ec;
    fs::path canonical = fs::weakly_canonical(exePath, ec);
    if (ec) canonical = exePath;
    executableDir_ = canonical.parent_path();
}

std::string PathService::Resolve(const fs::path& root, const std::string& stored) {
    if (stored.empty()) return "";
    fs::path p(stored);
    if (p.is_absolute()) return stored;
    return (root / p).string();
}

bool PathService::IsSafeOutputPath(const fs::path& path, std::string& reason) {
    if (path.empty()) { reason = "empty path"; return false; }
    fs::path p = path.lexically_normal();
    if (p == p.root_path()) { reason = "filesystem root"; return false; }
    if (p == fs::path(".") || p == fs::path("..")) { reason = "current/parent directory"; return false; }

    if (const char* home = std::getenv("HOME")) {
        if (!std::string(home).empty() && p == fs::path(home).lexically_normal()) {
            reason = "home directory"; return false;
        }
    }
    std::error_code ec;
    fs::path cwd = fs::current_path(ec);
    if (!ec && p == cwd.lexically_normal()) { reason = "current working directory"; return false; }

    return true;
}

bool PathService::IsSafeOutputPath(
    const fs::path& path,
    std::string& reason,
    const fs::path& projectRoot,
    const fs::path& engineRoot) {
    if (!IsSafeOutputPath(path, reason)) {
        return false;
    }

    std::error_code ec;
    fs::path output = fs::weakly_canonical(path, ec);
    if (ec) {
        output = path.lexically_normal();
    }

    auto rejectsExactRoot = [&](const fs::path& root, const char* label) {
        if (root.empty()) return false;
        std::error_code rootEc;
        fs::path protectedRoot = fs::weakly_canonical(root, rootEc);
        if (rootEc) {
            protectedRoot = root.lexically_normal();
        }
        if (output == protectedRoot) {
            reason = std::string(label) + " root";
            return true;
        }
        return false;
    };

    if (rejectsExactRoot(projectRoot, "project")) return false;
    if (rejectsExactRoot(engineRoot, "engine")) return false;
    return true;
}
