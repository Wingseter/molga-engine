#pragma once

#include <glad/glad.h>
#include "../Common/linmath.h"

#include "Rendering/RenderPassState.h"
#include "Core/Profiling/FrameProfile.h"

class Shader;
class Sprite;
class Camera2D;

class Renderer {
public:
    Renderer();
    ~Renderer();

    void Init();
    void Clear(float r, float g, float b, float a = 1.0f);
    void SetViewport(int width, int height);

    void Begin(Shader* shader, Camera2D* camera = nullptr);
    void SetShader(Shader* shader);
    void DrawSprite(Sprite* sprite);
    void End();

    bool IsDrawing() const;
    Shader* GetCurrentShader() const { return currentShader; }

    void SetProjection(float left, float right, float bottom, float top);

    const molga::RenderStats& Stats() const { return stats_; }
    void ResetStats() { stats_.Reset(); }

private:
    molga::RenderPassState pass;

    unsigned int VAO;
    unsigned int VBO;
    unsigned int EBO;
    Shader* currentShader; // non-owning; lifetime managed by caller
    mat4x4 projection;
    mat4x4 view;

    molga::RenderStats stats_;

    void SetupQuadBuffers();
};
