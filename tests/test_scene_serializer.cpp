#include "Core/SceneSerializer.h"
#include "ECS/GameObject.h"
#include "ECS/Components/Transform.h"
#include "ECS/Components/BoxCollider2D.h"
#include "doctest.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <vector>

// ── Single GameObject round-trip ─────────────────────────────────────────────

TEST_CASE("SceneSerializer: serialize and deserialize single GameObject") {
    // Create a GameObject with components
    auto original = std::make_shared<GameObject>("TestObject");
    Transform* t = original->AddComponent<Transform>(100.0f, 200.0f);
    t->SetRotation(45.0f);
    t->SetScale(2.0f, 3.0f);

    BoxCollider2D* bc = original->AddComponent<BoxCollider2D>(64.0f, 32.0f);
    bc->SetOffset(5.0f, 10.0f);
    bc->SetTrigger(true);

    // Serialize
    std::string json = SceneSerializer::SerializeGameObject(original.get());
    CHECK(!json.empty());
    CHECK(json != "{}");

    // Deserialize
    auto restored = SceneSerializer::DeserializeGameObject(json);
    REQUIRE(restored != nullptr);
    CHECK(restored->GetName() == "TestObject");
    CHECK(restored->IsActive());

    // Verify Transform
    Transform* rt = restored->GetComponent<Transform>();
    REQUIRE(rt != nullptr);
    CHECK(rt->GetX() == doctest::Approx(100.0f));
    CHECK(rt->GetY() == doctest::Approx(200.0f));
    CHECK(rt->GetRotation() == doctest::Approx(45.0f));
    CHECK(rt->GetScale().x == doctest::Approx(2.0f));
    CHECK(rt->GetScale().y == doctest::Approx(3.0f));

    // Verify BoxCollider2D
    BoxCollider2D* rbc = restored->GetComponent<BoxCollider2D>();
    REQUIRE(rbc != nullptr);
    CHECK(rbc->GetSize().x == doctest::Approx(64.0f));
    CHECK(rbc->GetSize().y == doctest::Approx(32.0f));
    CHECK(rbc->GetOffset().x == doctest::Approx(5.0f));
    CHECK(rbc->GetOffset().y == doctest::Approx(10.0f));
    CHECK(rbc->IsTrigger() == true);
}

// ── Scene save/load round-trip ───────────────────────────────────────────────

TEST_CASE("SceneSerializer: scene save and load") {
    // Create test scene
    std::vector<std::shared_ptr<GameObject>> originalScene;

    auto obj1 = std::make_shared<GameObject>("Player");
    obj1->AddComponent<Transform>(10.0f, 20.0f);
    obj1->AddComponent<BoxCollider2D>(32.0f, 32.0f);
    originalScene.push_back(obj1);

    auto obj2 = std::make_shared<GameObject>("Enemy");
    obj2->SetActive(false);
    Transform* t2 = obj2->AddComponent<Transform>(50.0f, 60.0f);
    t2->SetRotation(90.0f);
    originalScene.push_back(obj2);

    // Save to temp file
    const char* tmpPath = "/tmp/molga_test_scene.json";
    bool saved = SceneSerializer::SaveScene(tmpPath, originalScene);
    CHECK(saved);

    // Load back
    std::vector<std::shared_ptr<GameObject>> loadedScene;
    bool loaded = SceneSerializer::LoadScene(tmpPath, loadedScene);
    CHECK(loaded);
    REQUIRE(loadedScene.size() == 2);

    // Verify first object
    CHECK(loadedScene[0]->GetName() == "Player");
    CHECK(loadedScene[0]->IsActive());
    Transform* lt1 = loadedScene[0]->GetComponent<Transform>();
    REQUIRE(lt1 != nullptr);
    CHECK(lt1->GetX() == doctest::Approx(10.0f));
    CHECK(lt1->GetY() == doctest::Approx(20.0f));
    BoxCollider2D* lbc = loadedScene[0]->GetComponent<BoxCollider2D>();
    CHECK(lbc != nullptr);

    // Verify second object
    CHECK(loadedScene[1]->GetName() == "Enemy");
    CHECK(!loadedScene[1]->IsActive());
    Transform* lt2 = loadedScene[1]->GetComponent<Transform>();
    REQUIRE(lt2 != nullptr);
    CHECK(lt2->GetRotation() == doctest::Approx(90.0f));

    // Cleanup
    std::remove(tmpPath);
}

// ── ID preservation ─────────────────────────────────────────────────────────

TEST_CASE("SceneSerializer: ID preservation") {
    auto obj = std::make_shared<GameObject>("IDTest");
    obj->AddComponent<Transform>(1.0f, 2.0f);
    unsigned int originalID = obj->GetID();

    std::string json = SceneSerializer::SerializeGameObject(obj.get());
    auto restored = SceneSerializer::DeserializeGameObject(json);

    REQUIRE(restored != nullptr);
    CHECK(restored->GetID() == originalID);
}

// ── enabled serialization ───────────────────────────────────────────────────

TEST_CASE("SceneSerializer: enabled serialization") {
    auto obj = std::make_shared<GameObject>("EnabledTest");
    obj->AddComponent<Transform>(5.0f, 10.0f);
    BoxCollider2D* bc = obj->AddComponent<BoxCollider2D>(16.0f, 16.0f);
    bc->SetEnabled(false);

    std::string json = SceneSerializer::SerializeGameObject(obj.get());
    auto restored = SceneSerializer::DeserializeGameObject(json);

    REQUIRE(restored != nullptr);
    Transform* rt = restored->GetComponent<Transform>();
    REQUIRE(rt != nullptr);
    CHECK(rt->IsEnabled());  // default true

    BoxCollider2D* rbc = restored->GetComponent<BoxCollider2D>();
    REQUIRE(rbc != nullptr);
    CHECK(!rbc->IsEnabled());  // was disabled
}

// ── Parent-child serialization ──────────────────────────────────────────────

TEST_CASE("SceneSerializer: parent-child serialization") {
    std::vector<std::shared_ptr<GameObject>> originalScene;

    auto parent = std::make_shared<GameObject>("Parent");
    parent->AddComponent<Transform>(0.0f, 0.0f);
    originalScene.push_back(parent);

    auto child = std::make_shared<GameObject>("Child");
    child->AddComponent<Transform>(10.0f, 20.0f);
    child->SetParent(parent.get());
    originalScene.push_back(child);

    unsigned int parentID = parent->GetID();
    unsigned int childID = child->GetID();

    // Save
    const char* tmpPath = "/tmp/molga_test_hierarchy.json";
    bool saved = SceneSerializer::SaveScene(tmpPath, originalScene);
    CHECK(saved);

    // Load
    std::vector<std::shared_ptr<GameObject>> loadedScene;
    bool loaded = SceneSerializer::LoadScene(tmpPath, loadedScene);
    CHECK(loaded);
    REQUIRE(loadedScene.size() == 2);

    // Find parent and child by ID
    GameObject* loadedParent = nullptr;
    GameObject* loadedChild = nullptr;
    for (auto& obj : loadedScene) {
        if (obj->GetID() == parentID) loadedParent = obj.get();
        if (obj->GetID() == childID) loadedChild = obj.get();
    }
    REQUIRE(loadedParent != nullptr);
    REQUIRE(loadedChild != nullptr);

    // Verify hierarchy
    CHECK(loadedChild->GetParent() == loadedParent);
    REQUIRE(loadedParent->GetChildren().size() == 1);
    CHECK(loadedParent->GetChildren()[0] == loadedChild);

    std::remove(tmpPath);
}

// ── Error handling ───────────────────────────────────────────────────────────

TEST_CASE("SceneSerializer: invalid JSON") {
    auto result = SceneSerializer::DeserializeGameObject("not valid json{{{");
    CHECK(result == nullptr);
}

TEST_CASE("SceneSerializer: null GameObject") {
    std::string json = SceneSerializer::SerializeGameObject(nullptr);
    CHECK(json == "{}");
}

TEST_CASE("SceneSerializer: load nonexistent file") {
    std::vector<std::shared_ptr<GameObject>> objects;
    bool loaded = SceneSerializer::LoadScene("/tmp/nonexistent_test_file_12345.json", objects);
    CHECK(!loaded);
}
