#pragma once

#include "Rendering/RenderTarget.h"
#include "Rendering/OutputPresentationLayout.h"
#include "Rendering/PixelSize.h"
#include "Rendering/PostProcessProfile2D.h"

#include <memory>
#include <string>
#include <vector>

class Renderer;
class Shader;

namespace molga {

struct DrawTextureBinding;

struct PostProcessExecutionResult {
    bool success = false;
    bool postProcessed = false;
    int passes = 0;
    std::string error;
};

// Ordered HDR fullscreen pipeline. Resource allocation is separated from
// encoding so a failed resize leaves the last-good targets intact.
class PostProcessPipeline {
public:
    PostProcessPipeline();
    ~PostProcessPipeline() = default;

    PostProcessPipeline(const PostProcessPipeline&) = delete;
    PostProcessPipeline& operator=(const PostProcessPipeline&) = delete;

    bool Prepare(PixelSize size, const PostProcessProfile2D& profile,
                 std::string* errorOut = nullptr);
    RenderTarget& SceneTarget() { return sceneTarget_; }
    const RenderTarget& SceneTarget() const { return sceneTarget_; }

    PostProcessExecutionResult Execute(
        const PostProcessProfile2D& profile, Renderer& renderer,
        const ColorAttachmentDescriptor& destination,
        TextureFormat destinationFormat, PixelSize destinationSize,
        PixelRect destinationRect);

    PixelSize PreparedSize() const { return preparedSize_; }
    std::size_t BloomMipCount() const { return bloomDown_.size(); }

private:
    struct SampledTexture {
        TextureView view;
        SamplerHandle sampler;
    };

    bool PrepareShaders(const PostProcessProfile2D& profile,
                        std::string* errorOut);
    bool PrepareTargets(PixelSize size, bool needsBloom, std::string* errorOut);
    bool DrawToTarget(Renderer& renderer, Shader& shader,
                      const std::vector<DrawTextureBinding>& textures,
                      const void* constants, std::size_t constantsSize,
                      RenderTarget& destination, std::string& error);
    bool DrawToAttachment(Renderer& renderer, Shader& shader,
                          const std::vector<DrawTextureBinding>& textures,
                          const void* constants, std::size_t constantsSize,
                          const ColorAttachmentDescriptor& destination,
                          TextureFormat destinationFormat,
                          PixelRect destinationRect, std::string& error);

    int ExecuteBloom(Renderer& renderer, SampledTexture source,
                     RenderTarget& destination,
                     const BloomSettings2D& settings, std::string& error);
    int ExecuteColorAdjust(Renderer& renderer, SampledTexture source,
                           RenderTarget& destination,
                           const ColorAdjustSettings2D& settings,
                           std::string& error);
    int ExecuteVignette(Renderer& renderer, SampledTexture source,
                        RenderTarget& destination,
                        const VignetteSettings2D& settings,
                        std::string& error);

    RenderTarget sceneTarget_;
    RenderTarget pingA_;
    RenderTarget pingB_;
    std::vector<std::unique_ptr<RenderTarget>> bloomDown_;
    std::vector<std::unique_ptr<RenderTarget>> bloomUp_;
    PixelSize preparedSize_{};

    Shader* bloomDownShader_ = nullptr;
    Shader* bloomUpShader_ = nullptr;
    Shader* bloomCompositeShader_ = nullptr;
    Shader* colorAdjustShader_ = nullptr;
    Shader* vignetteShader_ = nullptr;
    Shader* resolveShader_ = nullptr;
};

} // namespace molga
