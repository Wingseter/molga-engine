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
