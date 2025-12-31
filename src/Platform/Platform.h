#ifndef MOLGA_PLATFORM_H
#define MOLGA_PLATFORM_H

#include <string>

namespace Platform {

// Dynamic library loading
void* LoadDynamicLibrary(const char* path);
void* GetSymbol(void* handle, const char* name);
void CloseDynamicLibrary(void* handle);
std::string GetDynamicLibraryError();

// Platform detection
enum class PlatformType {
    Windows,
    macOS,
    Linux,
    Unknown
};

PlatformType GetCurrentPlatform();
const char* GetPlatformName();

// File system utilities
std::string GetExecutablePath();
std::string GetWorkingDirectory();
bool SetWorkingDirectory(const std::string& path);

} // namespace Platform

#endif // MOLGA_PLATFORM_H
