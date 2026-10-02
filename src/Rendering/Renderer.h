#pragma once

#include "Common/linmath.h"
#include "Core/Profiling/FrameProfile.h"
#include "Rendering/GraphicsDevice.h"
#include "Rendering/RenderPassState.h"
#include "Rendering/RenderQueue.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

class Camera2D;
class Shader;
class Sprite;

namespace molga {
class RenderTarget;
struct LightingRenderContext2D;

struct DrawTextureBinding {
    enum class Stage : std::uint8_t { Vertex, Fragment };
    Stage stage = Stage::Fragment;
    std::uint32_t slot = 0;
    TextureView texture;
    SamplerHandle sampler;
};

// Backend-neutral packet consumed by the frame-streaming renderer. Uniform
// blocks follow SDL_GPU's stage-local binding order and are always 16-byte
// sized. Vertices and indices are copied into frame-owned CPU storage so no
// producer pointer can outlive collection.
struct DrawPacket {
    Shader* shader = nullptr;
    BlendState blend = BlendState::Alpha;
    bool depthTest = false;
    bool depthWrite = false;
    std::vector<std::uint8_t> vertices;
    std::uint32_t vertexStride = 0;
    std::vector<std::uint32_t> indices;
    std::vector<DrawTextureBinding> textures;
    std::vector<std::uint8_t> vertexUniforms;
    std::vector<std::uint8_t> fragmentUniforms;
};

} // namespace molga

// Renderer records one complete frame before issuing GPU work. All dynamic
// vertex/index uploads are encoded first; render passes and presentation then
// execute in their original order on the acquired SDL_GPU command buffer.
class Renderer {
public:
    Renderer();
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    bool Init(std::string* errorOut = nullptr);
    void Shutdown();

    // ── Task 11.2 Step 7f/7g: 종료의 세 단계를 따로 부를 수 있다 ────────────
    // Shutdown은 이 셋을 순서대로 부르는 편의 함수일 뿐이다. host가 소유하는
    // 재시도 가능한 종료(EngineShutdown)는 이 셋 사이에 캐시/소유자 해제
    // 단계를 끼워 넣어야 하므로 세 조각을 따로 필요로 한다.
    //
    // 1) 열려 있던 프레임을 닫고 제출한 뒤 GPU idle을 증명한다. 실패하면
    //    **아무것도 부수지 않고** 거짓이다. Task 6.2의 std::abort()는 여기서
    //    사라졌다: 재시도할 수 있는 종료가 생겼으므로 프로세스를 죽일 이유가
    //    없고, 죽이면 host가 붙들고 있는 외부 소유자도 함께 사라진다.
    bool DrainSubmittedFrames(std::string* errorOut = nullptr);
    // 2) 증명된 idle 위에서 반납 큐를 비운다. 여기 남은 fence 객체는 장치를
    //    가리키므로 장치보다 먼저 사라져야 한다.
    void ReleaseCompletedGpuLifetimes();
    // 3) 이 renderer가 만든 GPU 자원을 부순다. 1이 성공한 뒤에만 부른다.
    void DestroyDeviceResources();
    // 1이 성공한 적이 있는가. 재시도가 성공한 drain을 반복하지 않게 하는
    // 값이며, host의 단계 기계가 이것을 읽는다.
    bool HasProvenGpuIdle() const noexcept;

    bool BeginFrame(molga::FrameContext&& frame,
                    std::string* errorOut = nullptr);
    bool HasFrame() const;
    molga::FrameContext* CurrentFrame();
    const molga::FrameContext* CurrentFrame() const;

    // Main-target clear is deferred until the swapchain pass is actually
    // encoded. Editor frames share that pass with Dear ImGui.
    void Clear(float r, float g, float b, float a = 1.0f);
    void SetViewport(int width, int height);

    bool BeginTarget(molga::RenderTarget& target,
                     molga::Color4f clear = {},
                     molga::LoadAction load = molga::LoadAction::Clear,
                     std::string* errorOut = nullptr);
    bool BeginTextureTarget(molga::TextureView color,
                            molga::PixelRectU32 viewport,
                            molga::TextureFormat format,
                            molga::Color4f clear = {},
                            molga::LoadAction load = molga::LoadAction::Clear,
                            std::string* errorOut = nullptr);
    bool BeginTarget(molga::RenderTarget& target,
                     molga::PixelRectU32 viewport,
                     molga::Color4f clear,
                     molga::LoadAction load = molga::LoadAction::Clear,
                     std::string* errorOut = nullptr);
    bool BeginSwapchainPass(molga::LoadAction load,
                            molga::Color4f clear,
                            std::string* errorOut = nullptr);
    bool BeginSwapchainPass(molga::PixelRectU32 viewport,
                            molga::LoadAction load,
                            molga::Color4f clear,
                            std::string* errorOut = nullptr);
    bool EndTarget(std::string* errorOut = nullptr);
    bool SetPassViewport(molga::PixelRectU32 viewport,
                         std::string* errorOut = nullptr);
    bool SetPassScissor(molga::PixelRectU32 scissor,
                        std::string* errorOut = nullptr);
    // ── Task 11.2 Step 6d: 활성 패스의 뷰포트로 클립을 되돌린다 ─────────────
    // 활성 패스가 없으면 거짓이다. 조용히 참을 돌려주면 클립이 남은 채로
    // 다음 소비자가 그린다.
    bool ResetPassScissor(std::string* errorOut = nullptr);

    void Begin(Shader* shader, Camera2D* camera = nullptr);
    void SetShader(Shader* shader);
    void DrawSprite(Sprite* sprite);
    void End();
    bool IsDrawing() const;
    Shader* GetCurrentShader() const { return currentShader_; }
    void SetProjection(float left, float right, float bottom, float top);

    bool Submit(const molga::DrawPacket& packet,
                std::string* errorOut = nullptr);
    bool SubmitBatch(const std::vector<molga::Vertex2D>& vertices,
                     const molga::BatchKey& key,
                     const molga::LightingRenderContext2D* lighting,
                     std::string* errorOut = nullptr);
    bool SubmitGeometry(const std::vector<molga::Vertex2D>& vertices,
                        const std::vector<std::uint32_t>& indices,
                        const molga::BatchKey& key,
                        const molga::LightingRenderContext2D* lighting,
                        std::string* errorOut = nullptr);
    bool SubmitFullscreen(Shader& shader,
                          const std::vector<molga::DrawTextureBinding>& textures,
                          const void* fragmentUniforms,
                          std::size_t fragmentUniformSize,
                          molga::BlendState blend = molga::BlendState::Opaque,
                          std::string* errorOut = nullptr);
    bool Blit(molga::TextureView source, molga::PixelRectU32 sourceRect,
              const molga::ColorAttachmentDescriptor& destination,
              molga::PixelRectU32 destinationRect,
              molga::TextureFilter filter,
              std::string* errorOut = nullptr);

    // Split encoding lets the ImGui backend prepare its copy data after engine
    // uploads but before any render pass begins.
    bool PrepareUploads(std::string* errorOut = nullptr);
    bool EncodeRenderPasses(std::string* errorOut = nullptr);
    bool BeginMainPassForOverlay(std::string* errorOut = nullptr);
    bool EndMainPassAndSubmit(std::string* errorOut = nullptr);
    bool SubmitFrame(std::string* errorOut = nullptr);
    const molga::FrameTelemetry& LastFrameTelemetry() const;

    // ── Task 6.2: 제출된 프레임이 붙드는 atlas page ─────────────────────────
    // 이 프레임의 명령이 가리키는 page 하나를 붙든다. 토큰은 제출 시점에
    // 그 제출의 fence와 함께 GpuRetirementQueue로 넘어가고, fence가 신호한
    // 다음에만 풀린다.
    //
    // 키는 page 정체성이다(GlyphHandle::pageIdentity). 프레임 안에서 같은
    // page를 가리키는 명령은 수백 개가 될 수 있지만 반납 대상은 page 하나다.
    // 널 토큰은 무시하고 — 포화 tofu handle이 그 모양이다 — 0 정체성과
    // 활성 프레임 밖의 호출은 std::logic_error로 거절한다. 같은 정체성이 서로
    // 다른 소유자와 함께 오는 것도 거절한다: 둘 중 하나는 반납되지 않는다.
    //
    // ── Task 11.2 Step 7e: 정체성 옆의 출처 ─────────────────────────────────
    // atlas page 수열과 텍스처 바인딩 수명 수열은 서로 다른 두 수열이고 둘 다
    // 1에서 시작한다. 출처 없이 값만 키로 쓰면 page 1과 바인딩 1이 같은 자원이
    // 되어, 하나는 반납되지 않거나 아래 소유자 충돌 검사가 프레임 한가운데서
    // 던진다. 옛 두 인자 오버로드는 glyph page를 뜻한다.
    void RetainUntilFrameComplete(molga::ResourceLifetimeDomain domain,
                                  std::uint64_t identity,
                                  std::shared_ptr<const void> lifetime);
    void RetainUntilFrameComplete(std::uint64_t pageIdentity,
                                  std::shared_ptr<const void> pageLifetime);
    std::size_t ActiveFrameRetainedPageCount() const noexcept;

    const molga::RenderStats& Stats() const { return stats_; }
    molga::RenderStats& Stats() { return stats_; }
    void ResetStats() { stats_.Reset(); }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    molga::RenderPassState logicalPass_;
    Shader* currentShader_ = nullptr;
    mat4x4 projection_{};
    mat4x4 view_{};
    molga::RenderStats stats_;
};

namespace molga {
namespace detail {

// ── Task 6.2: Shutdown 안의 순서를 밖에서 관찰한다 ──────────────────────────
// Step 9의 계약은 "idle을 증명하지 못하면 아무것도 부수지 않는다"이고, 그
// 계약은 Shutdown 안에서만 참이거나 거짓이다. Shutdown이 돌아온 뒤에 무엇이
// 남았는지 세는 것으로는 abort를 반납 drain 뒤로 — 또는 GPU 자원 파괴 루프
// 뒤로 — 옮긴 구현과 구별되지 않는다(둘 다 프로세스는 죽는다).
//
// 그래서 Shutdown은 단계를 지날 때마다 이 hook을 부른다. abort는 그 hook들
// 보다 앞서야 하므로, 죽은 child가 남긴 단계 목록이 곧 "abort보다 먼저
// 일어난 일"의 전부다. GraphicsDevice.h의 주입들과 같은 이유로 출하되는
// 빌드에 남는다: 관찰 대상이 프로덕션 종료 경로 그 자체다.
//
// 프로세스 전역이고 되돌릴 수 있다. 기본값은 널이고, 널이면 아무 일도 하지
// 않는다.
using RendererShutdownStageHook = void (*)(const char* stage);
void SetRendererShutdownStageHookForTest(
    RendererShutdownStageHook hook) noexcept;

} // namespace detail
} // namespace molga
