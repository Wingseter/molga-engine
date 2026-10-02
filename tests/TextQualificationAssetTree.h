#pragma once

// 클린 체크아웃에서 그대로 재현되는 텍스트 자격(qualification) 애셋 트리.
// 소스와 그 옆의 .meta는 언제나 한 쌍으로 복사한다. 한쪽만 복사되면
// ScanProject가 새 GUID를 생성해 버리고, 그 순간 이 트리의 고정된 GUID 계약이
// 조용히 무너진다.

#include "SmokeTestSupport.h"

#include <array>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>

constexpr std::array<std::string_view, 13> kQualificationSources{
    "fonts/NotoSans-Regular.ttf",
    "fonts/NotoSansHebrew-Regular.ttf",
    "fonts/NotoSansArabic-Regular.ttf",
    "fonts/NotoSansDevanagari-Regular.ttf",
    "fonts/NotoSansThai-Regular.ttf",
    "fonts/NotoSansKR-Regular.otf",
    "licenses/NotoFonts-ffebf8c1-OFL.txt",
    "licenses/NotoCJK-Sans2.004-OFL.txt",
    "families/primary.fontfamily",
    "families/arabic.fontfamily",
    "families/cjk.fontfamily",
    "families/cycle-a.fontfamily",
    "families/cycle-b.fontfamily",
};

class QualificationAssetTreeFixture {
public:
    QualificationAssetTreeFixture()
        : temp_("text-qualification-assets"),
          projectRoot_(temp_.Path()),
          assetsRoot_(projectRoot_ / "Assets") {
        const std::filesystem::path sourceRoot =
            MOLGA_TEXT_QUALIFICATION_SOURCE_ROOT;
        for (const std::string_view relative : kQualificationSources) {
            const std::string relativePath(relative);
            CopyRequired(sourceRoot / relativePath, assetsRoot_ / relativePath);
            CopyRequired(sourceRoot / (std::string(relative) + ".meta"),
                         assetsRoot_ / (std::string(relative) + ".meta"));
        }
    }
    const std::filesystem::path& ProjectRoot() const { return projectRoot_; }
    const std::filesystem::path& AssetsRoot() const { return assetsRoot_; }

private:
    static void CopyRequired(const std::filesystem::path& source,
                             const std::filesystem::path& destination) {
        if (!std::filesystem::is_regular_file(source))
            throw std::runtime_error("missing fixture pair member: " + source.string());
        std::filesystem::create_directories(destination.parent_path());
        if (!std::filesystem::copy_file(
                source, destination, std::filesystem::copy_options::none))
            throw std::runtime_error("fixture destination exists: " + destination.string());
    }

    test_support::TempDirectory temp_;
    std::filesystem::path projectRoot_;
    std::filesystem::path assetsRoot_;
};
