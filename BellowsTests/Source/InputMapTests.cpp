#include <doctest/doctest.h>

#include "TestUtilities.h"

#include "Bellows/Core/Input.h"
#include "Bellows/Core/InputMap.h"
#include "Bellows/Core/InputNames.h"
#include "Bellows/Scene/Scene.h"
#include "Bellows/Scripting/ForgeScriptBindings.h"
#include "Bellows/Scripting/ScriptApi.h"
#include "Bellows/Scripting/ScriptEngine.h"

using namespace Bellows;

namespace {

	// Clean input and a known action map for each test.
	struct InputFixture
	{
		InputFixture()
		{
			Input::SetEnabled(true);
			Input::ResetState();
			Input::BeginFrame();
			InputMap::SetActions({
				{ "jump", { InputBinding::Key(KeyCode::Space), InputBinding::Gamepad(GamepadButton::A) } },
				{ "fire", { InputBinding::Mouse(MouseButton::Left) } },
				{ "unbound", {} },
			});
		}

		~InputFixture()
		{
			Input::ResetState();
			Input::BeginFrame();
			InputMap::SetActions({});
		}
	};

}

TEST_CASE("Input names find keys, mouse buttons and gamepad buttons in any spelling")
{
	CHECK(FindKeyCode("left_shift") == KeyCode::LeftShift);
	CHECK(FindKeyCode("Left Shift") == KeyCode::LeftShift);
	CHECK(FindKeyCode("w") == KeyCode::W);
	CHECK(FindKeyCode("7") == KeyCode::D7);
	CHECK_FALSE(FindKeyCode("NoSuchKey"));
	CHECK(FindMouseButton("RIGHT") == MouseButton::Right);
	CHECK(FindMouseButton("forward") == MouseButton::Button4);
	CHECK(FindGamepadButton("dpad_left") == GamepadButton::DPadLeft);
	CHECK_FALSE(FindGamepadButton("Z"));

	// Every listed name finds its own code again.
	for (const KeyName& entry : GetKeyCodeNames())
		CHECK(FindKeyCode(entry.Name) == entry.Code);
	for (const GamepadButtonName& entry : GetGamepadButtonNames())
		CHECK(GetGamepadButtonName(entry.Button) == std::string(entry.Name));
	CHECK(GetGamepadButtonNames().size() == MaxGamepadButton);
	CHECK(GetKeyCodeName(KeyCode::Menu) == nullptr);

	// The scripting names are the same table.
	CHECK(&Scripting::GetKeyNames() == &GetKeyCodeNames());
	CHECK(Scripting::KeyFromName("space") == KeyCode::Space);
	CHECK_THROWS_AS(Scripting::KeyFromName("NoSuchKey"), Scripting::ScriptValueError);
}

TEST_CASE("Gamepad buttons track down, pressed and released, and a disconnect releases them")
{
	InputFixture fixture;
	CHECK_FALSE(Input::IsGamepadConnected());

	Input::InjectGamepadButton(GamepadButton::A, true);
	CHECK(Input::IsGamepadButtonDown(GamepadButton::A));
	CHECK(Input::IsGamepadButtonPressed(GamepadButton::A));
	// No gamepad: the device poll leaves injected buttons alone.
	Input::UpdateGamepad(false, {});
	CHECK(Input::IsGamepadButtonDown(GamepadButton::A));
	Input::InjectGamepadButton(GamepadButton::A, false);
	CHECK(Input::IsGamepadButtonReleased(GamepadButton::A));

	Input::BeginFrame();
	std::array<bool, MaxGamepadButton> buttons {};
	buttons[static_cast<size_t>(GamepadButton::Start)] = true;
	Input::UpdateGamepad(true, buttons);
	CHECK(Input::IsGamepadConnected());
	CHECK(Input::IsGamepadButtonPressed(GamepadButton::Start));
	Input::BeginFrame();
	Input::UpdateGamepad(true, buttons);
	CHECK(Input::IsGamepadButtonDown(GamepadButton::Start));
	CHECK_FALSE(Input::IsGamepadButtonPressed(GamepadButton::Start));

	// Disabled input ignores new presses but keeps a held button until it is let go.
	Input::SetEnabled(false);
	buttons[static_cast<size_t>(GamepadButton::B)] = true;
	Input::UpdateGamepad(true, buttons);
	CHECK_FALSE(Input::IsGamepadButtonDown(GamepadButton::B));
	Input::SetEnabled(true);

	Input::BeginFrame();
	Input::UpdateGamepad(true, buttons);
	CHECK(Input::IsGamepadButtonDown(GamepadButton::Start));
	Input::BeginFrame();
	Input::UpdateGamepad(false, buttons);
	CHECK_FALSE(Input::IsGamepadConnected());
	CHECK_FALSE(Input::IsGamepadButtonDown(GamepadButton::Start));
	CHECK(Input::IsGamepadButtonReleased(GamepadButton::Start));
}

TEST_CASE("Input bindings round-trip through their project-file spelling")
{
	CHECK(InputBinding::Key(KeyCode::Space).ToString() == "Key:Space");
	CHECK(InputBinding::Mouse(MouseButton::Left).ToString() == "Mouse:Left");
	CHECK(InputBinding::Gamepad(GamepadButton::DPadUp).ToString() == "Gamepad:DPadUp");
	CHECK(InputBinding::Parse("key:left_shift") == InputBinding::Key(KeyCode::LeftShift));
	CHECK(InputBinding::Parse("GAMEPAD:start") == InputBinding::Gamepad(GamepadButton::Start));
	CHECK(InputBinding::Parse("Mouse:Middle") == InputBinding::Mouse(MouseButton::Middle));
	CHECK_FALSE(InputBinding::Parse("Space"));
	CHECK_FALSE(InputBinding::Parse("Key:NoSuchKey"));
	CHECK_FALSE(InputBinding::Parse("Joystick:A"));
	for (const auto& [action, bindings] : InputMap::GetDefaultActions())
	{
		CHECK_FALSE(bindings.empty());
		for (const InputBinding& binding : bindings)
			CHECK(InputBinding::Parse(binding.ToString()) == binding);
	}
}

TEST_CASE("Input actions are down while any bound input is held")
{
	InputFixture fixture;
	CHECK(InputMap::HasAction("jump"));
	CHECK_FALSE(InputMap::HasAction("Jump"));
	CHECK_FALSE(InputMap::IsActionDown("missing"));
	CHECK_FALSE(InputMap::IsActionDown("unbound"));

	Input::InjectKey(KeyCode::Space, true);
	CHECK(InputMap::IsActionDown("jump"));
	CHECK(InputMap::IsActionPressed("jump"));
	CHECK_FALSE(InputMap::IsActionDown("fire"));

	// A second input going down while the first is held does not press the action again.
	Input::BeginFrame();
	Input::InjectGamepadButton(GamepadButton::A, true);
	CHECK(InputMap::IsActionDown("jump"));
	CHECK_FALSE(InputMap::IsActionPressed("jump"));

	// Letting go of one input keeps the action down; letting go of both releases it.
	Input::BeginFrame();
	Input::InjectKey(KeyCode::Space, false);
	CHECK(InputMap::IsActionDown("jump"));
	CHECK_FALSE(InputMap::IsActionReleased("jump"));
	Input::BeginFrame();
	Input::InjectGamepadButton(GamepadButton::A, false);
	CHECK_FALSE(InputMap::IsActionDown("jump"));
	CHECK(InputMap::IsActionReleased("jump"));

	Input::BeginFrame();
	Input::InjectMouseButton(MouseButton::Left, true);
	CHECK(InputMap::IsActionPressed("fire"));
}

TEST_CASE("Scripts ask for input actions and gamepad buttons in Python and ForgeScript")
{
	InputFixture fixture;
	Input::InjectKey(KeyCode::Space, true);
	Input::InjectGamepadButton(GamepadButton::Y, true);

	std::string output;
	REQUIRE(ScriptEngine::ExecuteSource(
		"print(bellows.action_names(), bellows.is_action_down('jump'), bellows.is_action_pressed('jump'),\n"
		"      bellows.is_action_released('jump'), bellows.is_action_down('fire'),\n"
		"      bellows.is_gamepad_down('y'), bellows.is_gamepad_pressed('DPadUp'), bellows.gamepad_connected())", output));
	CHECK(output.find("['fire', 'jump', 'unbound'] True True False False True False False") != std::string::npos);
	CHECK_FALSE(ScriptEngine::ExecuteSource("bellows.is_action_down('dance')", output));
	CHECK(output.find("Unknown input action 'dance'") != std::string::npos);
	CHECK_FALSE(ScriptEngine::ExecuteSource("bellows.is_gamepad_down('Z')", output));
	CHECK(output.find("Unknown gamepad button 'Z'") != std::string::npos);

	Ref<Scene> scene = CreateRef<Scene>();
	Scripting::SetContextScene(scene.get());
	ForgeScript::VM machine;
	std::string printed;
	machine.SetPrintHandler([&](const std::string& text) { printed += text + "\n"; });
	Scripting::RegisterForgeScriptApi(machine);
	machine.Load(
		"say is_action_down(\"jump\")\n"
		"say is_action_released(\"jump\")\n"
		"say action_names().length\n"
		"say is_gamepad_down(\"Y\")\n"
		"try\n\tis_action_pressed(\"dance\")\ncatch p\n\tsay p\nend\n",
		"actions.forge");
	Scripting::SetContextScene(nullptr);
	CHECK_EQ(printed, std::string(
		"true\n"
		"false\n"
		"3\n"
		"true\n"
		"Unknown input action 'dance'. Actions are set in Project Settings > Input actions\n"));
}
