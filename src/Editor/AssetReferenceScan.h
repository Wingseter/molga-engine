#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace molga {

// 프로젝트 안에서 특정 애셋 guid를 참조하는 문서를 찾는다.
class AssetReferenceScan {
public:
    // assetRoot 아래 .json/.prefab/.mat 등을 훑어 guid 문자열을 포함한 파일을 반환.
    static std::vector<std::filesystem::path> FindReferencers(
        const std::filesystem::path& assetRoot, const std::string& guid);
};

} // namespace molga
