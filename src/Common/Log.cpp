#include "Log.h"
#include <iostream>

namespace Log {

void Info(const std::string& tag, const std::string& msg) {
    std::cout << "[" << tag << "] " << msg << std::endl;
}

void Warn(const std::string& tag, const std::string& msg) {
    std::cout << "[" << tag << "] [WARN] " << msg << std::endl;
}

void Error(const std::string& tag, const std::string& msg) {
    std::cerr << "[" << tag << "] [ERROR] " << msg << std::endl;
}

} // namespace Log
