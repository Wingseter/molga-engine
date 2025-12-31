#include "Platform.h"

#ifdef _WIN32
    #include <windows.h>
    #include <direct.h>
    #define getcwd _getcwd
    #define chdir _chdir
#else
    #include <dlfcn.h>
    #include <unistd.h>
    #include <limits.h>
    #ifdef __APPLE__
        #include <mach-o/dyld.h>
    #endif
#endif

namespace Platform {

void* LoadDynamicLibrary(const char* path) {
#ifdef _WIN32
    return LoadLibraryA(path);
#else
    return dlopen(path, RTLD_NOW);
#endif
}

void* GetSymbol(void* handle, const char* name) {
#ifdef _WIN32
    return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(handle), name));
#else
    return dlsym(handle, name);
#endif
}

void CloseDynamicLibrary(void* handle) {
#ifdef _WIN32
    FreeLibrary(static_cast<HMODULE>(handle));
#else
    dlclose(handle);
#endif
}

std::string GetDynamicLibraryError() {
#ifdef _WIN32
    DWORD error = GetLastError();
    if (error == 0) return "";

    LPSTR messageBuffer = nullptr;
    size_t size = FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, error, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPSTR)&messageBuffer, 0, nullptr);

    std::string message(messageBuffer, size);
    LocalFree(messageBuffer);
    return message;
#else
    const char* error = dlerror();
    return error ? std::string(error) : "";
#endif
}

PlatformType GetCurrentPlatform() {
#ifdef _WIN32
    return PlatformType::Windows;
#elif defined(__APPLE__)
    return PlatformType::macOS;
#elif defined(__linux__)
    return PlatformType::Linux;
#else
    return PlatformType::Unknown;
#endif
}

const char* GetPlatformName() {
    switch (GetCurrentPlatform()) {
        case PlatformType::Windows: return "Windows";
        case PlatformType::macOS: return "macOS";
        case PlatformType::Linux: return "Linux";
        default: return "Unknown";
    }
}

std::string GetExecutablePath() {
#ifdef _WIN32
    char path[MAX_PATH];
    GetModuleFileNameA(nullptr, path, MAX_PATH);
    return std::string(path);
#elif defined(__APPLE__)
    char path[PATH_MAX];
    uint32_t size = sizeof(path);
    if (_NSGetExecutablePath(path, &size) == 0) {
        return std::string(path);
    }
    return "";
#elif defined(__linux__)
    char path[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", path, sizeof(path) - 1);
    if (len != -1) {
        path[len] = '\0';
        return std::string(path);
    }
    return "";
#else
    return "";
#endif
}

std::string GetWorkingDirectory() {
    char buffer[1024];
    if (getcwd(buffer, sizeof(buffer))) {
        return std::string(buffer);
    }
    return "";
}

bool SetWorkingDirectory(const std::string& path) {
    return chdir(path.c_str()) == 0;
}

} // namespace Platform
