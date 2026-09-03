#pragma once

#include "Assets/FontAsset.h"
#include "Text/TextDiagnostic.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace molga {

// 검증된 불변 폰트 바이트의 유일한 권한자. authoring source는 출처일 뿐이고
// 여기 게시된 content-addressed 산출물만이 이후 모든 단계의 바이트 권한이다.
// 그래서 두 저장 모드가 서로를 대체할 수 없고, 어느 모드도 실패 시 원본
// authoring 파일로 되돌아가 열지 않는다.
class FontArtifactStore {
public:
    struct PackagedAuthority {
        std::filesystem::path relativePath;
        std::string artifactSha256;
        bool operator==(const PackagedAuthority&) const;
    };
    static FontArtifactStore ForProject(
        std::filesystem::path projectRoot);
    static std::optional<FontArtifactStore> ForSealedPackage(
        std::filesystem::path runtimeResourceRoot,
        std::vector<PackagedAuthority> manifestFonts,
        molga::text::TextDiagnosticSink&);
    std::optional<VerifiedFontArtifact> Publish(
        const std::filesystem::path& sourcePath,
        std::string_view expectedSourceSha256,
        molga::text::TextDiagnosticSink&) const;
    std::optional<std::shared_ptr<const std::vector<std::uint8_t>>>
    ReadVerified(const VerifiedFontArtifact&,
                 molga::text::TextDiagnosticSink&) const;
    bool IsProjectAuthorityFor(
        const std::filesystem::path& projectRoot) const noexcept;
    // Step 4c의 "Project는 ProjectLibrary store를, SealedPackage는 봉인
    // 패키지 store를 요구한다"를 실제로 강제하려면 mode를 물어볼 수 있어야
    // 한다. IsProjectAuthorityFor는 특정 root와의 일치까지 요구하므로 그
    // 판정을 대신할 수 없다.
    FontArtifactStorage Storage() const noexcept { return storage_; }
private:
    FontArtifactStore(FontArtifactStorage storage,
                      std::filesystem::path storageRoot,
                      std::vector<PackagedAuthority> packagedAuthorities);
    FontArtifactStorage storage_;
    std::filesystem::path storageRoot_;
    std::vector<PackagedAuthority> packagedAuthorities_;
};

} // namespace molga
