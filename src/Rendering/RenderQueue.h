#pragma once

#include <vector>
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include "../Common/linmath.h"
#include "Common/Types.h"
#include "Rendering/BlendMode.h"
#include "Rendering/GraphicsDevice.h"
#include "UI/UILayoutTypes.h"

class Renderer;
namespace molga {

struct SortKey {
    int cameraPass = 0;
    int sortingLayer = 0;
    int sortingOrder = 0;
    float depthOrYSort = 0.0f;
    uint64_t submissionIndex = 0;

    static float NormalizedDepth(float value) noexcept {
        return std::isfinite(value) ? value : 0.0f;
    }

    bool operator<(const SortKey& other) const {
        if (cameraPass != other.cameraPass) return cameraPass < other.cameraPass;
        if (sortingLayer != other.sortingLayer) return sortingLayer < other.sortingLayer;
        if (sortingOrder != other.sortingOrder) return sortingOrder < other.sortingOrder;
        const float depth = NormalizedDepth(depthOrYSort);
        const float otherDepth = NormalizedDepth(other.depthOrYSort);
        if (depth != otherDepth) return depth < otherDepth;
        return submissionIndex < other.submissionIndex;
    }

    bool operator==(const SortKey& other) const {
        return cameraPass == other.cameraPass &&
               sortingLayer == other.sortingLayer &&
               sortingOrder == other.sortingOrder &&
               NormalizedDepth(depthOrYSort) ==
                   NormalizedDepth(other.depthOrYSort) &&
               submissionIndex == other.submissionIndex;
    }
};

struct BatchKey {
    struct ExtraTexture {
        bool vertexStage = false;
        std::uint32_t slot = 0;
        TextureHandle texture;
        SamplerHandle sampler;
        std::uint64_t stableId = 0;
    };

    std::string shaderName;
    std::uint64_t shaderRevision = 0;
    TextureHandle texture;
    SamplerHandle textureSampler;
    std::uint64_t textureStableId = 0;
    TextureHandle normalTexture;
    SamplerHandle normalSampler;
    std::uint64_t normalTextureStableId = 0;
    BlendMode blendMode = BlendMode::Alpha;
    bool isBatchable = true;
    bool lit = false;
    std::uint32_t receiverLayer = 0;
    float normalStrength = 1.0f;
    std::uint64_t materialId = 0;
    std::shared_ptr<const std::vector<std::uint8_t>> materialParameters;
    std::shared_ptr<const std::vector<ExtraTexture>> materialTextures;

    bool operator<(const BatchKey& other) const {
        if (isBatchable != other.isBatchable) return isBatchable < other.isBatchable;
        if (lit != other.lit) return lit < other.lit;
        if (shaderName != other.shaderName) return shaderName < other.shaderName;
        if (shaderRevision != other.shaderRevision) return shaderRevision < other.shaderRevision;
        if (textureStableId != other.textureStableId) return textureStableId < other.textureStableId;
        if (normalTextureStableId != other.normalTextureStableId) {
            return normalTextureStableId < other.normalTextureStableId;
        }
        if (receiverLayer != other.receiverLayer) return receiverLayer < other.receiverLayer;
        if (normalStrength != other.normalStrength) return normalStrength < other.normalStrength;
        if (materialId != other.materialId) return materialId < other.materialId;
        return blendMode < other.blendMode;
    }

    bool operator==(const BatchKey& other) const {
        return isBatchable == other.isBatchable &&
               lit == other.lit &&
               shaderName == other.shaderName &&
               shaderRevision == other.shaderRevision &&
               textureStableId == other.textureStableId &&
               normalTextureStableId == other.normalTextureStableId &&
               receiverLayer == other.receiverLayer &&
               normalStrength == other.normalStrength &&
               materialId == other.materialId &&
               blendMode == other.blendMode;
    }

    bool operator!=(const BatchKey& other) const {
        return !(*this == other);
    }
};

struct Vertex2D {
    float x, y;
    float u, v;
    float r, g, b, a;
};

// ── Task 11.2 Step 7e: 수명 정체성이 어느 수열에서 왔는지 ────────────────────
// atlas page 정체성(GlyphAtlasCache::nextPageIdentity)과 텍스처 바인딩 수명
// 정체성(Texture.cpp::nextBindingLifetimeIdentity)은 **서로 다른 두 수열**이고
// 둘 다 1에서 시작한다. 그래서 page 1과 바인딩 1은 반드시 같은 프레임에
// 나타날 수 있다.
//
// 두 값이 한 지도의 같은 키가 되면 Renderer::RetainUntilFrameComplete가
// "하나의 정체성이 서로 다른 두 소유자와 함께 왔다"로 판단해 던지고, 그
// 예외는 SpriteBatcher를 열어 둔 채 제출 루프 밖으로 나간다. Task 11.2가
// UIRenderCollector로 스프라이트 바인딩 지분을 실어 보내기 시작하는 순간
// 그 충돌이 실제로 닿을 수 있게 되므로, 정체성 옆에 출처를 함께 싣는다.
enum class ResourceLifetimeDomain : std::uint8_t {
    GlyphPage,
    TextureBinding,
};

struct RenderCommand {
    SortKey sortKey;
    BatchKey batchKey;
    
    bool isBatchableSprite = false;
    std::array<Vertex2D, 4> vertices;
    // Chunk/particle producers publish immutable shared geometry. Rebuilding a
    // dirty cache can replace its shared_ptr without invalidating commands that
    // were already submitted for the current frame.
    std::shared_ptr<const std::vector<Vertex2D>> geometry;
    std::shared_ptr<const std::vector<std::uint32_t>> geometryIndices;
    std::optional<AABB> worldBounds;

    // ── Task 6.3: 이 명령이 읽는 GPU page의 이름과 지분 ─────────────────────
    // 텍스트 명령은 GlyphHandle::pageIdentity와 pageLifetime을 짝으로 복사해
    // 오도록 되어 있다. 이름과 지분이 함께 실리는 이유는 반납이 page 단위이기
    // 때문이다: 한 프레임 안에서 같은 page를 가리키는 명령은 수백 개가 될 수
    // 있지만 되돌려줄 것은 page 하나이고, 그 중복 제거의 키가 정체성이다.
    //
    // Task 8.2가 소비자를 옮긴 뒤로 이 두 필드를 채우는 프로덕션 생산자가
    // 있다: TextRenderer::CollectLayout이 그릴 수 있는 glyph마다 GlyphHandle의
    // 정체성과 토큰을 짝으로 복사한다. 아래 RetainCommandResourceLifetime의
    // 두 방어가 살아 있는 것은 그래서다.
    //
    // 두 필드 모두 BatchKey/SortKey 밖에 있다. 안에 넣으면 같은 atlas
    // 텍스처를 쓰는 glyph들이 page마다 다른 batch로 쪼개져, 수명 회계가
    // draw call 수를 바꾸게 된다.
    std::uint64_t resourceLifetimeIdentity = 0;
    std::shared_ptr<const void> resourceLifetime;
    // 정체성이 어느 수열에서 왔는지. 두 수열이 같은 값을 낼 수 있으므로
    // 이것이 없으면 프레임의 반납 회계가 둘을 한 자원으로 본다.
    ResourceLifetimeDomain resourceLifetimeDomain =
        ResourceLifetimeDomain::GlyphPage;

    // ── Task 11.2 Step 6a: UI draw order와 최종 물리 scissor ────────────────
    // 둘 다 BatchKey 밖에 있다. 특히 scissor는 **상태이지 배치 정체성이
    // 아니다**: 안에 넣으면 같은 텍스처를 쓰는 이웃 명령이 클립이 같아도
    // 서로 다른 batch로 쪼개진다. 클립 전이가 활성 batch를 flush하고,
    // 같은 클립이 이어질 때는 flush하지 않는다 — 그 차이는 픽셀이 아니라
    // batch 수에서만 보인다.
    //
    // uiDrawOrder는 UI 명령의 완전한 정렬 키다. sortKey의 넓은 필드로는
    // siblingPath를 표현할 수 없어(정수 하나로 접으면 형제 경로가 무너진다)
    // 키를 통째로 싣는다. 월드 명령에서는 비어 있다.
    std::optional<ui::UIDrawOrderKey> uiDrawOrder;
    std::optional<PixelRectU32> scissor;
};

// ── Task 6.3: 수집한 명령을 실제로 제출하는 유일한 루프 ─────────────────────
// 명령이 가리키는 page의 지분은 "그 명령이 실제로 그려질 때"만 프레임으로
// 넘어간다. 컬링되어 그려지지 않는 명령까지 붙들면, fence가 신호할 때까지
// 살아 있어야 할 이유가 없는 page가 예산을 먹는다.
//
// 템플릿인 이유는 하나다. "실제 제출 직전"은 값이 아니라 순서라서, 붙듦과
// 그리기가 한 로그에 순서대로 남는 것 말고는 관찰할 방법이 없는데,
// SpriteBatcher는 GPU 장치 없이 서지 못한다. RenderSystem2D는 진짜
// Renderer/SpriteBatcher로, test_render_queue는 호출을 받아 적는 대역으로
// 같은 코드를 인스턴스화한다 — 루프가 두 벌이면 테스트가 재는 것은 프로덕션이
// 아니라 그 사본이 된다.
template <typename RendererLike>
void RetainCommandResourceLifetime(RendererLike& renderer,
                                   const RenderCommand& command) {
    // 널 토큰은 지분이 없다는 뜻이다(공백 glyph와 포화 tofu가 그 모양이다).
    if (!command.resourceLifetime) return;
    // ── Task 8.2: 던지는 두 조건을 여기서 닫는다 ───────────────────────────
    // Renderer::RetainUntilFrameComplete는 정체성이 0이거나 활성 프레임이
    // 없으면 std::logic_error를 던진다. 이 호출은 SpriteBatcher::Begin/End
    // 사이에 있으므로 그 예외는 batcher를 열어 둔 채 RenderSystem2D::Render
    // 밖으로 나간다 — 프레임 하나가 깨지는 것이 아니라 batcher 상태가 깨진다.
    //
    // Task 8.2 전에는 resourceLifetime을 채우는 프로덕션 생산자가 없어 닿을
    // 수 없었다. 지금은 텍스트 명령이 그리는 glyph마다 이 두 필드를 채우므로
    // 두 조건 모두 살아 있다:
    //
    //  - 정체성 0 + 토큰 있음은 애초에 만들어지지 않는 모양이다(atlas는 셋을
    //    짝으로만 낸다). 그래도 받아 주면 이름 없는 여러 자원이 가짜 page 0
    //    하나로 뭉쳐 서로의 반납을 막으므로, 붙들지 않는 쪽이 맞다.
    //  - 프레임 밖 Render는 실제로 있다(ParticleSystem/SpriteRenderer/
    //    MarrowRenderer의 RenderSprite 경로, 그리고 프레임을 얻지 못한
    //    프레임). 붙들 fence가 없으므로 붙들 것도 없다: 지분을 넘길 곳이
    //    없다는 뜻이지, 지분이 필요한데 버린다는 뜻이 아니다.
    if (command.resourceLifetimeIdentity == 0U) return;
    if (!renderer.HasFrame()) return;
    renderer.RetainUntilFrameComplete(command.resourceLifetimeDomain,
                                      command.resourceLifetimeIdentity,
                                      command.resourceLifetime);
}

// ── Task 11.2 Steps 7a/7b/7c: 클립 전이는 이 루프 하나에만 있다 ─────────────
// 계약은 세 줄이다.
//
//  1. 같은 scissor가 이어지면 아무 일도 일어나지 않는다. flush도, 상태 호출도
//     없다. 클립이 batch 정체성이 아니라는 말의 뜻이 정확히 이것이고, 이
//     차이는 화면이 아니라 draw call 수에서만 보인다.
//  2. 달라지면 **먼저 flush하고 그다음에 상태를 바꾼다**. 순서가 뒤집히면
//     이미 모인 정점이 새 클립 아래에서 나간다 — 잘려야 할 것이 남고 남아야
//     할 것이 잘린다.
//  3. 상태 호출이 실패하면 그 자리에서 멈춘다. 이전 클립 아래에서 계속 그리는
//     것이 이 함수가 막으려는 바로 그 상태다.
//
// 시작 상태는 "클립 없음"이다(패스가 막 열렸으므로 scissor는 뷰포트 전체다).
// 그래서 첫 명령이 클립을 가지면 flush 없이 set 하나만 나가고, 첫 명령이
// 클립을 갖지 않으면 아무 호출도 나가지 않는다.
//
// 돌아오는 값은 "모든 보이는 명령이 제출되었다"이다. 거짓이면 호출자는
// batcher를 닫되 이 프레임을 신뢰하지 않는다.
template <typename RendererLike, typename BatcherLike>
bool SubmitVisibleCommands(const std::vector<RenderCommand>& commands,
                           const std::optional<AABB>& cameraBounds,
                           RendererLike& renderer, BatcherLike& batcher,
                           std::string* errorOut = nullptr) {
    std::optional<PixelRectU32> currentScissor;
    bool batchedSinceFlush = false;
    const auto flush = [&batcher, &batchedSinceFlush]() {
        batcher.Flush();
        batchedSinceFlush = false;
    };
    for (const RenderCommand& cmd : commands) {
        if (cameraBounds && cmd.worldBounds &&
            !cameraBounds->Intersects(*cmd.worldBounds)) {
            continue;
        }
        const bool draws =
            cmd.geometry != nullptr || cmd.isBatchableSprite;
        if (draws && cmd.scissor != currentScissor) {
            // 모인 정점이 있을 때만 flush한다. 없는데도 부르면 "같은 클립은
            // flush하지 않는다"와 구별되지 않는 잡음이 로그에 남는다.
            if (batchedSinceFlush) flush();
            std::string stateError;
            const bool applied =
                cmd.scissor ? renderer.SetPassScissor(*cmd.scissor, &stateError)
                            : renderer.ResetPassScissor(&stateError);
            if (!applied) {
                if (errorOut) {
                    *errorOut = stateError.empty()
                                    ? std::string("render clip state change "
                                                  "failed")
                                    : stateError;
                }
                // Step 7c의 나머지 절반. 실패해도 패스는 되돌린다. 그냥
                // 돌아가면 방금 세운 클립이 패스에 남고, 같은 패스를 이어
                // 쓰는 다음 소비자(ImGui 오버레이가 그렇다)가 UI 클립 아래에서
                // 그린다 — 큐 끝의 복원이 존재하는 이유가 정확히 그 상태를
                // 막는 것이다. 실패 경로만 그 보호를 빠져나가면 보호가 아니라
                // 우연이 된다.
                //
                // 최선 노력이다: 되돌리기 자체가 실패할 수도 있고(그 경우
                // 이 함수가 할 수 있는 일은 없다), 보고되는 사유는 언제나
                // 첫 실패의 것이다. 성공/실패 어느 쪽이든 돌아가는 값은
                // 거짓이다 — 남은 명령은 그리지 않는다.
                if (currentScissor) {
                    std::string restoreError;
                    if (renderer.ResetPassScissor(&restoreError)) {
                        currentScissor.reset();
                    }
                }
                return false;
            }
            currentScissor = cmd.scissor;
        }
        renderer.Stats().submittedCommands++;

        if (cmd.geometry) {
            if (cmd.geometryIndices) {
                RetainCommandResourceLifetime(renderer, cmd);
                batcher.DrawIndexedGeometry(*cmd.geometry,
                                            *cmd.geometryIndices,
                                            cmd.batchKey);
                batchedSinceFlush = true;
                continue;
            }
            if (!cmd.batchKey.isBatchable) flush();
            RetainCommandResourceLifetime(renderer, cmd);
            batcher.DrawGeometry(*cmd.geometry, cmd.batchKey);
            batchedSinceFlush = true;
            if (!cmd.batchKey.isBatchable) flush();
        } else if (cmd.isBatchableSprite) {
            if (!cmd.batchKey.isBatchable) flush();
            RetainCommandResourceLifetime(renderer, cmd);
            batcher.DrawSprite(cmd.vertices, cmd.batchKey);
            batchedSinceFlush = true;
            if (!cmd.batchKey.isBatchable) flush();
        }
    }
    // Step 7c: 마지막 명령 뒤에 클립이 남아 있으면 flush하고 패스 전체를
    // 되돌린 다음에야 batcher를 닫는다. 되돌리지 않으면 같은 패스를 이어
    // 쓰는 다음 소비자(ImGui 오버레이가 그렇다)가 UI 클립 아래에서 그린다.
    if (currentScissor) {
        if (batchedSinceFlush) flush();
        std::string stateError;
        if (!renderer.ResetPassScissor(&stateError)) {
            if (errorOut) {
                *errorOut = stateError.empty()
                                ? std::string("render clip reset failed")
                                : stateError;
            }
            return false;
        }
        currentScissor.reset();
    }
    if (errorOut) errorOut->clear();
    return true;
}

class RenderQueue {
public:
    RenderQueue() = default;
    
    void Submit(const RenderCommand& cmd) {
        if (viewBounds_ && cmd.worldBounds && !viewBounds_->Intersects(*cmd.worldBounds)) {
            ++culledCommands_;
            return;
        }
        auto mutableCmd = cmd;
        mutableCmd.sortKey.submissionIndex = nextSubmissionIndex_++;
        commands_.push_back(std::move(mutableCmd));
    }
    
    // ── Task 11.2 Step 6b: UI 명령은 완전한 draw key로 정렬한다 ─────────────
    // sortKey의 넓은 세 정수로는 siblingPath를 표현할 수 없다. 형제 경로를
    // sortingOrder 하나로 접으면 서로 다른 깊이의 두 형제가 같은 값을 갖고,
    // 그때 자손이 조상보다 앞서 그려진다. 그래서 UI 명령은 키를 통째로 싣고
    // 여기서 그 키로 비교한다.
    //
    // 두 완전한 키가 같으면 이미 배정된 submissionIndex가 마지막 결정자다 —
    // std::sort는 안정 정렬이 아니므로, 동률을 그냥 두면 같은 입력이 실행마다
    // 다른 순서를 낼 수 있다.
    //
    // ── 하나의 전순서, 규약이 아니라 코드로 ─────────────────────────────────
    // 예전 비교자는 "둘 다 uiDrawOrder를 가질 때"만 완전한 키로 비교하고
    // 나머지는 sortKey로 떨어졌다. 한 범위 위에 두 개의 서로 다른 순서가
    // 있으면 관계는 추이적이지 않다 — 그리고 비추이적 비교자를 받은
    // std::sort는 **미정의 동작**이다(잘못된 순서가 아니라 범위 밖 읽기다).
    //
    // 구체적 순환: 셋 다 cameraPass 1, 나머지 sortKey 필드는 기본값.
    //   U1{key=K_hi, submissionIndex 0}, L{키 없음, 1}, U2{key=K_lo, 2}
    //   comp(U2,U1)=참(완전한 키), comp(U1,L)=참(sortKey), comp(L,U2)=참.
    // 그리고 그 L은 가상의 것이 아니다: 레거시 UISystem::CollectRender가
    // cameraPass 1을 uiDrawOrder 없이 낸다. Task 12.3이 한 표면만 옮기는
    // 그 첫 프레임이 정확히 이 큐다.
    //
    // 그래서 여기서는 (cameraPass, 키를 가졌는가, 그 무리의 순서) 사전식
    // 하나로 비교한다. "UI 명령은 다른 cameraPass를 쓴다"는 강제되지 않는
    // 규약이었고, 규약은 정렬을 안전하게 만들지 못한다.
    void Sort() {
        std::sort(commands_.begin(), commands_.end(),
                  [](const RenderCommand& a, const RenderCommand& b) {
            if (a.sortKey.cameraPass != b.sortKey.cameraPass) {
                return a.sortKey.cameraPass < b.sortKey.cameraPass;
            }
            // 같은 패스 안에서 두 생산자가 섞이면 무리 자체가 먼저 정해진다.
            // 키 없는 쪽이 앞이다 — 어느 쪽이든 되지만 정해져 있어야 한다.
            if (a.uiDrawOrder.has_value() != b.uiDrawOrder.has_value()) {
                return !a.uiDrawOrder.has_value();
            }
            if (a.uiDrawOrder && b.uiDrawOrder) {
                if (*a.uiDrawOrder != *b.uiDrawOrder) {
                    return *a.uiDrawOrder < *b.uiDrawOrder;
                }
                return a.sortKey.submissionIndex < b.sortKey.submissionIndex;
            }
            return a.sortKey < b.sortKey;
        });
    }
    
    void Clear() {
        commands_.clear();
        nextSubmissionIndex_ = 0;
        culledCommands_ = 0;
    }
    
    const std::vector<RenderCommand>& GetCommands() const { return commands_; }
    bool HasLitReceivers() const {
        return std::any_of(commands_.begin(), commands_.end(),
            [](const RenderCommand& command) { return command.batchKey.lit; });
    }
    void ForceUnlit() {
        for (auto& command : commands_) {
            command.batchKey.lit = false;
            command.batchKey.normalTexture = {};
            command.batchKey.normalSampler = {};
            command.batchKey.normalTextureStableId = 0;
            command.batchKey.receiverLayer = 0;
            command.batchKey.normalStrength = 1.0f;
        }
    }
    void SetViewBounds(const AABB& bounds) { viewBounds_ = bounds; }
    void ClearViewBounds() { viewBounds_.reset(); }
    const std::optional<AABB>& GetViewBounds() const { return viewBounds_; }
    std::size_t CulledCommandCount() const { return culledCommands_; }
    
private:
    std::vector<RenderCommand> commands_;
    uint64_t nextSubmissionIndex_ = 0;
    std::optional<AABB> viewBounds_;
    std::size_t culledCommands_ = 0;
};

} // namespace molga
