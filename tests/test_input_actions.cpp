#include "Systems/Input.h"
#include "doctest.h"

#include <filesystem>
#include <fstream>

namespace {

void BeginTestFrame() {
    Input::BeginFrame();
}

} // namespace

TEST_CASE("Input default actions use typed symbolic controls") {
    Input::Init(0);
    Input::InitializeDefaultActions();

    const auto& actions = Input::GetActions();
    REQUIRE(actions.size() == 4);
    CHECK(actions[0].name == "Horizontal");
    CHECK(actions[0].bindings[0].control == "D");
    CHECK(actions[2].name == "Jump");
    CHECK(actions[2].bindings[0].control == "Space");
    CHECK(Input::IsValidControl(Input::DeviceType::Keyboard, "Escape"));
    CHECK_FALSE(Input::IsValidControl(Input::DeviceType::Keyboard, "NotAKey"));
}

TEST_CASE("Input action evaluation uses typed keyboard mouse and gamepad controls") {
    Input::Init(0);
    auto& actions = Input::GetActions();
    actions = {
        {"Shoot", false, {{Input::DeviceType::Mouse, "Left", 1.0f}}},
        {"Move", true,
         {{Input::DeviceType::Keyboard, "D", 1.0f},
          {Input::DeviceType::Keyboard, "A", -1.0f}}},
        {"Look", true,
         {{Input::DeviceType::GamepadAxis, "RightX", 1.0f}}}
    };

    BeginTestFrame();
    Input::SetMouseButtonForTesting(Input::MouseButton::Left, true);
    Input::SetKeyForTesting(Input::KeyCode::D, true);
    Input::SetGamepadAxisForTesting(Input::GamepadAxis::RightX, 0.1f);
    Input::Update();
    CHECK(Input::GetActionDown("Shoot"));
    CHECK(Input::GetAxis("Move") == doctest::Approx(1.0f));
    CHECK(Input::GetAxis("Look") == doctest::Approx(0.0f));

    BeginTestFrame();
    Input::SetKeyForTesting(Input::KeyCode::A, true);
    Input::SetGamepadAxisForTesting(Input::GamepadAxis::RightX, -0.8f);
    Input::Update();
    CHECK_FALSE(Input::GetActionDown("Shoot"));
    CHECK(Input::GetAxis("Move") == doctest::Approx(0.0f));
    CHECK(Input::GetAxis("Look") == doctest::Approx(-0.8f));

    BeginTestFrame();
    Input::SetMouseButtonForTesting(Input::MouseButton::Left, false);
    Input::Update();
    CHECK(Input::GetActionUp("Shoot"));
}

TEST_CASE("Input scroll is accumulated per window and consumed once") {
    Input::Init(0);
    constexpr molga::WindowId first = 1;
    constexpr molga::WindowId second = 2;

    Input::AddScrollForTesting(first, 1.0f, 2.0f);
    Input::AddScrollForTesting(first, -0.25f, 3.0f);
    Input::AddScrollForTesting(second, 9.0f, -4.0f);

    const InputSnapshot firstFrame = Input::ConsumeScrollForTesting(first);
    CHECK(firstFrame.scrollX == doctest::Approx(0.75f));
    CHECK(firstFrame.scrollY == doctest::Approx(5.0f));
    CHECK(Input::ConsumeScrollForTesting(first).scrollY == doctest::Approx(0.0f));

    const InputSnapshot other = Input::ConsumeScrollForTesting(second);
    CHECK(other.scrollX == doctest::Approx(9.0f));
    CHECK(other.scrollY == doctest::Approx(-4.0f));
}

TEST_CASE("Input discards wheel events outside the game output") {
    Input::Init(0);
    constexpr molga::WindowId source = 3;
    Input::AddScrollForTesting(source, 2.0f, 7.0f);
    const InputSnapshot outside = Input::ConsumeScrollForTesting(source, false);
    CHECK(outside.scrollX == doctest::Approx(0.0f));
    CHECK(outside.scrollY == doctest::Approx(0.0f));
    CHECK(Input::ConsumeScrollForTesting(source).scrollY == doctest::Approx(0.0f));
}

TEST_CASE("Input schema v2 round trips symbolic controls") {
    Input::Init(0);
    Input::InitializeDefaultActions();
    const nlohmann::json document = Input::SerializeActions();
    CHECK(document["schemaVersion"] == 2);
    CHECK(document["actions"][0]["bindings"][0]["control"] == "D");
    CHECK_FALSE(document["actions"][0]["bindings"][0].contains("code"));

    const std::filesystem::path path = "temp_test_input_actions.json";
    std::string error;
    REQUIRE(Input::SaveActions(path.string(), &error));
    Input::GetActions().clear();
    REQUIRE(Input::LoadActions(path.string(), &error));
    CHECK(Input::GetActions().size() == 4);
    CHECK(Input::GetActions()[2].bindings[0].control == "Space");
    std::filesystem::remove(path);
}

TEST_CASE("Input runtime rejects legacy numeric documents") {
    Input::Init(0);
    const nlohmann::json legacy = nlohmann::json::array({
        {{"name", "Jump"}, {"isAxis", false},
         {"bindings", nlohmann::json::array({
             {{"device", "Keyboard"}, {"code", 32}, {"multiplier", 1.0f}}
         })}}
    });
    std::string error;
    CHECK_FALSE(Input::DeserializeActions(legacy, &error));
    CHECK(error.find("INPUT_SCHEMA_MIGRATION_REQUIRED") != std::string::npos);
}

TEST_CASE("Legacy input conversion maps numeric codes and fails unknown codes") {
    const nlohmann::json legacy = nlohmann::json::array({
        {{"name", "Move"}, {"isAxis", true},
         {"bindings", nlohmann::json::array({
             {{"device", "Keyboard"}, {"code", 68}, {"multiplier", 1.0f}},
             {{"device", "GamepadAxis"}, {"code", 0}, {"multiplier", 1.0f}}
         })}}
    });
    nlohmann::json migrated;
    std::string error;
    REQUIRE(Input::MigrateLegacyDocument(legacy, migrated, error));
    CHECK(migrated["schemaVersion"] == 2);
    CHECK(migrated["actions"][0]["bindings"][0]["control"] == "D");
    CHECK(migrated["actions"][0]["bindings"][1]["control"] == "LeftX");

    nlohmann::json invalid = legacy;
    invalid[0]["bindings"][0]["code"] = 9999;
    CHECK_FALSE(Input::MigrateLegacyDocument(invalid, migrated, error));
    CHECK(error.find("unsupported legacy code") != std::string::npos);

    const nlohmann::json malformedSchema = {
        {"schemaVersion", "2"}, {"actions", nlohmann::json::array()}
    };
    CHECK_FALSE(Input::MigrateLegacyDocument(
        malformedSchema, migrated, error));
    CHECK(error.find("legacy array or schema v2") != std::string::npos);

    nlohmann::json malformedMultiplier = legacy;
    malformedMultiplier[0]["bindings"][0]["multiplier"] = "fast";
    CHECK_FALSE(Input::MigrateLegacyDocument(
        malformedMultiplier, migrated, error));
    CHECK(error.find("non-numeric legacy multiplier") != std::string::npos);
}
