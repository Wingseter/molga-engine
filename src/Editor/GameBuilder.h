#pragma once

#include "../Core/BuildProfile.h"
#include "../Core/BuildPlan.h"
#include <string>
#include <vector>

struct BuildSettings {
    BuildProfile profile;
    std::string projectRoot;
};

class GameBuilder;

namespace molga::detail {

// ── Task 8.2: 폰트 산출물 단계를 밖에서 부르는 창 ────────────────────────────
// EmitAssetCatalog는 Build()의 한가운데에 있으므로 그 전체를 돌리지 않고는
// 밖에서 닿을 수 없었고, 그래서 이 단계의 실패 경로 — 카탈로그가 이름을 댄
// 폰트 바이트가 프로젝트 라이브러리에 없을 때의 fail-fast — 는 패키지된 e2e
// smoke에서만, 그것도 간접적으로 지나갔다. 그 fail-fast가 없으면 빌드가
// 성공한 채로 두부만 그리는 패키지를 내놓는다.
//
// Renderer.h/GraphicsDevice.h의 주입들과 같은 이유로 출하되는 빌드에 남는다:
// 관찰 대상이 프로덕션 빌드 경로 그 자체다. 클래스의 공개 표면이 아니라
// detail에 둔다.
bool EmitAssetCatalogForTest(GameBuilder& builder,
                             const std::string& outputPath);

}  // namespace molga::detail

class GameBuilder {
public:
    static GameBuilder& Get();

    // Build the game
    bool Build(const BuildSettings& settings);

    // Get last error message
    const std::string& GetLastError() const { return lastError; }

    // Build progress (0.0 - 1.0)
    float GetProgress() const { return progress; }
    const std::string& GetCurrentStep() const { return currentStep; }

private:
    friend bool molga::detail::EmitAssetCatalogForTest(GameBuilder&,
                                                       const std::string&);

    GameBuilder() = default;

    bool CreateOutputDirectory(const std::string& path);
    bool CopyAssets(const std::string& outputPath);
    bool CopyShaders(const std::string& outputPath);
    bool GenerateGameConfig(const BuildSettings& settings, const BuildPlan& plan, const std::string& outputPath);
    bool CopyExecutable(const std::string& outputPath, const std::string& gameName);
    bool CopyScenes(const BuildPlan& plan, const std::string& outputPath);
    bool CopyUserScripts(const std::string& outputPath, std::string& outLibraryPath);
    bool EmitAssetCatalog(const std::string& outputPath);
    bool CopyPlaceholderResource(const std::string& outputPath);
    bool CopyTextRuntimeResources(const std::string& outputPath);

    std::string lastError;
    float progress = 0.0f;
    std::string currentStep;
};
