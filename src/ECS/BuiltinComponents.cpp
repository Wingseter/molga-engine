#include "BuiltinComponents.h"

#include "GameObject.h"
#include "ComponentFactory.h"
#include "Components/AudioListener.h"
#include "Components/AudioSource.h"
#include "Components/Animator2D.h"
#include "Components/BoxCollider2D.h"
#include "Components/Camera.h"
#include "Components/CircleCollider2D.h"
#include "Components/ParticleSystem.h"
#include "Components/PointLight2D.h"
#include "Components/PrefabInstance.h"
#include "Components/Rigidbody2D.h"
#include "Components/SpriteRenderer.h"
#include "Components/ShadowOccluder2D.h"
#include "Components/TextRenderer2D.h"
#include "Components/TilemapRenderer.h"
#include "Components/Transform.h"
#include "Components/UICanvas.h"
#include "Components/RectTransform.h"
#include "Components/UIImage.h"
#include "Components/UILabel.h"
#include "Components/UIButton.h"

#ifdef MOLGA_MARROW_SUPPORT
#include "Components/MarrowRenderer.h"
#endif

void RegisterBuiltinComponents() {
    auto& factory = ComponentFactory::Get();
    factory.Register<Transform>(Transform::StaticTypeName());
    factory.Register<SpriteRenderer>(SpriteRenderer::StaticTypeName());
    factory.Register<Animator2D>(Animator2D::StaticTypeName());
    factory.Register<TilemapRenderer>(TilemapRenderer::StaticTypeName());
    factory.Register<TextRenderer2D>(TextRenderer2D::StaticTypeName());
    factory.Register<ParticleSystem>(ParticleSystem::StaticTypeName());
    factory.Register<PointLight2D>(PointLight2D::StaticTypeName());
    factory.Register<ShadowOccluder2D>(ShadowOccluder2D::StaticTypeName());
    factory.Register<BoxCollider2D>(BoxCollider2D::StaticTypeName());
    factory.Register<CircleCollider2D>(CircleCollider2D::StaticTypeName());
    factory.Register<Rigidbody2D>(Rigidbody2D::StaticTypeName());
    factory.Register<AudioSource>(AudioSource::StaticTypeName());
    factory.Register<AudioListener>(AudioListener::StaticTypeName());
    factory.Register<Camera>(Camera::StaticTypeName());
    factory.Register<PrefabInstance>(PrefabInstance::StaticTypeName());
    factory.Register<UICanvas>(UICanvas::StaticTypeName());
    factory.Register<RectTransform>(RectTransform::StaticTypeName());
    factory.Register<UIImage>(UIImage::StaticTypeName());
    factory.Register<UILabel>(UILabel::StaticTypeName());
    factory.Register<UIButton>(UIButton::StaticTypeName());
#ifdef MOLGA_MARROW_SUPPORT
    factory.Register<MarrowRenderer>(MarrowRenderer::StaticTypeName());
#endif
}
