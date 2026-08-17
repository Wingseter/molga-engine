#include "Platform/Process.h"
#include <array>
#include <cstdio>
#include <cstdlib>

#ifndef _WIN32
#include <sys/wait.h>
#endif

namespace molga {

ProcessResult SystemProcessRunner::Run(const std::string& command,
                                      const std::string& workdir,
                                      const std::function<void(const std::string&)>& onLine,
                                      const std::function<bool()>& isCancelled) {
    std::string fullCmd;
    if (!workdir.empty()) {
        fullCmd = "cd \"" + workdir + "\" && " + command + " 2>&1";
    } else {
        fullCmd = command + " 2>&1";
    }

#ifdef _WIN32
    FILE* pipe = _popen(fullCmd.c_str(), "r");
#else
    FILE* pipe = popen(fullCmd.c_str(), "r");
#endif

    if (!pipe) {
        return { -1, false };
    }

    std::array<char, 256> buffer;
    bool wasCancelled = false;
    std::string lineBuffer;

    while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
        if (isCancelled && isCancelled()) {
            wasCancelled = true;
            break;
        }
        
        lineBuffer += buffer.data();
        size_t pos;
        while ((pos = lineBuffer.find('\n')) != std::string::npos) {
            std::string line = lineBuffer.substr(0, pos + 1);
            if (onLine) {
                onLine(line);
            }
            lineBuffer = lineBuffer.substr(pos + 1);
        }
    }

    if (!wasCancelled && !lineBuffer.empty()) {
        if (onLine) {
            onLine(lineBuffer + "\n");
        }
    }

#ifdef _WIN32
    int status = _pclose(pipe);
    int exitCode = status;
#else
    int status = pclose(pipe);
    int exitCode = status;
    if (status != -1 && WIFEXITED(status)) {
        exitCode = WEXITSTATUS(status);
    }
#endif

    return { wasCancelled ? -1 : exitCode, wasCancelled };
}

} // namespace molga
