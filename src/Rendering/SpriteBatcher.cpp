#include "Rendering/SpriteBatcher.h"
#include "Rendering/Renderer.h"
#include "Rendering/Shader.h"
#include "Rendering/ShaderManager.h"
#include "Rendering/Texture.h"
#include <glad/glad.h>
#include "Core/Profiling/ProfileScope.h"

namespace molga {

SpriteBatcher::SpriteBatcher() {
    vertices_.reserve(MAX_SPRITES * 4);
}

SpriteBatcher::~SpriteBatcher() {
    Shutdown();
}

void SpriteBatcher::Init() {
    // Generate indices
    std::vector<unsigned int> indices;
    indices.resize(MAX_SPRITES * 6);
    for (unsigned int i = 0; i < MAX_SPRITES; ++i) {
        indices[i * 6 + 0] = i * 4 + 0;
        indices[i * 6 + 1] = i * 4 + 2;
        indices[i * 6 + 2] = i * 4 + 3;
        indices[i * 6 + 3] = i * 4 + 0;
        indices[i * 6 + 4] = i * 4 + 1;
        indices[i * 6 + 5] = i * 4 + 2;
    }

    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);
    glGenBuffers(1, &EBO);

    glBindVertexArray(VAO);

    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, MAX_SPRITES * 4 * sizeof(Vertex2D), nullptr, GL_DYNAMIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), indices.data(), GL_STATIC_DRAW);

    // Position attribute
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex2D), (void*)offsetof(Vertex2D, x));
    glEnableVertexAttribArray(0);

    // TexCoord attribute
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex2D), (void*)offsetof(Vertex2D, u));
    glEnableVertexAttribArray(1);

    // Color attribute
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex2D), (void*)offsetof(Vertex2D, r));
    glEnableVertexAttribArray(2);

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
}

void SpriteBatcher::Shutdown() {
    if (VAO) {
        glDeleteVertexArrays(1, &VAO);
        VAO = 0;
    }
    if (VBO) {
        glDeleteBuffers(1, &VBO);
        VBO = 0;
    }
    if (EBO) {
        glDeleteBuffers(1, &EBO);
        EBO = 0;
    }
}

void SpriteBatcher::Begin(Renderer* renderer) {
    renderer_ = renderer;
    vertices_.clear();
    spriteCount_ = 0;
    hasActiveKey_ = false;
}

void SpriteBatcher::DrawSprite(const std::array<Vertex2D, 4>& vertices, const BatchKey& key) {
    if (!renderer_) return;

    if (spriteCount_ >= MAX_SPRITES || (hasActiveKey_ && activeKey_ != key)) {
        if (hasActiveKey_ && activeKey_ != key) {
            renderer_->Stats().batchBreaks++;
        } else {
            renderer_->Stats().batchBreaks++;
        }
        Flush();
    }

    if (!hasActiveKey_) {
        activeKey_ = key;
        hasActiveKey_ = true;
    }

    // Append vertices
    for (const auto& v : vertices) {
        vertices_.push_back(v);
    }
    spriteCount_++;
    renderer_->Stats().submittedSprites++;
}

void SpriteBatcher::DrawGeometry(const std::vector<Vertex2D>& vertices,
                                 const BatchKey& key) {
    if (vertices.size() % 4 != 0) return;
    for (std::size_t offset = 0; offset < vertices.size(); offset += 4) {
        std::array<Vertex2D, 4> quad{
            vertices[offset], vertices[offset + 1],
            vertices[offset + 2], vertices[offset + 3]
        };
        DrawSprite(quad, key);
    }
}

void SpriteBatcher::Flush() {
    if (spriteCount_ == 0 || !renderer_) return;

    MOLGA_PROFILE_SCOPE("SpriteBatcher.Flush", molga::ProfileCategory::Rendering);

    // Apply Shader
    Shader* shader = activeKey_.shader;
    if (!shader) {
        // Every command reaching SpriteBatcher uses the batched vertex format
        // (position/UV/per-vertex color). UI rectangles, text glyphs and
        // tilemap geometry intentionally leave the shader unspecified, so the
        // compatible batch shader is their canonical fallback.
        shader = ShaderManager::Get().Get("batch");
        if (!shader) shader = ShaderManager::Get().Get("default");
    }
    if (shader) {
        renderer_->SetShader(shader);
        shader->SetBool("useTexture", activeKey_.texture != nullptr);
        if (activeKey_.texture) {
            shader->SetInt("uTexture", 0);
            activeKey_.texture->Bind(0);
            renderer_->Stats().textureBinds++;
        }
    }

    // Apply BlendMode
    switch (activeKey_.blendMode) {
        case BlendMode::Opaque:
            glDisable(GL_BLEND);
            break;
        case BlendMode::Alpha:
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            break;
        case BlendMode::Additive:
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE);
            break;
        case BlendMode::Multiply:
            glEnable(GL_BLEND);
            glBlendFunc(GL_DST_COLOR, GL_ZERO);
            break;
    }

    // Bind and upload VBO
    glBindVertexArray(VAO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferSubData(GL_ARRAY_BUFFER, 0, vertices_.size() * sizeof(Vertex2D), vertices_.data());

    // Draw
    glDrawElements(GL_TRIANGLES, spriteCount_ * 6, GL_UNSIGNED_INT, nullptr);

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    // Update stats
    auto& stats = renderer_->Stats();
    stats.drawCalls++;
    stats.batches++;
    stats.batchFlushes++;
    stats.verticesUploadedBytes += spriteCount_ * 4 * sizeof(Vertex2D);
    if ((int)spriteCount_ > stats.maxSpritesPerBatch) {
        stats.maxSpritesPerBatch = (int)spriteCount_;
    }

    // Reset for next batch
    vertices_.clear();
    spriteCount_ = 0;
    hasActiveKey_ = false;
}

void SpriteBatcher::End() {
    Flush();
    renderer_ = nullptr;
}

} // namespace molga
