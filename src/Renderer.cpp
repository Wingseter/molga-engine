#include "Renderer.h"
#include "Shader.h"
#include "Sprite.h"
#include "Texture.h"

Renderer::Renderer() : VAO(0), VBO(0), EBO(0), currentShader(nullptr) {
    mat4x4_identity(projection);
}

Renderer::~Renderer() {
    if (VAO) glDeleteVertexArrays(1, &VAO);
    if (VBO) glDeleteBuffers(1, &VBO);
    if (EBO) glDeleteBuffers(1, &EBO);
}

void Renderer::Init() {
    SetupQuadBuffers();
}

void Renderer::SetupQuadBuffers() {
    // Quad vertices: position (2) + texcoord (2)
    float vertices[] = {
        // pos      // tex
        0.0f, 1.0f, 0.0f, 1.0f,  // top-left
        1.0f, 0.0f, 1.0f, 0.0f,  // bottom-right
        0.0f, 0.0f, 0.0f, 0.0f,  // bottom-left

        0.0f, 1.0f, 0.0f, 1.0f,  // top-left
        1.0f, 1.0f, 1.0f, 1.0f,  // top-right
        1.0f, 0.0f, 1.0f, 0.0f   // bottom-right
    };

    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);

    glBindVertexArray(VAO);

    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    // Position attribute
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    // Texture coord attribute
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
}

void Renderer::Clear(float r, float g, float b, float a) {
    glClearColor(r, g, b, a);
    glClear(GL_COLOR_BUFFER_BIT);
}

void Renderer::SetViewport(int width, int height) {
    glViewport(0, 0, width, height);
}

void Renderer::SetProjection(float left, float right, float bottom, float top) {
    mat4x4_ortho(projection, left, right, bottom, top, -1.0f, 1.0f);
}

void Renderer::Begin(Shader* shader) {
    currentShader = shader;
    currentShader->Use();
    currentShader->SetMat4("projection", (float*)projection);
}

void Renderer::DrawSprite(Sprite* sprite) {
    if (!currentShader || !sprite) return;

    mat4x4 model;
    sprite->GetModelMatrix(model);

    currentShader->SetMat4("model", (float*)model);
    currentShader->SetVec4("uColor", sprite->color[0], sprite->color[1], sprite->color[2], sprite->color[3]);

    if (sprite->texture) {
        currentShader->SetBool("useTexture", true);
        currentShader->SetInt("uTexture", 0);
        sprite->texture->Bind(0);
    } else {
        currentShader->SetBool("useTexture", false);
    }

    glBindVertexArray(VAO);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
}

void Renderer::End() {
    currentShader = nullptr;
}
