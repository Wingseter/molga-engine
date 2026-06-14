#include "Systems/Audio.h"
#include "ECS/GameObject.h"
#include "ECS/Components/AudioSource.h"
#include "ECS/Components/AudioListener.h"
#include "Core/SceneSerializer.h"
#include "doctest.h"
#include <memory>

TEST_CASE("AudioSource & AudioListener: serialization and properties") {
    auto obj = std::make_shared<GameObject>("AudioObject");
    AudioSource* source = obj->AddComponent<AudioSource>();
    
    REQUIRE(source != nullptr);
    CHECK(source->GetVolume() == doctest::Approx(1.0f));
    CHECK(source->GetPitch() == doctest::Approx(1.0f));
    CHECK_FALSE(source->IsLooping());
    CHECK_FALSE(source->GetPlayOnAwake());
    CHECK_FALSE(source->IsSpatial());
    CHECK(source->GetMinDistance() == doctest::Approx(1.0f));
    CHECK(source->GetMaxDistance() == doctest::Approx(500.0f));

    // Set properties
    source->SetClipPath("assets/audio/test.wav");
    source->SetVolume(0.5f);
    source->SetPitch(1.5f);
    source->SetLooping(true);
    source->SetPlayOnAwake(true);
    source->SetSpatial(true);
    source->SetMinDistance(2.0f);
    source->SetMaxDistance(300.0f);

    CHECK(source->GetClipPath() == "assets/audio/test.wav");
    CHECK(source->GetVolume() == doctest::Approx(0.5f));
    CHECK(source->GetPitch() == doctest::Approx(1.5f));
    CHECK(source->IsLooping());
    CHECK(source->GetPlayOnAwake());
    CHECK(source->IsSpatial());
    CHECK(source->GetMinDistance() == doctest::Approx(2.0f));
    CHECK(source->GetMaxDistance() == doctest::Approx(300.0f));

    // Serialize GameObject
    std::string json = SceneSerializer::SerializeGameObject(obj.get());
    CHECK(!json.empty());

    // Deserialize GameObject
    auto restored = SceneSerializer::DeserializeGameObject(json);
    REQUIRE(restored != nullptr);

    AudioSource* rsource = restored->GetComponent<AudioSource>();
    REQUIRE(rsource != nullptr);
    CHECK(rsource->GetClipPath() == "assets/audio/test.wav");
    CHECK(rsource->GetVolume() == doctest::Approx(0.5f));
    CHECK(rsource->GetPitch() == doctest::Approx(1.5f));
    CHECK(rsource->IsLooping());
    CHECK(rsource->GetPlayOnAwake());
    CHECK(rsource->IsSpatial());
    CHECK(rsource->GetMinDistance() == doctest::Approx(2.0f));
    CHECK(rsource->GetMaxDistance() == doctest::Approx(300.0f));
}

TEST_CASE("AudioListener: serialization") {
    auto obj = std::make_shared<GameObject>("ListenerObject");
    AudioListener* listener = obj->AddComponent<AudioListener>();
    REQUIRE(listener != nullptr);

    std::string json = SceneSerializer::SerializeGameObject(obj.get());
    CHECK(!json.empty());

    auto restored = SceneSerializer::DeserializeGameObject(json);
    REQUIRE(restored != nullptr);

    AudioListener* rlistener = restored->GetComponent<AudioListener>();
    REQUIRE(rlistener != nullptr);
}

TEST_CASE("AudioSource: play, stop, pause, resume, and IsPlaying state") {
    bool audioInit = Audio::Init();
    
    auto obj = std::make_shared<GameObject>("PlaybackObject");
    AudioSource* source = obj->AddComponent<AudioSource>();
    source->SetClipPath("assets/audio/dummy.wav");
    
    CHECK_FALSE(source->IsPlaying());

    // Call playback methods - they should not crash even if audio is not fully initialized or sound not loaded
    source->Play();
    CHECK_FALSE(source->IsPlaying());

    source->Pause();
    source->Resume();
    source->Stop();
    CHECK_FALSE(source->IsPlaying());

    if (audioInit) {
        Audio::Shutdown();
    }
}
