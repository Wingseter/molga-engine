#include "Assets/FontArtifactStore.h"

#include "Common/Sha256.h"
#include "Core/PersistentStorage.h"

#include <algorithm>
#include <fstream>
#include <utility>

namespace molga {
namespace {

// 폰트 원본/산출물의 상한. 이 위로는 검증된 불변 바이트로 취급하지 않는다.
constexpr std::uintmax_t kMaximumArtifactBytes = 256U * 1024U * 1024U;

void ReportInvalid(molga::text::TextDiagnosticSink& sink, std::string message,
                   std::string remediation) {
    // 호출 한 번에 진단 하나. 폰트/글리프마다 무제한으로 늘어나는 진단은
    // 이 경로에 존재하지 않는다.
    molga::text::TextDiagnostic diagnostic;
    diagnostic.code = molga::text::TextDiagnosticCode::FontInvalid;
    diagnostic.severity = molga::text::TextSeverity::Error;
    diagnostic.subsystem = "font-artifact";
    diagnostic.message = std::move(message);
    diagnostic.remediation = std::move(remediation);
    sink.Report(std::move(diagnostic));
}

bool CanonicalComponentsUnder(const std::filesystem::path& root,
                              const std::filesystem::path& candidate,
                              bool requireStrictlyBelow) {
    std::error_code error;
    const std::filesystem::path canonicalRoot =
        std::filesystem::weakly_canonical(root, error);
    if (error || canonicalRoot.empty()) return false;
    const std::filesystem::path canonicalCandidate =
        std::filesystem::weakly_canonical(candidate, error);
    if (error || canonicalCandidate.empty()) return false;

    auto rootPart = canonicalRoot.begin();
    auto candidatePart = canonicalCandidate.begin();
    for (; rootPart != canonicalRoot.end(); ++rootPart, ++candidatePart) {
        if (candidatePart == canonicalCandidate.end() ||
            *candidatePart != *rootPart) {
            return false;
        }
    }
    if (requireStrictlyBelow && candidatePart == canonicalCandidate.end()) {
        return false;
    }
    if (!requireStrictlyBelow && candidatePart != canonicalCandidate.end()) {
        return false;
    }
    return true;
}

// 이미 정규화된 상대 경로를 저장 루트 아래로 해석한다. weakly_canonical이
// symlink를 따라가므로, 루트 밖을 가리키는 symlink는 아래 prefix 비교에서
// 걸린다.
bool ResolveUnderRoot(const std::filesystem::path& root,
                      const std::string& relativeGeneric,
                      std::filesystem::path& out) {
    std::filesystem::path candidate = root;
    for (const auto& part : std::filesystem::path(relativeGeneric)) {
        candidate /= part;
    }
    if (!CanonicalComponentsUnder(root, candidate, true)) return false;
    std::error_code error;
    if (std::filesystem::is_symlink(std::filesystem::symlink_status(candidate,
                                                                    error))) {
        return false;
    }
    out = candidate;
    return true;
}

bool ReadArtifactBytes(const std::filesystem::path& path,
                       std::vector<std::uint8_t>& out) {
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error)) return false;
    const std::uintmax_t size = std::filesystem::file_size(path, error);
    if (error || size == 0U || size > kMaximumArtifactBytes) return false;
    std::ifstream input(path, std::ios::binary);
    if (!input) return false;
    out.resize(static_cast<std::size_t>(size));
    input.read(reinterpret_cast<char*>(out.data()),
               static_cast<std::streamsize>(out.size()));
    return static_cast<bool>(input) &&
           static_cast<std::size_t>(input.gcount()) == out.size();
}

} // namespace

bool FontArtifactStore::PackagedAuthority::operator==(
    const PackagedAuthority& other) const {
    return relativePath == other.relativePath &&
           artifactSha256 == other.artifactSha256;
}

FontArtifactStore::FontArtifactStore(
    FontArtifactStorage storage, std::filesystem::path storageRoot,
    std::vector<PackagedAuthority> packagedAuthorities)
    : storage_(storage), storageRoot_(std::move(storageRoot)),
      packagedAuthorities_(std::move(packagedAuthorities)) {}

FontArtifactStore FontArtifactStore::ForProject(
    std::filesystem::path projectRoot) {
    return FontArtifactStore(FontArtifactStorage::ProjectLibrary,
                             std::move(projectRoot), {});
}

std::optional<FontArtifactStore> FontArtifactStore::ForSealedPackage(
    std::filesystem::path runtimeResourceRoot,
    std::vector<PackagedAuthority> manifestFonts,
    molga::text::TextDiagnosticSink& sink) {
    std::error_code error;
    if (runtimeResourceRoot.empty() ||
        !std::filesystem::is_directory(runtimeResourceRoot, error)) {
        ReportInvalid(sink, "sealed package resource root is not a directory",
                      "point the runtime at the verified package resource root");
        return std::nullopt;
    }

    std::vector<PackagedAuthority> validated;
    validated.reserve(manifestFonts.size());
    for (const PackagedAuthority& authority : manifestFonts) {
        std::string normalized;
        if (!NormalizeFontArtifactRelativePath(authority.relativePath, normalized)) {
            ReportInvalid(sink,
                          "packaged font manifest path is not a safe relative "
                          "path: " + authority.relativePath.string(),
                          "re-emit the package manifest from the build");
            return std::nullopt;
        }
        // Step 4c: a sealed-package font locator lives under Assets/ and
        // nowhere else, so an authority outside that subtree is refused before
        // the store exists rather than at the first read.
        if (normalized.rfind("Assets/", 0) != 0U) {
            ReportInvalid(sink,
                          "packaged font manifest path is outside Assets/: " +
                          normalized,
                          "stage packaged fonts under Assets/");
            return std::nullopt;
        }
        if (!IsLowercaseSha256(authority.artifactSha256)) {
            ReportInvalid(sink,
                          "packaged font manifest SHA-256 is not lowercase "
                          "hexadecimal: " + normalized,
                          "re-emit the package manifest from the build");
            return std::nullopt;
        }
        validated.push_back({std::filesystem::path(normalized),
                             authority.artifactSha256});
    }

    std::sort(validated.begin(), validated.end(),
              [](const PackagedAuthority& lhs, const PackagedAuthority& rhs) {
                  if (lhs.relativePath != rhs.relativePath) {
                      return lhs.relativePath < rhs.relativePath;
                  }
                  return lhs.artifactSha256 < rhs.artifactSha256;
              });
    validated.erase(std::unique(validated.begin(), validated.end()),
                    validated.end());
    for (std::size_t index = 1; index < validated.size(); ++index) {
        if (validated[index].relativePath ==
            validated[index - 1U].relativePath) {
            // 같은 경로에 서로 다른 SHA가 있으면 어느 쪽이 권한인지 결정할 수
            // 없다. 하나를 골라 계속하면 조용히 잘못된 바이트를 신뢰하게 된다.
            ReportInvalid(sink,
                          "packaged font manifest lists conflicting SHA-256 "
                          "values for " +
                          validated[index].relativePath.generic_string(),
                          "re-emit the package manifest from the build");
            return std::nullopt;
        }
    }
    return FontArtifactStore(FontArtifactStorage::PackagedResource,
                             std::move(runtimeResourceRoot),
                             std::move(validated));
}

std::optional<VerifiedFontArtifact> FontArtifactStore::Publish(
    const std::filesystem::path& sourcePath,
    std::string_view expectedSourceSha256,
    molga::text::TextDiagnosticSink& sink) const {
    if (storage_ != FontArtifactStorage::ProjectLibrary) {
        ReportInvalid(sink,
                      "a sealed package font store cannot publish artifacts",
                      "import fonts through the authoring project");
        return std::nullopt;
    }
    if (!IsLowercaseSha256(expectedSourceSha256)) {
        ReportInvalid(sink, "font source SHA-256 is not lowercase hexadecimal",
                      "recompute the source digest before publishing");
        return std::nullopt;
    }

    std::vector<std::uint8_t> bytes;
    if (!ReadArtifactBytes(sourcePath, bytes)) {
        ReportInvalid(sink,
                      "could not read the font source: " + sourcePath.string(),
                      "check that the authored font file is readable");
        return std::nullopt;
    }
    const std::string sourceSha = Sha256Bytes(bytes.data(), bytes.size());
    if (sourceSha != expectedSourceSha256) {
        ReportInvalid(sink,
                      "the font source changed between import and publication",
                      "reimport the font asset");
        return std::nullopt;
    }
    // 산출물은 원본의 파생물이 아니라 같은 바이트의 불변 사본이므로 두 SHA는
    // 파생 관계가 아니라 필수 등식이다.
    const std::string artifactSha = Sha256Bytes(bytes.data(), bytes.size());
    if (artifactSha != sourceSha) {
        ReportInvalid(sink,
                      "the published font artifact is not byte-identical to "
                      "its source",
                      "reimport the font asset");
        return std::nullopt;
    }

    const std::string relative = FontArtifactRelativePath(artifactSha);
    std::filesystem::path destination;
    if (!ResolveUnderRoot(storageRoot_, relative, destination)) {
        ReportInvalid(sink,
                      "the project font artifact path does not resolve below "
                      "the project root",
                      "reopen the project from a real directory");
        return std::nullopt;
    }
    std::string publishError;
    if (!PersistentStorage::AtomicPublishImmutableBytes(
            destination, bytes, artifactSha, &publishError)) {
        ReportInvalid(sink,
                      "could not publish the immutable font artifact: " +
                      publishError,
                      "free Library/Imported/Fonts and reimport the font");
        return std::nullopt;
    }

    VerifiedFontArtifact artifact;
    artifact.locator.storage = FontArtifactStorage::ProjectLibrary;
    artifact.locator.relativePath = std::filesystem::path(relative);
    artifact.sourceSha256 = sourceSha;
    artifact.artifactSha256 = artifactSha;
    artifact.byteSize = static_cast<std::uint64_t>(bytes.size());
    return artifact;
}

std::optional<std::shared_ptr<const std::vector<std::uint8_t>>>
FontArtifactStore::ReadVerified(const VerifiedFontArtifact& artifact,
                                molga::text::TextDiagnosticSink& sink) const {
    if (artifact.locator.storage != storage_) {
        // 어느 모드도 상대 모드의 locator를 대신 열어 주지 않는다. 그렇게
        // 하면 봉인된 패키지가 프로젝트 라이브러리를, 혹은 그 반대를 권한으로
        // 삼게 된다.
        ReportInvalid(sink,
                      "the font artifact locator does not belong to this store",
                      "reload the catalog with the matching storage authority");
        return std::nullopt;
    }
    if (!IsLowercaseSha256(artifact.artifactSha256) ||
        !IsLowercaseSha256(artifact.sourceSha256) ||
        artifact.artifactSha256 != artifact.sourceSha256 ||
        artifact.byteSize == 0U) {
        ReportInvalid(sink, "the recorded font artifact identity is invalid",
                      "reimport the font asset");
        return std::nullopt;
    }

    std::string normalized;
    if (!NormalizeFontArtifactRelativePath(artifact.locator.relativePath, normalized)) {
        ReportInvalid(sink, "the font artifact path is not a safe relative path",
                      "reimport the font asset");
        return std::nullopt;
    }
    if (storage_ == FontArtifactStorage::ProjectLibrary) {
        if (normalized != FontArtifactRelativePath(artifact.artifactSha256)) {
            ReportInvalid(sink,
                          "the project font artifact path is not its own "
                          "content address",
                          "reimport the font asset");
            return std::nullopt;
        }
    } else {
        const PackagedAuthority wanted{std::filesystem::path(normalized),
                                       artifact.artifactSha256};
        const auto occurrences = std::count(packagedAuthorities_.begin(),
                                            packagedAuthorities_.end(), wanted);
        if (occurrences != 1) {
            ReportInvalid(sink,
                          "the packaged font is not authorized by the verified "
                          "runtime manifest: " + normalized,
                          "rebuild the package so its manifest lists this font");
            return std::nullopt;
        }
    }

    std::filesystem::path resolved;
    if (!ResolveUnderRoot(storageRoot_, normalized, resolved)) {
        ReportInvalid(sink,
                      "the font artifact does not resolve below its storage "
                      "root: " + normalized,
                      "reinstall the project or package resources");
        return std::nullopt;
    }
    auto bytes = std::make_shared<std::vector<std::uint8_t>>();
    if (!ReadArtifactBytes(resolved, *bytes)) {
        ReportInvalid(sink,
                      "could not read the font artifact: " + normalized,
                      "reinstall the project or package resources");
        return std::nullopt;
    }
    if (static_cast<std::uint64_t>(bytes->size()) != artifact.byteSize ||
        Sha256Bytes(bytes->data(), bytes->size()) != artifact.artifactSha256) {
        ReportInvalid(sink,
                      "the font artifact bytes do not match their recorded "
                      "identity: " + normalized,
                      "reinstall the project or package resources");
        return std::nullopt;
    }
    return std::shared_ptr<const std::vector<std::uint8_t>>(std::move(bytes));
}

bool FontArtifactStore::IsProjectAuthorityFor(
    const std::filesystem::path& projectRoot) const noexcept {
    if (storage_ != FontArtifactStorage::ProjectLibrary) return false;
    if (projectRoot.empty() || storageRoot_.empty()) return false;
    // 문자열 비교는 "/a/b"와 "/a/b/"를, 그리고 symlink된 두 경로를 다르게
    // 본다. 정규화 후 component 단위로 완전히 같아야만 같은 권한이다.
    return CanonicalComponentsUnder(storageRoot_, projectRoot, false);
}

} // namespace molga
