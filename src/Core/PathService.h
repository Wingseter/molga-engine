#pragma once

#include <filesystem>
#include <string>

// 실행 파일 위치를 기준으로 모든 경로를 절대화하는 서비스(싱글톤).
class PathService {
public:
    static PathService& Get();

    // argv[0]로 실행 파일 디렉터리를 확정한다(플랫폼별).
    void InitFromExecutable(const char* argv0);

    const std::filesystem::path& ExecutableDir() const { return executableDir_; }

    // 실행 파일 옆에 배포되는 엔진 리소스(Shaders/ 등).
    std::filesystem::path EngineResource(const std::string& rel) const {
        return executableDir_ / rel;
    }

    // 에셋 경로 해석의 기준 루트(에디터=프로젝트 루트, 런타임=실행 파일 디렉터리).
    void SetAssetRoot(const std::filesystem::path& root) { assetRoot_ = root; }
    const std::filesystem::path& AssetRoot() const { return assetRoot_; }

    // 저장된 에셋 경로(상대/절대)를 절대 경로 문자열로 해석.
    std::string ResolveAsset(const std::string& stored) const {
        const std::filesystem::path& root = assetRoot_.empty() ? executableDir_ : assetRoot_;
        return Resolve(root, stored);
    }

    // ── 순수 헬퍼(테스트 대상) ──────────────────────────────────────────────
    static std::string Resolve(const std::filesystem::path& root, const std::string& stored);
    static bool IsSafeOutputPath(const std::filesystem::path& path, std::string& reason);
    static bool IsSafeOutputPath(
        const std::filesystem::path& path,
        std::string& reason,
        const std::filesystem::path& projectRoot,
        const std::filesystem::path& engineRoot);

private:
    PathService() = default;
    std::filesystem::path executableDir_;
    std::filesystem::path assetRoot_;
};
