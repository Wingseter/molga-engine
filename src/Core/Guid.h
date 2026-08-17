#pragma once

#include <cctype>
#include <iomanip>
#include <random>
#include <sstream>
#include <string>

namespace molga {

// 32자리 소문자 hex 식별자. PrefabRegistry::GenerateGUID와 동일 포맷.
class Guid {
public:
    static std::string Generate() {
        static std::random_device rd;
        static std::mt19937 gen(rd());
        static std::uniform_int_distribution<uint32_t> dis(0, 0xFFFFFFFF);
        std::stringstream ss;
        ss << std::hex << std::setfill('0')
           << std::setw(8) << dis(gen) << std::setw(8) << dis(gen)
           << std::setw(8) << dis(gen) << std::setw(8) << dis(gen);
        return ss.str();
    }

    static bool IsValid(const std::string& s) {
        if (s.size() != 32) return false;
        for (char c : s) {
            if (!std::isxdigit(static_cast<unsigned char>(c))) return false;
        }
        return true;
    }
};

} // namespace molga
