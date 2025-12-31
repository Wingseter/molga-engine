#include "SpriteRenderer.h"
#include "../GameObject.h"
#include "../../Renderer.h"
#include "../../Shader.h"
#include "../../Camera2D.h"
#include "../../Sprite.h"

void SpriteRenderer::RenderSprite(Renderer* renderer, Shader* shader, Camera2D* camera) {
    if (!gameObject || !enabled) return;

    Transform* transform = gameObject->GetComponent<Transform>();
    if (!transform) return;

    // Create a temporary sprite for rendering
    Sprite sprite;

    Vector2 worldPos = transform->GetWorldPosition();
    Vector2 worldScale = transform->GetWorldScale();
    float worldRot = transform->GetWorldRotation();

    sprite.SetPosition(worldPos.x, worldPos.y);
    sprite.SetSize(width * worldScale.x, height * worldScale.y);
    sprite.SetRotation(worldRot);
    sprite.SetColor(color.r, color.g, color.b, color.a);

    if (texture) {
        sprite.SetTexture(texture);
    }

    // Apply flip
    if (flipX) {
        sprite.x += sprite.width;
        sprite.width = -sprite.width;
    }
    if (flipY) {
        sprite.y += sprite.height;
        sprite.height = -sprite.height;
    }

    renderer->Begin(shader, camera);
    renderer->DrawSprite(&sprite);
    renderer->End();
}
