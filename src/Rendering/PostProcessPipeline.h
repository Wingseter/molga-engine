#pragma once

#include "Rendering/Framebuffer.h"
#include "Rendering/OutputPresentationLayout.h"
#include "Rendering/PixelSize.h"
#include "Rendering/PostProcessProfile2D.h"

#include <memory>
#include <string>
#include <vector>

class Shader;

namespace molga {

struct PostProcessExecutionResult {
    bool success = false;
    bool postProcessed = false;
    int passes = 0;
    std::string error;
};

// Ordered HDR fullscreen pipeline. Prepare() is deliberately separate so all
// allocations and shader programs can be validated before world rendering.
class PostProcessPipeline {
public:
    PostProcessPipeline();
    ~PostProcessPipeline();

    PostProcessPipeline(const PostProcessPipeline&) = delete;
    PostProcessPipeline& operator=(const PostProcessPipeline&) = delete;

    bool Prepare(PixelSize size, const PostProcessProfile2D& profile,
                 std::string* errorOut = nullptr);
    Framebuffer& SceneTarget() { return sceneTarget_; }
    const Framebuffer& SceneTarget() const { return sceneTarget_; }

    PostProcessExecutionResult Execute(
        const PostProcessProfile2D& profile,
        unsigned int destinationFramebuffer,
        PixelSize destinationSize);
    PostProcessExecutionResult Execute(
        const PostProcessProfile2D& profile,
        unsigned int destinationFramebuffer,
        PixelSize destinationSize,
        PixelRect destinationRect);

    PixelSize PreparedSize() const { return preparedSize_; }
    std::size_t BloomMipCount() const { return bloomDown_.size(); }

private:
    bool PrepareShaders(const PostProcessProfile2D& profile,
                        std::string* errorOut);
    bool PrepareQuad(std::string* errorOut);
    bool PrepareTargets(PixelSize size, bool needsBloom, std::string* errorOut);

    void DrawSingleTexture(Shader& shader, unsigned int source,
                           Framebuffer& destination);
    bool DrawResolve(unsigned int source, unsigned int destinationFramebuffer,
                     PixelSize destinationSize, PixelRect destinationRect);

    int ExecuteBloom(unsigned int source, Framebuffer& destination,
                     const BloomSettings2D& settings);
    int ExecuteColorAdjust(unsigned int source, Framebuffer& destination,
                           const ColorAdjustSettings2D& settings);
    int ExecuteVignette(unsigned int source, Framebuffer& destination,
                        const VignetteSettings2D& settings);

    Framebuffer sceneTarget_;
    Framebuffer pingA_;
    Framebuffer pingB_;
    std::vector<std::unique_ptr<Framebuffer>> bloomDown_;
    std::vector<std::unique_ptr<Framebuffer>> bloomUp_;
    PixelSize preparedSize_{};

    Shader* bloomDownShader_ = nullptr;
    Shader* bloomUpShader_ = nullptr;
    Shader* bloomCompositeShader_ = nullptr;
    Shader* colorAdjustShader_ = nullptr;
    Shader* vignetteShader_ = nullptr;
    Shader* resolveShader_ = nullptr;

    unsigned int fullscreenVao_ = 0;
    unsigned int fullscreenVbo_ = 0;
};

} // namespace molga
