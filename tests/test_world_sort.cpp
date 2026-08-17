#include "Core/ProjectSettings.h"
#include "ECS/Component.h"
#include "ECS/GameObject.h"
#ifdef MOLGA_MARROW_SUPPORT
#include "ECS/Components/MarrowRenderer.h"
#endif
#include "ECS/Components/ParticleSystem.h"
#include "ECS/Components/SpriteRenderer.h"
#include "ECS/Components/TextRenderer2D.h"
#include "ECS/Components/TilemapRenderer.h"
#include "ECS/Components/Transform.h"
#include "Rendering/RenderQueue.h"
#include "Rendering/WorldRenderTraversal.h"
#include "Rendering/WorldSort2D.h"
#include "doctest.h"

#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace {

struct ProjectSettingsScope {
    ProjectSettingsScope() : before(ProjectSettings::Get().Serialize()) {
        ProjectSettings::Get().SetDefaults();
    }
    ~ProjectSettingsScope() { ProjectSettings::Get().Deserialize(before); }
    nlohmann::json before;
};

class TraversalProbeA final : public Component {
public:
    COMPONENT_TYPE(TraversalProbeA)
    explicit TraversalProbeA(int marker) : marker_(marker) {}
    void CollectRender(molga::RenderQueue& queue) override {
        molga::RenderCommand command;
        command.sortKey.sortingOrder = marker_;
        queue.Submit(command);
    }
private:
    int marker_ = 0;
};

class TraversalProbeB final : public Component {
public:
    COMPONENT_TYPE(TraversalProbeB)
    explicit TraversalProbeB(int marker) : marker_(marker) {}
    void CollectRender(molga::RenderQueue& queue) override {
        molga::RenderCommand command;
        command.sortKey.sortingOrder = marker_;
        queue.Submit(command);
    }
private:
    int marker_ = 0;
};

void CheckFlatSortJson(const nlohmann::json& json) {
    CHECK(json.at("sortingLayer") == "Foreground");
    CHECK(json.at("sortingOrder") == -4);
    CHECK(json.at("sortMode") == "YAxis");
    CHECK(json.at("ySortOffset").get<float>() == doctest::Approx(3.5f));
}

template <typename RendererComponent>
void AuthorYAxisSort(RendererComponent& component) {
    component.SetSortingLayer("Foreground");
    component.SetSortingOrder(-4);
    component.SetSortMode(molga::SortMode2D::YAxis);
    component.SetYSortOffset(3.5f);
}

} // namespace

TEST_CASE("world render traversal preserves object and component slots") {
    auto first = std::make_shared<GameObject>("First");
    first->AddComponent<TraversalProbeA>(1);
    first->AddComponent<TraversalProbeB>(2);
    auto second = std::make_shared<GameObject>("Second");
    second->AddComponent<TraversalProbeA>(3);
    auto disabled = second->AddComponent<TraversalProbeB>(4);
    disabled->SetEnabled(false);
    auto inactive = std::make_shared<GameObject>("Inactive");
    inactive->AddComponent<TraversalProbeA>(5);
    inactive->SetActive(false);

    molga::RenderQueue queue;
    molga::CollectWorldRender(
        {first, second, inactive}, queue,
        [](Component& component, molga::RenderQueue& target) {
            if (!dynamic_cast<TraversalProbeB*>(&component)) return false;
            molga::RenderCommand replacement;
            replacement.sortKey.sortingOrder = 99;
            target.Submit(replacement);
            return true;
        });

    REQUIRE(queue.GetCommands().size() == 3U);
    CHECK(queue.GetCommands()[0].sortKey.sortingOrder == 1);
    CHECK(queue.GetCommands()[1].sortKey.sortingOrder == 99);
    CHECK(queue.GetCommands()[2].sortKey.sortingOrder == 3);
    CHECK(queue.GetCommands()[0].sortKey.submissionIndex == 0U);
    CHECK(queue.GetCommands()[1].sortKey.submissionIndex == 1U);
    CHECK(queue.GetCommands()[2].sortKey.submissionIndex == 2U);
}

TEST_CASE("world renderer sorting fields serialize flat with legacy defaults") {
    ProjectSettingsScope settingsScope;

    auto spriteObject = std::make_shared<GameObject>("Sprite");
    spriteObject->AddComponent<Transform>();
    auto* sprite = spriteObject->AddComponent<SpriteRenderer>();
    AuthorYAxisSort(*sprite);
    nlohmann::json spriteJson;
    sprite->Serialize(spriteJson);
    CheckFlatSortJson(spriteJson);
    SpriteRenderer legacySprite;
    legacySprite.Deserialize({{"sortingOrder", 8}});
    CHECK(legacySprite.GetSortingLayer() == "Default");
    CHECK(legacySprite.GetSortingOrder() == 8);
    CHECK(legacySprite.GetSortMode() == molga::SortMode2D::Fixed);
    CHECK(legacySprite.GetYSortOffset() == 0.0f);

    TextRenderer2D text;
    AuthorYAxisSort(text);
    nlohmann::json textJson;
    text.Serialize(textJson);
    CheckFlatSortJson(textJson);
    TextRenderer2D legacyText;
    legacyText.Deserialize({{"sortingOrder", 9}});
    CHECK(legacyText.GetSortingLayer() == "Default");
    CHECK(legacyText.GetSortMode() == molga::SortMode2D::Fixed);

    ParticleSystem particles;
    AuthorYAxisSort(particles);
    nlohmann::json particleJson;
    particles.Serialize(particleJson);
    CheckFlatSortJson(particleJson);
    CHECK(particleJson.at("schemaVersion") == 2);
    ParticleSystem legacyParticles;
    legacyParticles.Deserialize({{"sortingOrder", 10}});
    CHECK(legacyParticles.GetSortingLayer() == "Default");
    CHECK(legacyParticles.GetSortMode() == molga::SortMode2D::Fixed);

#ifdef MOLGA_MARROW_SUPPORT
    MarrowRenderer marrow;
    AuthorYAxisSort(marrow);
    nlohmann::json marrowJson;
    marrow.Serialize(marrowJson);
    CheckFlatSortJson(marrowJson);
    MarrowRenderer legacyMarrow;
    legacyMarrow.Deserialize({{"sortingOrder", 11}});
    CHECK(legacyMarrow.GetSortingLayer() == "Default");
    CHECK(legacyMarrow.GetSortMode() == molga::SortMode2D::Fixed);
#endif

    TilemapRenderer tilemap;
    tilemap.SetSortingLayer("Foreground");
    tilemap.SetSortingOrder(-4);
    nlohmann::json tilemapJson;
    tilemap.Serialize(tilemapJson);
    CHECK(tilemapJson.at("sortingLayer") == "Foreground");
    CHECK(tilemapJson.at("sortingOrder") == -4);
    CHECK_FALSE(tilemapJson.contains("sortMode"));
    CHECK_FALSE(tilemapJson.contains("ySortOffset"));
    TilemapRenderer legacyTilemap;
    legacyTilemap.Deserialize({{"sortingOrder", 12}});
    CHECK(legacyTilemap.GetSortingLayer() == "Default");
    CHECK(legacyTilemap.GetSortingOrder() == 12);
}

TEST_CASE("Sprite Particle and enabled Marrow use one resolved component Y key") {
    ProjectSettingsScope settingsScope;
    ProjectSettings::Get().sortingLayers = {"Default", "Foreground"};

    auto spriteObject = std::make_shared<GameObject>("Sprite");
    spriteObject->AddComponent<Transform>()->SetPosition(0.0f, 20.0f);
    auto* sprite = spriteObject->AddComponent<SpriteRenderer>();
    AuthorYAxisSort(*sprite);
    molga::RenderQueue spriteQueue;
    sprite->CollectRender(spriteQueue);
    REQUIRE(spriteQueue.GetCommands().size() == 1U);
    CHECK(spriteQueue.GetCommands()[0].sortKey.sortingLayer == 1);
    CHECK(spriteQueue.GetCommands()[0].sortKey.sortingOrder == -4);
    CHECK(spriteQueue.GetCommands()[0].sortKey.depthOrYSort ==
          doctest::Approx(23.5f));

    auto particleObject = std::make_shared<GameObject>("Particles");
    particleObject->AddComponent<Transform>()->SetPosition(0.0f, 30.0f);
    auto* particles = particleObject->AddComponent<ParticleSystem>();
    particles->config.spawnRate = 0.0f;
    particles->GetEmitter().SetConfig(particles->config);
    particles->Emit(1);
    AuthorYAxisSort(*particles);
    molga::RenderQueue particleQueue;
    particles->CollectRender(particleQueue);
    REQUIRE_FALSE(particleQueue.GetCommands().empty());
    for (const auto& command : particleQueue.GetCommands()) {
        CHECK(command.sortKey.sortingLayer == 1);
        CHECK(command.sortKey.sortingOrder == -4);
        CHECK(command.sortKey.depthOrYSort == doctest::Approx(33.5f));
    }

#ifdef MOLGA_MARROW_SUPPORT
    auto marrowObject = std::make_shared<GameObject>("Marrow");
    marrowObject->AddComponent<Transform>()->SetPosition(0.0f, 40.0f);
    auto* marrow = marrowObject->AddComponent<MarrowRenderer>();
    AuthorYAxisSort(*marrow);
    // An unresolved Marrow asset must not publish the legacy fallback draw.
    molga::RenderQueue marrowQueue;
    marrow->CollectRender(marrowQueue);
    CHECK(marrowQueue.GetCommands().empty());
    const molga::SortKey marrowKey = molga::MakeWorldSortKey(
        marrow->GetWorldSortSettings(),
        marrowObject->GetComponent<Transform>()->GetWorldPosition().y);
    CHECK(marrowKey.sortingLayer == 1);
    CHECK(marrowKey.sortingOrder == -4);
    CHECK(marrowKey.depthOrYSort == doctest::Approx(43.5f));
#endif
}

TEST_CASE("Tilemap sorting keeps base layer index and per-layer offset") {
    ProjectSettingsScope settingsScope;
    ProjectSettings::Get().sortingLayers = {"Default", "World"};

    molga::WorldSortSettings2D settings;
    settings.sortingLayer = "World";
    settings.sortingOrder = molga::ComposeWorldSortingOrder(-8, 3, -2);
    const molga::SortKey key = molga::MakeWorldSortKey(settings);
    CHECK(key.sortingLayer == 1);
    CHECK(key.sortingOrder == -7);
    CHECK(key.depthOrYSort == 0.0f); // Tilemaps never row/cell Y-sort.

    CHECK(molga::ComposeWorldSortingOrder(
              std::numeric_limits<int>::max(), 1, 1) ==
          std::numeric_limits<int>::max());
    CHECK(molga::ComposeWorldSortingOrder(
              std::numeric_limits<int>::min(), -1, -1) ==
          std::numeric_limits<int>::min());
}
