#include "Systems/Input.h"
#include "doctest.h"
#include <filesystem>
#include <fstream>
#include <iostream>

TEST_CASE("Input Action Map Subsystem Tests") {
    // Initialize Input system
    Input::Init(nullptr);

    // Initialize default actions
    Input::InitializeDefaultActions();

    auto& actions = Input::GetActions();
    REQUIRE(actions.size() >= 4);

    // Find Horizontal Action
    Input::Action* horiz = nullptr;
    for (auto& action : actions) {
        if (action.name == "Horizontal") {
            horiz = &action;
            break;
        }
    }
    REQUIRE(horiz != nullptr);
    CHECK(horiz->isAxis == true);
    CHECK(horiz->bindings.size() == 3);

    // Find Jump Action
    Input::Action* jump = nullptr;
    for (auto& action : actions) {
        if (action.name == "Jump") {
            jump = &action;
            break;
        }
    }
    REQUIRE(jump != nullptr);
    CHECK(jump->isAxis == false);
}

TEST_CASE("Input Action registration and custom bindings") {
    Input::Init(nullptr);
    auto& actions = Input::GetActions();
    actions.clear();

    Input::Action customAction;
    customAction.name = "CustomJump";
    customAction.isAxis = false;

    Input::Binding spaceBind;
    spaceBind.device = Input::DeviceType::Keyboard;
    spaceBind.code = 32; // GLFW_KEY_SPACE
    spaceBind.multiplier = 1.0f;

    customAction.bindings.push_back(spaceBind);
    actions.push_back(customAction);

    REQUIRE(actions.size() == 1);
    CHECK(actions[0].name == "CustomJump");
    CHECK(actions[0].bindings.size() == 1);
    CHECK(actions[0].bindings[0].code == 32);
}

TEST_CASE("Input Action evaluation: Keyboard and Mouse") {
    Input::Init(nullptr);
    auto& actions = Input::GetActions();
    actions.clear();

    // Custom Button Action
    Input::Action shoot;
    shoot.name = "Shoot";
    shoot.isAxis = false;

    Input::Binding leftMouse;
    leftMouse.device = Input::DeviceType::Mouse;
    leftMouse.code = 0; // Left click
    leftMouse.multiplier = 1.0f;
    shoot.bindings.push_back(leftMouse);
    actions.push_back(shoot);

    // Custom Axis Action
    Input::Action move;
    move.name = "Move";
    move.isAxis = true;

    Input::Binding dKey;
    dKey.device = Input::DeviceType::Keyboard;
    dKey.code = 68; // 'D'
    dKey.multiplier = 1.0f;

    Input::Binding aKey;
    aKey.device = Input::DeviceType::Keyboard;
    aKey.code = 65; // 'A'
    aKey.multiplier = -1.0f;

    move.bindings.push_back(dKey);
    move.bindings.push_back(aKey);
    actions.push_back(move);

    // Initially, nothing pressed
    Input::SetMouseButtonForTesting(0, false);
    Input::SetKeyForTesting(68, false);
    Input::SetKeyForTesting(65, false);
    Input::Update();

    CHECK(Input::GetAction("Shoot") == false);
    CHECK(Input::GetAxis("Move") == doctest::Approx(0.0f));

    // Press left mouse button
    Input::SetMouseButtonForTesting(0, true);
    Input::Update();
    CHECK(Input::GetAction("Shoot") == true);

    // Press 'D' (move positive)
    Input::SetMouseButtonForTesting(0, false);
    Input::SetKeyForTesting(68, true);
    Input::Update();
    CHECK(Input::GetAction("Shoot") == false);
    CHECK(Input::GetAxis("Move") == doctest::Approx(1.0f));

    // Press both 'D' and 'A' (should cancel out to 0.0f)
    Input::SetKeyForTesting(65, true);
    Input::Update();
    CHECK(Input::GetAxis("Move") == doctest::Approx(0.0f));

    // Release 'D', only 'A' pressed
    Input::SetKeyForTesting(68, false);
    Input::Update();
    CHECK(Input::GetAxis("Move") == doctest::Approx(-1.0f));
}

TEST_CASE("Input Action evaluation: Gamepad buttons and Axis deadzone") {
    Input::Init(nullptr);
    auto& actions = Input::GetActions();
    actions.clear();

    Input::Action customGP;
    customGP.name = "GamepadAction";
    customGP.isAxis = true;

    Input::Binding gpAxis;
    gpAxis.device = Input::DeviceType::GamepadAxis;
    gpAxis.code = 0;
    gpAxis.multiplier = 1.0f;
    customGP.bindings.push_back(gpAxis);
    actions.push_back(customGP);

    // Check deadzone: values below 0.15f should be ignored
    Input::SetGamepadAxisForTesting(0, 0.1f);
    Input::Update();
    CHECK(Input::GetAxis("GamepadAction") == doctest::Approx(0.0f));

    // Values above 0.15f should be read
    Input::SetGamepadAxisForTesting(0, 0.4f);
    Input::Update();
    CHECK(Input::GetAxis("GamepadAction") == doctest::Approx(0.4f));

    // Values below -0.15f
    Input::SetGamepadAxisForTesting(0, -0.8f);
    Input::Update();
    CHECK(Input::GetAxis("GamepadAction") == doctest::Approx(-0.8f));
}

TEST_CASE("Input Action Edge detection (Down/Up)") {
    Input::Init(nullptr);
    auto& actions = Input::GetActions();
    actions.clear();

    Input::Action jump;
    jump.name = "Jump";
    jump.isAxis = false;

    Input::Binding spaceBind;
    spaceBind.device = Input::DeviceType::Keyboard;
    spaceBind.code = 32;
    spaceBind.multiplier = 1.0f;
    jump.bindings.push_back(spaceBind);
    actions.push_back(jump);

    // Frame 0: Not pressed
    Input::SetKeyForTesting(32, false);
    Input::Update();
    CHECK(Input::GetAction("Jump") == false);
    CHECK(Input::GetActionDown("Jump") == false);
    CHECK(Input::GetActionUp("Jump") == false);

    // Frame 1: Pressed (Down triggers)
    Input::SetKeyForTesting(32, true);
    Input::Update();
    CHECK(Input::GetAction("Jump") == true);
    CHECK(Input::GetActionDown("Jump") == true);
    CHECK(Input::GetActionUp("Jump") == false);

    // Frame 2: Still Pressed (Down becomes false, Action remains true)
    Input::Update();
    CHECK(Input::GetAction("Jump") == true);
    CHECK(Input::GetActionDown("Jump") == false);
    CHECK(Input::GetActionUp("Jump") == false);

    // Frame 3: Released (Up triggers, Action becomes false)
    Input::SetKeyForTesting(32, false);
    Input::Update();
    CHECK(Input::GetAction("Jump") == false);
    CHECK(Input::GetActionDown("Jump") == false);
    CHECK(Input::GetActionUp("Jump") == true);

    // Frame 4: Still Released (Up becomes false)
    Input::Update();
    CHECK(Input::GetAction("Jump") == false);
    CHECK(Input::GetActionDown("Jump") == false);
    CHECK(Input::GetActionUp("Jump") == false);
}

TEST_CASE("Input scroll is accumulated per native source and consumed once") {
    Input::Init(nullptr);
    auto* first = reinterpret_cast<GLFWwindow*>(0x1);
    auto* second = reinterpret_cast<GLFWwindow*>(0x2);

    Input::AddScrollForTesting(first, 1.0f, 2.0f);
    Input::AddScrollForTesting(first, -0.25f, 3.0f);
    Input::AddScrollForTesting(second, 9.0f, -4.0f);

    const InputSnapshot firstFrame = Input::ConsumeScrollForTesting(first);
    CHECK(firstFrame.scrollX == doctest::Approx(0.75f));
    CHECK(firstFrame.scrollY == doctest::Approx(5.0f));
    const InputSnapshot consumed = Input::ConsumeScrollForTesting(first);
    CHECK(consumed.scrollX == doctest::Approx(0.0f));
    CHECK(consumed.scrollY == doctest::Approx(0.0f));

    const InputSnapshot otherSource = Input::ConsumeScrollForTesting(second);
    CHECK(otherSource.scrollX == doctest::Approx(9.0f));
    CHECK(otherSource.scrollY == doctest::Approx(-4.0f));
}

TEST_CASE("Input discards wheel events outside the game output") {
    Input::Init(nullptr);
    auto* source = reinterpret_cast<GLFWwindow*>(0x3);
    Input::AddScrollForTesting(source, 2.0f, 7.0f);

    const InputSnapshot outside = Input::ConsumeScrollForTesting(source, false);
    CHECK(outside.scrollX == doctest::Approx(0.0f));
    CHECK(outside.scrollY == doctest::Approx(0.0f));
    const InputSnapshot nextFrame = Input::ConsumeScrollForTesting(source, true);
    CHECK(nextFrame.scrollX == doctest::Approx(0.0f));
    CHECK(nextFrame.scrollY == doctest::Approx(0.0f));
}

TEST_CASE("Input Action Serialization and Deserialization") {
    Input::Init(nullptr);
    Input::InitializeDefaultActions();

    std::string testFile = "temp_test_input_actions.json";
    
    // Save actions
    auto& actionsBefore = Input::GetActions();
    size_t sizeBefore = actionsBefore.size();
    REQUIRE(sizeBefore > 0);
    
    // Create copy to compare with
    std::vector<Input::Action> actionsBeforeCopy = actionsBefore;
    
    Input::SaveActions(testFile);

    // Load actions back
    Input::Init(nullptr); // clears and resets
    Input::LoadActions(testFile);

    auto& actionsAfter = Input::GetActions();
    CHECK(actionsAfter.size() == sizeBefore);

    if (actionsAfter.size() == sizeBefore) {
        for (size_t i = 0; i < sizeBefore; ++i) {
            CHECK(actionsAfter[i].name == actionsBeforeCopy[i].name);
            CHECK(actionsAfter[i].isAxis == actionsBeforeCopy[i].isAxis);
            CHECK(actionsAfter[i].bindings.size() == actionsBeforeCopy[i].bindings.size());
        }
    }

    // Clean up
    std::filesystem::remove(testFile);
}
