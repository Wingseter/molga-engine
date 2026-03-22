#include "Core/SceneSerializer.h"
#include "ECS/GameObject.h"
#include "ECS/Components/Transform.h"
#include "ECS/Components/BoxCollider2D.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <vector>

static bool approx(float a, float b, float eps = 1e-4f) {
    return std::fabs(a - b) < eps;
}

// ── Single GameObject round-trip ─────────────────────────────────────────────

static void test_serialize_deserialize_gameobject() {
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
    assert(!json.empty());
    assert(json != "{}");

    // Deserialize
    auto restored = SceneSerializer::DeserializeGameObject(json);
    assert(restored != nullptr);
    assert(restored->GetName() == "TestObject");
    assert(restored->IsActive());

    // Verify Transform
    Transform* rt = restored->GetComponent<Transform>();
    assert(rt != nullptr);
    assert(approx(rt->GetX(), 100.0f));
    assert(approx(rt->GetY(), 200.0f));
    assert(approx(rt->GetRotation(), 45.0f));
    assert(approx(rt->GetScale().x, 2.0f));
    assert(approx(rt->GetScale().y, 3.0f));

    // Verify BoxCollider2D
    BoxCollider2D* rbc = restored->GetComponent<BoxCollider2D>();
    assert(rbc != nullptr);
    assert(approx(rbc->GetSize().x, 64.0f));
    assert(approx(rbc->GetSize().y, 32.0f));
    assert(approx(rbc->GetOffset().x, 5.0f));
    assert(approx(rbc->GetOffset().y, 10.0f));
    assert(rbc->IsTrigger() == true);
}

// ── Scene save/load round-trip ───────────────────────────────────────────────

static void test_scene_save_load() {
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
    assert(saved);

    // Load back
    std::vector<std::shared_ptr<GameObject>> loadedScene;
    bool loaded = SceneSerializer::LoadScene(tmpPath, loadedScene);
    assert(loaded);
    assert(loadedScene.size() == 2);

    // Verify first object
    assert(loadedScene[0]->GetName() == "Player");
    assert(loadedScene[0]->IsActive());
    Transform* lt1 = loadedScene[0]->GetComponent<Transform>();
    assert(lt1 != nullptr);
    assert(approx(lt1->GetX(), 10.0f));
    assert(approx(lt1->GetY(), 20.0f));
    BoxCollider2D* lbc = loadedScene[0]->GetComponent<BoxCollider2D>();
    assert(lbc != nullptr);

    // Verify second object
    assert(loadedScene[1]->GetName() == "Enemy");
    assert(!loadedScene[1]->IsActive());
    Transform* lt2 = loadedScene[1]->GetComponent<Transform>();
    assert(lt2 != nullptr);
    assert(approx(lt2->GetRotation(), 90.0f));

    // Cleanup
    std::remove(tmpPath);
}

// ── ID preservation ─────────────────────────────────────────────────────────

static void test_id_preservation() {
    auto obj = std::make_shared<GameObject>("IDTest");
    obj->AddComponent<Transform>(1.0f, 2.0f);
    unsigned int originalID = obj->GetID();

    std::string json = SceneSerializer::SerializeGameObject(obj.get());
    auto restored = SceneSerializer::DeserializeGameObject(json);

    assert(restored != nullptr);
    assert(restored->GetID() == originalID);
}

// ── enabled serialization ───────────────────────────────────────────────────

static void test_enabled_serialization() {
    auto obj = std::make_shared<GameObject>("EnabledTest");
    Transform* t = obj->AddComponent<Transform>(5.0f, 10.0f);
    BoxCollider2D* bc = obj->AddComponent<BoxCollider2D>(16.0f, 16.0f);
    bc->SetEnabled(false);

    std::string json = SceneSerializer::SerializeGameObject(obj.get());
    auto restored = SceneSerializer::DeserializeGameObject(json);

    assert(restored != nullptr);
    Transform* rt = restored->GetComponent<Transform>();
    assert(rt != nullptr);
    assert(rt->IsEnabled());  // default true

    BoxCollider2D* rbc = restored->GetComponent<BoxCollider2D>();
    assert(rbc != nullptr);
    assert(!rbc->IsEnabled());  // was disabled
}

// ── Parent-child serialization ──────────────────────────────────────────────

static void test_parent_child_serialization() {
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
    assert(saved);

    // Load
    std::vector<std::shared_ptr<GameObject>> loadedScene;
    bool loaded = SceneSerializer::LoadScene(tmpPath, loadedScene);
    assert(loaded);
    assert(loadedScene.size() == 2);

    // Find parent and child by ID
    GameObject* loadedParent = nullptr;
    GameObject* loadedChild = nullptr;
    for (auto& obj : loadedScene) {
        if (obj->GetID() == parentID) loadedParent = obj.get();
        if (obj->GetID() == childID) loadedChild = obj.get();
    }
    assert(loadedParent != nullptr);
    assert(loadedChild != nullptr);

    // Verify hierarchy
    assert(loadedChild->GetParent() == loadedParent);
    assert(loadedParent->GetChildren().size() == 1);
    assert(loadedParent->GetChildren()[0] == loadedChild);

    std::remove(tmpPath);
}

// ── Error handling ───────────────────────────────────────────────────────────

static void test_invalid_json() {
    auto result = SceneSerializer::DeserializeGameObject("not valid json{{{");
    assert(result == nullptr);
}

static void test_null_gameobject() {
    std::string json = SceneSerializer::SerializeGameObject(nullptr);
    assert(json == "{}");
}

static void test_load_nonexistent_file() {
    std::vector<std::shared_ptr<GameObject>> objects;
    bool loaded = SceneSerializer::LoadScene("/tmp/nonexistent_test_file_12345.json", objects);
    assert(!loaded);
}

// ── main ─────────────────────────────────────────────────────────────────────

int main() {
    test_serialize_deserialize_gameobject();
    test_scene_save_load();
    test_id_preservation();
    test_enabled_serialization();
    test_parent_child_serialization();
    test_invalid_json();
    test_null_gameobject();
    test_load_nonexistent_file();

    std::printf("test_scene_serializer: all tests passed\n");
    return 0;
}
