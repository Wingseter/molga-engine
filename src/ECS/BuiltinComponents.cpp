#include "BuiltinComponents.h"

#include "GameObject.h"
#include "ComponentFactory.h"
#include "Components/AudioListener.h"
#include "Components/AudioSource.h"
#include "Components/BoxCollider2D.h"
#include "Components/Camera.h"
#include "Components/CircleCollider2D.h"
#include "Components/ParticleSystem.h"
#include "Components/PrefabInstance.h"
#include "Components/Rigidbody2D.h"
#include "Components/SpriteRenderer.h"
#include "Components/TextRenderer2D.h"
#include "Components/TilemapRenderer.h"
#include "Components/Transform.h"

#ifdef MOLGA_MARROW_SUPPORT
#include "Components/MarrowRenderer.h"
#endif

void RegisterBuiltinComponents() {
    auto& factory = ComponentFactory::Get();
    factory.Register<Transform>(Transform::StaticTypeName());
    factory.Register<SpriteRenderer>(SpriteRenderer::StaticTypeName());
    factory.Register<TilemapRenderer>(TilemapRenderer::StaticTypeName());
    factory.Register<TextRenderer2D>(TextRenderer2D::StaticTypeName());
    factory.Register<ParticleSystem>(ParticleSystem::StaticTypeName());
    factory.Register<BoxCollider2D>(BoxCollider2D::StaticTypeName());
    factory.Register<CircleCollider2D>(CircleCollider2D::StaticTypeName());
    factory.Register<Rigidbody2D>(Rigidbody2D::StaticTypeName());
    factory.Register<AudioSource>(AudioSource::StaticTypeName());
    factory.Register<AudioListener>(AudioListener::StaticTypeName());
    factory.Register<Camera>(Camera::StaticTypeName());
    factory.Register<PrefabInstance>(PrefabInstance::StaticTypeName());
#ifdef MOLGA_MARROW_SUPPORT
    factory.Register<MarrowRenderer>(MarrowRenderer::StaticTypeName());
#endif
}
