#include "ECS/GameObject.h"
#include "ECS/Components/Transform.h"
#include "ECS/Components/ParticleSystem.h"
#include "doctest.h"
#include <nlohmann/json.hpp>

TEST_CASE("ParticleSystem: properties") {
    ParticleSystem ps;
    CHECK(ps.playOnAwake == true);
    CHECK(ps.looping == true);
    CHECK(ps.presetName == "Custom");
    CHECK(ps.useAdditiveBlending == false);
    CHECK(ps.GetSortingOrder() == 0);

    ps.SetSortingOrder(10);
    CHECK(ps.GetSortingOrder() == 10);
}

TEST_CASE("ParticleSystem: preset loading") {
    ParticleSystem ps;
    // Set preset Name
    ps.presetName = "Fire";
    ps.config = ParticlePresets::Fire();
    ps.GetEmitter().SetConfig(ps.config);
    
    CHECK(ps.config.spawnRate == doctest::Approx(30.0f));
    CHECK(ps.config.maxParticles == 200);
}

TEST_CASE("ParticleSystem: serialization and deserialization roundtrip") {
    auto obj1 = std::make_shared<GameObject>("EmitterObj1");
    auto* ps1 = obj1->AddComponent<ParticleSystem>();
    
    ps1->playOnAwake = false;
    ps1->looping = false;
    ps1->presetName = "Snow";
    ps1->useAdditiveBlending = true;
    ps1->SetSortingOrder(5);
    ps1->config.spawnRate = 123.45f;
    ps1->config.maxParticles = 500;
    ps1->GetEmitter().SetConfig(ps1->config);

    nlohmann::json j;
    ps1->Serialize(j);

    auto obj2 = std::make_shared<GameObject>("EmitterObj2");
    auto* ps2 = obj2->AddComponent<ParticleSystem>();
    ps2->Deserialize(j);

    CHECK(ps2->playOnAwake == false);
    CHECK(ps2->looping == false);
    CHECK(ps2->presetName == "Snow");
    CHECK(ps2->useAdditiveBlending == true);
    CHECK(ps2->GetSortingOrder() == 5);
    CHECK(ps2->config.spawnRate == doctest::Approx(123.45f));
    CHECK(ps2->config.maxParticles == 500);
}

TEST_CASE("ParticleSystem: transform position syncing") {
    auto obj = std::make_shared<GameObject>("EmitterObj");
    auto* transform = obj->AddComponent<Transform>();
    auto* ps = obj->AddComponent<ParticleSystem>();

    transform->SetPosition(50.0f, -100.0f);
    ps->Update(0.1f);

    CHECK(ps->GetEmitter().x == doctest::Approx(50.0f));
    CHECK(ps->GetEmitter().y == doctest::Approx(-100.0f));
}

TEST_CASE("ParticleSystem: active state & emission lifetime") {
    auto obj = std::make_shared<GameObject>("EmitterObj");
    auto* ps = obj->AddComponent<ParticleSystem>();
    
    ps->playOnAwake = false;
    ps->looping = false;
    ps->config.maxLife = 0.5f;
    ps->config.spawnRate = 10.0f;
    ps->config.maxParticles = 50;
    ps->GetEmitter().SetConfig(ps->config);

    // Initial state
    CHECK(!ps->GetEmitter().IsActive());
    CHECK(!ps->GetEmitter().IsEmitting());

    // Play
    ps->Play();
    CHECK(ps->GetEmitter().IsEmitting());
    CHECK(ps->GetEmitter().IsActive());

    // Stop
    ps->Stop();
    CHECK(!ps->GetEmitter().IsEmitting());

    // Test automatic Stop when !looping and running beyond maxLife
    ps->Play();
    CHECK(ps->GetEmitter().IsEmitting());
    ps->Update(0.3f);
    CHECK(ps->GetEmitter().IsEmitting());
    ps->Update(0.3f); // total 0.6f > maxLife (0.5f)
    CHECK(!ps->GetEmitter().IsEmitting());

    // Test Emit (Burst)
    ps->Emit(5);
    CHECK(ps->GetEmitter().GetActiveCount() > 0);
}
