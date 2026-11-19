#pragma once

#include "Bellows/Core/Events.h"
#include "Bellows/Core/KeyCodes.h"

#include <glm/vec2.hpp>

#include <array>

namespace Bellows {

	// Who is asking. The game (scripts, InputMap, everything by default) does not see input the game UI
	// has taken; the UI itself reads the raw state.
	enum class InputAccess : uint8_t { Game = 0, Raw };

	// Input the game UI has taken this frame (pointer over a panel, keys for a focused text field, all of
	// it under a modal canvas). Taken input reads as not held, not pressed and not released for the game,
	// and taken pointer input also hides the scroll wheel. The mouse position is never taken.
	struct InputCapture
	{
		bool Pointer = false;
		bool Keyboard = false;
		bool Gamepad = false;

		bool operator==(const InputCapture& other) const = default;
	};

	// Frame-based input state.
	//
	// The state is fed by window events (or injected by tests and AI agents), never polled
	// directly from the OS, so games behave identically in windowed, headless and automated runs.
	//   - IsKeyDown:     the key is currently held
	//   - IsKeyPressed:  the key went down during the current frame
	//   - IsKeyReleased: the key went up during the current frame
	class Input
	{
	public:
		// Clears per-frame state. Called by the application once per frame before events are processed.
		static void BeginFrame();

		// Feeds an event into the input state. Returns nothing; events are never consumed.
		static void OnEvent(Event& event);

		static bool IsKeyDown(KeyCode key, InputAccess access = InputAccess::Game);
		static bool IsKeyPressed(KeyCode key, InputAccess access = InputAccess::Game);
		static bool IsKeyReleased(KeyCode key, InputAccess access = InputAccess::Game);

		static bool IsMouseButtonDown(MouseButton button, InputAccess access = InputAccess::Game);
		static bool IsMouseButtonPressed(MouseButton button, InputAccess access = InputAccess::Game);
		static bool IsMouseButtonReleased(MouseButton button, InputAccess access = InputAccess::Game);

		// The first connected gamepad (any controller GLFW maps to the standard layout).
		static bool IsGamepadConnected();
		static bool IsGamepadButtonDown(GamepadButton button, InputAccess access = InputAccess::Game);
		static bool IsGamepadButtonPressed(GamepadButton button, InputAccess access = InputAccess::Game);
		static bool IsGamepadButtonReleased(GamepadButton button, InputAccess access = InputAccess::Game);
		// Called by the window once per frame with the gamepad's buttons; a disconnect releases them all.
		static void UpdateGamepad(bool connected, const std::array<bool, MaxGamepadButton>& buttonsDown);

		// Mouse position relative to the active game viewport (top-left origin, in pixels).
		static glm::vec2 GetMousePosition();
		static glm::vec2 GetMouseDelta();
		static glm::vec2 GetScrollDelta(InputAccess access = InputAccess::Game);

		// The editor sets this to the viewport rectangle (in window coordinates) so games
		// receive viewport-local mouse coordinates. Runtime builds use the full window.
		static void SetViewportRect(const glm::vec2& position, const glm::vec2& size);
		static glm::vec2 GetViewportSize();

		// When disabled, key and mouse events are ignored (e.g. the editor viewport is not focused).
		static void SetEnabled(bool enabled);
		static bool IsEnabled();

		// Synthetic input for automated tests and AI agents.
		static void InjectKey(KeyCode key, bool down);
		static void InjectMouseButton(MouseButton button, bool down);
		static void InjectGamepadButton(GamepadButton button, bool down);
		static void InjectMousePosition(const glm::vec2& viewportPosition);
		static void InjectScroll(const glm::vec2& offset);	// Wheel notches, like a scroll event (+y away from the user)

		// Set by the scene's UI each frame before scripts run; cleared when play stops.
		static void SetCapture(const InputCapture& capture);
		static const InputCapture& GetCapture();

		// Releases every key and button (used when play mode stops or focus is lost).
		static void ResetState();
	private:
		static void SetKey(KeyCode key, bool down);
		static void SetMouseButton(MouseButton button, bool down);
		static void SetGamepadButton(GamepadButton button, bool down);
	};

}
