#pragma once

#include "Common/Fixed26_6.h"
#include "Rendering/RenderTarget.h"
#include "Rendering/CameraOutputLayout.h"
#include "Rendering/OutputPresentationLayout.h"
#include "Rendering/PixelSize.h"
#include "Rendering/PostProcessPipeline.h"
#include "Rendering/LightingPipeline2D.h"

#include <memory>
#include <optional>
#include <cstdint>
#include <unordered_map>
#include <vector>
#include <unordered_set>

class Camera;
class GameObject;
class World;
class Renderer;
class Shader;
// Task 8.2 Step 7d: 게임 출력은 텍스트 권한을 스스로 찾지 않는다. 값으로 담지
// 않으므로 선언만 있으면 되고, 그래서 이 헤더는 텍스트 헤더를 끌어오지 않는다.
class TextRenderer;
namespace molga::text { class TextDiagnosticSink; }
namespace molga::ui { struct UISnapshot; }

namespace molga {

struct CameraOutputResult {
    unsigned int cameraObjectId = 0;
    std::uint64_t cameraInstanceId = 0;
    CameraOutputRole outputRole = CameraOutputRole::Disabled;
    int depth = 0;
    PixelRect viewport{};
    bool rendered = false;
    bool postProcessed = false;
    bool postProcessFallback = false;
    int postProcessPasses = 0;
    bool lightingApplied = false;
    bool lightingFallback = false;
    bool shadowFallback = false;
    int selectedLightCount = 0;
    int shadowedLightCount = 0;
    int shadowCasterDrawCount = 0;
    int lightingPasses = 0;
    int shadowPasses = 0;
};

using CameraRenderResult = CameraOutputResult;

struct GameOutputResult {
    Camera* mainCamera = nullptr;
    bool rendered = false;
    bool presented = false;
    bool allocationFailed = false;
    bool postProcessed = false;
    bool postProcessFallback = false;
    int postProcessPasses = 0;
    bool lightingApplied = false;
    bool lightingFallback = false;
    bool shadowFallback = false;
    int selectedLightCount = 0;
    int shadowedLightCount = 0;
    int shadowCasterDrawCount = 0;
    int lightingPasses = 0;
    int shadowPasses = 0;
    std::vector<CameraOutputResult> cameraResults;
    CameraOutputLayout cameraLayout{};
    OutputPresentationLayout presentation{};
};

struct GameOutputRequest {
    PixelSize targetSize{};
    PixelSize logicalSize{};
    GameOutputScaleMode scaleMode = GameOutputScaleMode::Native;
    // Null presents to the acquired main-window swapchain. Editor Game View
    // supplies its private render target explicitly.
    RenderTarget* destination = nullptr;
    // Task 12.3 Step 5: UI 층은 프레임 소유자가 게시한 스냅샷 하나만 그린다. 소유하지
    // 않는다 — 소유자가 그 프레임이 끝날 때까지 UISnapshotPtr를 쥐고 있다. null이면
    // UI 층이 없다. 스냅샷의 논리 뷰포트는 UiLogicalViewport()와 같아야 한다.
    const molga::ui::UISnapshot* uiSnapshot = nullptr;
};

// The sole game-output path used by both the standalone player and Game View.
// The caller owns the current render target (swapchain or offscreen texture).
class GameOutputRenderer {
public:
    // 이 요청의 UI 스냅샷이 가져야 하는 논리 뷰포트(26.6). 출력 배치가 정한 논리 크기를
    // 쓴다: Native 모드에서는 요청한 논리 크기가 아니라 대상 크기가 논리 크기다. 두
    // 프레임 소유자(Game View, 런타임)가 같은 계산을 쓰도록 한 곳에 둔다. 유효하지
    // 않거나 26.6로 담기지 않으면 nullopt.
    static std::optional<FixedSize> UiLogicalViewport(
        GameOutputScaleMode scaleMode, PixelSize logicalSize,
        PixelSize targetSize);

    static Camera* FindMainCamera(
        const std::vector<std::shared_ptr<GameObject>>& objects);

    // textRenderer/textDiagnostics는 이 프레임의 소유자가 해석한 그 하나다.
    // 기본값이 없는 이유는 Step 1i와 같다: 기본값이 있으면 텍스트를 담은
    // world가 권한 없이 그려질 수 있고, 그때 텍스트는 진단 하나 없이 화면에서만
    // 사라진다.
    // Task 10.2 Step 9d: 진짜 World를 받는다. UI 배치와 hit-test는 월드
    // 세대로 런타임 식별자를 만들고 캐시 키를 잡으므로, 벡터만 받으면 그 값을
    // 알 방법이 없어 세대 0을 지어내게 된다. 카메라/세계 순회만 world.Objects()를
    // 유도한다.
    GameOutputResult Render(
        World& world,
        const GameOutputRequest& request,
        Renderer& renderer,
        Shader* spriteShader,
        TextRenderer& textRenderer,
        molga::text::TextDiagnosticSink& textDiagnostics);

    // Compatibility entry point for the original direct Native path. It carries no
    // UI snapshot, so it draws no UI layer.
    static GameOutputResult Render(
        World& world,
        PixelSize outputSize,
        Renderer& renderer,
        Shader* spriteShader,
        TextRenderer& textRenderer,
        molga::text::TextDiagnosticSink& textDiagnostics);

    PixelSize LogicalFramebufferSize() const {
        return {logicalFramebuffer_.Width(), logicalFramebuffer_.Height()};
    }
    TextureView LogicalColorView() const {
        return logicalFramebuffer_.ColorView();
    }

    std::size_t CachedPostProcessPipelineCount() const {
        return postProcessPipelines_.size();
    }
    std::size_t CachedLightingPipelineCount() const {
        return lightingPipelines_.size();
    }

private:
    GameOutputResult RenderLogical(
        World& world,
        PixelSize logicalSize,
        const molga::ui::UISnapshot* uiSnapshot,
        Renderer& renderer,
        Shader* spriteShader,
        TextRenderer& textRenderer,
        molga::text::TextDiagnosticSink& textDiagnostics);

    RenderTarget logicalFramebuffer_;
    std::unordered_map<std::uint64_t, std::unique_ptr<PostProcessPipeline>>
        postProcessPipelines_;
    std::unordered_map<std::uint64_t, std::unique_ptr<LightingPipeline2D>>
        lightingPipelines_;
    std::unordered_map<std::uint64_t, std::unique_ptr<RenderTarget>>
        cameraTargets_;
    PixelSize lastObservedTarget_{};
    bool observedTarget_ = false;
    std::unordered_set<std::string> postProcessWarnings_;
    std::unordered_set<std::string> lightingWarnings_;
};

} // namespace molga
