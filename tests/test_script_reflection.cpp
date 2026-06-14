#include "doctest.h"
#include "Scripting/Script.h"
#include "Scripting/ScriptField.h"
#include "Scripting/ScriptManager.h"
#include "Core/World.h"
#include "ECS/GameObject.h"
#include <nlohmann/json.hpp>
#include <memory>

namespace {

// 모든 노출 필드 타입을 한 번에 검증하기 위한 테스트 스크립트.
class ReflectTestScript : public Script {
public:
    SCRIPT_CLASS(ReflectTestScript)

    float       f = 1.0f;
    int         i = 2;
    bool        b = false;
    std::string s = "hello";
    Vector2     v{3.0f, 4.0f};
    Color       c{0.1f, 0.2f, 0.3f, 0.4f};

    void RegisterFields(ScriptFieldRegistry& r) override {
        r.Float("f", &f, 1.0f, 0.0f, 100.0f)
         .Int("i", &i)
         .Bool("b", &b)
         .String("s", &s)
         .Vec2("v", &v)
         .Color("c", &c);
    }
};

} // namespace

TEST_CASE("Script reflection: RegisterFields builds the registry once") {
    ReflectTestScript script;
    const ScriptFieldRegistry& reg = script.Fields();
    REQUIRE(reg.Fields().size() == 6);
    CHECK(reg.Fields()[0].name == "f");
    CHECK(reg.Fields()[0].type == ScriptFieldType::Float);
    CHECK(reg.Fields()[5].name == "c");
    CHECK(reg.Fields()[5].type == ScriptFieldType::Color);
}

TEST_CASE("Script reflection: serialize/deserialize round-trips every field type") {
    ReflectTestScript src;
    src.f = 42.5f;
    src.i = 7;
    src.b = true;
    src.s = "world";
    src.v = Vector2(11.0f, 22.0f);
    src.c = Color(0.5f, 0.6f, 0.7f, 0.8f);

    nlohmann::json j;
    src.Serialize(j);

    // 저장된 키가 실제로 존재하는지 (씬 파일에 남는지) 확인.
    CHECK(j.contains("f"));
    CHECK(j.contains("s"));
    CHECK(j.contains("c"));

    ReflectTestScript dst; // 기본값 상태
    dst.Deserialize(j);

    CHECK(dst.f == doctest::Approx(42.5f));
    CHECK(dst.i == 7);
    CHECK(dst.b == true);
    CHECK(dst.s == "world");
    CHECK(dst.v.x == doctest::Approx(11.0f));
    CHECK(dst.v.y == doctest::Approx(22.0f));
    CHECK(dst.c.r == doctest::Approx(0.5f));
    CHECK(dst.c.g == doctest::Approx(0.6f));
    CHECK(dst.c.b == doctest::Approx(0.7f));
    CHECK(dst.c.a == doctest::Approx(0.8f));
}

TEST_CASE("Script reflection: missing keys keep existing defaults") {
    nlohmann::json j;
    j["f"] = 99.0f; // f만 존재

    ReflectTestScript dst;
    dst.Deserialize(j);

    CHECK(dst.f == doctest::Approx(99.0f)); // 갱신됨
    CHECK(dst.i == 2);                      // 기본값 유지
    CHECK(dst.s == "hello");                // 기본값 유지
}

TEST_CASE("Script reflection: base Script with no fields serializes to empty object") {
    Script bare;
    nlohmann::json j;
    bare.Serialize(j);
    CHECK(bare.Fields().Empty());
    CHECK(j.is_null()); // 아무 키도 쓰지 않음
}

// ── 오브젝트/프리팹 참조 필드 ──────────────────────────────────────────────

namespace {
class RefTestScript : public Script {
public:
    SCRIPT_CLASS(RefTestScript)

    ObjectRef target;   // 같은 서브트리 내부를 가리킬 수 있는 참조
    ObjectRef external; // 외부(씬) 오브젝트 참조
    PrefabRef prefab;

    void RegisterFields(ScriptFieldRegistry& r) override {
        r.Object("target", &target)
         .Object("external", &external)
         .Prefab("prefab", &prefab);
    }
};
} // namespace

TEST_CASE("Object/Prefab ref: serialize round-trip") {
    RefTestScript src;
    src.target.targetId = 42;
    src.prefab.guid = "abc-123";

    nlohmann::json j;
    src.Serialize(j);
    CHECK(j["target"] == 42u);
    CHECK(j["prefab"] == "abc-123");

    RefTestScript dst;
    dst.Deserialize(j);
    CHECK(dst.target.targetId == 42u);
    CHECK(dst.prefab.guid == "abc-123");
    CHECK(dst.external.targetId == 0u); // 미설정 유지
}

TEST_CASE("Object ref: RemapReferences remaps internal, leaves external") {
    RefTestScript s;
    s.target.targetId = 100;   // idRemap에 존재 (내부)
    s.external.targetId = 999; // idRemap에 없음 (외부)

    std::unordered_map<unsigned int, unsigned int> remap = {{100u, 500u}};
    s.RemapReferences(remap);

    CHECK(s.target.targetId == 500u);   // 새 id로 갱신
    CHECK(s.external.targetId == 999u); // 외부 참조는 그대로
}

TEST_CASE("Object ref: id-remap through World::Instantiate keeps internal refs consistent") {
    // RefTestScript를 ScriptManager에 등록(복제 시 타입명으로 재생성됨).
    ScriptManager::Get().RegisterDynamic("RefTestScript", []() -> std::unique_ptr<Script> {
        return std::make_unique<RefTestScript>();
    });

    World world;
    auto a = std::make_shared<GameObject>("A");
    auto b = std::make_shared<GameObject>("B");
    world.Add(a);
    world.Add(b);
    b->SetParent(a.get());

    auto* scriptA = static_cast<RefTestScript*>(a->AddComponentRaw(new RefTestScript()));
    scriptA->target.targetId = b->GetID();      // 같은 서브트리 내부(B) 참조

    GameObject* clone = world.Instantiate(a.get());
    // Instantiate는 지연 큐에 넣으므로, 라이브 조회(FindById) 전에 플러시한다.
    world.FlushDeferred(0.0f);

    REQUIRE(clone != nullptr);
    REQUIRE(clone->GetChildren().size() == 1);

    GameObject* cloneB = clone->GetChildren()[0];
    auto* cloneScript = clone->GetComponent<RefTestScript>();
    REQUIRE(cloneScript != nullptr);

    // 내부 참조가 복제본 B의 새 id로 리매핑되어야 한다(원본 B를 가리키면 안 됨).
    CHECK(cloneScript->target.targetId == cloneB->GetID());
    CHECK(cloneScript->target.targetId != b->GetID());

    // 그리고 그 참조는 실제 복제본 B로 해석된다.
    CHECK(cloneScript->Resolve(cloneScript->target) == cloneB);
}
