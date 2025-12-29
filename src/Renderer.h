#ifndef MOLGA_RENDERER_H
#define MOLGA_RENDERER_H

#include <glad/glad.h>
#include "linmath.h"

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
    void DrawSprite(Sprite* sprite);
    void End();

    void SetProjection(float left, float right, float bottom, float top);

private:
    unsigned int VAO;
    unsigned int VBO;
    unsigned int EBO;
    Shader* currentShader;
    mat4x4 projection;
    mat4x4 view;

    void SetupQuadBuffers();
};

#endif // MOLGA_RENDERER_H
