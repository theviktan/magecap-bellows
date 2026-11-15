#pragma once

#include "Bellows/Core/Base.h"
#include "Bellows/Core/Layer.h"

namespace Bellows {

	class ImGuiRenderer;

	// Owns the Dear ImGui context, the GLFW platform backend and the NVRHI renderer.
	// Draws into the application back buffer at the end of each frame.
	class ImGuiLayer : public Layer
	{
	public:
		ImGuiLayer();
		~ImGuiLayer() override;

		void OnAttach() override;
		void OnDetach() override;
		void OnEvent(Event& event) override;

		void Begin();
		void End();

		// When true, ImGui consumes mouse/keyboard events it wants to capture.
		void SetBlockEvents(bool block) { m_BlockEvents = block; }
		// When true, the back buffer is cleared before the UI is drawn (applications that draw nothing else).
		void SetClearBackBuffer(bool clear) { m_ClearBackBuffer = clear; }
	private:
		Scope<ImGuiRenderer> m_Renderer;
		bool m_BlockEvents = true;
		bool m_ClearBackBuffer = true;
		bool m_HasPlatformBackend = false;
	};

}
