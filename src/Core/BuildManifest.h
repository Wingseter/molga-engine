#pragma once

#include <string>
#include <vector>

// 빌드에 반드시 존재해야 하는 파일/디렉터리 목록과 검증.
struct BuildManifest {
    std::vector<std::string> requiredFiles;

    // 누락된 항목 목록을 반환.
    std::vector<std::string> FindMissing() const;

    // 누락이 하나라도 있으면 false + errorOut에 원인.
    bool Validate(std::string& errorOut) const;
};
