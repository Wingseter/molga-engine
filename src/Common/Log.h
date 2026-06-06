#pragma once

#include <string>

namespace Log {
    void Info(const std::string& tag, const std::string& msg);
    void Warn(const std::string& tag, const std::string& msg);
    void Error(const std::string& tag, const std::string& msg);
}
